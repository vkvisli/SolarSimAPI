/*=============================================================================
  Consumer Proxy
  
  The purpose of the consumer proxy is to represent a consumer agent on a remote
  node. In order to do this, it is created by the Actor Manager on the remote 
  node when a "schedule" command is received from the consumer agent. The proxy
  interacts with the Load Scheduler until a solution is found. Hence it is 
  implemented as the constructor and a set of message handlers.
  
  The found solution is sent back to the remote consumer agent, and once this
  consumer agent has finished the execution of the load, it will ask the 
  Actor Manger on the proxy's node to kill the proxy.

  REFERENCES:
  
  [1] Geir Horn: Scheduling Time Variant Jobs on a Time Variant Resource, 
      in the Proceedings of The 7th Multidisciplinary International Conference 
      on Scheduling : Theory and Applications (MISTA 2015), Zdenek Hanzálek et
      al. (eds.), pp. 914-917, Prague, Czech Republic, 25-28 August 2015.
 
  Author: Geir Horn, University of Oslo, 2015-2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <string>
#include "ConsumerProxy.hpp"

namespace CoSSMic {

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

// According to the framework in [1] the total energy that must be considered 
// for this load is the energy needed for the load, plus the cost of keeping 
// this load going to the end of the consumption interval. When the interval
// is given by the load scheduler, the next message handler will compute their
// cost.

void ConsumerProxy::ComputeTotalEnergy( 
  const StartTimeProposal & Proposal, const Theron::Address TheScheduler )
{
  double EnergyCost = EnergyNeeded * ( Proposal.ConsumptionInterval.upper() 
    - (Proposal.ProposedStartTime + JobDuration)  );
  
  Send( EnergyCost, TheScheduler );
}

// Once the final start time has been decided by the load scheduler it is 
// returned as a separate message. It should be noted that this start time
// can very well be undefined, in which no start time could be obtained at
// this producer. Currently it only forwards this conclusion back to the 
// remote consumer agent.

void ConsumerProxy::SetStartTime( 
     const Producer::AssignedStartTime & Time2Start, 
	   const Theron::Address TheScheduler )
{
	if ( StartTime && ( StartTime == Time2Start ) )
		return;

	StartTime = Time2Start;
  Send( StartTime, TheProducer, ConsumerAddress );
}

// ---------------------------------------------------------------------------
// The constructor and the destructor
// ---------------------------------------------------------------------------

// The constructor receives the schedule command and the address of the remote
// consumer agent and the load scheduler. After storing the necessary 
// information, it sends a message to the scheduler asking for a start time.
//
// The only thing to note is that the proxy uses a special name for the actor.
// This is done in order to be able to replace its sender address when it sends 
// a message to a remote node. Depending on the communication infrastructure 
// used, it may be necessary for an actor communicating with other actors to 
// have rather complex structures at the link layer. Since a consumer proxy
// is volatile, i.e. created and then removed on the producer node, giving it
// the full right of communication may be a significant overhead. With the 
// special name, this could be detected before sending, and its address 
// replaced with the address of the Actor Manager on this node, for instance.
// Since the actor takes a normal C string as argument, it is easiest to 
// combine the strings using a temporary standard string, although this makes
// the constructor call a little more complex.
//
// Note that the producer ID is added to the actor name in order to minimise 
// race related conflicts where the consumer change producer and asks the 
// current producer to kill the proxy and the second producer to create the 
// consumer proxy. The issue is then if the two producers are at the same 
// network end point, and hence the two consumer proxies are created in the 
// same actor framework. Without the produce ID, the two proxies would have 
// the same name, and Theron would refuse the creation of the second actor.
// Once debugging has finished, this name should be removed completely, and
// then this race condition would not occur with automatic actor names assigned
// by Theron. However, it was impossible to prevent the race condition, and 
// the only way to ensure that the proxy is deleted before the new proxy is 
// created at another producer is to implement an acknowledgement for the kill
// message (see the consumer agent for details).
  
ConsumerProxy::ConsumerProxy( 
               const CoSSMic::Producer::ScheduleCommand & TheCommand, 
               const Theron::Address & TheConsumer, 
               const Theron::Address & ProducerReference   )
: Actor(),
  StandardFallbackHandler( GetAddress().AsString() ),
  ConsumerAddress( TheConsumer ), 
  TheProducer( ProducerReference ),
  StartInterval( TheCommand.AllowedStartWindow() ), 
  StartTime()
{
   
  JobDuration     = TheCommand.Duration();
  EnergyNeeded    = TheCommand.TotalEnergy();
  
  // Then the message handlers are registered.
  
  RegisterHandler(this, &ConsumerProxy::ComputeTotalEnergy  );
  RegisterHandler(this, &ConsumerProxy::SetStartTime  	    );
}

// The destructor clears the currently assigned start time - if any and then 
// acknowledge the proxy removal

ConsumerProxy::~ConsumerProxy( void )
{
	Send( Producer::AcknowledgeProxyRemoval(), TheProducer, ConsumerAddress );
}

} // End namespace CoSSMic
