/*=============================================================================
  Consumer Proxy
  
  The consumer proxy is created at the producer side when a schedule command is
  received by the Actor Manager. This indicates that a remote load should be 
  scheduled. 
  
  The consumer proxy actor will immediately register with the Load Scheduler, 
  and this will in turn initiate a schedule operation. During the schedule 
  operation the consumer will compute the weight of the load given the length
  of the consumption interval and its assigned start time. This weight is 
  fundamentally the total energy consumption by the load, plus its extension 
  to the end of the load interval. For details see [1].
  
  REFERENCES:
  
  [1] Geir Horn: Scheduling Time Variant Jobs on a Time Variant Resource, 
      in the Proceedings of The 7th Multidisciplinary International Conference 
      on Scheduling : Theory and Applications (MISTA 2015), Zdenek Hanzálek et
      al. (eds.), pp. 914-917, Prague, Czech Republic, 25-28 August 2015.
      
  Author: Geir Horn, University of Oslo, 2015-2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef CONSUMER_PROXY
#define CONSUMER_PROXY

#include "Actor.hpp"
#include "StandardFallbackHandler.hpp"

#include "TimeInterval.hpp"
#include "ActorManager.hpp"
#include "Producer.hpp"

// All the code that is specific to the CoSSMic project is isolated in its own
// name space. 

namespace CoSSMic {

// The actual consumer proxy class is an actor that represents all information
// of the load, but on the same network endpoint as the producer in order to 
// facilitate the scheduling with only node local communication.

class ConsumerProxy : public virtual Theron::Actor,
											public virtual Theron::StandardFallbackHandler
{
private:
  
  // The consumer proxy only needs to store the job duration and the energy 
  // needed for the load since the interval for the allowed start is passed 
  // as a constraint to the scheduler, and the proposed start time should 
  // therefore respect these constraints and should not be tested again.
  
  Time   JobDuration;
  double EnergyNeeded;
  
  // It also needs to store the address of the actor for which this proxy should
  // respond when a good start time has been assigned. Since a consumer proxy 
	// does not have an external address, it needs to use the Producer responsible 
	// for scheduling this consumer, and use this address when sending the 
	// assigned start time back to the attached consumer.
 
  Theron::Address ConsumerAddress, 
									TheProducer;
  
  // The Earliest Start Time and Latest Start Time of a job defines the 
  // allowed start interval.
  
  TimeInterval StartInterval;
	
	// It also remember the assigned start time for the associated consumer
	
	Producer::AssignedStartTime StartTime;
  
public:
  
  // The consumer proxy is crated by the Actor Manager when it gets a Schedule 
  // command, and this is immediately passed to the constructor. It also needs 
	// the address of the Consumer actor for returning the scheduling decision.
  
  ConsumerProxy( const Producer::ScheduleCommand & TheCommand,
                 const Theron::Address & TheConsumer,
								 const Theron::Address & ProducerReference 	      );

  // ---------------------------------------------------------------------------
  // Access functions
  // ---------------------------------------------------------------------------
  
  // There is also a simple function to get the total energy the job needs
  
  inline double GetEnergy( void ) const
  {
    return EnergyNeeded;
  }

  // It is also a function to check the job's duration
  
  inline Time GetDuration( void ) const
  {
    return JobDuration;
  }

  // There is a small utility function to report the consumer address of this
  // proxy.
  
  inline Theron::Address GetConsumer( void ) const
  {
    return ConsumerAddress;
  }
  
  inline Producer::AssignedStartTime GetStartTime( void ) const
	{
		return StartTime;
	}
	
	inline TimeInterval AllowedInterval( void ) const
	{
	  return StartInterval;  
	}

  // ---------------------------------------------------------------------------
  // Scheduling information
  // ---------------------------------------------------------------------------
  // When the solver iterates to find a good start time for all tasks, it will
  // repeatedly propose new start times in given consumption intervals to the 
  // consumer proxies given by the following class. Since the proxies are on 
  // the same node as the scheduler, and this interaction is well defined it 
  // is no need to protect the values of the class, or make it serialiseable.  
  
public:
	
  class StartTimeProposal
  {
  public:
    
    Time 	 				ProposedStartTime;
    TimeInterval  ConsumptionInterval;
    
    StartTimeProposal( const Time AssignedStartTime, 
								       const TimeInterval & ConsumptionPeriod )
    : ConsumptionInterval( ConsumptionPeriod )
    {
      ProposedStartTime = AssignedStartTime;
    }
    
    // There is a copy constructor to ensure that this can be safely passed 
    // between threads
    
    StartTimeProposal( const StartTimeProposal & OtherMessage )
		: ProposedStartTime( OtherMessage.ProposedStartTime ),
		  ConsumptionInterval( OtherMessage.ConsumptionInterval )
		{}
  };

  // During the computation of the local schedule the solution algorithm will 
  // propose several possible start times, and the start time belongs to a 
  // consumption interval. The following handler computes the total energy 
  // by this assignment and the length of the interval according to the 
  // objective function derived in [1].
  
  void ComputeTotalEnergy( const StartTimeProposal & Proposal,
												   const Theron::Address TheScheduler );

  // When the scheduler has made the final decision on a start time, it sends 
  // this back to the proxy. Note that this value can be unassigned, and in 
  // this case the load cannot be scheduled on this producer.
  
  void SetStartTime( const Producer::AssignedStartTime & Time2Start,
								     const Theron::Address TheScheduler );
  
  // There is a destructor to ensure correct behaviour when this 
  // class is deleted. By itself it does nothing.
  
  ~ConsumerProxy( void );
	
};	// End class Consumer Proxy
  
}; 	// End name space CoSSMic
#endif 	// CONSUMER_PROXY
