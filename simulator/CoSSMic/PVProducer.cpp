/*=============================================================================
  PVProducer
  
  The PV Producer orchestrates several other actors (or receivers if there is 
  a need to wait for the response messages to arrive). The Predictor takes 
  care of the predicted production; the consumer proxies maintains the demand 
  side; and the consumption intervals and the single consumer heuristic 
  participate in the scheduling. 
      
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <stdexcept>			              // For standard errors
#include <sstream>			                // For string construction
#include <map>				                  // For time series
#include <list>				                  // For lists of objects
#include <atomic>			                  // For thread safe counters
#include <chrono>			                  // For system system

#include <gsl/gsl_roots.h> 		          // For the equality of two functions.
#include <gsl/gsl_errno.h> 		          // For error messages from GSL
#include <nlopt.hpp>    		            // Solving the optimisation problem
#include <boost/numeric/interval.hpp>   // For intervals

#include "Actor.hpp"		          			// The Theron++ actor framework
#include "RandomGenerator.hpp"          // The random number generator
#include "PresentationLayer.hpp"	      // The message presentation layer

#include "TimeInterval.hpp"		          // For time related functions
#include "ConsumerProxy.hpp"		        // For the consumer interaction
#include "PVProducer.hpp"		            // The actual producer class
#include "Clock.hpp"			              // To get Now from system or simulator

#ifdef CoSSMic_DEBUG
  #include "ConsolePrint.hpp"						// Debug messages
#endif

namespace CoSSMic 
{
/*****************************************************************************
  New prediction command
******************************************************************************/
//
// The message sent across the network is serialized and it must therefore 
// support the serialising and de-serialising methods. Serialising this message
// is trivial.

Theron::SerialMessage::Payload 
PVProducer::NewPrediction::Serialize( void ) const
{
  return "PREDICTION_UPDATE " + NewPredictionFile;
}

// The de-serialising is also straightforward following the patterns of most
// de-serialising functions.

bool PVProducer::NewPrediction::Deserialize( 
     const Theron::SerialMessage::Payload & Payload )
{
  std::istringstream Message( Payload );
  std::string Command;
  
  Message >> Command;
  
  if ( Command == "PREDICTION_UPDATE" )
  {
    Message >> NewPredictionFile;
    return true;
  }
  else
    return false;
}

/*****************************************************************************
  Kill proxy
******************************************************************************/
// 
// As explained in the header file, the earliest start time set for a consumer
// defines the start of the scheduling operation provided that this start time 
// is such that the load will have started during the next schedule computation.
//
// One of the reasons for killing a consumer proxy is that the associated 
// consumer has finished executing. This consumer could be the one with the 
// least start time, and in this case the next minimal start time must be 
// identified as the start of the scheduling interval. 
//
// If the nearest start time epoch is in the past, the origin of the prediction  
// should be moved to this least start time. However, if the nearest start time
// is in the future, the current prediction origin must be kept since a new 
// consumer may arrive that can start between now and the earliest start time 
// of the current set of consumers.

void PVProducer::KillProxy( const Producer::KillProxyCommand & TheCommand, 
												    const Theron::Address TheConsumer    )
{
  // First a reference to the consumer to be killed is obtained
  
  auto ConsumerToKill = FindConsumer( TheConsumer );
  
  // This should always return a valid consumer, otherwise a run-time error 
  // will be signalled.
  
  if ( ConsumerToKill != EndConsumer() )
  {
    // If the consumer to be removed is the consumer with the earliest start
    // time, it is necessary to invalidate the start time reference.
    
    if ( ConsumerToKill == EarliestStartingConsumer )
      EarliestStartingConsumer = EndConsumer();
    
    // Then the consumer proxy can be removed and the removal acknowledged by 
    // the standard kill proxy function.
    
    Producer::KillProxy( TheCommand, TheConsumer );
    
    // If the earliest starting consumer reference is invalid and there are 
    // more consumers assigned to this producer, a new earliest start time 
    // must be found.
    
    if ( (EarliestStartingConsumer == EndConsumer() ) && 
         (NumberOfConsumers() > 0) )
    {
      // It is guaranteed that the first consumer exists, and therefore it is 
      // a candidate for having the earliest possible start time.
      
      EarliestStartingConsumer = FirstConsumer();
      
      // Then all the other consumers but the first must be checked 
      
      for ( auto Consumer = FirstConsumer()++; Consumer != EndConsumer();
		        ++Consumer )
      {
				if ( (*Consumer)->GetStartTime() )
				  if( ! (*EarliestStartingConsumer)->GetStartTime() ||
				      ( (*Consumer)->GetStartTime() < 
                (*EarliestStartingConsumer)->GetStartTime() ) )
				    EarliestStartingConsumer = Consumer;
      }
      
      // Note that there is no guarantee that any of the assigned consumers 
      // will have a valid start time - it is only certain that the first 
      // consumer exist. However, if a valid start time has been found it 
      // will be sent to predictor the as the new origin of the prediction 
      // domain.
      
      if ( (*EarliestStartingConsumer)->GetStartTime() )
	      Send( (*EarliestStartingConsumer)->GetStartTime().value(), 
	            Prediction->GetAddress() );
    }
  }
  else
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage <<  __FILE__ << " at line " << __LINE__ << ": "
						     << GetAddress().AsString() 
								 << " asked to remove unassigned proxy for consumer "
								 << TheConsumer.AsString();
		 
    throw std::runtime_error( ErrorMessage.str() );
  }
}

/*****************************************************************************
  Load Scheduling
******************************************************************************/
// 
// The load scheduling is based on a general non-linear optimisation minimising
// the need for external power from the electricity grid. Solving for the 
// minimum with only one load makes no sense, and first a special heuristic 
// is defined for this case.
//
// -----------------------------------------------------------------------------
// Single consumer heuristic
// -----------------------------------------------------------------------------
//
// In the case where there is only a single consumer assigned to the producer,
// its start time can be found by solving the point in time when the total 
// cumulative consumption of the load equals the the total cumulative 
// prediction. Then the load must be started duration time units before this
// time, and if this is before its earliest start time, it is started at its 
// earliest start time. 
//
// The following receiver sends the total consumption to the predictor, which 
// computes the equality point and returns the time of this point. Then the 
// start time of the consumer is computed and returned to the consumer proxy 
// agent.

class SingleConsumerHeuristic : public Theron::Receiver
{
private: 
  
  TimeInterval	 AllowedInterval;	  			// Consumer constraints
  Time		       JobDuration;							// The time the job needs 
  bool		       Done; 	  								// Flag for completion
  
  Producer::AssignedStartTime StartTime;	// The start time if any
  
public:
  
  // The result of this earliest possible finish time is returned as an 
  // assigned start time message. Based on this, the load's feasible start 
  // time is computed if a solution could be found, otherwise and empty start
  // time is returned to the consumer.
  
  void ComputeStartTime( const Producer::AssignedStartTime & EarliestEnd, 
												 const Theron::Address ThePredictor )
  {
    if ( EarliestEnd )
    {
      // If the end time found is before the time the job would end if it 
      // is started at the earliest possible start time, there is enough 
      // production predicted for the load to start at its earliest possibility.
      
      if ( EarliestEnd.value() <= AllowedInterval.lower() + JobDuration )
	      StartTime = Producer::AssignedStartTime( AllowedInterval.lower() );
      else
      {
				// Since the load cannot be started at its earliest start, but has to
				// be delayed and start so that the job finishes at the earliest end 
				// time (since this is the earliest start possible). However, this 
				// requires that the needed start time is in the allowed start time 
				// interval for the load. If this is not the case, it is not possible 
				// to schedule this load on this producer and the start time is 
				// returned unassigned.
				
				if ( boost::numeric::in( EarliestEnd.value() - JobDuration, 
																 AllowedInterval ) )
				  StartTime = Producer::AssignedStartTime( 
														     EarliestEnd.value() - JobDuration );
      }
    }
    
    Done = true;
  }
  
  // There is a small function to wait for the solution to be found and returned
  // and after this function the object can safely be destroyed.
  
  Producer::AssignedStartTime ComputeSolution( void )
  {
    while ( !Done ) Wait();
    
    return StartTime;
  }
  
  // The constructor sends a start time proposal to the concerned load with 
  // the consumption interval equal to the load's own duration counted from 
  // the earliest start of the load.
  
  SingleConsumerHeuristic( Theron::Framework & TheFramework,
			   const TimeInterval & 		StartTimeInterval,
			   const Time 				  		Duration,
			   double 									JobEnergy,
			   const Theron::Address & 	ThePredictor  )
  : Theron::Receiver(), AllowedInterval( StartTimeInterval ), 
    JobDuration( Duration ), StartTime()
  {
    Done    = false;
    
    // Register the handlers for messages
    
    RegisterHandler( this, &SingleConsumerHeuristic::ComputeStartTime );
    
    // Then the total energy of the job is sent to the Predictor in order 
    // to have it compute the first time when the predicted production equals
    // this request, i.e. the earliest time the load can finish.
    
    Send( JobEnergy, ThePredictor );
  }
  
};

// -----------------------------------------------------------------------------
// Partitioning the load set
// -----------------------------------------------------------------------------
//
// It is necessary to split the loads into those that have started, or are 
// likely to start before the scheduling operation has finished, and those 
// loads that can be assigned (new) start times some time in the future. The 
// time necessary for scheduling is the exponentially weighted average where 
// the emphasis is on the last 100 scheduling operations.

void PVProducer::PartitionLoads(void)
{
  // The partitioning of the previous scheduling operation is cleared first.
  
  ActiveLoads.clear();
  StartedLoads.clear();
  FutureLoads.clear();
  
  // Any start time before a time horizon is considered to be already started
  // and inserted into the set of started loads. First this horizon is computed
  
  Time NowHorizon = Now() + std::chrono::system_clock::to_time_t( 
   std::chrono::system_clock::time_point( TimeOffset ) );
	
	// The scheduling can only take place in the future, and not over the 
	// whole prediction interval which may already contain past times.
	
	TimeInterval SchedulingInterval( std::max( Now(), PredictionDomain.lower() ), 
																	 PredictionDomain.upper() );
  
  // The partitioning means to allocate the various consumers into started 
  // loads, i.e. loads that cannot be re-scheduled; active loads, i.e. loads 
  // having start time intervals that (partially) overlap with the prediction 
  // interval; or future loads for which the allowed start time window entirely 
  // is to the right (in the future) of the prediction.
  
  for ( auto Consumer = FirstConsumer(); Consumer != EndConsumer(); ++Consumer )
	{
	  auto TheConsumer( *Consumer );
	  
    if ( TheConsumer->GetStartTime() && 
			 ( TheConsumer->GetStartTime().value() <= NowHorizon ) )
      StartedLoads.push_back( Consumer );
    else 
      if ( boost::numeric::overlap( TheConsumer->AllowedInterval(), 
															      SchedulingInterval ) )
				ActiveLoads.push_back( Consumer );
      else if ( SchedulingInterval < TheConsumer->AllowedInterval() )
				FutureLoads.push_back( Consumer );
      else
      {
				// Something is seriously wrong if a load cannot be allocated to any 
				// of the intervals. The only thing to do is throwing an exception
				// informing the user about the failure.
				
				std::ostringstream ErrorMessage;
				
				ErrorMessage <<  __FILE__ << " at line " << __LINE__ << ": "
										 << "PV Producer " << GetAddress().AsString() 
								     << " has consumer " 
								     << TheConsumer->GetConsumer().AsString()
								     << " with start time window " 
								     << TheConsumer->AllowedInterval()
								     << " outside of prediction domain " << PredictionDomain;
					     
				throw std::runtime_error( ErrorMessage.str() );
      }
  }
      
  // The result of the partitioning is reported on the console if the debug
  // prints are effective
  
  #ifdef CoSSMic_DEBUG
    Theron::ConsolePrint DebugMessages;

    DebugMessages << "Producer " << GetAddress().AsString() << " at time " 
								  << NowHorizon << " :" << std::endl;
    DebugMessages << "Prediction domain = "<< PredictionDomain << std::endl;
    DebugMessages << "Started loads = " << StartedLoads.size() << std::endl;
    DebugMessages << "Active loads  = " << ActiveLoads.size() << std::endl;
    DebugMessages << "Future loads  = " << FutureLoads.size() << std::endl;
  #endif
}

// -----------------------------------------------------------------------------
// Consumption intervals
// -----------------------------------------------------------------------------
//
// The objective function is given in [1] as a sum over all consumption 
// periods consisting of two parts: The total energy demanded by a load in 
// that period, and the integral of the predicted energy in the period. 
//
// The concept of consumption interval is developed first as a separate 
// class that takes a proposed start time and a managed consumer proxy object. 
// It will accept a suggested interval if the provided consumption interval 
// overlaps with the current interval, or if the current interval is empty.
// If it accepts the consumption interval, it will also keep a pointer to 
// the consumer proxies that are allocated to this interval. 
//
// There are two salient features to note:
//
// 1) Each consumption interval is an isolated unit. The consumers can be 
//    allocated to one and only one consumption interval. Hence the 
//    computations of the objective function for each consumption interval 
//    can happen in parallel.
//
// 2) For each consumption interval the contribution of the loads energy 
//    consumption is independent of the predicted energy production over the 
//    same consumption interval. Hence these two terms can be computed in 
//    parallel. 
// 
// The architecture is therefore as follows: 
//
// A. Each consumer proxy is responsible for the calculations related to that 
//    consumer, and for that reason the consumer proxies are actors.
// C. There is a dedicated actor responsible for computing the predicted 
//    energy production. 
//
// Once the interval has been established, it will be communicated to the 
// consumer proxies and the predictor, and their responses collected in a 
// dedicated receiver to be defined below.

class ConsumptionInterval 
{
private:
  
  // For the consumer to be able to compute its contribution to the total energy
  // it needs the start time proposal and the length of the interval. Hence 
  // we have to remember both which consumers that are allocated to this 
  // consumption interval, and the start times proposed for them.
  
  class AllocatedConsumer
  {
  public: 
    
    Producer::ConsumerReference TheConsumer;
    double 											ProposedStartTime;
    
    AllocatedConsumer( const Producer::ConsumerReference & ConsumerToStore,
							         double ItsStartTime )
    : TheConsumer( ConsumerToStore )
    {
      ProposedStartTime = ItsStartTime;
    }
  };
  
  // Then we maintain a list of consumers allocated to this consumption 
  // interval, and note that there will always be at least one consumer in the 
  // interval where the singularity occurs when the consumption interval is 
  // identical to the consumer's consumption interval.
  
  std::list< AllocatedConsumer > AssociatedConsumers;
	
  // The length of the consumption interval for all the associated consumers 
  // is stored as a simple time interval.
  
  TimeInterval TheInterval;
  
  // The constructor takes a reference to the managed proxy structure and the 
  // proposed start time for this interval, and constructs the initial 
  // interval. 

public:
  
  ConsumptionInterval( const Producer::ConsumerReference & TheConsumer,
								       double SuggestedStartTime )
  : AssociatedConsumers(), 
    TheInterval( SuggestedStartTime, 
								 SuggestedStartTime + (*TheConsumer)->GetDuration() )
  {
    AssociatedConsumers.emplace_back( TheConsumer, SuggestedStartTime );
  }
  
  // The union function accepts the provided consumer if its consumption  
  // interval overlaps the current interval, otherwise it is refused and false 
  // is returned.
  
  bool Union( const Producer::ConsumerReference & TheConsumer, 
				      double SuggestedStartTime )
  {
    TimeInterval ConsumerActivity( SuggestedStartTime, SuggestedStartTime 
																	   + (*TheConsumer)->GetDuration() );
    
    if ( boost::numeric::overlap( TheInterval, ConsumerActivity ) )
    {
      TheInterval = boost::numeric::hull( TheInterval, ConsumerActivity );
      AssociatedConsumers.emplace_back( TheConsumer, SuggestedStartTime );
      return true;
    }
    else
      return false;
  }
  
  // Once the interval has been completely established, i.e. no more consumers
  // will be added, we can start to compute the contribution of this consumption
  // interval to the overall objective function value. This is done by the 
  // receiver to follow next, and it is more efficient if it is allowed to 
  // read the consumption interval's data directly.
  
  friend class CollectContribution;

	// The destructor currently does nothing
  
  ~ConsumptionInterval( void )
  { }
};

// The contribution to the objective function is collected from each of the 
// consumption intervals by a receiver so that the owning thread can wait for 
// all the contributions to arrive. 

class CollectContribution : public Theron::Actor
{
private:
	
	// It keeps a copy of the address for the predictor
	
	Theron::Address ThePredictor;
	
	double 			 TotalValue;
	volatile unsigned int OutstandingRequests;
	
	// The reception of the values is made to a handler that notifies a 
	// conditional wait when there are no more outstanding requests.
	
	std::timed_mutex WaitGuard;
	std::condition_variable_any ResponseReceived;
	
	// The handler for contributions simply accumulates them. One could think 
	// that it would be desirable to reduce the number of outstanding requests 
	// in this handler, but in the receiver methodology it simply means that 
	// the incoming messages are recorded after being served, and the count of 
	// outstanding requests will be balanced against this count. This is important
	// since the handler will be called from the Receiver's Postman thread while
	// the outstanding request is in the main thread executing this Receiver.
	
	void ReceiveContribution( const double & OneContribution, 
														const Theron::Address TheSender )
	{
		std::unique_lock< std::timed_mutex > Lock( WaitGuard, 
																							 std::chrono::seconds(10) );
		
		TotalValue += OneContribution;
		OutstandingRequests--;
		ResponseReceived.notify_one();
	}

public:
	
	// The constructor takes an address for the predictor and stores this for 
	// future initialisations. 
	
	CollectContribution( const Theron::Address PredictorAddress )
	: Actor(), ThePredictor( PredictorAddress ),
	  TotalValue(0.0), OutstandingRequests(0), 
	  WaitGuard(), ResponseReceived()
	{
		// Register the handler to receive the responses.
		
		RegisterHandler( this, &CollectContribution::ReceiveContribution );
	}
	
	void Initialise( 
								const std::list< ConsumptionInterval > & ConsumptionIntervals )
	{

		std::unique_lock< std::timed_mutex > Lock( WaitGuard, 
																							 std::chrono::seconds(10) );
		
		TotalValue = 0.0;
		OutstandingRequests = 0;
		
		// Loop over all consumption intervals and for each send the size of the 
		// interval to the predictor, and then send the start time proposal to each
		// assigned consumer.
		
		for ( const ConsumptionInterval & Consumption : ConsumptionIntervals )
		{
		  // The predictor will need the consumption interval to compute the 
			// integral	of the predicted production over this interval
			
			Send( Consumption.TheInterval, ThePredictor );
			
			// Then the start time is proposed to each of the associated consumers
			// to compute their contribution to this interval.
			
			for( const ConsumptionInterval::AllocatedConsumer & ConsumerRecord
				   : Consumption.AssociatedConsumers )
				Send( ConsumerProxy::StartTimeProposal(ConsumerRecord.ProposedStartTime,
																					     Consumption.TheInterval ), 
					  (*ConsumerRecord.TheConsumer)->GetAddress() );

			// The number of outstanding requests is the number of consumers in this 
			// interval plus one for the request to the predictor for the value of 
			// the whole interval. It does not matter if some of these requests have 
			// already received a reply, because the balance will only be done when 
			// the value function is called.
			
			OutstandingRequests += Consumption.AssociatedConsumers.size() + 1;
		}
	}
	
	// The actual value is returned from a helper function using the standard 
	// wait function semantics to ensure that all outstanding requests have 
	// arrived before retuning to the calling thread. Since it will be called 
	// from the PV Producer message handler, it will block that handler, but not 
	// this actor's own postman, and incoming replies should therefore be 
	// handled as normal.
	
	double Value( void )	
	{
		std::unique_lock< std::timed_mutex > Lock( WaitGuard, 
																							 std::chrono::seconds(10) );
		
		// The value function will block the calling thread as long as there are 
		// any outstanding requests. It will be notified by the message handler 
		// as soon as a message arrives, but in case this notification gets lost 
		// there is an extreme time out of 10 seconds to ensure that the thread 
		// recovers.
		
	  ResponseReceived.wait_for( Lock, std::chrono::seconds(10),
													 [&](void)->bool{ return OutstandingRequests == 0; });

		return TotalValue;
	}
	
	// The destructor currently does nothing, but is a place holder for correct 
	// termination of the receiver.
	
	~CollectContribution( void )
	{ }
};

// -----------------------------------------------------------------------------
// Objective function
// -----------------------------------------------------------------------------
//
// First a small helper function to search through a list of consumption 
// intervals to find one that fits a given consumer and its proposed start 
// time. If an interval could be found, it returns true, if it could not be 
// matched with any existing interval, it returns falls.

bool Allocate2Interval( std::list< ConsumptionInterval > & ConsumptionIntervals,
			Producer::ConsumerReference TheConsumer,
			double StartTime )
{
  for ( auto & Period : ConsumptionIntervals )  
    if ( Period.Union( TheConsumer, StartTime ) )
      return true;

  // If the search and the function were not terminated because the consumer 
  // was allocated to one of the existing intervals, it terminates because 
  // there is no suitable interval and false can safely be returned.
 
  return false;
}

// The objective function receives a set of proposed start times from the solver
// and will first construct all the consumption intervals for these start times,
// and then evaluate the contribution of each consumption interval to the 
// overall objective value.

double PVProducer::ObjectiveFunction(
		   const std::vector< double > & ProposedStartTimes )
{
  // There is a list of consumption intervals as there can potentially be 
  // as many intervals as there are consumers if their individual consumption 
  // intervals are disjoint.
  
  std::list< ConsumptionInterval > ConsumptionIntervals;
  
  // First the necessary intervals are created to contain all the started loads.
  // For these the start time must have been set since they are all started, 
  // and it is therefore possible to pass this directly as the starting point
  // of the interval if a new interval must be constructed.
 
  for ( auto & TheConsumer : StartedLoads )
    if ( ! Allocate2Interval( ConsumptionIntervals, TheConsumer,
												      (*TheConsumer)->GetStartTime().value() ) )
      ConsumptionIntervals.emplace_back( TheConsumer,
					 (*TheConsumer)->GetStartTime().value() );
  
  // For the active loads, it is necessary to allocate them to consumption 
  // intervals based on the proposed start times.Note that the number of 
  // proposed start times must equal the number of active loads, and therefore 
  // there is no need to test the iterator for validity to ensure that it does 
  // not move past the last element of the start time vector.
  
  auto SuggestedStartTime = ProposedStartTimes.begin();
  
  // Each of the active loads are tried against the consumption intervals 
  // and if the load cannot be allocated to one of the existing intervals,
  // a new will be created.
  
  for ( auto & TheConsumer : ActiveLoads )
  {
    // If the consumer cannot be allocated to any of the existing intervals,
    // a new interval will be created for this consumer
    
    if ( ! Allocate2Interval( ConsumptionIntervals, TheConsumer, 
												      *SuggestedStartTime ) )
      ConsumptionIntervals.emplace_back( TheConsumer,	 *SuggestedStartTime   );
    
    // The consumer is now associated with one and only one consumption 
    // interval, and we can move on to the next consumer. For this we have to
    // move to the next proposed start time.
    
    ++SuggestedStartTime;
  }
  
  // With all the consumption intervals defined, the computation of the 
  // objective value can be started by the collector, which will terminate when
  // all the parts of the value have been received from the other actors and 
  // the prediction.
  
  Collector->Initialise( ConsumptionIntervals );
  
  return Collector->Value();
}

// It is necessary to define a C-style objective function that forwards the 
// objective function call to the objective function in the load scheduler. 
// The trick is to use the NLopt provided function parameter structure, and 
// define this to be equal to the 'this' pointer of the load scheduler instance 
// invoking the solver.

double C_ObjectiveFunction( const std::vector< double > & ProposedStartTimes, 
												    std::vector< double > & Gradient, 
												    void * FunctionParameters )
{
  PVProducer * This = reinterpret_cast< PVProducer *>( FunctionParameters );
  
  return This->ObjectiveFunction( ProposedStartTimes );
}


// -----------------------------------------------------------------------------
// SCHEDULING: The New Load message handler
// -----------------------------------------------------------------------------
//
// The handler receives the schedule request and computes a start time for ALL 
// loads that are assigned to this producer. However, this may not be possible,
// and if a consumer does not receive a valid start time, it is expected that 
// it will try a different producer, i.e. kill its proxy with this consumer.

void PVProducer::NewLoad( const Producer::ScheduleCommand & TheCommand, 
												  const Theron::Address TheConsumer )
{
  // The time to produce a schedule is recorded in order to be able to define 
  // the time window 'now + delta' where a consumer can start consuming 
  // energy before the new schedule is ready, and hence consumers with assigned
  // start times in this window should be considered as already started. To
  // find the 'delta' it is necessary to know how long the scheduling operation
  // will take, and the start time is recorded.
  
  std::chrono::system_clock::time_point StartTime 
					     = std::chrono::system_clock::now(); 

  // The standard producer handler is invoked to create a proxy for this load,
  // provided that the energy requested is larger than zero. This is because 
  // a zero energy load is sent to trigger the production of a new schedule when
  // the production prediction is updated. If the request is from the predictor,
  // the allowed start interval will contain the prediction domain, and this 
  // will be stored for any scheduling operation until the next prediction 
  // arrives. Note that a new prediction domain implies that the prediction has
  // been updated, and therefore it is necessary to compute a new schedule.
    
  if ( TheCommand.TotalEnergy() > 0.0 )
    Producer::NewLoad( TheCommand, TheConsumer );
  else
    PredictionDomain = TheCommand.AllowedStartWindow();

  // First the assigned consumers are partitioned into those who have started 
  // and those who have not, and those who have to wait for a future 
  // prediction to cover their allowed start intervals.

  PartitionLoads();
  
  // The start time computation is different depending on whether there is one 
  // active load or whether it is necessary to make a decision based on 
  // combining the multiple loads. 
  
  if ( ActiveLoads.size() == 1 ) // *** SINGLE CONSUMER ***
  {
    // The actual consumer proxy is obtained by taking the first active load, 
		// which is safe since there is currently only one load.
    
    auto Consumer( *ActiveLoads[0] );
    
    // The single consumer scheduler can then be started based on the 
    // information provided in the schedule command. However, this must 
    // instead be taken from the the assigned consumer because this can be a 
    // re-scheduling in response to a production prediction update, and in 
    // this case the command is void.
    
    SingleConsumerHeuristic SingleScheduler( GetFramework(), 
    boost::numeric::intersect( Consumer->AllowedInterval(), 
													     TimeInterval( Now(), PredictionDomain.upper() )),
		Consumer->GetDuration(), 
		Consumer->GetEnergy(),
		Prediction->GetAddress()   
	  );
    
    // The computed start time is sent back to the requesting consumer.
    
    Send( SingleScheduler.ComputeSolution(), Consumer->GetAddress() );
    
    // If there are no running loads, this active consumer is by definition 
    // the load first to start.
    
    if ( StartedLoads.empty() )
			EarliestStartingConsumer =  FindConsumer( Consumer->GetConsumer() );
	  }
  else if ( ActiveLoads.size() > 1 ) // *** MULTIPLE CONSUMERS ***
  {
    // Creating the solver object with the algorithm to use for the solution 
    // and the number of start times to find.
    
    nlopt::opt Solver( nlopt::algorithm::LN_BOBYQA, ActiveLoads.size() );

    // There are two soft parameters related to the convergence of the solver:
    // The tolerance on the objective function  indicates that the 
    // algorithm should stop whenever two successive evaluations of the objective 
    // functions gives values that are less than the tolerance; and the 
    // evaluation limit gives the maximum number of objective function 
    // evaluations accepted for finding a solution.
    
    Solver.set_ftol_abs( ObjectiveFunctionTolerance );
    Solver.set_maxeval ( EvaluationLimit );
    
    // The boundaries are collected for all the start times. We will start
    // the search for new load start times from the current solution. If there  
    // is no start time currently set, we will set an initial guess as a random 
    // number in the allowed interval. Note that the scan will be done with 
    // iterators since we will remove the jobs that have already started based  
    // on their absolute time.
    //
    // The start time values will after this search hold the initial start times
    // for all loads to be scheduled, and the upper and lower bound vectors will 
    // respectively contain the earlies and latest start time for the loads.

    std::vector< double > StartTimeValues, UpperBounds, LowerBounds;
    
    for ( auto TheConsumer : ActiveLoads )
    {
      LowerBounds.push_back( 
									std::max( (*TheConsumer)->AllowedInterval().lower(), Now()  ));
      
      UpperBounds.push_back( std::min((*TheConsumer)->AllowedInterval().upper(), 
																			PredictionDomain.upper() ) );

      if ( (*TheConsumer)->GetStartTime() )
				StartTimeValues.push_back( (*TheConsumer)->GetStartTime().value() );
      else
				StartTimeValues.push_back( 
									 Random::Number( LowerBounds.back(),  UpperBounds.back() ));      
    }
    
    // The bounds are then registered to guide the solver's search.
    
    Solver.set_upper_bounds( UpperBounds );
    Solver.set_lower_bounds( LowerBounds );
    
    // Note that with these bounds the problem essentially becomes unconstrained
    // and we do not need to register any constraints for the problem. The 
    // objective function to be minimised has to be forwarded as a C-style 
    // function, i.e. a function that has no "this" pointer. However, it 
    // supports passing a pointer to a set of parameters needed to compute the 
    // value of the objective function. By passing 'this' as the pointer to the 
    // parameter structure, the class internal objective function can be invoked 
    // on 'this' producer instance. There is a slight overhead by this indirect 
    // invocation of the object function, but this is probably offset by the 
    // convenience of having the objective function as a part of the class.
    
    Solver.set_min_objective( C_ObjectiveFunction, this );
    
    // Then the problem can be solved. Note that the solver will throw an
    // exception if encounters an error in the solution instead of negative 
    // result values. We will catch the errors, and note the outcome of the  
    // run. In addition to the initial start time values as collected above,
    // the solver will also need a variable to hold the best objective function
    // it managed to obtain.
    
    double 	  		ObjectiveValue = 0.0;
    nlopt::result SolutionResult;

		try
    {
      SolutionResult = Solver.optimize( StartTimeValues, ObjectiveValue );
    }
    catch ( nlopt::roundoff_limited NLOptError )
    {
      SolutionResult = nlopt::ROUNDOFF_LIMITED;
      #ifdef CoSSMic_DEBUG
        Theron::ConsolePrint DebugMessage;
        DebugMessage << GetAddress().AsString() << " ROUNDOFF_LIMITED" 
										 << std::endl;
      #endif
    }
    catch ( std::invalid_argument NLOptError )
    {
      SolutionResult = nlopt::INVALID_ARGS;
      #ifdef CoSSMic_DEBUG
        Theron::ConsolePrint DebugMessage;
        DebugMessage << GetAddress().AsString() << " INVALID_ARGS" << std::endl;
      #endif
    }
    catch ( std::bad_alloc NLOptError )
    {
      SolutionResult = nlopt::OUT_OF_MEMORY;
      #ifdef CoSSMic_DEBUG
        Theron::ConsolePrint DebugMessage;
        DebugMessage << GetAddress().AsString() << " OUT_OF_MEMORY" 
										 << std::endl;
      #endif
    }
    catch ( std::runtime_error NLOptError )
    {
      SolutionResult = nlopt::FAILURE;
      #ifdef CoSSMic_DEBUG
        Theron::ConsolePrint DebugMessage;
        DebugMessage << GetAddress().AsString() << " FAILURE" << std::endl;
      #endif
    }
    
    // If a new solution was found, we will store the solution. It should be 
    // noted that although the solver terminated, most of the positive results 
    // may indicate a poor result quality. We are not dealing with this 
    // situation yet.
    //
    // If the producer provides excess energy over all consumption intervals, 
    // then the solver will not be able to find a best solution as there can 
    // be many solutions that are equally good. Consider for instance a sunny 
    // day when the production is running at maximum capacity the full day. Then
    // the cumulative production is a linear function of constant slope and any 
    // start time assigned to a load will give the same objective value. With 
    // several loads the picture is identical if the consumptions of each load is 
    // unique, i.e. no other load is started within its activity period. In 
    // these cases the solver will simply fail to provide a good solution, and  
    // any of the solutions with minimal objective function is as good as any  
    // other. The solver will fail with the best solution seen, and for this 
    // reason we treat failure as a success (even though it may seem strange)
 
    switch ( SolutionResult )
    {
      case nlopt::SUCCESS:
			case nlopt::ROUNDOFF_LIMITED:
      case nlopt::STOPVAL_REACHED:
      case nlopt::FTOL_REACHED:
      case nlopt::XTOL_REACHED:
      case nlopt::MAXEVAL_REACHED:
      case nlopt::MAXTIME_REACHED:
      case nlopt::FAILURE:
      {
        // We will set, send, and collect the information needed in one pass 
        // over the list of waiting consumers trying to obtain energy from this
        // producer. The start time values do exist since the vector was 
				// initialised, but if there is a solver failure they may not have 
				// valid values. TODO: Recover gracefully from solver failures

				auto StartTime = StartTimeValues.begin();
        
        for ( auto & Consumer : ActiveLoads )
        {
	        Send( AssignedStartTime( std::lround( *StartTime ) ), 
								(*Consumer)->GetAddress() );
	        ++StartTime;
        }
        break;
      }
      
      default:
			// If the solver failed, no action is taken. However, as indicated 
			// above, this situation should never happen.
			std::cout << "WHAT IS GOING ON?!?" << std::endl;
			break;
    }
    
    // If there are running consumers, then their start time is by definition 
    // less than 'now', and the earliest starting consumer will point to one 
    // of the consumers in that set and need not be updated since it will be 
    // updated when the consumer proxy for the first running load is killed. 
    // However, if there are no running consumers, the previously first
    // starting consumer may have been given a different starting time and it 
    // is necessary to search for the consumer in the set of active loads with 
    // the least starting time, if the starting time is defined.
    //
    // If a start time has been assigned, it should be recorded as the least 
    // start time seen if it is less than the earliest start time seen. Note 
    // however that the earliest start time reference is initialised to the 
    // first consumer, and this may not have a start time. If this is the case
    // the start time of the current consumer is used (since it exist it must 
    // by definition be the least start time seen until now).
    
    if ( StartedLoads.empty() )
    {
			auto EarliestCandidate = *ActiveLoads.begin();
			
			for ( auto & Consumer : ActiveLoads )
				if ( (*Consumer)->GetStartTime() )
				  if ( ! (*EarliestCandidate)->GetStartTime() ||
				      ((*Consumer)->GetStartTime().value() < 
						   (*EarliestCandidate)->GetStartTime().value()) )
				    EarliestCandidate = Consumer;
			
			// Then the earliest starting consumer iterator must be set to the 
			// position of the consumer in the producer's list of assigned proxies. 
			
      EarliestStartingConsumer = 
				FindConsumer( (*EarliestCandidate)->GetAddress() );
    }
  }
  else      // *** NO CONSUMERS ***
    return; // Nothing to do, i.e. no active loads detected.
  
  // If a real scheduling took place, i.e. the previous return statement did 
  // not terminate the execution before reaching this point, we can record the 
  // time it took to produce the schedule and use that value as an input for the 
  // offset for the next computation. The latter is computed as an exponential
  // moving average of the time to compute a solution for which an observation 
  // T time steps back in time will be discounted by a(1-a)^(T-1). By taking the 
  // logarithm of this factor we get
  //
  // Log[ a(1-a)^(T-1) ] = Log[a] + (T-1)*Log[1-a]
  // 
  // Hence if we want the discount factor to be 10^(-6) after 101 scheduling
  // operations, we have to solve
  // 
  // Log[a] + 100*Log[1-a] = -6 Log[10]
  // 
  // giving the following solution when solved numerically. 
  
  const double a = 0.10956263608822413;
  
  // Computing the time offset and scale it is non trivial with the chrono 
  // library. First we need to find the time taken to do the computation in 
  // milliseconds by a duration cast to ensure that we get the correct 
  // resolution. Then we have to convert the representation to double to be 
  // able to scale the duration for the time offset. Finally we have to
  // compute set the time offset as the double converted to a milliseconds 
  // duration.
  
  std::chrono::milliseconds delta = 
    std::chrono::duration_cast< std::chrono::milliseconds >( 
      std::chrono::system_clock::now() - StartTime );
    
  std::chrono::duration< double > ComputationTime( delta ),
				  CurrentOffset( TimeOffset );
				  
  double NewOffset = a * ComputationTime.count() 
					    + (1.0 - a) * CurrentOffset.count();
			  
  TimeOffset = std::chrono::duration_cast< std::chrono::milliseconds >( 
    std::chrono::duration< double >( NewOffset ) );
  
}



/*****************************************************************************
  Constructor and destructor
******************************************************************************/

// The virtual base classes of the producer must be called as this is the point
// in the code where they will be constructed. However, this duplicates the way
// of naming the actor, but the naming here will prevail since the producer's 
// invocation of the actor constructor will never be used.

PVProducer::PVProducer( const IDType & ProducerID, 
												const std::string & PredictionFile, 
												double SolutionTolerance, int MaxEvaluations)
: Actor( ( ValidID( ProducerID ) ? 
	       std::string( PVProducerNameBase + ProducerID ).data() 
	       : std::string() )  ), 
	StandardFallbackHandler( GetAddress().AsString()),
  DeserializingActor( GetAddress().AsString() ),
  Producer( ProducerID ),
  Prediction(), Collector( ),
  PredictionDomain(), ActiveLoads(), StartedLoads(), FutureLoads(),
  TimeOffset(), EarliestStartingConsumer( FirstConsumer() )
{
  ObjectiveFunctionTolerance = SolutionTolerance;
  EvaluationLimit 	         = MaxEvaluations;

	// Initialise Prediction and Collector
	
	Prediction = std::make_shared < Predictor >( PredictionFile, GetAddress(), 
               std::string( "prediction" + ProducerID ).data() );
              
  Collector = std::make_shared< CollectContribution >(Prediction->GetAddress());
	
  // Register message handlers. Note that new load and kill proxy are 
  // registered by the generic producer.
  
  RegisterHandler(this, &PVProducer::UpdatePrediction );
}

} // End name space CoSSMic
