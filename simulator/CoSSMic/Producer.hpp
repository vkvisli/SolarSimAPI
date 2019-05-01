/*=============================================================================
  Producer
  
  A producer is a generic concept that supports two messages: 'NewLoad' which 
  indicates that a load needs energy from a producer. The producer will then 
  create a proxy representing the remote consumer requesting this load. When
  the load has executed, or if the consumer is being cancelled for some reason,
  then the consumer issues a 'KillProxy' message, and this must be acknowledged
  to prevent race conditions in distributed settings.
  
  The producer is an actor that will work locally, but it does support 
  serialisation and de-serialisation of the messages using the general 
  mechanism offered by the presentation layer. Whether it will act as an 
  actor or register with the presentation layer as an agent depends on which 
  constructor that is invoked.
  
  REFERENCES:
  
  [1] Geir Horn: Scheduling Time Variant Jobs on a Time Variant Resource, 
      in the Proceedings of The 7th Multidisciplinary International Conference 
      on Scheduling : Theory and Applications (MISTA 2015), Zdenek Hanzálek et
      al. (eds.), pp. 914-917, Prague, Czech Republic, 25-28 August 2015.
 
  Author: Geir Horn, University of Oslo, 2015-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef COSSMIC_PRODUCER
#define COSSMIC_PRODUCER

#include <string>									// For text strings
#include <list>										// For storing assigned loads
#include <memory>									// For shared pointers
#include <iterator>								// For iterator operations
#include <type_traits>						// For testing base classes
#include <optional> 		          // Optionally assigned start times

#include "Actor.hpp"							// The Theron++ actor framework
#include "SerialMessage.hpp"			// Support for network messages
#include "StandardFallbackHandler.hpp"

#include "IDType.hpp"							// CoSSMic IDs
#include "TimeInterval.hpp"				// CoSSMic Time and time intervals
#include "NetworkEndPoint.hpp"	  // Network endpoint for external communication
#include "PresentationLayer.hpp"	// Serial network messaging
#include "DeserializingActor.hpp" // Support for receiving a serial message

namespace CoSSMic 
{

// The consumer proxy will refer to nested classes of the producer, but the 
// producer only needs the entire consumer proxy class. In order to avoid a 
// circular reference, the consumer proxy is forward declared here.

class ConsumerProxy;

// Then the producer base class can be defined.

class Producer : virtual public Theron::Actor,
								 virtual public Theron::StandardFallbackHandler,
								 virtual public Theron::DeserializingActor
{
public:

  // ---------------------------------------------------------------------------
  // Producer types
  // ---------------------------------------------------------------------------
  //
  // Since producer actors can exist on different nodes, i.e. network endpoints, 
  // their type must be coded in the address name of the actor in order to know
  // that it is a producer and its type. However, it should be up to the actor
  // type to define the address convention. For instance, the Grid producer 
  // could be called "producer[0]:[0]" or it could be called "grid[0]:[0] or 
  // something else. If an actor has the address of another actor and want to 
  // know if it is a producer it must invoke a static function on the producer 
  // type in order to check if the address corresponds to the given type. 
  // 
  // the following implements a uniform interface to these functions at the 
  // added benefit of checking at compile time that the given class really is 
	// a known producer.

	template < class ProducerType >
	static bool CheckAddress( const Theron::Address & TheProducerAddress )
	{
		static_assert( std::is_base_of< Producer, ProducerType >::value,
		  "Producer::CheckAddress called for a type which is not a producer" );
		
		return ProducerType::TypeName( TheProducerAddress.AsString() );
	}

	// To be consistent it is necessary to provide the function to test the type
	// name also for the producer base class, and it is based on the generic 
	// naming convention for producers
	
private:
	
	constexpr static auto ProducerNameBase = "producer";
	
	static bool TypeName( const std::string & ActorID )
	{
		if ( ActorID.find( ProducerNameBase ) == std::string::npos )
			return false;
		else
			return true;
	}
	
  // ---------------------------------------------------------------------------
  // The Schedule Command for new loads
  // ---------------------------------------------------------------------------
  //
  // A Schedule Command is sent to the Producer when a new load should 
  // be scheduled on this end point. The requesting load must send the Earliest 
  // Start Time, the Latest Start Time, and the cumulative load function. 
  // According to the framework outlined in [1] it is only necessary to transfer
  // the maximum energy needed by the load.
  //
  // This command could be sent transparently across the network to a remote 
  // actor manager, and it is therefore a message serialisation implementing 
  // functions to serialize and de-serialize the content.
  
public:
  
  class ScheduleCommand : public Theron::SerialMessage
  {
  private:
    
    TimeInterval 	AllowedStart;  	// Earliest start time, latest start time
    Time 	 				JobDuration;		// The time the job is active
    double 	 			EnergyNeeded;  	// Energy consumption
    
  public:
    
    // Access functions to read the private fields
    
    inline Time EarliestStartTime( void ) const
    { return AllowedStart.lower(); }
    
    inline Time LatestStartTime( void ) const
    { return AllowedStart.upper(); }
    
    inline Time Duration( void ) const
    { return JobDuration; }
    
    inline double TotalEnergy( void ) const
    { return EnergyNeeded; }
    
    inline TimeInterval AllowedStartWindow( void ) const
    { return AllowedStart; }
    
    // The constructor simply takes all of these fields as arguments
    
    ScheduleCommand( Time EarliestStart, Time LatestStart, 
								     Time Delta, double Energy );

    // The copy constructor does the same initialisation based on the given 
    // command.
    
    ScheduleCommand( const ScheduleCommand & OtherCommand );
    
    // Then the class implements the necessary functions to serialize and de-
    // serialize this class over the network.
    
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool 
	    Deserialize( const Theron::SerialMessage::Payload & Payload ) override;
    
    // Since the de-serialise function must be called on an existing object 
    // and it will initialise that object, there must be an default (empty) 
    // constructor as well.
    
    ScheduleCommand( void ) : AllowedStart()
    {
      JobDuration  = 0;
      EnergyNeeded = 0.0;
    }
    
    // And a constructor simply calling the method to de-serialise the message.
    // Note that there is no mercy if the payload does not correspond to a 
    // schedule command and in this case a standard invalid argument exception
    // will be thrown.
    
    ScheduleCommand( const Theron::SerialMessage::Payload & Payload );
		
		// Finally there is a virtual destructor that does nothing but is included
		// for completeness.
    
		virtual ~ScheduleCommand( void )
		{ }
  };

  // ---------------------------------------------------------------------------
  // Assigning start time to loads
  // ---------------------------------------------------------------------------
  // 
  // At the end of the scheduling process a start time is assigned to the loads
  // that can have their energy supplied from this producer. However it can
  // also happen that the producer is over-subscribed so that not all loads can
  // get energy from this producer. They will then have an unassigned start 
  // time. This an ideal application of the std::optional class. However,
  // the assigned start time will be sent by the proxy to the (possibly remote)
  // consumer agent, and it must therefore be a message that can be serialised.
  
  class AssignedStartTime : public std::optional< Time >,
												    public Theron::SerialMessage
  {
  public:
    
		// The standard optional does not support comparison of optionals, so these
		// must be explicitly defined. Note that in order to compare favourably, 
		// both start times must have defined values, otherwise the comparison 
		// should fail.
		
		inline bool operator == ( const AssignedStartTime & Other )
		{
			if ( has_value() && Other.has_value() )
				return value() == Other.value();
			else
				return false;
		}
		
		inline bool operator < ( const AssignedStartTime & Other )
		{
			if ( has_value() && Other.has_value() )
				return value() < Other.value();
			else
				return false;
		}
		
    // The functions to serialise and de-serialise the message are inherited
    
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;

    // There is a constructor that basically invokes the method above to de-
    // serialise a string
    
    AssignedStartTime( const Theron::SerialMessage::Payload & Payload );
    
    // There are two constructors depending on whether the value should be 
    // initialised or not.
    
    AssignedStartTime( void )
    : std::optional< Time >()
    { }
    
    AssignedStartTime( const Time & TimeSet )
    : std::optional< Time >( TimeSet )
    { }
    
    // The copy constructor is very similar to the latter direct constructor
    
    AssignedStartTime( const AssignedStartTime & OtherMessage )
		: std::optional< Time >( OtherMessage )
		{}
    
    // There is a standard virtual destructor to needed for serialised messages
    
    virtual ~AssignedStartTime( void )
		{ }
		
		// In order to support the output of the assigned start time, the stream 
		// operator must be defined, with an explicit test to see if a value is 
		// defined or not.
		
		friend std::ostream & operator << ( std::ostream & OutputStream, 
																        const AssignedStartTime & StartTime )
		{
			if ( StartTime )
				OutputStream << StartTime.value();
			else
				OutputStream << "--";
			
			return OutputStream;
		}
  };

  // ---------------------------------------------------------------------------
  // Consumer proxies management
  // ---------------------------------------------------------------------------
  // The consumer proxies are managed in a structure 
  // having a pointer to the managed proxy object, which is a managed pointer 
  // that will be sent to the Predictor actor once the consumer has started 
  // its execution.

private:
  
  using ManagedConsumerPointer = std::shared_ptr< ConsumerProxy >;
  
  // The proxies for the consumers assigned to this producer is stored in a
  // simple list.
  
  std::list< ManagedConsumerPointer > AssignedConsumers;
	
	// The consumer proxies will use the producer to send messages back to the 
	// consumer actor that may be on a remote node (network endpoint), and since
	// the producer is an agent with an external address whereas the proxy is 
	// not, the message should be sent from the producer.
	
	friend class ConsumerProxy;
  
public:
	
  // Then there is a series of interface functions that can be used by derived
  // producers to store and delete elements, and to step through the list. These
  // functions generally operates on iterators to the elements since the list 
  // guarantees that iterators to valid elements remain valid also after 
  // deletion. The elements are available through iterators.
	
	using ConsumerReference = std::list< ManagedConsumerPointer >::iterator;
  
  // The functions returning the first and the last consumers are basically 
  // identical to the begin and end functions of the list of consumers.
  
  inline ConsumerReference FirstConsumer( void )
  {
    return AssignedConsumers.begin();
  }
  
  inline ConsumerReference EndConsumer( void )
  {
    return AssignedConsumers.end();
  }
  
  inline ConsumerReference LastConsumer( void )
  {
    return std::prev( AssignedConsumers.end() );
  }
  
  // There is also a function to remove a consumer from the list of 
  // assigned consumers that simply calls the erase function.
  
  inline void DeleteConsumer( const ConsumerReference & TheConsumer )
  {
    AssignedConsumers.erase( TheConsumer );
  }
  
  // It is easy to check how many consumers that are currently assigned
  
  inline std::size_t NumberOfConsumers( void )
  {
    return AssignedConsumers.size();
  }
  
  // It may be necessary to check if a consumer with a given address is 
  // allocated to this producer.
  
  ConsumerReference FindConsumer( const Theron::Address & TheConsumer );
       
  // ---------------------------------------------------------------------------
  // The Kill Proxy command
  // ---------------------------------------------------------------------------
  //
  // The consumer actor will kill its proxy on this node in three situations: it
  // was not allocated a start time, and must therefore try to source energy 
  // at a different producer node; or it has finished running the load and 
  // there is nothing more to do; or the user has cancelled this job. 
  
  class KillProxyCommand : public Theron::SerialMessage
  {
  public:
    
    // The only thing we need is to ensure the implementation of the serialize
    // functions

    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool 
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;

    // The class has a constructor that simply calls the method to de-serialise
    // a received message. This will throw an invalid argument exception if the 
    // payload does not correspond to a kill proxy command.
    
    KillProxyCommand( const Theron::SerialMessage::Payload & Payload );
    
    // We also need to provide the default constructor, but this has nothing 
    // to do since we are not storing any data with this command.
    
    KillProxyCommand( void ) = default;
		KillProxyCommand( const KillProxyCommand & OtherMessage ) = default;
    
    // And a virtual destructor to ensure proper inheritance if needed
    
    virtual ~KillProxyCommand( void )
    { }
  };

  // ---------------------------------------------------------------------------
  // Acknowledge proxy removal
  // ---------------------------------------------------------------------------
  // 
  // Once the concerned proxy has been removed, the removal is acknowledged by
  // sending an acknowledge command back to the consumer. Only when the 
  // consumer receives this message can it try to schedule the load on another 
  // producer.
  
  class AcknowledgeProxyRemoval 
  : public Theron::SerialMessage
  {
  public:
    
    // Again this class only implements the functions to support serialisation
    
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;

    AcknowledgeProxyRemoval( 
		 const Theron::SerialMessage::Payload & Payload );
    
    // In addition there is an empty, default constructor and the destructor
    
    AcknowledgeProxyRemoval( void ) = default;
		AcknowledgeProxyRemoval( 
			const AcknowledgeProxyRemoval & OtherMessage ) = default;

    virtual ~AcknowledgeProxyRemoval( void )
    { }
  };
  
protected:

  // ---------------------------------------------------------------------------
  // Message handlers
  // ---------------------------------------------------------------------------
  // The handler for the schedule message will by default only allocate and 
  // store the consumer proxy.
  
  virtual void NewLoad( const ScheduleCommand & TheCommand, 
                        const Theron::Address TheConsumer  );
    
  // The handler for the kill proxy command simply removes the proxy from the 
  // list, provided that it exists, and acknowledge the removal. The consumer 
  // agent will kill its proxy on this node in three situations: it was not 
  // allocated a start time, and must therefore try to source energy at a 
  // different producer node; or it has finished running the load and there is 
  // nothing more to do; or the user has cancelled this job. 

  virtual void KillProxy( const KillProxyCommand & TheCommand, 
                          const Theron::Address TheConsumer    );

  // ---------------------------------------------------------------------------
  // Shut down management
  // ---------------------------------------------------------------------------
  // When the Actor Manager is told to shut down it will remove the local 
	// consumers, which will then each engage the normal process to remove its 
	// proxy from the assigned producer. A producer for the closing Actor Manager
	// may however also host remote consumers that will not be notified by the 
	// closing Actor Manager. The Actor Manager will therefore send the shut down
	// message to all local producers, and the producers will then reset all 
	// assigned start times.
	
	// The Shut Down message must be declared as a specific Producer message 
	// because the Actor manager needs to include the Producer header and 
	// therefore there will be a circular dependency between the two headers. 
	// C++ does not allow a forward declaration of a nested class, i.e. the 
	// Actor Manager's Shut down message, and as such a shut down message 
	// specific to the producer must be provided. However, in contrast with the 
	// Actor Manager's shut down message, this will only be passed between the 
	// actor manager and its producers and all actors are at the same node, and 
	// it is therefore not necessary to serialise the message.
	
public:
	
	class ShutdownMessage
	{		
	public:
		
		ShutdownMessage( void ) = default;
		ShutdownMessage( const ShutdownMessage & OtherMessage ) = default;
	};
	
	// The actual handler for this message can then be defined acting on this 
	// message from the Actor Manager
	
private:
	
	void ShutDownHandler( const ShutdownMessage & Message, 
										    const Theron::Address HouseholdActorManager );
	
	// The shut down handler will replace the new load handler with one that 
	// simply rejects all schedule requests. 
	
	void RejectLoads( const ScheduleCommand & TheCommand, 
		                const Theron::Address TheConsumer  );
	
	// The handler receiving a requests to kill a proxy is extended with a test 
	// to check if the last proxy has been removed. If this is the case, then it 
	// will de-register the producer as an agent, i.e. ask the session layer to 
	// remove its external address. Theoretically, no more messages should then 
	// be received, and it should be possible to remove the producer actor.
	
	void AgentTermination( const KillProxyCommand & TheCommand, 
                         const Theron::Address TheConsumer    );
	
	// Since the agent termination must confirm back to the actor manager when 
	// all loads have removed their proxies, it needs to know the actor manager 
	// address. This is set by the shut down handler that will be directly 
	// invoked by the actor manager.
	
	Theron::Address TheActorManager;
	
  // ---------------------------------------------------------------------------
  // Constructors and destructor
  // ---------------------------------------------------------------------------
  //
  // The first constructor is used when the producer is operating as an actor
  // only with no external presence. It therefore only requires the Theron 
  // framework and the ID assigned to the producer. Without a valid ID, the
  // actor framework will give the producer a unique address. If an ID is 
  // given and it is not unique, Theron's default mechanism for checking 
  // uniqueness will apply, and Theron will terminate the application if there 
  // is another producer with the same name. TODO: Theron calls assert(false) 
  // after printing the error message - this should be a standard throw in 
  // order for the application to recover if possible. (See the Theron assert 
  // header under detail). By itself, this constructor will initialise the 
  // producer as an actor and register the message handlers for the new load 
  // message and for the kill proxy command.

public:

  Producer( const IDType & ProducerID );
  
  // The compatibility constructor simply delegates the construction to the 
	// normal constructor
  
  Producer( Theron::Framework & TheFramework, const IDType & ProducerID )
	: Producer( ProducerID )
	{ }
  
  // There is a virtual destructor that will throw a runtime error if the 
  // list of assigned consumers is not empty at the time of destruction.
  // The destructor does nothing, but it implicitly lets the list of assigned 
  // proxies delete the proxies if needed. However, one should respect the above
  // shut down procedure and not just delete the object.
  
  virtual ~Producer()
	{ }
};
  
}	// name space CoSSMic
#endif  // COSSMIC_PRODUCER
