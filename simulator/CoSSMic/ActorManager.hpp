/*=============================================================================
  Actor Manager
  
  The Actor Manager is the end point of a node in the CoSSMic distributed 
  scheduling system. Its role is to interact with the other actor managers, 
  and the multi-agent CoSSMic platform. 
  
  For a house that is equipped with the capacity for renewable energy 
  production, it is typically first called upon to create a "producer". A 
  Producer is implemented by two actors: The production predictor in charge of 
  receiving new predictions for the production and compute the available 
  energy for a given consumption interval. The second actor is the scheduler 
  responsible for assigning start times to the loads accepted to run on energy
  from this producer.
  
  A load is a request for energy that comes with a user specified Earliest Start
  Time (EST), a deadline by which the task associated with the load must be 
  completed, and a load profile showing how the energy will be consumed over
  time, E(t). This request goes from the Task Manager in the multi agent system
  to the Actor Manager on the same network node (endpoint). This will in turn 
  instantiate a Consumer actor to try to source energy for the load. This 
  Consumer actor will have an external network presence, and select an available
  Producer as a possible source of energy. It will then send a schedule command
  (see Producer) to this Producer actor.
  
  When receiving the schedule command, the Producer will create a Consumer 
  Proxy which will be running until the load is no longer active on the network
  node (endpoint) of the producer. This Consumer Proxy will interact with the 
  producer during the scheduling, and once a start time is assigned this will 
  be sent back to the Consumer actor.
  
  Author: Geir Horn, University of Oslo, 2015-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef ACTOR_MANAGER
#define ACTOR_MANAGER

#include <list>
#include <set>
#include <memory>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <optional>

#include "Actor.hpp"								// The Theron++ actor framework
#include "SerialMessage.hpp"				// For network messages
#include "StandardFallbackHandler.hpp" // Standard error reporting
#include "NetworkEndPoint.hpp"			// The network communication
#include "SessionLayer.hpp"					// The external address mapping
#include "PresentationLayer.hpp"	  // The serialisation of messages
#include "DeserializingActor.hpp"   // Support for receiving a serial message

#include "TimeInterval.hpp"         // For the fixed schedule delay
#include "IDType.hpp"               // ID format
#include "Producer.hpp"             // The Producer (PV panels)

// All the code that is specific to the CoSSMic project is isolated in its own
// name space. 

namespace CoSSMic
{
// In order to avoid a circular dependency on the Reward Calculator class it is 
// forward declared in this file
  
class RewardCalculator;

// The Consumer class is using the shut down message from the actor manager 
// and to avoid circular definitions, the class is only forward declared here

class ConsumerAgent;
  
// The actual manager is an actor that responds to a set of commands. It is 
// derived from the De-serializing Actor since it supports messages to be sent 
// over the network.
  
class ActorManager : virtual public Theron::Actor, 
										 virtual public Theron::StandardFallbackHandler,
                     virtual public Theron::DeserializingActor
{
private:
  
  // The actor manager should be the unique actor manager on a network endpoint
  // and its actor address should be unique. The Theron address of the actor 
  // manager is therefore defined as a function - it should have been a static 
  // variable, but static variables are global variables and Theron does not 
  // support global variables.
  
  constexpr static auto ActorManagerName = "actormanager";
  
  // In order to access this globally a static function is provided
  
public:
  
  inline static Theron::Address Address( void )
  {
    return Theron::Address( ActorManagerName );
  }
  
  // The Actor manager needs to keep track of the producers available on this
  // endpoint. They are simply kept in a list of managed pointers since it is 
  // not clear that we will need to access them frequently by ID or name, and 
  // so it can be acceptable to spin over the list to find a producer if needed.
  // When a producer is deleted, it is moved to the deleted producer list until
  // it has completed its shut down procedure and then it can finally be 
  // removed from the system.
  
private:
  
  std::list< std::shared_ptr< Producer > > Producers, DeletedProducers;
  
  // In the same way it keeps the assigned consumer agents running on this local 
  // node (network endpoint) corresponding to a load that has been scheduled for 
  // receiving energy from a producer or is in the process of finding and 
  // attaching to a producer that may provide energy. 
  
  std::list< std::shared_ptr< ConsumerAgent > > Consumers, DeletedConsumers;
	  
  // When searching for a solution it is useful to limit the accuracy needed 
  // or the number of iterations the solvers should be allowed to compute a 
  // good solution. These parameters are given to the Actor Manager constructor
  // and stored in two variables so that they can be forwarded to the 
  // PV producer when it is created.
  
  double SolutionTolerance;
  int    MaxEvaluations;
  
	// The address of the actor representing the household's task manager must be 
	// learned from the incoming messages since there should be only one task 
	// manager in the household.
	
	Theron::Address HouseholdTaskManager;

  // ---------------------------------------------------------------------------
  // Consumer rewards
  // ---------------------------------------------------------------------------
  // Based on the amount of PV energy exchanged, the various consumers and 
  // producers will be rewarded. There are many ways to compute and distribute 
  // the reward, and the Actor Manager therefore has the address of the reward
  // calculator only. This is given to the constructor, and the actual reward 
  // calculator must therefore be started before the actor manager.
  
  Theron::Address Evaluator;
  
  // ---------------------------------------------------------------------------
  // Starting producers
  // ---------------------------------------------------------------------------
  // The Actor Manager is told by the Task Manager running on the local host 
  // to create a producer on the local endpoint. The message received
  // is the add producer command. The message must contain the type of the 
  // producer to create, the ID of the producer, and in the case of a PV 
  // producer the file name of the initial prediction file.
  
public:
    
  class AddProducer : public Theron::SerialMessage
  {
  public:
    
    // The different producers supported must be uniquely identified. If this
    // message was not to be serialised, one could have implemented this with 
    // sub-classing the command for the various types, but since it should be 
    // constructed from a string it is easier to implement the support as a 
    // test. There are stream operators for this enumeration defined at the 
    // end of this file.
    
    enum class Type
    {
      Grid,
      PhotoVoltaic,
      Battery
    };
    
  private:
    
    Type   			ProducerType;
    IDType 			NewProducerID;
    std::string PredictionFile;

    // Since the message can be serialised it provides the virtual functions
    // for this
 
	protected:
		
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool 
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;
    
  public:
    
    // The standard constructor takes the producer type, its ID and the 
    // prediction file and just store them.
    
    AddProducer( Type TheType, IDType ID, std::string FileName )
    : ProducerType( TheType ), NewProducerID( ID ), PredictionFile( FileName )
    { }
    
    // Interface functions provide read access to the information.
    
    inline Type GetType( void ) const
    { return ProducerType; }
    
    inline IDType GetID( void ) const
    { return NewProducerID; }
    
    inline std::string GetFileName( void ) const
    { return PredictionFile; }
    
    // There is also a constructor calling the the de-serialise method, and 
    // if that fails, it will throw an invalid argument exception.
    
    AddProducer( const Theron::SerialMessage::Payload & Payload );
		
		// There is a default constructor initialising the variables to some kind 
		// of default values.
		
		inline AddProducer( void )
		: NewProducerID(), PredictionFile()
		{
			ProducerType = Type::Grid;
		}
    
		// There is a copy constructor to ensure that the message can be passed 
		// to other threads in a safe way.
		
		AddProducer( const AddProducer & OtherMessage )
		: ProducerType( OtherMessage.ProducerType ), 
		  NewProducerID( OtherMessage.NewProducerID ),
		  PredictionFile( OtherMessage.PredictionFile )
		{	}
		
    // The virtual destructor is just a mechanism for running the related 
    // destructor of the producer list.
    
    virtual ~AddProducer( void )
    {}
  };
  
  // The add producer message is handled by the create producer method. The 
  // sender is insignificant.

private:
  
  void CreateProducer( const AddProducer & Command, 
                       const Theron::Address TheTaskManager );

  // ---------------------------------------------------------------------------
  // Load creation
  // ---------------------------------------------------------------------------
  // The load creation results in the creation of a Consumer Agent. The message
  // takes multiple parameters, of which some are optional. It is also a message
  // that can be serialised across the network.

public:
  
  class CreateLoad : public Theron::SerialMessage
  {
  private:
    
    IDType 	 	 	 LoadID;             // Unique ID identifying the load
    Time 	 	 		 EarliestStartTime,  // EST - do not start before
		             LatestStartTime;	 	 // LST - must start at this time
    std::string  Profile;		     		 // CSV file name for the load profile
    unsigned int SequenceNumber;	   // The number of the run for the device

    // The mandatory functions for messages to be sent over the network must 
    // also be defined.

	protected:
		
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool 
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;
        
  public:
    
    // Then there are static interface functions to read the values of the 
    // fields
    
    inline IDType GetID( void ) const
    { return LoadID; }
    
    inline Time GetEST( void ) const
    { return EarliestStartTime; }
    
    inline Time GetLST( void ) const
    { return LatestStartTime; }
    
    inline std::string GetFileName( void ) const
    { return Profile; }
    
    inline unsigned int GetSequence( void ) const
    { return SequenceNumber; }
    
    // Finally, there is a constructor to create the message from the serialised
    // payload. Basically this is only calling the de-serialise method, and 
    // it throws a standard invalid argument expression if this initialisation 
    // fails.
    
    CreateLoad( const Theron::SerialMessage::Payload & Payload );
		
		// The default constructor is used for the cases where the message is 
		// first constructed and then initialised by de-serialising a payload.
		
		inline CreateLoad( void )
		: LoadID(), Profile()
		{
			EarliestStartTime = 0;
			LatestStartTime   = 0;
			SequenceNumber    = 0;
		}
    
    // The standard constructor takes these fields and assigns the variables
    
    CreateLoad( IDType ID, Time EST, Time LST, 
								const std::string & ProfileFileName,
								unsigned int TheSequenceNumber,
								const  std::optional< unsigned int > & ExpectedProducers
							      =  std::optional< unsigned int >()  );
    
		// The message has a copy constructor to allow it to be sent to other 
		// threads
		
		CreateLoad( const CreateLoad & OtherMessage )
		: LoadID( OtherMessage.LoadID ), 
			EarliestStartTime( OtherMessage.EarliestStartTime ),
			LatestStartTime( OtherMessage.LatestStartTime ),
			Profile( OtherMessage.Profile ),
			SequenceNumber( OtherMessage.SequenceNumber )
		{}
		
		// The message has a standard virtual destructor since it has virtual 
		// functions.
		
		virtual ~CreateLoad( void )
		{ }
  };
  
  // These messages are handled by the new consumer function that allocates 
  // and starts the consumer actor. There is a causality check to see if the 
  // loads latest start time allows the load to be scheduled. This implies 
  // that the latest start time must be at time Now + some estimated schedule 
  // delay. This is fixed as a number of seconds in a time constant.
  
private:
  
  static const Time FixedSchedulingDelay = 5;
    
  void NewConsumer( const CreateLoad & TheLoad, 
										const Theron::Address TheTaskManager );

	// ---------------------------------------------------------------------------
  // Load deletion
  // ---------------------------------------------------------------------------
	//
	// This is a complicated process involving several actors, messages and 
	// message handlers. The following steps should be executed:
	// 
	//  1. The Task manager informs the Actor Manager by a delete load message 
	//     That the load should be deleted. 
	// 	2. The reward for the consumed energy should be computed by the Reward
	//     calculator. Hence there must be a first message from the actor to 
	//     the reward calculator informing the reward calculator that this 
	//     load has been completed (or cancelled). This is done by the delete 
	//     load message handler that also moves the consumer actor to a list of 
	//     consumers to be deleted.
	//  3. The reward calculator computes the reward and sends this as a message
	//     back to the consumer agent directly. 
	//  4. The reward calculator informs the actor manager that the reward has 
	//     been sent by an acknowledge energy message. The handler for this 
	//     message sends a shut down message to the consumer.
	//  5. The consumer informs the selected producer that its proxy can be 
	//     removed.
	//  6. The producer acknowledge the proxy removal back to the consumer
	//  7. The consumer sends a shut down complete message back to the actor 
	//     manager. When this message has been received, the consumer agent 
	//     is deleted by the shut down complete handler.
	//
	// A complicating factor is that the task manager can move on to create a 
	// new load for the exact same device (ID) just after the delete load 
	// message. If a create load message arrives before the above process is 
	// completed, i.e. while the consumer is still in the list of consumers 
	// pending deletion, the create load message will simply be sent by the 
	// new consumer handler to the actor manager (itself). This because 
	// messages will be delivered in sequence, and hopefully the other 
	// messages of the above sequence have arrived before the create load 
	// message is processed again. Otherwise, the create load message will 
	// again be put back in the queue.
	
  // The delete load command is issued when the associated task is done, or 
  // when the user cancels the load.
  
public:
  
  class DeleteLoad : public Theron::SerialMessage
  {
  private:
    
    IDType LoadID, ProducerID;
    double TotalEnergy;

    // The two necessary functions to ensure that this message can be 
    // sent and received from remote actors.
 
	protected:
		
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool 
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;
    
  public:
    
    // The standard constructor to store the ID of the load, the energy it has
    // consumed and the provider of this energy
    
    DeleteLoad( const IDType & ID, const double Energy, 
								const IDType & TheProducer )
    : LoadID( ID ), ProducerID( TheProducer ), TotalEnergy( Energy )
    {
			if ( !LoadID || !ProducerID )
			{
				std::ostringstream ErrorMessage;
				
				ErrorMessage << "Delete load message constructed with at least one "
				             << "invalid ID. Load ID = " << LoadID 
				             << " and Producer ID = " << ProducerID;
										 
			  throw std::invalid_argument( ErrorMessage.str() );
			}
		}
    
    // There is also a copy constructor to ensure the transfer of the message 
    // to a different thread
    
    DeleteLoad( const DeleteLoad & OtherMessage )
		: LoadID( OtherMessage.LoadID ), ProducerID( OtherMessage.LoadID ),
		  TotalEnergy( OtherMessage.TotalEnergy )
		{}
    
    // Then there are interface functions to access the ID, the energy and 
    // the producer.
    
    inline IDType GetID( void ) const
    { return LoadID; }    
    
    inline IDType GetProducer( void ) const
    { return ProducerID; }
    
    inline double GetEnergy( void ) const
    { return TotalEnergy; }
    
    // Finally there is the constructor that can be directly used for a 
    // serialised payload, and that will throw a standard invalid argument 
    // exception if the payload cannot be properly de-serialised.
    
    DeleteLoad( const Theron::SerialMessage::Payload & Payload );

		// The default constructor is used when the message is first constructed 
		// and then initialised with the de-serialisation function above
		
		inline DeleteLoad( void )
		: LoadID(), ProducerID()
		{
			TotalEnergy = 0;
		}
		
		// A virtual destructor is needed also for the delete load message
		
		virtual ~DeleteLoad( void )
		{ }
  };
  
  // The handler for this message will look up the consumer ID and move the 
	// consumer to the list of consumers pending deletion. It will then notify
	// the reward calculator that this load has been completed.
  
private:
  
  void RemoveConsumer( const DeleteLoad & TheCommand, 
								       const Theron::Address TheTaskManager  );

  // The Actor Manager will send a message to the Reward Calculator when it gets
  // a delete load command from the Task Manager. This should remove the given
  // consumer from the local endpoint; either because the associated load has 
  // terminated or because the user has cancelled the load. If the device is 
  // intermittent, the next load is likely to be requested as soon as the 
  // previous load has terminated. Since the associated consumers are named 
  // after the device IDs, it is necessary to ensure that the current consumer
  // agent has terminated before a new can be created. For this reason there 
  // will be an acknowledgement sent from the Reward Calculator back to the 
  // Actor Manager once the reward has been calculated and sent to the consumer.
	//
	// It seems to be an issue with an empty class, and as such it is defined to 
	// contain a descriptive string. 
  
public:
  
  class AcknowledgeEnergy
  { 
	public:
		
		// The address of the consumer agent whose reward has been computed 
		// based on its consumed energy is returned in the message.
		
		const Theron::Address RewardedConsumer;
		
		// The constructor only sets this address based on the given consumer
		
		AcknowledgeEnergy( const Theron::Address & TheConsumer )
		: RewardedConsumer( TheConsumer )
		{ }
		
		AcknowledgeEnergy( const AcknowledgeEnergy & Other ) = default;
	};

	// There will be a handler for this acknowledgement that will send a shut 
	// down message to the consumer.
	
private:
	
	void RewardComputed( const ActorManager::AcknowledgeEnergy & Ack, 
											 const Theron::Address TheRewardCalculator );

	// The next message is then from the consumer that it has completed the 
	// shut down process, and can be deleted. The shut down completed message 
	// and the corresponding handler is defined below under the shut down 
	// management.
	
  // ---------------------------------------------------------------------------
  // Shut down
  // ---------------------------------------------------------------------------
  // The Actor Manager will continue to run as an agent in the Multi-Agent
  // system (MAS) responding to received messages. It is the main interface 
  // between the actors and the other parts of the MAS, and as an actor it 
  // should continue to execute forever. However, the main thread starting 
  // Theron and the Actor Manager must wait for some termination event in order
  // to avoid terminating just after creating the actor manager. 
  //
  // A shut down mechanism is therefore provided. It consists of a simple 
  // message that can be serialised as "SHUTDOWN". Then there is a standard 
  // Receiver to block the main thread waiting for the shut down message, and 
  // a message handler to receive the shut down command and forward it to the 
  // receiver. The message is defined first.
  
public:
  
  class ShutdownMessage : public Theron::SerialMessage
  {
	public:
    
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool 
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;

    // There are two constructors: one with no arguments and one taking the 
    // serialised message payload and throwing an exception if the payload is 
    // not a shut down command.
    
    ShutdownMessage( void ) = default;
    ShutdownMessage( const Theron::SerialMessage::Payload & Payload );
		
		ShutdownMessage( const ShutdownMessage & OtherMessage ) = default;
		
    // A virtual destructor is needed for a serialised message
    
    virtual ~ShutdownMessage( void )
		{}
  };
    
  // Finally the actor manager must provide a handler to receive a shut down 
  // message from the MAS and make sure that the actor manager terminates 
	// correctly
  
private:
  
  void ShutDownHandler( const ShutdownMessage & Message, 
												const Theron::Address TheTaskManager );
	
	// The shut down message is also used from the actor manager to the consumers 
	// and producers when they are supposed to shut down. When they have completed 
	// the shut down operation they will respond with a confirmation message. Note
	// that this message must come from consumers and producers managed by this 
	// actor manager, and hence they must be at the same network endpoint as the 
	// actor manager. Consequently, the message does not need to support 
	// serialisation.
	
public:
	
	class ConfirmShutDown 
	{
	public:
    
		// The constructors are the default ones
		
		ConfirmShutDown( void ) = default;
		ConfirmShutDown( const ConfirmShutDown & Other ) = default;
		
		// The virtual destructor is just a place holder
		
		virtual ~ConfirmShutDown( void )
		{ }
	};
  
	// The handler for the confirmation will act differently depending on whether 
	// the sender is a consumer or a producer. For consumers, the consumer agent 
	// will just be removed. This deletion will be confirmed back to the task 
	// manager by sending a delete load message with the ID of the deleted 
	// consumer. Producers will just be deleted. 
	// 
	// If all consumers and all producers are deleted, it could indicate a global 
	// shut down also for the actor manager. However, this is not assured since 
	// the actor manager could be running on a household where there are not 
	// attached producers, and hence it should not terminate every time there are 
	// no more active consumers. It must therefore be a global flag to indicate 
	// that a the system is shutting down.
	
private:
	
	bool GlobalShutDown;
	
	// Then the actual message handler
	
	void ShutDownComplete( const ConfirmShutDown & Confirmation, 
												 const Theron::Address ClosedAgent     );
	
  // ---------------------------------------------------------------------------
  // Constructors and destructor
  // ---------------------------------------------------------------------------
  // The constructor is associated with a network end point and takes the 
  // solution tolerance and the maximum number of objective function evaluations 
  // as input parameters. Both have default values, and the number of 
  // evaluations is by default unlimited.
  
public:
  
    ActorManager ( const Theron::Address & TheCalculator, 
									 double ToleranceForSolution = 1e-8, 
									 int EvaluationLimit = std::numeric_limits<int>::max() );

  // There is a destructor to ensure that all created actors are properly 
  // destroyed when the actor manager terminates. It currently takes care of 
  // de-registering the agent with the session layer.
  
  virtual ~ActorManager( void );
  
};	// End of class Actor Manager
}; 	// End name space CoSSMic

/*****************************************************************************
  Stream operators
******************************************************************************/

// There are operators to print and read the producer types supported in the 
// add producer command to and from streams. They are declared here so that 
// other users will know about their existence.
    
extern std::ostream & operator<< ( std::ostream & Output, 
       const CoSSMic::ActorManager::AddProducer::Type & ProducerType );

extern std::istream & operator>> ( std::istream & Input, 
       CoSSMic::ActorManager::AddProducer::Type & ProducerType );


#endif	// ACTOR_MANAGER
