/*=============================================================================
  Actor Manager
  
  The Actor Manager is the end point of a node in the CoSSMic distributed 
  scheduling system. Its role is to interact with the other actor managers, 
  and the multi-agent CoSSMic platform, in particular the Task Manager.

  See the header file for detailed explanations on structure and use.
  
  Author: Geir Horn, University of Oslo, 2015-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <string>
#include <sstream>
#include <stdexcept>
#include <map>
#include <list>
#include <algorithm>
#include <thread>
#include <chrono>
#include <optional>

#include <boost/algorithm/string.hpp> // To convert to upper-case 

#include "SessionLayer.hpp"

#ifdef CoSSMic_DEBUG
  #include "ConsolePrint.hpp"        // Debug messages
#endif

#include "Clock.hpp"                 // The CoSSMic time Now
#include "IDType.hpp"                // Device and producer IDs
#include "ActorManager.hpp"          // The Actor Manager interface
#include "Producer.hpp"              // The generic producer
#include "PVProducer.hpp"            // The PV producer
#include "ConsumerAgent.hpp"         // The Consumers (loads)
#include "RewardCalculator.hpp"      // The generic reward calculator

/*=============================================================================

 I/O stream operators not part of the name space.

=============================================================================*/

// The producer can support multiple types, and they have to be uniquely 
// serialised and de-serialised by the relevant io operators. The output 
// operator is a simple switch based on the type value given.

std::ostream & operator<< (std::ostream & Output, 
				       const CoSSMic::ActorManager::AddProducer::Type & ProducerType)
{
  switch ( ProducerType )
  {
    case CoSSMic::ActorManager::AddProducer::Type::Grid :
      Output << "Grid";
      break;
    case CoSSMic::ActorManager::AddProducer::Type::PhotoVoltaic :
      Output << "PV";
      break;
    case CoSSMic::ActorManager::AddProducer::Type::Battery :
      Output << "Battery";
      break;
  }
  
  return Output;
}

// The input operator provides a mapping from the different string 
// representations of the producer types to the correct enumeration, and 
// then uses this constant map to look up the right value.

std::istream & operator>> ( std::istream & Input, 
				       CoSSMic::ActorManager::AddProducer::Type & ProducerType )
{
  static const std::map< std::string, CoSSMic::ActorManager::AddProducer::Type >
  GetType = {
    { "GRID", 	    CoSSMic::ActorManager::AddProducer::Type::Grid    	   },
    { "PV"  , 	    CoSSMic::ActorManager::AddProducer::Type::PhotoVoltaic },
    { "PVPRODUCER", CoSSMic::ActorManager::AddProducer::Type::PhotoVoltaic },
    { "BATTERY",    CoSSMic::ActorManager::AddProducer::Type::Battery      }
  };
  
  // Read a string from the input stream and make sure it is upper case
  
  std::string TypeString;
  
  Input >> TypeString;
  
  boost::to_upper( TypeString );
  
  // Then the string is used to look up the producer type using the map's 
  // at function, which will throw an out of range error if the string does 
  // not correspond to a legal producer type.
  
  ProducerType = GetType.at( TypeString );
  
  return Input;
}

// The actor manager implementation is encapsulated in the CoSSMic name space

namespace CoSSMic {

/*=============================================================================

 Starting producers

=============================================================================*/
//
// The method to serialise a command message is relatively straightforward: 
// First setting the command and then adding the type, the ID, and prediction 
// file. The prediction file can of course be empty if it is not needed by the 
// producer type.

Theron::SerialMessage::Payload 
ActorManager::AddProducer::Serialize( void ) const
{
  std::ostringstream Message;
  
  Message << "CREATE_PRODUCER " << ProducerType << " " 
				  << NewProducerID  << " " 
				  << PredictionFile << std::endl;
    
  return Message.str();
}

// The de-serialise method checks the command and then adds the type, the ID and 
// the file name. The file name will be checked if it is required, i.e. the 
// type is a PV producer.

bool ActorManager::AddProducer::Deserialize( 
  const Theron::SerialMessage::Payload & Payload )
{
  std::istringstream Message( Payload );
  std::string Command;
  
  Message >> Command;
  
  if ( Command == "CREATE_PRODUCER" )
  {
    Message >> ProducerType;
    Message >> NewProducerID;
    Message >> PredictionFile;
    
    if ( (ProducerType == Type::PhotoVoltaic) && PredictionFile.empty() )
      return false;
    else return true;
  }
  else return false;
}

// The constructor from a serialised message simply calls the de-serialise 
// method, and throws a standard invalid argument exception if the operation 
// fails as the message is then not a message for creating producers.

ActorManager::AddProducer::AddProducer( 
    const Theron::SerialMessage::Payload & Payload)
{
  if ( ! Deserialize( Payload ) )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << " : "
								 << "AddProducer != " << Payload;
								 
		throw std::invalid_argument( ErrorMessage.str() );
	}
    
}

// The actual producer is created by the message handler for add producer 
// messages. The producer is only created if there are no existing producer 
// with the same ID. Note that this test is done to verify that this actor 
// manager does not have a similarly named producer. There could of course be 
// another producer with the same name at a different endpoint, and it cannot
// be tested.
//
// There could be a pre-check for the household ID as well, but this is 
// currently not implemented.

void ActorManager::CreateProducer( const ActorManager::AddProducer & Command, 
																   const Theron::Address TheTaskManager )
{
  std::string NewProducerName = "producer" + Command.GetID();
  
	auto CheckProducerName = 
			 [&]( std::shared_ptr< Producer > & TheProducer )->bool{
						return NewProducerName == TheProducer->GetAddress().AsString(); };
	
  if ( ValidID( Command.GetID() ) && 
       std::none_of( Producers.begin(), Producers.end(), CheckProducerName )  &&
			 std::none_of( DeletedProducers.begin(), DeletedProducers.end(), 
										 CheckProducerName ) )
    switch ( Command.GetType() )
    {
      case AddProducer::Type::Grid :
        // Not supported yet
        break;
      case AddProducer::Type::PhotoVoltaic :
        Producers.push_back( 
	        std::make_shared< PVProducer >( Command.GetID(), 
									                        Command.GetFileName(), 
									                        SolutionTolerance, MaxEvaluations ) );
        break;
      case AddProducer::Type::Battery :
        // Not supported yet
        break;
    }
    
  // The address of the actor performing the task manager role is recorded 
  
  HouseholdTaskManager = TheTaskManager;
}

/*=============================================================================

 Load creation

=============================================================================*/
// 
// The method to serialise the the create load command is trivial. Note that 
// we assume that the receiver does not need any of the optional fields that
// may be available in the full message format. Note that the special number 
// zero is used for the number of producers to indicate that this number is 
// not known and undefined is also supported.

Theron::SerialMessage::Payload ActorManager::CreateLoad::Serialize( void ) const
{
  std::ostringstream Message;
  
  Message << "LOAD " << "ID " << LoadID  
				  << " EST " << EarliestStartTime << " LST " << LatestStartTime 
				  << " SEQUENCE " << SequenceNumber
				  << " PROFILE " << Profile << std::endl;
	  
  return Message.str();
}

// De-serialising the string is more demanding since the message can contain 
// optional information not necessary for the scheduling and the keywords can 
// come in any order. An example of all possible keywords is the string
//
// LOAD status 0 profile u1d1m1.prof EST 123450 AET UNDEFINED AST UNDEFINED 
// execution_type single_run LST 123900 mode 1 type 1 id 393 deviceID 1
// producers UNDEFINED SEQUENCE 123
//
// Thus every keyword is followed by an argument at the exception of the first 
// keyword which is the command "LOAD". It is also assumed that every numerical
// field can be undefined, except for the EST and the LST fields that must be 
// given for the scheduling problem to make sense.

bool ActorManager::CreateLoad::Deserialize(
  const Theron::SerialMessage::Payload & Payload)
{
  enum class Tags
  { 
    ActualEndOfTime,		  // Integral time in seconds
    AssignedStartTime,		// Integral time in seconds
    DeviceID,				      // Integral value in seconds
    EarliestStart,			  // Integral time in seconds
    ExecutionType,			  // String
    Identification,			  // Integral value
    LatestStart,			    // Integral time in seconds
    Mode,					        // Integral value
    Profile,				      // String file name
    Sequence,				      // Sequence number
    Status,					      // Integral value
    Type					        // Integral value
  };
 
  // The keywords for these tags are defined in upper case letters as a unique
  // way to compare and find them.
  
  const std::map< std::string, Tags > Keywords =
  { {"AET", 		        Tags::ActualEndOfTime     },
    {"AST", 		        Tags::AssignedStartTime   },
    {"DEVICEID", 	      Tags::DeviceID            },
    {"EST", 		        Tags::EarliestStart       },
    {"EXECUTION_TYPE",  Tags::ExecutionType 	    },
    {"ID", 		          Tags::Identification 	    },
    {"LST", 		        Tags::LatestStart	        },
    {"MODE",		        Tags::Mode		            },
    {"PROFILE",		      Tags::Profile		          },
    {"SEQUENCE",	      Tags::Sequence		        },
    {"STATUS",		      Tags::Status		          },
    {"TYPE",		        Tags::Type		            }
  };
  
  // The needed message variables are initialised deleting any previous content
  
  LoadID.Clear();
  EarliestStartTime = 0;
  LatestStartTime   = 0;
  Profile.erase();
  
  // The first check is to see if the command is a LOAD command, and if it is 
  // we can continue to parse the payload for as long as there is content left.
  
  std::istringstream Message( Payload );
  std::string 	     Command;
  
  Message >> Command; 
  
  if ( Command == "LOAD" )
  {
    while( !Message.eof() )
    { 
      // Read the next information item tag from the message and convert it to
      // upper case to be insensitive to the way the tag is provided to us
      
      std::string Tag;
      Message >> Tag;
      boost::to_upper( Tag );

      // Then we read the supported tags, and simply discard the content of the 
      // unsupported ones. Note that the "at" function will throw a standard 
      // out of range message if an unknown tag appears.

      switch ( Keywords.at( Tag ) )
      {
		case Tags::EarliestStart:
		  Message >> EarliestStartTime;

		  break;
		case Tags::LatestStart:
		  Message >> LatestStartTime;
		  break;
		case Tags::Identification:
		  Message >> LoadID;
		  break;
		case Tags::Profile:
		  Message >> Profile;
		  break;
		case Tags::Sequence:
		  Message >> SequenceNumber;
		  break;
		default:
		  Message >> Command; // Skip the value argument for this tag
		  break;
      }
    }
   
   // The values can only be accepted if all the mandatory fields were actually 
   // given, so we have to test for validity of all fields.
   
   if ( ValidID(LoadID) && (EarliestStartTime > 0) && (LatestStartTime > 0)
        && ( EarliestStartTime <= LatestStartTime ) && (Profile.length() > 0) 
        && ( SequenceNumber > 0) )
     return true;
   else
     return false;
  }
  else return false;
}

// The command constructors are simple and easy to understand. The most 
// elaborate is the one that calls the de-serialising method on a given message 
// as it will throw the standard invalid argument exception if the 
// initialisation fails.

ActorManager::CreateLoad::CreateLoad(
  const Theron::SerialMessage::Payload & Payload )
{
  if ( ! Deserialize( Payload ) )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "CreateLoad != " + Payload;
		
		throw std::invalid_argument( ErrorMessage.str() );
	}
    
}

// The full parameter constructor simply stores the given values.

ActorManager::CreateLoad::CreateLoad( IDType ID, Time EST, Time LST, 
	      const std::string & ProfileFileName,
	      unsigned int TheSequenceNumber,
	      const  std::optional< unsigned int > & ExpectedProducers  )
: Profile( ProfileFileName )
{
  LoadID 	   				= ID;
  EarliestStartTime = EST;
  LatestStartTime   = LST;
  SequenceNumber    = TheSequenceNumber;
}

// The message handler simply creates the consumer agent and inserts it into
// the list of consumers active on this node (network endpoint). The sender of
// this command is taken to be the task manager on the network endpoint, and 
// its actor address is passed on to the consumer so that it can use it to 
// inform the task manager when it has a start time assigned. Again, the 
// request is silently ignored if there is already a consumer with this ID.

void ActorManager::NewConsumer( const ActorManager::CreateLoad & TheLoad, 
																const Theron::Address TheTaskManager )
{
  // Indicate that the consumer is being created.
  
	#ifdef CoSSMic_DEBUG
		  Theron::ConsolePrint DebugMessage;
			
			DebugMessage <<  Now() << " New consumer " <<  TheLoad.GetID() <<  " [" 
									 << TheLoad.GetEST() << "," << TheLoad.GetLST() << "]" 
									 << std::endl;
	#endif

  // Cache the consumer ID
	 
	IDType NewConsumerID( TheLoad.GetID() );
	
	// The address of the task manager is also cached
	
	HouseholdTaskManager = TheTaskManager;

  // Causality check to see that the latest start time is in the future plus 
  // the schedule delay. 
	
  if ( TheLoad.GetLST() < ( Now() + FixedSchedulingDelay ) )
    Send( ConsumerAgent::CancelStartTime( NewConsumerID ), TheTaskManager );
  else
  {
    // The load can be scheduled, and it is necessary to check that no other 
    // consumer with the same ID already exists. A comparator function is used 
		// for this.
		
		auto CompareConsumer = 
					[&]( std::shared_ptr< ConsumerAgent > & TheConsumer )->bool{ 
            return TheConsumer->GetID() == NewConsumerID; };
		
		// Three cases exists: A consumer with the given ID exist in the list of 
		// closing consumers, in this case the create load message will simply be 
		// re-sent to the actor manager hoping that the consumer has been properly 
		// deleted next time the message is processed. 
						
		if ( std::any_of( DeletedConsumers.begin(), DeletedConsumers.end(), 
											CompareConsumer ) )
		{
			Send( TheLoad, GetAddress() );
			return;
		}
			
		// The if a consumer with the given ID does not exist, it will be created.
		// If a consumer does exist as an active consumer, then this request is 
		// simply ignored since it could be an issue with the task manager that 
		// it tries to create the same load multiple times.  
		
    if ( std::none_of( Consumers.begin(), Consumers.end(), CompareConsumer  ) )
	  {
      Consumers.emplace_back( new ConsumerAgent( NewConsumerID,
					      TheLoad.GetEST(), TheLoad.GetLST(), 
					      TheLoad.GetSequence(), TheLoad.GetFileName(), TheTaskManager ));
					      
      // Then the reward calculator is informed about this new consumer
			
			Send( RewardCalculator::AddConsumer( Consumers.back()->GetAddress() ), 
						Evaluator );
	  }
    #ifdef CoSSMic_DEBUG
      else       
        DebugMessage << "Consumer " << NewConsumerID << " already exists!" 
										 << std::endl;  
    #endif
  }
  
}

/*=============================================================================

 Load deletion

=============================================================================*/
//
// Instructions to delete a consumer will be sent using the delete consumer 
// message class that can be serialised as the command followed by the ID to 
// remove.

Theron::SerialMessage::Payload ActorManager::DeleteLoad::Serialize( void ) const
{
  std::ostringstream Message;
  
  Message << "DELETE_LOAD " << LoadID << " " << TotalEnergy << " "
				  << ProducerID << std::endl;
  
  return Message.str();
}

// This message can be easily de-serialised

bool ActorManager::DeleteLoad::Deserialize(
  const Theron::SerialMessage::Payload & Payload )
{
  std::istringstream Message( Payload );
  std::string 	     Command;
  
  Message >> Command;
  
  if ( (Command == "DELETE_LOAD") && !Message.eof() )
  {
    Message >> LoadID >> TotalEnergy >> ProducerID;    
    return true;
  }
  else return false;
}

// The previous de-serialising method is directly invoked by the constructor 
// accepting the serialised payload

ActorManager::DeleteLoad::DeleteLoad(
  const Theron::SerialMessage::Payload & Payload )
: LoadID(), ProducerID(), TotalEnergy(0)
{
  if ( ! Deserialize( Payload ) )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "DeleteLoad != " << Payload;
								 
	  throw std::invalid_argument( ErrorMessage.str() );
	}
}

// The handler dealing with these commands needs to find the consumer with the 
// right ID and then delete this consumer if it exists. It uses the standard 
// find-if function with a special lambda comparator that will compare the 
// ID of the stored consumer with the ID of the consumer to search for as given
// by the command.

void ActorManager::RemoveConsumer( const ActorManager::DeleteLoad & TheCommand, 
                                   const Theron::Address TheTaskManager )
{
  IDType SearchedID( TheCommand.GetID() );
  
  auto TheConsumer  = std::find_if( Consumers.begin(), Consumers.end(),
    [=](const std::shared_ptr< ConsumerAgent > & aConsumer)->bool
    { return aConsumer->GetID() == SearchedID; }
  );
  
  if ( TheConsumer != Consumers.end() )
  {

		#ifdef CoSSMic_DEBUG
		  Theron::ConsolePrint DebugMessage;
			
			DebugMessage <<  Now() << " Delete consumer " << SearchedID
									 << " that got energy " << TheCommand.GetEnergy()
									 << " from producer " 	<< TheCommand.GetProducer()
									 << std::endl;
		#endif

		Theron::Address ConsumerAddress = (*TheConsumer)->GetAddress();
		
    // When the new energy request is sent to the reward calculator it will be 
    // sent with the acknowledgement receiver as the sender allowing the ack 
    // to be sent directly to this.
    
    Send( RewardCalculator::AddEnergy( 
          ConsumerAddress, TheCommand.GetEnergy(),TheCommand.GetProducer() ),
          Evaluator );

		// Then the Consumer is inserted into the list of deleted consumers and 
		// deleted from the list of active consumers
		
		DeletedConsumers.push_back( *TheConsumer );
    Consumers.erase( TheConsumer );    
  }
  
  // Finally the task manager address is updated
  
  HouseholdTaskManager = TheTaskManager;
}

// The reward calculator will respond with an acknowledge energy message once 
// the reward has been computed and sent to the consumer actor. Then a shut 
// down message can be sent to the consumer. This message will be processed 
// by the consumer after the reward message. The consumer will then de-register
// its proxy with the currently selected producer and confirm with a shut down
// complete message once the proxy is confirmed deleted by the producer.
// 
// Not that in this case there is no need to actually look up the consumer in 
// the list of deleted consumers since its address is given by the acknowledge
// energy message, and so the delete request can readily be sent.

void ActorManager::RewardComputed( const ActorManager::AcknowledgeEnergy & Ack, 
																   const Theron::Address TheRewardCalculator )
{
	Send( ShutdownMessage(), Ack.RewardedConsumer );
}

/*=============================================================================

 Shut down management

=============================================================================*/
//
// -----------------------------------------------------------------------------
// Shut down message
// -----------------------------------------------------------------------------
//
// Serialisation of a shut down message is trivial...

Theron::SerialMessage::Payload 
ActorManager::ShutdownMessage::Serialize( void ) const
{
  return "SHUTDOWN";
}

// De-serialisation implies to check if the right command is provided and then 
// either accept or reject the message

bool ActorManager::ShutdownMessage::Deserialize(
  const Theron::SerialMessage::Payload & Payload ) 
{
  if ( Payload == "SHUTDOWN" ) return true;
  else 
    return false;
}

// The message constructor uses the Deserialize function to check if this is 
// the right type of message, and throw an exception if it is not.

ActorManager::ShutdownMessage::ShutdownMessage(
  const Theron::SerialMessage::Payload & Payload)
: ShutdownMessage()
{
  if ( ! Deserialize( Payload ) )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "Shutdown != " << Payload;
								 
	  throw std::invalid_argument( ErrorMessage.str() );
	}
}

// -----------------------------------------------------------------------------
// Shut down handler
// -----------------------------------------------------------------------------
//
// The message handler will remove any consumers hosted by the 
// actor manager, then forward the shut down message to the shut down receiver
// object since the main thread may wait for this.

void ActorManager::ShutDownHandler( 
		 const ActorManager::ShutdownMessage & Message, 
	   const Theron::Address TheTaskManager )
{
	// Deleting the consumers first since they may need to communicate with 
	// the producers. The normal delete consumer handler is used to remove 
	// each consumer. This to avoid duplication of code and to ensure that the 
	// protocol with the other actors is properly respected. 
	
	while ( ! Consumers.empty() )
	{
		auto Consumer = Consumers.begin();
		
		Theron::Address ProducerAddress;
		std::string     ProducerID;
		
		// Theoretically the consumer could just have been rejected from a producer.
		// This leads the consumer to kill the proxy on the rejecting producer,
		// and the next producer will not be selected before the rejecting producer
		// confirms that the proxy has been deleted. If the system is shutting down
		// in this interval, then there selected producer is invalid, and it is 
		// not possible to remove correctly the consumer. In this case, the only 
		// option is to wait for the consumer to select the producer. Since the 
		// removal of the proxy can involve a producer on a remote node, it is 
		// important to wait sufficiently long.
		
		while ( (ProducerAddress = (*Consumer)->GetSelectedProducer()) 
			      == Theron::Address::Null() )
			std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
		
		// At this point the producer address should be valid and the consumer 
		// can be deleted. Note that the ID of the producer has to be given, not 
		// its address. The address should be of the form "producer[x]:[y]:[z]"
		// where "[x]:[y]:[z]" is the ID part of the producer name.

		ProducerID = ProducerAddress.AsString();
		ProducerID = ProducerID.substr( ProducerID.find_first_of("[") ); 
		
		RemoveConsumer( DeleteLoad( (*Consumer)->GetID(), (*Consumer)->GetEnergy(), 
													      IDType( ProducerID ) ), 
									  Theron::Address::Null() );
	}

	// Then the (local) producers will be told to shut down by sending them the 
	// shut down message, and move the producer to the deleted producer lists. It 
	// will do this by deleting the 
	
	while ( ! Producers.empty() )
	{
		auto aProducer( Producers.front() );
		
	  Send( Producer::ShutdownMessage(), aProducer->GetAddress() );
		DeletedProducers.push_back( aProducer );
		Producers.pop_front();
	}
	
	// Finally, the task manager address is recorded and the  global shut down 
	// flag is set to indicate that the actor manager must close once the last 
	// consumer and producer has confirmed its shut down.
	
	HouseholdTaskManager = TheTaskManager;
	GlobalShutDown 			 = true;
}

// -----------------------------------------------------------------------------
// Shut down confirmation handler
// -----------------------------------------------------------------------------
//

void ActorManager::ShutDownComplete( const ConfirmShutDown & Confirmation, 
																		 const Theron::Address ClosedAgent     )
{
	// If the closed agent is a deleted consumer, it is simply deleted.
	
	auto TheConsumer  = std::find_if( DeletedConsumers.begin(), 
																		DeletedConsumers.end(),
    [&](const std::shared_ptr< ConsumerAgent > & aConsumer)->bool
    { return aConsumer->GetAddress() == ClosedAgent; }  );
  
  if ( TheConsumer != DeletedConsumers.end() )
	{
		// In the case of a consumer a delete load message should be sent back to 
		// the household's task manager. This message should contain the ID of the 
		// closing consumer and the ID of its final producer.
		
		IDType ConsumerID( ClosedAgent.AsString() ), 
					 ProducerID( (*TheConsumer)->GetSelectedProducer().AsString() );
		
	  Send( DeleteLoad( ConsumerID, 0.0, ProducerID ), HouseholdTaskManager );
	 
		DeletedConsumers.erase( TheConsumer );
		
    #ifdef CoSSMic_DEBUG
      Theron::ConsolePrint DebugMessage;
      DebugMessage << "Actor Manager has removed consumer " 
                   << ConsumerID <<  std::endl;
    #endif
	}
	else
	{
		// The agent could be a producer closing, and if so it should be deleted. 
		// In this case we do not need to confirm the deletion to anyone
		
		auto TheProducer = std::find_if( DeletedProducers.begin(), 
																		 DeletedProducers.end(),
				 [&](const std::shared_ptr< Producer > & aProducer)->bool
				 { return aProducer->GetAddress() == ClosedAgent; } );
		
		if ( TheProducer != DeletedProducers.end() )
			DeletedProducers.erase( TheProducer );
	}
	
	// In either case, if the global shut down has been flagged, the shut down 
	// is complete if there are no more producers and no more consumers pending 
	// their shut down operations.
	
	if ( GlobalShutDown && Consumers.empty() && Producers.empty() 
											&& DeletedConsumers.empty() && DeletedProducers.empty() )
	{
		// In this case a shut down message must be passed back to the task manager 
		// to inform it about the successful shut down.
		
		Send( ShutdownMessage(), HouseholdTaskManager );
		
		// Finally, the Network Endpoint's shut down procedure can be started, and 
		// this should terminate with all actors being removed and eventually cause
		// main() to be terminated.
		
		Send( Theron::Network::ShutDownMessage(), Theron::Network::GetAddress() );
	}
}

// The various agents asked to shut down will confirm when thy are ready to 
// close. 

/*****************************************************************************
  Constructor and destructor
******************************************************************************/

ActorManager::ActorManager( const Theron::Address & TheCalculator,
												    double ToleranceForSolution, int EvaluationLimit  )
: Actor( ActorManagerName ),
  StandardFallbackHandler( ActorManagerName ),
  DeserializingActor( ActorManagerName ),
  Producers(), DeletedProducers(), 
  Consumers(), DeletedConsumers(), 
  HouseholdTaskManager(), Evaluator( TheCalculator )
{
	if ( ToleranceForSolution > FixedSchedulingDelay )
	  SolutionTolerance = ToleranceForSolution;
	else
		SolutionTolerance = FixedSchedulingDelay;
	
  MaxEvaluations    = EvaluationLimit;
	GlobalShutDown    = false;
	
  // Then we will register the handlers for the messages to be received by 
  // the actor manager
  
  RegisterHandler(this, &ActorManager::CreateProducer   );
  RegisterHandler(this, &ActorManager::NewConsumer      );
  RegisterHandler(this, &ActorManager::RemoveConsumer   );
	RegisterHandler(this, &ActorManager::RewardComputed   );
  RegisterHandler(this, &ActorManager::ShutDownHandler  );	
	RegisterHandler(this, &ActorManager::ShutDownComplete );
} 

// The destructor does nothing.

ActorManager::~ActorManager( void )
{}

}; 	// End name space CoSSMic
