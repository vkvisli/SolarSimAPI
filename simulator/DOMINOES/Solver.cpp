/*==============================================================================
Solver

This is the implementation of the solver class

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#include <algorithm>                         // For adding vector elements
#include <sstream>                           // For nicely formatted errors
#include <stdexcept>                         // Standard exceptions.
#include <filesystem>                        // Dealing with files
#include <fstream>                           // Reading and writing files
#include <iterator>                          // Iterator next
#include <cstdlib>                           // Integer division

#include <boost/numeric/conversion/cast.hpp> // Casting numeric types
#include "csv.h"                             // The CSV parser
#include "RandomGenerator.hpp"               // LA-Framework generator

#include "CSVtoTimeSeries.hpp"               // CSV time series parser
#include "Consumer.hpp"                      // The Consumer class
#include "Solver.hpp"                        // The solver class
#include "Interpolation.hpp"                 // Interpolating object

/*==============================================================================

The Energy Objective receiver

==============================================================================*/
//
// Setting the production values implies that the Negative production vector is
// resized to the length of the production sample vector, and then filled with
// the negative elements.

void Dominoes::Solver::EnergyObjective::SetProductionValues(
  const std::vector< double > & Values )
{

	if ( Values.size() == ProductionSamples->size() )
  {
		double PastCummulativeProduction = 0.0;

	  IntervalProduction.resize( Values.size() );
		TotalConsumption.resize(   Values.size() );

	  std::transform( Values.begin(), Values.end(), IntervalProduction.begin(),
	    [ &PastCummulativeProduction ]( double CummulativeProduction)->double{
				  double DeltaProduction =
				         CummulativeProduction - PastCummulativeProduction;
				  PastCummulativeProduction = CummulativeProduction;
					return DeltaProduction; });
	}
	else
  {
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
		             << "The size of the production values vector "
								 << Values.size() << " is not equal to the number of "
								 << "sample times " << ProductionSamples->size();

	  throw std::logic_error( ErrorMessage.str() );
	}
}

// The reset function simply copies the pre-computed negative production values
// to the net energy vector so that it is ready to accumulate the consumption
// values as they arrive.

void Dominoes::Solver::EnergyObjective::Reset( void )
{
  TotalConsumption.assign( ProductionSamples->size(), 0.0 );
}

// The handler receives the consumption for a single consumer and adds these
// to the total consumption vector.

void Dominoes::Solver::EnergyObjective::SingleConsumption(
  const std::vector< double > & ConsumptionValues,
  const Theron::Address TheConsumer )
{
  if ( ConsumptionValues.size() == TotalConsumption.size() )
    std::transform( ConsumptionValues.begin(), ConsumptionValues.end(),
                    TotalConsumption.begin(), TotalConsumption.begin(),
                    []( double Consumption, double OldTotal )->double{
                        return OldTotal + Consumption; });
  else
  {
    std::ostringstream ErrorMessage;

    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
                 << "The size of the a consumers consumption vector "
                 << ConsumptionValues.size()
                 << " does not match the size of the production vector "
                 << TotalConsumption.size();

    throw std::logic_error( ErrorMessage.str() );
  }
}


// The net energy that must be taken from the grid is now equal to the
// sum of the differences between the production and the consumption in each
// interval. This is first computed for the sample times and then interpolated
// and integrated to find the total energy that is needed by the building.

double Dominoes::Solver::EnergyObjective::Value( void )
{
	std::vector< double > GridEnergy( TotalConsumption.size(), 0 );

	std::transform( IntervalProduction.begin(), IntervalProduction.end(),
									TotalConsumption.begin(), GridEnergy.begin(),
									[](double Production, double Consumption)->double{
										if ( Production >= Consumption )
											return 0.0;
										else
											return Consumption - Production;
									} );

  Interpolation
  EnergyValue( ProductionSamples->begin(), ProductionSamples->end(),
               GridEnergy.begin(), GridEnergy.end() );

  return Integral( EnergyValue,
                   EnergyValue.DomainLower(), EnergyValue.DomainUpper() );
}

// The function to request the time coverage of a consumer is trivial as it
// is only sending a request message to the given consumer.

void Dominoes::Solver::EnergyObjective::RequestConsumptionCoverage(
	   const Dominoes::Consumer & TheConsumer )
{
	Send( Consumer::TimeCoverageRequest(), TheConsumer.GetAddress() );
}

// The function receiving the consumption coverage must adjust the time axis
// and extend the stored negative production for this new range only if the
// consumption coverage extends outside of the already covered production
// time span.

void Dominoes::Solver::EnergyObjective::ExtendTimeAxis(
	   const Consumer::TimeCoverage & ConsumerCoverage,
		 const Theron::Address TheConsumer )
{
	auto FirstProductionTime = ProductionSamples->begin();
  auto LastProductionTime  = ProductionSamples->rbegin();

  // Extending the time axis to the left by inserting elements before the first
	// time point. This is slightly more complicated than the normal vector
	// extension at the end where both push back or resize could have been used.
	// The calculations progresses by first finding the time between two
	// consecutive samples looking at the time difference between the first
	// production time and the second production time. Then the missing time
	// is computed and finally the number of new samples to add is computed.
	// A vector with the new time samples will be generated and then inserted at
	// the front of the vector.

	if ( ConsumerCoverage.lower() < *FirstProductionTime )
  {
		CoSSMic::Time DeltaT =
									  *std::next( FirstProductionTime ) - *FirstProductionTime,
									TimeToCover =
									  *FirstProductionTime - ConsumerCoverage.lower();

		// The elements to add is the ceiling of the division between the time to
		// cover and the the delta sample time. In general, this division will
		// result in a reminder and one more time sample should be added, i.e.
		// the ceiling of the division is used. The boost numeric cast is used to
		// ensure that the number is not too big to be allocated to a vector.

		Index ElementsToAdd = boost::numeric_cast< Index >(
																     std::div( TimeToCover, DeltaT ).quot + 1 );

		// Since the time stamp must decrease for each sample to the left of
		// the current first production time, the delta must be subtracted.
		// unfortunately there is no constructor for a vector allowing a generator
		// function, and the standard generator function takes two iterators
		// implying that the vector has to be directly initialised.

	  std::vector< CoSSMic::Time > LeftTimes( ElementsToAdd );

		// Consider the situation where the lower limit of the consumer coverage
		// is less than the delta time away from zero, then subtracting the
		// delta time for the next sample would result in a negative time (rather
		// a wrap-around of the integer representing the time). It is therefore
		// necessary to check if the first element of the vector must be zero or
		// the natural sample before the first consumer coverage time.

		auto TimeStamp = LeftTimes.begin();

		if ( ConsumerCoverage.lower() < DeltaT )
		{
			*TimeStamp     = 0;
			*(++TimeStamp) = *FirstProductionTime - (ElementsToAdd-1)*DeltaT;
		}
		else
			*TimeStamp = *FirstProductionTime - ElementsToAdd * DeltaT;

		// Then the rest of the samples can be initialised in a simple iteration

		while ( TimeStamp != LeftTimes.end() )
		{
			CoSSMic::Time TimeValue = *(TimeStamp++) + DeltaT;
			*TimeStamp              = TimeValue;
		}

		// The new production sample times are inserted at the start of the
		// production samples.

		ProductionSamples->insert( ProductionSamples->begin(),
															 LeftTimes.begin(), LeftTimes.end() );

		// There is no production for these times, but the size of the production
		// vector should be the same as the production samples vector.

		IntervalProduction.insert( IntervalProduction.begin(),
															 ElementsToAdd, 0.0 );
	}

	// Extending the time axis and the energy values to the right: First the
  // number of elements to add is computed by finding the sample time
	// between the last two samples in the production series, and the time to
	// cover which is the difference between the upper limit of the consumer
	// coverage and the end of the production time. Then the number of samples
	// to add is the time to cover divided by the sample time.

	if ( *LastProductionTime < ConsumerCoverage.upper() )
  {
		CoSSMic::Time DeltaT =
                    *LastProductionTime - *std::next( LastProductionTime ),
		              TimeToCover =
	                  ConsumerCoverage.upper() - *LastProductionTime;

		Index ElementsToAdd = boost::numeric_cast< Index >(
																				 std::div( TimeToCover, DeltaT ).quot );

		// There is a series of time stamps that must be generated starting from
		// the last time. The production for these added intervals is obviously
		// zero.

		CoSSMic::Time TimeStamp  = *LastProductionTime;

		// Then the elements can be inserted. Note that starting the index at
		// zero and stopping at the number of elements to add essentially inserts
		// one more element than the pure division corresponding to the ceiling of
		// the previous division.

		for ( Index i = 0; i <= ElementsToAdd; i++ )
	  {
			TimeStamp += DeltaT;
			ProductionSamples->push_back( TimeStamp  );
			IntervalProduction.push_back( 0.0 );
		}
	}
}

// The constructor creates a legal, but useless object as it must first be
// initialised

Dominoes::Solver::EnergyObjective::EnergyObjective(
  const SampleTime & ProductionTimes )
: Theron::Receiver(),
  IntervalProduction(), TotalConsumption(),
  ProductionSamples( ProductionTimes )
{
  RegisterHandler( this, &EnergyObjective::SingleConsumption );
	RegisterHandler( this, &EnergyObjective::ExtendTimeAxis    );
}

/*==============================================================================

Solver

==============================================================================*/
//
// The objective function dispatches the solution candidate received to the
// consumers as start times, and then waits for them to return the consumption
// values. It should be noted that as there in general can be more threads
// than there are cores in a multi-actor system, many messages can be handled
// by the energy objective before control is returned to the solver thread.
// The Theoron Wait function is also specified to wait for a given number of
// messages, but it could return earlier, and it could even return with no
// message processed. Thus, the return value must be verified.

Optimization::VariableType Dominoes::Solver::ObjectiveFunction(
  const Optimization::Variables & VariableValues )
{
  auto TheConsumer   = Consumers.begin();
  auto ConsumersToGo = Consumers.size();

  EnergyCost.Reset();

  for ( const Optimization::VariableType & AssignedStartTime : VariableValues )
    Theron::Actor::Send(
        boost::numeric_cast< CoSSMic::Time >( AssignedStartTime ),
        EnergyCost.GetAddress(), (TheConsumer++)->GetAddress() );

  while ( ConsumersToGo )
    ConsumersToGo -= EnergyCost.Wait( ConsumersToGo );

  return EnergyCost.Value();
}

// Setting the bound constraints is simply scanning the consumer vector and
// and store the start time intervals.

std::vector< Optimization::NonLinear::Bound::Interval >
Dominoes::Solver::BoundConstraints( void )
{
  std::vector< Interval > Bounds;

  for ( const Consumer & TheConsumer : Consumers )
    Bounds.push_back( TheConsumer.GetStartInterval() );

  return Bounds;
}

// The actual optimisation will take place in a dedicated function that will
// end by writing out the assigned start times to the file whose name is given
// as argument to the function.

void Dominoes::Solver::AssignStartTimes( const std::filesystem::path & ASTFile,
																				 const CoSSMic::TimeInterval & SolarDay )
{
   Optimization::Variables InitialValues;

   // If the solar day is not defined the initial value is just set to a
   // random time in the start interval of the consumer to avoid the solver
   // to lock into a particular solution by setting the starting point to
   // a fixed value in the interval. However,  if the solar day is given,
   // then the starting time is drawn randomly from the part of the solar
   // day overlapping with the start interval. In the unlikely event that there
   // is no overlap the requested start interval is entirely outside of the
   // production and it does not matter when the consumer starts, and the
   // start time is again picked at random over the start interval for the
   // consumer.

	 if( boost::numeric::empty( SolarDay ) )
	   for( const Consumer & TheConsumer : Consumers )
	     InitialValues.push_back(
	                   Random::Number( TheConsumer.GetStartInterval() ) );
	 else
		 for( const Consumer & TheConsumer : Consumers )
		 {
			 CoSSMic::Time EarliestStart = std::max( SolarDay.lower(),
																		 TheConsumer.GetStartInterval().lower() ),
										 LatestStart   = std::min( SolarDay.upper(),
										                 TheConsumer.GetStartInterval().upper() );

       if( LatestStart <= EarliestStart )
	       InitialValues.push_back(
				               Random::Number( TheConsumer.GetStartInterval() ) );
       else
				 InitialValues.push_back( Random::Number(
										   CoSSMic::TimeInterval( EarliestStart,  LatestStart ) ) );
		 }

   // Then solving the optimal start time problem

   auto Solution = FindSolution( InitialValues );

   // Then open the assigned start time file and output the total grid energy
   // value

   std::ofstream Result( ASTFile );

   Result << "Total grid energy " << Solution.ObjectiveValue << std::endl;

   // Store the solution to the given file before closing.

   auto Consumer  = Consumers.begin();
   auto StartTime = Solution.VariableValues.begin();

   while( Consumer != Consumers.end() )
   {
     Result << Consumer->GetName() << " "
            << static_cast< CoSSMic::Time >(*StartTime)
            << std::endl;
     ++Consumer; ++StartTime;
   }

   Result.close();
}

/*==============================================================================

Constructor & Destructor

==============================================================================*/
//
// The constructor initialises the producer time series first, and then the
// consumers.

Dominoes::Solver::Solver( const std::filesystem::path ProducerFile,
                          const std::filesystem::path ConsumerEvents )
: NL::Optimizer< NL::Algorithm::Local::Approximation::Rescaling >(),
  Consumers(), ProductionSamples( new std::vector< CoSSMic::Time >() ),
  EnergyCost( ProductionSamples )
{
  // The producer time series can be imported using the standard CSV parsing
  // function. However, this will return a map, and the solver has two vectors
  // of data.

  std::vector< double > ProducedEnergy;

  for ( const auto & ProductionPoint : CoSSMic::CSVtoTimeSeries(ProducerFile) )
  {
    ProductionSamples->push_back( ProductionPoint.first );
    ProducedEnergy.push_back( ProductionPoint.second );
  }

  EnergyCost.SetProductionValues( ProducedEnergy );

  // In CoSSMic it was assumed that the consumption devices would become
  // available one by one over time. This means that devices that has already
  // been started should not be re-scheduled. This complicated the whole
  // problem. In the simplified Dominoes simulator, all devices to be scheduled
  // are available the start of the simulation. Hence the CSV file has the
  // following format:
  // Earliest start time, latest start time, ID, Consumption profile file name
  // The CSV parser uses C-style returns and requires separate variables to
  // hold the individual parts of the line being parsed.

  io::CSVReader<4, io::trim_chars<'\t'>, io::no_quote_escape<';'> >
  CSVParser( ConsumerEvents );

  CSVParser.set_header("ID","EST","LST","ConsumptionFile");

  // Variables to hold the parsed numerical values

  CoSSMic::Time EarliestStartTime, LatestStartTime;
  std::string   DeviceID, ConsumptionProfile;

  // The consumer events file is read line by line and the values obtained
  // used to create a new consumer actor. The consumer actor's constructor
	// will send a message to the consumer actor to load the consumption profile
	// file.

  while ( CSVParser.read_row( DeviceID, EarliestStartTime, LatestStartTime,
                              ConsumptionProfile ) )
    Consumers.emplace_back( DeviceID, EarliestStartTime, LatestStartTime,
                            ConsumptionProfile, ProductionSamples );

	// Then the time coverage of each consumer is requested to ensure that
	// the time axis covers all possible consumption intervals. The interaction
	// with the consumers is done by the Energy Cost object.

	for ( const Consumer & TheConsumer : Consumers )
		EnergyCost.RequestConsumptionCoverage( TheConsumer );

	// Finally it is just to wait until all consumers have loaded their
	// consumption profiles and reported back their time coverage. Only then
	// the problem is properly set up.

	auto ConsumersToGo = Consumers.size();

	while ( ConsumersToGo )
    ConsumersToGo -= EnergyCost.Wait( ConsumersToGo );
}

// The destructor simply removes the consumers.

Dominoes::Solver::~Solver( void )
{
  Consumers.clear();
}
