/*=============================================================================
  Predictor

  The description of the Predictor actor can be found in the corresponding 
  header file. The main purpose of the predictor is to maintain the predicted
  production of energy, and to use this for the acceptance test of incoming 
  loads and to schedule the accepted loads.
  
  The CSV Parser used here is the Fast C++ CSV Parser by Ben Strasser available
  at [1] and licensed under the BSD license
  
  REFERENCES
  
  [1] https://github.com/ben-strasser/fast-cpp-csv-parser
  [2] Geir Horn: Scheduling Time Variant Jobs on a Time Variant Resource, 
      in the Proceedings of The 7th Multidisciplinary International Conference 
      on Scheduling : Theory and Applications (MISTA 2015), Zdenek Hanzálek et
      al. (eds.), pp. 914-917, Prague, Czech Republic, 25-28 August 2015.
      
  Author: Geir Horn, University of Oslo, 2015
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <map>
#include <list>
#include <algorithm>
#include <limits>
#include <iterator>

#include <gsl/gsl_roots.h> 				// For finding the equality of two functions.
#include <gsl/gsl_errno.h> 				// For error messages from GSL

#include "TimeInterval.hpp"       // To have CoSSMic time
#include "Clock.hpp"		      		// To have Now from system or simulator 
#include "Predictor.hpp"	      	// The class definition
#include "ConsumerProxy.hpp"	  	// To interact with consumers
#include "CSVtoTimeSeries.hpp"    // To parse CSV files

namespace CoSSMic {
// -----------------------------------------------------------------------------
// Objective value computation
// -----------------------------------------------------------------------------

// The Predictor's contribution to the objective function is easily found in 
// [2]. However, the prediction function and the integrated prediction are 
// both interpolation objects and we must ensure that we are calling them with 
// arguments within their ranges, which in this case is the same for both 
// objects. In general the consumption interval could consist of three 
// sub-intervals with respect to the domain for the interpolation functions:
//
// 1) The interval preceding the start of the interpolation domain
// 2) The part within the interpolation domain
// 3) The part after the interpolation domain
//
// The first interval can be discarded because both the prediction and the 
// integrated prediction will be zero in this interval. Hence the domain 
// interior evaluation interval starts with at 
//
//  	maximum( lower bound Prediction, lower bound Consumption Interval )
// 
// provided that the intervals overlap; and correspondingly it ends at 
//
// 	minimum( upper bound Prediction, upper bound Consumption Interval )
//
// The third interval will be empty if it was the upper bound of the consumption
// interval that was selected as the interior intervals upper bound. However,
// if the upper bound of the consumption interval is larger than the prediction
// bound, then the third interval extends from the upper bound of the prediction
// domain until the upper bound of the consumption interval. 
//
// A careful analysis is necessary for the extended interval. Given that the 
// expression we want to evaluate from [2] is 
// 
// 	p(L)*(U-L) - Integral(p(t),L,U)
// 
// where p(t) is the prediction function, L is the lower bound of the interval
// and U is the upper bound. Since we have P(T) = Integral(p(t),0,T) where 
// "0" means the start of the domain, we can replace the last term with 
//
// 	Integral(p(t),L,U) = P(U)-P(L)
//
// and the expression to evaluate is
//
// p(L)*(U-L) - [ P(U)-P(L) ]
//
// The prediction function will keep its maximum value if we are outside of 
// the prediction domain. Hence the first term is p(T)*(U-L), if T is the upper
// bound of the domain for p(t). The second part is the integral of p(t) over 
// the interval. However, since p(t) = p(T) for the whole interval, this the 
// last term becomes P(T)*(U-L). Subtracting this from the first term yields 
// zero, and we can safely ignore the extended interval. 


void Predictor::ComputeObjectiveValue( const TimeInterval & ConsumptionInterval, 
																       const Theron::Address Sender )
{
  // The value to return is initialised, and we temporarily set the evaluation
  // interval to the full domain to check if it overlaps with the consumption 
  // interval
  
  double Value = 0.0;
  
  TimeInterval  
    EvaluationInterval( Prediction.DomainLower(), Prediction.DomainUpper() );
  
  // The predictor's contribution to the objective value is different from zero
  // only if the prediction domain overlaps with the given consumption interval.
  // The evaluation interval is then the intersection of the domain and the 
  // given consumption interval.
    
  if ( boost::numeric::overlap( ConsumptionInterval, EvaluationInterval ) )
  {
    EvaluationInterval = 
	  boost::numeric::intersect( ConsumptionInterval, EvaluationInterval );

    Value = Prediction( EvaluationInterval.lower() )
				      * boost::numeric::width( EvaluationInterval )
				      - ( IntegratedPrediction( EvaluationInterval.upper() )
					  - IntegratedPrediction( EvaluationInterval.lower() ) );
  }
  
  Send( Value, Sender);
}

// Computing the time root is just to find when the Prediction equals the given
// total load consumption. This is an ideal job for the GNU Scientific Library.
// The function to evaluate is a problem since the GSL only allows function 
// pointers, and lambdas that capture cannot be assigned to pure function 
// pointers. The solution is to use the parameter pointer allowed by the
// function and a structure containing a pointer and a value.

struct RootFunctionParameters
{ 
  Interpolation * ThePrediction;
  double          LoadConsumption;
};

double EnergyDifferenceFunction( double t, void * Parameters )
{
  RootFunctionParameters * FunctionTerms = 
							      reinterpret_cast< RootFunctionParameters * >( Parameters );
		     
  return ( FunctionTerms->ThePrediction->operator()(t) 
			     - FunctionTerms->LoadConsumption );
};


void Predictor::FindTimeRoot( const double & TotalLoadConsumption, 
												      const Theron::Address SingleScheduler   )
{
  // The parameters define the function for the solver to use. The prediction
  // is a continuous function giving the cumulative energy produced. The solver
  // is searching for the time when the amount of produced energy is at least 
  // as large as the energy required by the load, starting from the current 
  // time. Let the energy produced until time t >= now be P(t). Then the solver
  // will find a time T > now such that 
  // 		P(T) - P(now) >= total load consumption
  // as this is the earliest time the load can finish. Since the last term on
  // the left hand side and the right hand side are both constants, it is easier
  // to solve 
  //		P(T) >= ( total load consumption + P(now) )
  
  RootFunctionParameters TheFunctionTerms;
  Time 			 						 now = Now();
  
  TheFunctionTerms.LoadConsumption = TotalLoadConsumption + Prediction( now );
  TheFunctionTerms.ThePrediction   = &Prediction;  
 
	// However, it could be that this problem has no solution. The simplest case
	// occur when there will be no further production of PV power under the 
	// prediction horizon. In this case, P(t) will equal P(now) for all known 
	// time stamps. 
	
	if ( Prediction( Prediction.DomainUpper() ) 
																						> TheFunctionTerms.LoadConsumption )
	{
		// Problem makes sense and the solver structure and type must be allocated
	  
	  gsl_root_fsolver *Solver = gsl_root_fsolver_alloc( gsl_root_fsolver_brent );
	  
	  // This is is provided to the GSL solver in its parameter structure together 
	  // with a pointer to the difference function to invoke to find the zero.
	  
	  gsl_function Energy;
	  Energy.function = &EnergyDifferenceFunction ;
	  Energy.params   = &TheFunctionTerms;
	  
	  // Then we bind the solver and the function, and frame the solution space
	  // to the limits of the future prediction domain
	  
	  gsl_root_fsolver_set( Solver, &Energy, now, Prediction.DomainUpper() );
	  
	  // We have to iterate the solver until it has converged or the maximum number
	  // of iterations are exceeded. 
	  
	  int IterationCounter = 0,
	      Status;
	  
	  do
	  {
	    IterationCounter++;
	    gsl_root_fsolver_iterate( Solver );
	    Status = gsl_root_test_interval( gsl_root_fsolver_x_lower( Solver ),
					     gsl_root_fsolver_x_upper( Solver ),
					     0, 0.001 );
	  }
	  while ( (Status == GSL_CONTINUE) && (IterationCounter < 1000) );
	  
	  // Then the solution is verified and returned. Note that we do not return 
	  // the root, but the upper limit of the framed root since we are only 
	  // interested in the time point when there is sufficient energy. Since the 
	  // solver functions returns double values, and we want the ceiling value 
	  // as an integer, we have to add 0.5 before the resulting value is truncated
	  // by the cast operation.
	  
	  Time Solution = 
			   static_cast< Time >( gsl_root_fsolver_x_upper( Solver ) + 0.5 );
	  
	  if ( Prediction( Solution ) >= TotalLoadConsumption )
	    Send( Producer::AssignedStartTime( Solution ), SingleScheduler );
	  else
	    Send( Producer::AssignedStartTime(), SingleScheduler );
	  
	  // Finally we clean up by removing the solver structure.
	  
	  gsl_root_fsolver_free( Solver );
	}
	else // Problem did not make sense.
		Send( Producer::AssignedStartTime(), SingleScheduler );
}


// -----------------------------------------------------------------------------
// Updating the prediction origin
// -----------------------------------------------------------------------------
// 
// Shortening the past period of the prediction will only change the domain of 
// the prediction function and the integral function. However, there is no 
// need to consider this before the next prediction update since it simply 
// means that the first part of the functions (before the new origin) will 
// not be used from now. It is therefore sufficient just to record the new 
// origin, and then add parts of the current prediction as the historical part
// of the new prediction when it arrives.

void Predictor::SetPredictionOrigin( const Time & MinStartTime, 
																     const Theron::Address TheProducer )
{
  PredictionOrigin = MinStartTime;  
}

// -----------------------------------------------------------------------------
// Update the prediction profile
// -----------------------------------------------------------------------------
// The update handler uses the CSV parser to convert the file into the time series
// and sets the interpolation objects to new values.

void Predictor::UpdatePrediction( 
     const std::string & TheFilename, 
     const Theron::Address TheProducer )
{
  std::map< Time, double > TimeSeries( CSVtoTimeSeries( TheFilename ) );

  // The time series provides the predicted energy generated by this producer 
  // from the start of the series. This implies that the first time stamp in 
  // the series will have zero energy. In order to produce a consistent, global
  // prediction the energy of the current prediction at the start time 
  // of the new prediction must be added to all energy values. If the time 
	// series does not start at zero energy value as it should, the series will 
	// be re-based by subtracting the first energy value from all samples.
	
	if ( TimeSeries.begin()->second > 0.0 )
  {
		double FirstEnergyValue = TimeSeries.begin()->second;
		
		for ( auto & TimeStamp : TimeSeries )
			TimeStamp.second -= FirstEnergyValue;
	}

	// If the prediction series uses relative time, i.e. the time stamps starts 
	// at zero which means "now", they should be converted to absolute time 
	// stamps. A complicating factor is that the time series is a map, and
  // we need to change the time stamp, but the key in a map is a constant
  // so we need to construct a new map based on the changed times and then
  // re-initialise the time series.
  //
  // Note that the default is that the time series is given with absolute times
  // since in reality there will be delays from the production of the 
  // prediction until the prediction is received and some of the initial 
  // values must be skipped. A flag set at the compile time will decide if 
  // the prediction time series will be re-based, see the makefile.
	
	#ifdef RELATIVE_PREDICTION
  {
    Time CurrentTime = Now();
    
    std::map< Time, double > AbsoluteTimeSeries;
    
    for ( auto & TimePoint : TimeSeries )
      AbsoluteTimeSeries.emplace( TimePoint.first + CurrentTime, 
				                          TimePoint.second );
    
    TimeSeries.swap( AbsoluteTimeSeries );
  }
	#endif

  // When invoked at the first time, there is no prediction available, and 
  // the checking the prediction domain and value will cause an exception to 
  // be raised. Testing the validity of the interpolation would be a waste if 
  // the test was performed on every update, whereas there is no overhead of 
  // an exception if it is not raised. Adding the last value of the current 
  // prediction is therefore tried, and if and exception is thrown somewhere it 
  // will terminate the corrections and the prediction will be initialised with 
  // the provided time series starting at zero energy.
  
  try
  {
    double PredictionOriginEnergy;
    
    if ( Prediction.DomainUpper() <= TimeSeries.begin()->first )
      PredictionOriginEnergy = Prediction( Prediction.DomainUpper() );
    else
      PredictionOriginEnergy = Prediction( TimeSeries.begin()->first );
    
    for ( auto & TimeStamp : TimeSeries )
      TimeStamp.second += PredictionOriginEnergy;
  }
  catch ( std::exception & TheException )
  { }
  
  // With the understanding that the prediction means estimated future energy 
  // production starting from 'now', it may be necessary to keep some of the 
  // previous prediction as the history needed to ensure the complete 
  // scheduling of all tasks taking into account the ones that have started 
  // before 'now'. 
  //
  // It is also clear that there is in general a lag in the server side push 
  // of the prediction and its specialisation for each producer, which can be 
  // up to two hours. Hence, the received time series may contain predictions 
  // for 'now' - 2h, and this could be sufficient to cover all assigned and 
  // started consumers.
  //
  // If the provided prediction back history is insufficient, it must be padded
  // with samples from the current prediction covering the necessary interval 
  // to allow the complete consideration of all assigned consumers.
  //
  // If padding is necessary, the samples are taken at the same intervals as
  // the samples in the time series, but in the negative direction with respect
  // to the series starting point T. Say that the samples in the series with 
  // respect to the origin are at times T, T+t1, T+t2, T+t3... then the 
  // padded series will have times T-t1, T-t2, T-t3... until the necessary 
  // history is covered.
  //
  // Hence if a sample time is s(i) = T+t(i), then t(i) = s(i)-T and the 
  // negative sample times will be 
  //
  // 	T-t(i) = T - (s(i) - T) = 2T - s(i)
  //
  // There is no risk that this will result in a negative time since the time 
  // origin is POSIX time measured in seconds since first January 1970 and 
  // no prediction horizon will ever be that long, and no load will ever be 
  // that long.
  //
  // It is important to note an invariant for this algorithm to work: The 
  // length of any prediction must be greater or equal to the longest load that  
  // will be scheduled. This is an obvious assumption though, and should be 
  // enforced at the point where the loads are planned. Since the running loads  
  // were scheduled against the current prediction, it does cover the necessary 
  // historical range, and given that the new prediction covers the longest 
  // load possible, any past load will not be longer than this. 
  
  auto TimeIterator  		= TimeSeries.begin();
  Time SampleTime       = TimeIterator->first;
  const Time StartBasis = 2 * SampleTime;
  
  while ( ( PredictionOrigin < SampleTime ) && 
				  ( ++TimeIterator  != TimeSeries.end() ) )
  {
    SampleTime = StartBasis - TimeIterator->first;
    
    // Because the interpolation works with real number of double precision 
    // and the sample time is an integer number,  it could be that the sample 
    // time turns out to be evaluated as less than the least time in the 
    // prediction. In this case the value stored is the one given by the 
    // lower bound of the prediction.
    
    if ( SampleTime < Prediction.DomainLower() )
			SampleTime = static_cast < Time >( Prediction.DomainLower() );
    
    // A consequence of this is that the same sample time could occur 
    // multiple times as the lower bound of the prediction domain. For 
    // This reason, the emplace method cannot be used, and the pair must
		// be explicitly passed to the the insert function that will ignore the 
		// insertion if the key, i.e. the sample time, already exists.
    
    TimeSeries.insert( std::make_pair( SampleTime, Prediction( SampleTime ) ) );
  }
  
  // Computing the new interpolation object is trivial using its constructor
  // for maps.
  
  Prediction = Interpolation( TimeSeries );
  
  // Integrating this is done in parts producing the integral from one time 
  // point to the next, and then interpolating over this new time series. It 
  // should be noted that by definition the integral value at the first time 
  // stamp is zero. We always integrate from zero to let the integration 
  // algorithm cope with the interpolation method.

  // For now integration is done successively between two adjacent time stamps
  // of the time series, this may be less accurate than always integrating from 
  // the start, but the interpolating polynomial should be smooth and thus 
  // up to a small numerical error we should have the same result as if we had
  // integrated from the start of the time series every time.
  
  std::map< Time, double > IntegratedValues;
  
  SampleTime 	 	 = TimeSeries.begin()->first;   // Time of first sample
  double IntegratedValue = 0.0;				// Total until time
  
  for ( auto & PredictionPoint : TimeSeries )
  {
    IntegratedValue += Integral(Prediction, SampleTime, PredictionPoint.first);
    IntegratedValues.emplace( PredictionPoint.first, IntegratedValue );
    SampleTime = PredictionPoint.first;
  }
  
  // Finally we can interpolate this integrated series to enable fast access to
  // the integral values.
  
  IntegratedPrediction = Interpolation( IntegratedValues );
  
  // Then the scheduler is called upon to compute the new schedule for the 
  // updated prediction. This is triggered by sending a zero-energy load to 
  // the producer. Note that the prediction interval is transferred as the 
  // scheduling interval so that the scheduler knows the valid time window of
  // the prediction. The interval limits are taken from the time series to 
  // avoid converting the domain limits of the prediction from double to 
  // the integral time values (with possible numeric consequences).
  
  Send( Producer::ScheduleCommand( 
			  TimeSeries.begin()->first, std::prev( TimeSeries.end() )->first, 
				0, 0.0 ), TheProducer );
}

// -----------------------------------------------------------------------------
// Constructor and destructor
// -----------------------------------------------------------------------------
// The constructor registers all the message handlers and then sets the initial 
// prediction calling directly the message handler.

Predictor::Predictor( const std::string & PredictionFile,
								      const Theron::Address & ProducerAddress,
								      const std::string & ActorName )
: Actor( ActorName ),
  StandardFallbackHandler( GetAddress().AsString() ),
  Prediction(), IntegratedPrediction(), TheProducer( ProducerAddress )
{
  RegisterHandler(this, &Predictor::ComputeObjectiveValue );
  RegisterHandler(this, &Predictor::FindTimeRoot 	  );
  RegisterHandler(this, &Predictor::UpdatePrediction      );
  RegisterHandler(this, &Predictor::SetPredictionOrigin   );
	
  // The prediction origin is initialised to the maximal possible value in 
  // order to prevent the addition of a non-existing history on the first 
  // prediction update.
  
  PredictionOrigin = std::numeric_limits< Time >::max();
  
  // Then we can set the initial prediction. Note that this is presented as 
  // a request from the producer, which will cause the update handler to 
  // send the prediction interval back to the producer and request its  
  // schedule to be re-computed - this should be harmless if there are no 
  // associated consumers for the producer.
  
  UpdatePrediction( PredictionFile, TheProducer  );
}


} // end namespace CoSSMic
