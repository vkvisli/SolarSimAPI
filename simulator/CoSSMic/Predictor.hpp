/*=============================================================================
  Predictor
  
  The main role of the predictor is to keep the most updated production 
  prediction in raw and integral form as interpolated functions. When the 
  prediction time series is received, it is first interpolated and then this 
  interpolated function is integrated at the same time stamps as in the original
  prediction series. 
  
  A secondary task of the predictor is to participate to the acceptance of 
  incoming scheduling requests. The mechanism is explained in the Load Scheduler
  header. Essentially all scheduled consumers will report their total energy 
  consumed at the time they finish, and then the cumulative total load is 
  computed by adding these together for each time stamp. If a new load is to 
  be admitted there must be enough spare capacity at the times of maxiumum 
  consumption of the already scheduled loads to execute the load at any of 
  these times. This is verified by the predictor.
  
  Finally, the predictor will manage the loads that have started execution. 
  These will be passed from the load scheduler as it is not supposed to stop 
  a running job, however their presence has the effect of reducing the available
  produced energy for the period the load is active, and this must be taken into
  account when predicting the energy available for not yet scheduled jobs.
  
  REFERENCES:
  
  [1] Geir Horn: Scheduling Time Variant Jobs on a Time Variant Resource, 
      in the Proceedings of The 7th Multidisciplinary International Conference 
      on Scheduling : Theory and Applications (MISTA 2015), Zdenek Hanzálek et
      al. (eds.), pp. 914-917, Prague, Czech Republic, 25-28 August 2015.
      
  Author: Geir Horn, University of Oslo, 2015-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef PREDICTOR
#define PREDICTOR

#include <memory>
#include <list>

#include "Actor.hpp"
#include "StandardFallbackHandler.hpp"

#include "Interpolation.hpp"
#include "TimeInterval.hpp"
#include "Producer.hpp"

#ifdef CoSSMic_DEBUG
	#include "ConsolePrint.hpp"
#endif


namespace CoSSMic {

class Predictor : public virtual Theron::Actor,
									public virtual Theron::StandardFallbackHandler
{
private:
  
  // The prediction is stored as an interpolated function. It also stores the 
  // integrated prediction to be able to respond quickly during the computation
  // of the objective function.
  
  Interpolation Prediction, IntegratedPrediction;
  
  // It is necessary to remember the address of the producer in order to 
  // properly acknowledge the removal of finished loads, and the scheduler in 
  // order to trigger the production of a new schedule if the prediction is 
  // updated.
  
  Theron::Address TheProducer;
  
 // ---------------------------------------------------------------------------
 // Objective value computation
 // ---------------------------------------------------------------------------
 // To obtain the objective function value, the scheduler will ask the predictor
 // to compute its part of the value based on each of the consumption 
 // intervals. Hence it sends the consumption interval, and the handler will 
 // respond with a double as the value.
  
 void ComputeObjectiveValue( const TimeInterval & ConsumptionInterval,
												     const Theron::Address Sender );
 
 // If there is only a single consumer associated with this producer, a 
 // different heuristic will be used: We will then solve for the point in time
 // when the cumulative predicted production equals the load's total production
 // and return this time point to the scheduler. It will return the solution 
 // as an assigned start time to the scheduler, and this time may potentially
 // be empty if no solution could be found.
 
 void FindTimeRoot( const double & TotalLoadConsumption, 
								    const Theron::Address TheSchedulerActor   );
 
 // ---------------------------------------------------------------------------
 // Update the prediction profile
 // ---------------------------------------------------------------------------
 // There is a handler for this command which is forwarded from the PV Producer 
 // when it arrives from the routine producing the prediction profile for this
 // producer. Since the update prediction command is defined inside the 
 // PV Producer and the PV Producer delegates the processing to this predictor,
 // then it would be a circular dependency should this handler also require 
 // the same prediction command. Fortunately, only the file name is needed and 
 // this is therefore used as the updating message.

 void UpdatePrediction( const std::string & TheFilename, 
												const Theron::Address TheProducer );

 // ---------------------------------------------------------------------------
 // Update the prediction domain
 // ---------------------------------------------------------------------------
 // The scheduling will always be done over a domain whose lower bound is the 
 // minimum of the current time and the earliest start time of a load (which 
 // could be in the past). As consumers finish their execution, the earliest 
 // start time could move to another load's start time. In other words, the 
 // prediction function will always contain some past time history to allow 
 // a coherent scheduling and the length of this past time interval will change
 // dynamically as consumers finish. When a load finishes, the PV Producer will
 // send the new least starting time to the predictor, and if this is less than
 // now and larger than the current start of the prediction domain, the 
 // prediction and the integrated prediction will both be updated.
 
 void SetPredictionOrigin( const Time & MinStartTime, 
												   const Theron::Address TheProducer );
 
 // The current prediction origin is stored in a time variable and taken into
 // account when the next prediction update occurs.
 
 Time PredictionOrigin;
 
 // ---------------------------------------------------------------------------
 // Constructor and destructor
 // ---------------------------------------------------------------------------
 // The constructor takes the file name of the initial prediction 
 // as arguments since it makes no sense creating a predictor without a 
 // prediction. It also needs the address of the producer for which it provides
 // the prediction, and optionally an identifying actor name.
 
public:
 
 Predictor( const std::string & PredictionFile,
				    const Theron::Address & ProducerAddress,
				    const std::string & ActorName = std::string() );
 
 // The destructor is simply an entry point for allowing the destructor of the 
 // internal objects to be executed.
 
 virtual ~Predictor()
 { }
 
};

}				// End namespace CoSSMic
#endif 	// PREDICTOR
