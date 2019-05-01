/*=============================================================================
  Reward Calculator
  
  The reward calculator is a generic interface for classes that compute and 
  distribute the rewards to the consumers. The fundamental protocol is as 
  follows
  
  1. The Task Manager will ask the Actor Manager to create a consumer for 
     a given load. The Actor Manager will create the corresponding Consumer 
     Agent and inform the reward calculator about this new consumer.
  
  2. The total energy consumption of a consumer is known when it stops 
     executing the load it represents. The CoSSMic Task Manager will then 
     send a message to the Actor Manager to terminate the load. The Actor 
     Manager will send the New Energy method to the reward calculator. This 
     message must be acknowledged by the reward calculator since the Actor 
     Manger must know that the reward for this load has been calculated and 
     sent back to the Consumer Agent before the consumer agent can be safely 
     terminated, which is the next operation done by the same message handler 
     of the Actor Manager. The consumer agent must be destroyed before the 
     Actor Manager is ready to process its next message since this message 
     theoretically could recreate the same consumer for a different load.
  
  3. The reward calculator will then inform all peer reward calculators about 
     this newly closed energy transaction so that they are able to reward the 
     consumers and producers on the other nodes in the system. 
     
  The reward calculator maintains three sets:
  
    I. A set of peer reward calculator addresses for forwarding the information 
       about the transaction.
     
   II. A set of active local consumers to be rewarded
   
  III. A set of local producers to know if the PV energy was delivered by one 
       of the producers on this node (network endpoint).
       
  In addition it maintains a counter for the total PV energy produced in the 
  neighbourhood and the total PV energy shared by the producers on this node.
  
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef REWARD_CALCULATOR
#define REWARD_CALCULATOR

#include <set>			            		// To store consumers and remote SVR agents
#include <unordered_set>	 					// Keeping the IDs of local producers

#include "Actor.hpp"	 							// The Theron++ actor framework
#include "StandardFallbackHandler.hpp"
#include "PresentationLayer.hpp"		// For serialisation of external messages
#include "DeserializingActor.hpp" 	// Support for receiving a serial message
#include "SessionLayer.hpp"	 				// For detecting peer reward calculators
#include "NetworkEndPoint.hpp"  		// To allow the calculator to be an agent
#include "IDType.hpp"		 						// The CoSSMic agent ID
#include "AddressHash.hpp"	 				// For Theron Addresses in unordered maps

namespace CoSSMic
{

// The reward calculator is an agent since it exchanges messages with other 
// reward calculators on remote network endpoints
  
class RewardCalculator : virtual public Theron::Actor,
												 virtual public Theron::StandardFallbackHandler,
												 virtual public Theron::DeserializingActor
{  
  // ---------------------------------------------------------------------------
  // Variables
  // ---------------------------------------------------------------------------

private:
  
  // The reward should be a number in the interval [0,1] representing the 
  // relative value of the game attributed to the consumer rewarded. This 
  // implies that it is necessary to know the system wide PV energy consumed.
  
  double NeighbourhoodPVEnergy;
  
  // To compute this energy, it is necessary to accumulate the energy from 
  // local producers only if the consumer of the energy is not local. It should
  // be noted that only the total PV value needs to be recorded, so it is 
  // sufficient to keep the total value
  
  double TotalPVShared;
    
  // There is an issue with consumer agents being present only when they are 
  // playing, i.e. their load is active or they wait for the load to start. 
  // Hence, it is not possible to reward an inactive consumer since it does
  // not exist and sending it a message will cause a Theron exception and 
  // program termination. It is therefore necessary to remember explicitly 
  // the set of active consumers on this network endpoint. 
  //
  // It may seem unnecessary to store the active consumers since they are 
  // known by the Actor Manager, and it is bad practice to store information 
  // in two places with the risk of loosing the synchronisation. However, in
  // this synchronisation it is not a problem because the actor manager has to 
  // inform the reward calculator about created consumers in order to create
  // a row for this consumer in the energy matrix, and then again when the 
  // load is deleted for the reward calculator to produce the reward. 
  
  std::set< Theron::Address > ActiveConsumers;
  
  // Derived classes may need to read the energy values and the list of active
  // consumers, and consequently interface functions are provided to read them
  
protected:
  
  inline double GetNeighbourhoodPVEnergy( void )
  { return NeighbourhoodPVEnergy; }
  
  inline double GetSharedPVEnergy( void )
  { return TotalPVShared; }
  
  inline const std::set< Theron::Address > & GetConsumers( void )
  { return ActiveConsumers; }
  
  // All reward calculators must share the same value for the total PV energy 
  // consumed and hence once one of them gets notified about a consumption, 
  // this information must be shared with all the others. Hence, it is also 
  // necessary to have a set of addresses for these remote reward calculators. 
  // A normal set will be used since it is supposedly faster for element 
  // iteration than an unordered set.
  
private:
  
  std::set< Theron::Address > RewardCalculators;
  
  // In order to populate this set of remote reward calculators, this 
  // calculator must set up a subscription with the session layer to be notified
  // about new known actors, and then filter out the reward calculators. This
  // subscription must be cancelled when the reward calculator terminates, 
  // which requires a message to be sent to the session layer. Hence the 
  // address of the session layer must be stored between the constructor and 
  // the destructor.
  
  Theron::Address SessionServer;

  // The rewards will be communicated also to the local household user, who 
  // should also be informed about how much energy the household's producers
  // share with the neighbourhood. This requires the reward calculator to know
  // which are the local producers. This is again duplicating similar 
  // information in the Actor Manager, and a special protocol is defined to 
  // register local producers. A producer that has once been active locally, 
  // will never be forgotten even if the producer may disappear, e.g. a 
  // departing electrical vehicle taking its battery away. This because the 
  // energy shared from a producer stays as the household's contribution to 
  // the total neighbourhood energy consumption even if it no longer produces 
  // energy for the neighbourhood. The local producers are stored in as a 
  // unsorted set for quick lookup.
  
  std::unordered_set< IDType > LocalProducers;  

  // ---------------------------------------------------------------------------
  // MESSAGE: New local producer
  // ---------------------------------------------------------------------------
  //
  // The message that a new, local producer is available will be communicated 
  // from the local Actor Manager to the reward calculator via sending a 
  // message which is essentially the ID of the local producer. Thus, this is
  // an node internal message that is not supposed to be sent over the network, 
  // and hence it is not serialised. 
  
public:
  
  class NewProducer : public IDType
  { 
	public:
		
		NewProducer( const IDType & ID )
		: IDType( ID )
		{ }
		
		NewProducer( const NewProducer & OtherMessage )
		: IDType( OtherMessage )
		{}
	};
  
  // The message handler for this message simply adds the new producer ID to 
  // the set of local producers
  
private:
  
  void RegisterProducer( const NewProducer & TheProducer, 
                         const Theron::Address TheActorManager );
  
  // ---------------------------------------------------------------------------
  // MESSAGE: New PV Energy message
  // ---------------------------------------------------------------------------
  //
  // This message is used to broadcast the PV energy to the remote reward 
  // calculators. It contains the PV energy and the producer ID. The later is 
  // needed to accumulate the energy received by remote consumers on the 
  // producer node.

public:
  
  class NewPVEnergy : public Theron::SerialMessage
  {
  private:
    
    double EnergyValue;
    IDType Producer;
    
  public:
    
    inline double Energy( void ) const
    { return EnergyValue; }
    
    inline const IDType & ProducerID( void ) const
    { return Producer; }
    
    // The mandatory functions for serialising and de-serialising the message
    
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;

    // The explicit constructor simply sets the energy value
    
    NewPVEnergy( double ConsumedEnergy, const IDType & TheProducer )
    : Producer( TheProducer )
    {
      EnergyValue = ConsumedEnergy;
    }
    
    // The constructor for serialised messages from remote endpoints. It will
    // throw an invalid argument exception if the de-serialisation fails.
    
    NewPVEnergy( const Theron::SerialMessage::Payload & Payload );
		
		// The default constructor is used when the message is first constructed 
		// and then initialised with by de-serialising a payload.
		
		inline NewPVEnergy( void )
		: Producer()
		{
			EnergyValue = 0;
		}
		
		// There is a copy constructor ensuring that the message can be transferred
		// among threads
		
		NewPVEnergy( const NewPVEnergy & OtherMessage )
		: EnergyValue( OtherMessage.EnergyValue ), Producer( OtherMessage.Producer )
		{}
    
		// A virtual destructor is needed for serialised messages
		
		virtual ~NewPVEnergy( void )
		{ }
  };
  
  // When this message is received by a reward calculator, the default behaviour 
  // is just to accumulate energy value to the neighbourhood PV energy value and
  // to the energy produced by local producers if that is the case.
  
protected:
  
  virtual void NewPVEnergyValue( const NewPVEnergy & EnergyMessage, 
												         const Theron::Address Sender       );

  // After the energy has been used by a derived message handler to compute 
  // the reward, this should be saved as a reward file to be used by the other
  // parts of the CoSSMic system. 
  
  void SaveRewardFile( double NodeRewardValue );

  // ---------------------------------------------------------------------------
  // MESSAGE: Consumer management
  // ---------------------------------------------------------------------------
  //
  // When a consumer agent is created, the Actor Manager will inform this 
  // agent about the event by sending a new consumer message with the address 
  // of the new consumer actor. The message will be ignored if the consumer 
  // is already known from a previous load execution. If it is unknown it 
  // will increase the size of the energy exchange matrix, and be added to 
  // the set of local consumers. It is safe to use the actor address 
  // of the consumer as a global identifier since the consumer is an agent 
  // with external presence, its actor ID has to be unique for the whole 
  // neighbourhood.
  
public:
  
  class AddConsumer
  {
  private:
    
    Theron::Address TheConsumer;
    
  public:
    
    // There are also a utility function to obtain the consumer address
    
    inline Theron::Address GetAddress( void ) const
    { return TheConsumer; }

    // There are two constructors, one to construct copy a consumer address 
    // and one taking a payload of this message type and de-serialize it to
    // an address. The latter will throw an invalid argument expression if the 
    // payload does not correspond to an add consumer message
    
    AddConsumer( const Theron::Address & ConsumerAddress )
    : TheConsumer( ConsumerAddress )
    { }    
  };
  
  // The handler for this request will first check if the consumer is already 
  // known. If the consumer is unknown, i.e. never seen before, the add consumer
  // request will be forwarded to remote reward calculators to add it to all 
  // the energy exchange graphs. If the consumer has been seen before, the 
  // message must therefore come from the local Actor Manager as a consequence 
  // of creating a known consumer again for another load execution. In this 
  // case the load will be added only to the set of local consumers.
  
protected:
  
  virtual void NewConsumer( const AddConsumer & ConsumerRequest, 
			    const Theron::Address Sender );

  // Note that there is no need to reverse this process since the New Energy 
  // handler below will automatically delete the consumer from the active set
  // once the associated energy has been used to update the rewards.  
  
  // ---------------------------------------------------------------------------
  // MESSAGE: Compute the reward
  // ---------------------------------------------------------------------------
  //  
  // When a load terminates, the host actor manager will inform its reward
  // calculator about the new energy exchange using the add energy message.
  
public:
  
  class AddEnergy
  {
  private:
    
    Theron::Address ConsumerAddress;
    double	        ConsumedEnergy;
    IDType	        ProducerID;
    
  public:
    
    // Interface to extract the stored information
    
    inline Theron::Address Consumer( void ) const
    { return ConsumerAddress; }
    
    inline double Energy( void ) const
    { return ConsumedEnergy; }
    
    inline IDType Producer( void ) const
    { return ProducerID; }
    
    // The constructor simply fills the fields with the given values
    
    AddEnergy( const Theron::Address & TheConsumer, const double TheEnergy,
	       const IDType TheProducer )
    : ConsumerAddress( TheConsumer ), ConsumedEnergy( TheEnergy ),
      ProducerID( TheProducer )
    { }
  }; 
  
  // The default behaviour of the message handler is to dispatch the message
  // to all the peer reward calculators, erase the consumer from the set of 
  // active consumers, and acknowledge the reward computation back to the 
  // Actor Manager. This function should therefore be called as the last 
  // operation in any derived handler.
  
protected:
  
  virtual void NewEnergy( const AddEnergy & EnergyMessage, 
												  const Theron::Address Sender    );
  
  // ---------------------------------------------------------------------------
  // Detecting peer reward calculators
  // ---------------------------------------------------------------------------
  //  
  // The reward calculator will subscribe with the session layer to be notified 
  // when new peer actors become available. In order to find the other reward 
  // calculators they must be consistently named across all nodes. The name is 
  // therefore constructed from a fixed name root, extended by the domain name 
  // of the node (typically the symbolic IP address of the endpoint)
  
  constexpr static auto NameRoot = "RewardCalculator_";
  
  // As the peers become known to the session layer it will notify the 
  // actors subscribing by sending a new peer added message, which will be 
  // captured by the following handler.
  
private:
  
  void AddCalculator( 
       const Theron::SessionLayerMessages::NewPeerAdded & NewAgent,
       const Theron::Address SessionLayerServer );
  
  // A similar behaviour but in the reverse order is done by the handler 
  // deleting calculators, e.g. when a node goes off-line.
  
  void RemoveCalculator( 
       const Theron::SessionLayerMessages::PeerRemoved & LeavingAgent,
       const Theron::Address SessionLayerServer );
  
  // There is an external message that will be sent to remote reward calculators
  // when this shuts down to prevent them from sending further messages to this
  // calculator. 
  
  class Shutdown : public Theron::SerialMessage
  { 
	private:
		
		std::string Description;
		
  public:
    
    // The mandatory functions for serialising and de-serialising the message
    
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;

    // The default constructor is empty since the class is not storing any 
    // information.
    
    Shutdown( void )
    {
			Description = "Reward Calculator: Shut down";
		};
    
    // There is also a constructor for the serialized payload
    
    Shutdown( const Theron::SerialMessage::Payload & Payload );
		
		// And a copy constructor
		
		Shutdown( const Shutdown & OtherMessage )
		: Description( OtherMessage.Description )
		{ }
		
		// A serialised message must have a virtual destructor even if it does 
		// nothing
		
		virtual ~Shutdown( void )
		{ }
  };
  
  // The handler for this message removes the sender from the list of active 
  // reward calculators that must be informed about the PV energy being produced
  
  void ForgetCalculator( const Shutdown & ShutdownMessage, 
			 const Theron::Address ClosingCalculator );
  
  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
  // 
	// There should be only one reward calculator per household, so the household 
	// ID is taken as a location parameter and added to the name of the actor.
	
public:
  
  RewardCalculator( const std::string & Location );
  
  // The destructor must be virtual to ensure that the classes are destroyed
  // in the right order. At this level it only de-register the subscription 
  // for new peer reward calculators, and inform the other reward calculators
  // that this calculator stops.
  
  virtual ~RewardCalculator();

};
  
}	// name space CoSSMic
#endif 	// REWARD_CALCULATOR
