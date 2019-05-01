/*=============================================================================
  Producer
  
  This file contains the implementation of the member functions of the generic 
  producer base class. See the header file for a reference to the interface
  and operation.

  Author: Geir Horn, University of Oslo, 2015-2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <sstream>												// For serialisation & error reporting
#include <stdexcept>											// For standard exceptions
#include <stdlib.h>     									// exit and exit failure
#include <algorithm>                      // std lib algorithms

#include <boost/optional/optional_io.hpp> // For serializing Assigned start time

#include "SessionLayer.hpp"
#include "Producer.hpp"
#include "ConsumerProxy.hpp"


namespace CoSSMic {
  
// -----------------------------------------------------------------------------
// Schedule Command for new loads
// -----------------------------------------------------------------------------
//
// The the interval is created as empty if the lower bound is not less or 
// equal to the upper bound. If the schedule command is correctly created this
// should not be a problem, but we do check for user errors by assigning the
// bounds explicitly in correct order.

Producer::ScheduleCommand::ScheduleCommand( Time EarliestStart, 
																				    Time LatestStart,  
																				    Time Delta, double Energy )
: AllowedStart()
{
  if ( EarliestStart <= LatestStart )
    AllowedStart.assign( EarliestStart, LatestStart );
  else
    AllowedStart.assign( LatestStart, EarliestStart );  
  
  JobDuration  = Delta;
  EnergyNeeded = Energy;
}

// For the copy constructor we do not need to check the input since we are 
// already given a valid command.
  
Producer::ScheduleCommand::ScheduleCommand(
  const Producer::ScheduleCommand& OtherCommand )
: AllowedStart( OtherCommand.AllowedStart )
{
  JobDuration  = OtherCommand.JobDuration;
  EnergyNeeded = OtherCommand.EnergyNeeded;
}

// The serialise method puts the various fields one after the other separated by 
// a space into a string. The energy needed is a real number and currently we
// trust the default way of writing this out as a string since "On the default 
// floating-point notation, the precision field specifies the maximum number of 
// meaningful digits to display both before and after the decimal point" 
// according to http://www.cplusplus.com/reference/ios/fixed/

Theron::SerialMessage::Payload 
Producer::ScheduleCommand::Serialize( void ) const
{
  std::ostringstream Message;
  
  Message << "SCHEDULE" << " " << AllowedStart.lower() << " " 
				  << AllowedStart.upper() << " " << JobDuration << " "
				  << EnergyNeeded << std::endl;
	  
  return Message.str();
}

// De-serializing a message is just inverting the above process. Note however 
// that it is assumed that the command follows the payload, so that the test
// on the command makes sense. Note also that we test for the case where the 
// message has been manually constructed to ensure that the consumption interval
// is as correct as possible.

bool Producer::ScheduleCommand::Deserialize(
  const Theron::SerialMessage::Payload & Payload)
{
  std::istringstream Message( Payload );
  std::string Command;
  
  Message >> Command;
  
  if ( Command == "SCHEDULE" )
  {
    Time EarliestStart = 0, 
	 LatestStart   = 0;
	 
    Message >> EarliestStart >> LatestStart;

    if ( EarliestStart <= LatestStart )
      AllowedStart.assign( EarliestStart, LatestStart );
    else
      AllowedStart.assign( LatestStart, EarliestStart );
   
    Message >> JobDuration >> EnergyNeeded;
    
    return true;    
  }
  else return false;
}

// The constructor taking a serialised payload as argument will throw an 
// exception if the de-serialisation fails

Producer::ScheduleCommand::ScheduleCommand(
  const Theron::SerialMessage::Payload & Payload )
: AllowedStart()
{
  if ( ! Deserialize( Payload ) )
  {
	  std::ostringstream ErrorMessage;
	  
	  ErrorMessage <<  __FILE__ << " at line " << __LINE__ << ": "
							   << "Schedule command != " << Payload;
				   
	  throw std::invalid_argument( ErrorMessage.str() );
  }
}

// -----------------------------------------------------------------------------
// Assigned start time
// -----------------------------------------------------------------------------
//
// The assigned start time class is used to communicate back to the consumer 
// the time that has been set for this load, if any. It must therefore support
// serialisation. 
// 
// For the serialisation we must either send different keywords depending on 
// whether we do have a start time or not. If we do have a start time, it is 
// serialised by its stream operator.

Theron::SerialMessage::Payload 
Producer::AssignedStartTime::Serialize( void ) const
{
	if ( has_value() )
  {
    std::ostringstream Message;
    
    Message << "ASSIGNED_START_TIME " << *this;
    
    return Message.str();
  }
  else
    return "ASSIGNED_START_TIME_UNINITIALISED";
}

// For the de-serialisation, we can ignore the uninitialised part since this is 
// the default state of an object, and only initialise the time value if it is 
// received.

bool Producer::AssignedStartTime::Deserialize(
  const Theron::SerialMessage::Payload & Payload )
{
  std::istringstream Message( Payload );
  std::string Command;
  
  Message >> Command;
  
  if ( Command == "ASSIGNED_START_TIME" )
  {
    Time SetTime;
    Message >> SetTime;
    this->operator=( SetTime );
    return true;
  }
  else if ( Command == "ASSIGNED_START_TIME_UNINITIALISED" )
    return true;
  else
    return false;
}

// The constructor from a serialised message uses the method to de-serialise 
// the message and throws if that is not possible

Producer::AssignedStartTime::AssignedStartTime(
  const Theron::SerialMessage::Payload & Payload )
{
  if ( ! Deserialize( Payload ) )
  {
	  std::ostringstream ErrorMessage;
	  
	  ErrorMessage <<  __FILE__ << " at line " << __LINE__ << ": "
							   << "Assigned start time != " << Payload;
				   
	  throw std::invalid_argument( ErrorMessage.str() );
  }
}


// -----------------------------------------------------------------------------
// Killing proxy command
// -----------------------------------------------------------------------------
//
// Serializing the kill proxy command is just to write the command in the
// payload.

std::string Producer::KillProxyCommand::Serialize(void) const
{
  return "KILLPROXY";
}

// De-serializing the message is just ensuring that the string contains the 
// right command.

bool Producer::KillProxyCommand::Deserialize(
  const Theron::SerialMessage::Payload & Payload )
{
  std::istringstream Message( Payload );
  std::string Command;
  
  Message >> Command;
  
  if ( Command == "KILLPROXY" ) return true;
  else 
    return false;
}

// The constructor calling the de-serialising method will throw an invalid 
// argument exception if the command is not a kill proxy command.

Producer::KillProxyCommand::KillProxyCommand(
  const Theron::SerialMessage::Payload & Payload)
: KillProxyCommand()
{
  if ( ! Deserialize( Payload ) )
  {
	  std::ostringstream ErrorMessage;
	  
	  ErrorMessage <<  __FILE__ << " at line " << __LINE__ << ": "
							   << "Kill proxy != " << Payload;
				   
	  throw std::invalid_argument( ErrorMessage.str() );
  }
}

// -----------------------------------------------------------------------------
// Acknowledge the proxy removal
// -----------------------------------------------------------------------------

// Serialising the acknowledge command follows the above pattern for the kill
// command.

std::string Producer::AcknowledgeProxyRemoval::Serialize(void) const
{
  return "ACKNOWLEDGE_PROXY_REMOVAL";
}

bool Producer::AcknowledgeProxyRemoval::Deserialize(
  const Theron::SerialMessage::Payload & Payload)
{
  std::istringstream Message( Payload );
  std::string Command;
  
  Message >> Command;
  
  if ( Command == "ACKNOWLEDGE_PROXY_REMOVAL" ) return true;
  else
    return false;
}

Producer::AcknowledgeProxyRemoval::AcknowledgeProxyRemoval(
  const Theron::SerialMessage::Payload & Payload )
: AcknowledgeProxyRemoval()
{
  if( ! Deserialize( Payload ) )
  {
	  std::ostringstream ErrorMessage;
	  
	  ErrorMessage <<  __FILE__ << " at line " << __LINE__ << ": "
							   << "Acknowledge kill proxy != " << Payload;
				   
	  throw std::invalid_argument( ErrorMessage.str() );
  }
}

// ---------------------------------------------------------------------------
// Consumer proxy management
// ---------------------------------------------------------------------------
//
// The find function is basically a loop that breaks if there is a matching 
// consumer. If the loop runs to the end with no consumer found, the function
// will return an uninitialised optional

CoSSMic::Producer::ConsumerReference
Producer::FindConsumer( const Theron::Address & TheConsumer )
{
	for ( auto TheProxy  = AssignedConsumers.begin(); 
						 TheProxy != AssignedConsumers.end(); 
					 ++TheProxy )
     if ( (*TheProxy)->GetConsumer() == TheConsumer )
       return TheProxy;
 
  return AssignedConsumers.end();
}

// ---------------------------------------------------------------------------
// Message handling with default processing
// ---------------------------------------------------------------------------
//
// When a new load arrives it will simply be added to the list of assigned 
// loads. One could think that emplace back with a shared pointer created with 
// the standard make shared pointer would be OK. However, this leads to a 
// use count of two for the shared pointer in the list. Apparently this is 
// an effect of the copy paradigm of the STL library, and since the shared 
// pointer created by the make shared function is a temporary pointer, it 
// will not reduce the use counter when it goes out of scope.
//
// The solution is to use a direct allocation instead.

void Producer::NewLoad( const Producer::ScheduleCommand & TheCommand, 
                        const Theron::Address TheConsumer )
{
  AssignedConsumers.emplace_back( 
		  new ConsumerProxy( TheCommand, TheConsumer, GetAddress() ) );  	
}

// Inversely, when there is a request to kill a load, it will simply be removed
// from the list of loads, provided that it exists. This check should be 
// unnecessary since the request comes from a consumer which should know where
// its proxy is since it knows which consumer provides the energy.

void Producer::KillProxy( const Producer::KillProxyCommand & TheCommand, 
												  const Theron::Address TheConsumer )
{
  auto TheProxy = FindConsumer( TheConsumer );
  
  if ( TheProxy != AssignedConsumers.end() )
    AssignedConsumers.erase( TheProxy );
  else
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						     << GetAddress().AsString() 
                 << " asked to remove unassigned proxy for consumer "
                 << TheConsumer.AsString();
		 
    throw std::runtime_error( ErrorMessage.str() );
  }
}

// ---------------------------------------------------------------------------
// Shut down management
// ---------------------------------------------------------------------------
//
// The handler for shut down requests makes sure to de-register the handlers 
// for new loads and for killing proxies, and replace them with the versions 
// that rejects all loads and that de-register the producer when the last proxy 
// has been removed. If this producer has no consumers assigned it will 
// immediately de register as an agent to prevent that any consumers will select
// this as producer.
//
// It is safe to change the message handlers within a message handler since 
// Theron will only call one handler at the time, so the new handers will be 
// used if there are any messages in the queue when this handler terminates.

void Producer::ShutDownHandler( const ShutdownMessage & Message, 
														    const Theron::Address HouseholdActorManager )
{
	// De-register the handlers for the schedule command and the kill proxy
	// commands.
	
	DeregisterHandler( this, &Producer::NewLoad   		 );
	DeregisterHandler( this, &Producer::KillProxy 		 );
	
	// Register the handlers for graceful termination
	
	RegisterHandler( this, &Producer::RejectLoads 		 );
	RegisterHandler( this, &Producer::AgentTermination );
	
	// If there are no assigned consumers, it is just to de-register the producer
	// as an externally reachable agent. Otherwise, if there are assigned 
	// consumers their assigned start time should be cancelled. This should make 
	// the consumers select other producers after killing their proxies.
	//
	// Note that even if the list of assigned consumers is empty, there could be 
	// requests waiting in the queue, and de-registration is only possible if 
	// the queue only contains the single message that triggers this shut-down 
	// handler
	
	if ( AssignedConsumers.empty() && ( GetNumQueuedMessages() == 1 )  )
		Send( ActorManager::ConfirmShutDown(), HouseholdActorManager );
	else
		for ( auto Consumer : AssignedConsumers )
			if ( Consumer->GetStartTime() )
				Send( AssignedStartTime(), Consumer->GetAddress() );	
			
	// Finally, the address of the actor manager is stored in order to correctly 
	// confirm the shut down operation once the last consumer removes a proxy.
	
	TheActorManager = HouseholdActorManager;
}

// The handler to reject loads will first invoke the new load handler to enqueue
// the proxy. The proxy will then be the last proxy of the list, and it will 
// then be assigned an empty start time which should make it move away to 
// another producer.

void Producer::RejectLoads( const ScheduleCommand & TheCommand, 
						                const Theron::Address TheConsumer   )
{
	Producer::NewLoad( TheCommand, TheConsumer );
	
	Send( AssignedStartTime(), AssignedConsumers.back()->GetAddress() );
}

// When the proxies are removed, they are killed by the default method taking 
// them out of the proxy list, and if the list is empty and no further messages
// than the current is waiting processing by this actor, it will notify the 
// actor manager that he shut down is complete.

void Producer::AgentTermination( const KillProxyCommand & TheCommand, 
				                         const Theron::Address TheConsumer )
{
	Producer::KillProxy( TheCommand, TheConsumer );
	
	if ( AssignedConsumers.empty() && ( GetNumQueuedMessages() == 1 ) )
		Send( ActorManager::ConfirmShutDown(), TheActorManager );	
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
//
// The actor constructor initialises the actor and the de-serialising extension,
// and registers the two message handlers.

Producer::Producer( const IDType & ProducerID )
: Actor( ( ValidID( ProducerID ) ? 
         std::string( ProducerNameBase + ProducerID ).data() : std::string() )),
  StandardFallbackHandler( GetAddress().AsString() ),
  DeserializingActor( GetAddress().AsString() ),
  TheActorManager()
{
  RegisterHandler( this, &Producer::NewLoad   			);
  RegisterHandler( this, &Producer::KillProxy 			);
	RegisterHandler( this, &Producer::ShutDownHandler );
}

} // name space CoSSMic
