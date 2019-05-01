/*=============================================================================
  Consumer Agent

  The consumer agent is created when the task manager schedules a new load. 
  This agent has an external address and will first read the load profile file
  provided. Then it will select a possible producer agent for obtaining the 
  energy needed by the load. 
  
  If it gets a negative response from the producer, it will select another 
  producer, and this process will repeat until it finds a producer that is able
  to provide the requested energy. Since the Grid is an infinite source of 
  energy, we know that the search will always terminate when the consumer tries
  the Grid energy producer.
  
  It should be noted that a producer of energy can at any time re-decide and 
  cancel the agreed provision of energy provided that this cancellation 
  happens before the actual energy consumption has been started. In this case,
  the consumer agent will return to the main loop of requesting energy from 
  producers. 
  
  The consumer agent will inform the local Task Manager once it has a start 
  time assigned that it gets energy from the said producer, and wait for the 
  Task Manager to request the removal of the load once the task has finished 
  execution.
        
  Author: Geir Horn, University of Oslo, 2015-2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef CONSUMER_AGENT
#define CONSUMER_AGENT

#include <vector>
#include <map>
#include <set>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "Actor.hpp"
#include "SerialMessage.hpp"
#include "StandardFallbackHandler.hpp"
#include "NetworkEndPoint.hpp"
#include "PresentationLayer.hpp"
#include "SessionLayer.hpp"
#include "DeserializingActor.hpp" // Support for receiving a serial message

#include "Interpolation.hpp"

#include "IDType.hpp"
#include "TimeInterval.hpp"
#include "Producer.hpp"
#include "ActorManager.hpp"
#include "Grid.hpp"

#include "LearningEnvironment.hpp"
#include "VariableActionSet.hpp"

namespace CoSSMic {

class ConsumerAgent : virtual public Theron::Actor, 
                      virtual public Theron::StandardFallbackHandler,
											virtual public Theron::DeserializingActor
{
  // ---------------------------------------------------------------------------
  // Execution state
  // ---------------------------------------------------------------------------
  // 
	// The consumer agent can be in different states that needs to be tracked 
	// in order to ensure a proper response. The different states are:

private:
	
	enum class ExecutionState
	{
	  // The scheduling request has been sent to the producer and the response is
	  // pending either a rejection or a start time
	  Scheduling,                                               
	  // When the start time has been allocated the state shifts to 
	  StartTime, 
	  // If the scheduling request was rejected, the proxy allocated at the 
	  // producer must be deleted before a new scheduling request can be sent,  
	  // and the scheduler must therefore acknowledge that the proxy associated
	  // with this consumer is deleted. A special state indicates this situation
	  AwaitingAcknowledgement, 
	  // There is also a variable to indicate that the consumer is idle. This 
	  // typically means that it has started up, and that it is waiting for 
	  // producer discovery, but it could be used in other situations in the 
		// future
	  Idle,
	  // There is a more tricky situation occurring if some producers become 
	  // available or shuts down while a scheduling operation is pending. Given
	  // that the consumer tries to learn the best producer to use, the learning 
	  // algorithm should have a positive reward if the selected producer do 
	  // provide a start time, and a negative feedback if the producer rejects 
	  // the request. However, if the producer set has changed during the remote 
	  // scheduling, the learning will been re-initialised and providing a 
	  // response to the producer selection made prior to this re-initialisation 
	  // makes no sense. Consequently, if there is a change in the producer set 
	  // during a scheduling operation, this is marked as invalid and treated 
	  // similar to a rejection from the producer selected by the previous 
	  // learning state. This situation is indicated by a special state
	  InvalidScheduling
	  // The actual variable is just called the state.
  } State; 
        	
  // ---------------------------------------------------------------------------
  // State parameters
  // ---------------------------------------------------------------------------
  // 
  // The defining parameters of the consumer are stored
  
  IDType LoadID; 		         // Unique ID identifying the load
  Time   EarliestStartTime,	 // EST - do not start before
         LatestStartTime,	   // LST - must start at this time
         Duration;		       // The duration of the load
  double TotalEnergy;		     // Total energy required by the load
	 
  // The consumer agent corresponds to a mode on a given device, and this 
  // information is encoded into the ID, but in order to distinguish the 
  // actual run, each load will have a sequence number. This will be given to
  // the actor at start-up, and returned to the task manager whenever a start
  // time has been assigned to the load.
  
  unsigned int SequenceNumber;
	
public:
  
  // Some interface functions to read the parameters.
        
  inline IDType GetID( void ) const
  { return LoadID; }
  
  inline Time GetEST( void ) const
  { return EarliestStartTime; }
  
  inline Time GetLST( void ) const
  { return LatestStartTime; }
  
  inline double GetEnergy( void ) const
  { return TotalEnergy; }
  
private:

  // ---------------------------------------------------------------------------
  // Producer management
  // ---------------------------------------------------------------------------
  // 
  // We will need to remember the address of the selected producer. This is 
	// necessary because if the user kills this  before it has finished, we may 
	// still have a schedule request running and we need to inform the scheduler 
	// that this load is can be forgotten.
  
  Theron::Address SelectedProducer;
  
  // The consumer also needs to know its task manager so that it can inform this
  // about changes in the assigned start time, and of possible cancellation. 
  // This is initialise in the constructor as it is passed on from the Actor 
  // Manager as the address of the actor requesting the consumer creation.
  
  Theron::Address TaskManager;
  
  // The directory of available producer addresses is maintained as a set of 
  // actor addresses, leaving to the Session layer to map these to potentially
  // remote addresses. Since all producers must have an external address,
  // they must register with the network layer on their respective nodes. There
  // is one potential exception from this rule as the Grid may be a local actor
  // with only local presence. Its address is therefore added by the constructor
  // and if it is defined again later as a global actor, the addresses will be 
  // the same and the set will ensure that the grid producer is recorded only 
  // once.
  //
  // This set is stored as a vector because the learning method basically only 
  // selects an integer in the set {0,...,N-1}, and this vector serves as the 
  // mapping from the selected action, i.e. vector index, to the corresponding 
  // producer address.
  
  std::vector< Theron::Address > Producers;

  // The consumer constructor will set up a subscription to the session layer
  // to be informed about the known peers and peers arriving or leaving in the 
  // future. The Session Layer will respond to the subscription request with the 
  // currently known agents, i.e. actors that have external addresses and can
  // communicate across the network, and these responses will go to the 
  // following message handler. This handler will check if the actor is a 
  // producer as identifiable from the naming convention, and then store it in
  // the Producer directory. If there is no learning automata attached to select
  // the producers, one will be created with the number of producers known at
  // that point, and the selection will be initiated. This implies that if the
  // consumer agent is created with no expected number of producers, the first
  // producer whose address arrives to the handler will almost inadvertently be
  // selected. If a number of producers is given to the constructor, the 
  // handler will wait until this number of addresses has been received before
  // it selects a producer to try. If more producers arrive beyond this number,
  // the learning automata is re-initialised and future selections is based on 
  // the new number of producers.
  
  void AddProducer( 
       const Theron::SessionLayerMessages::NewPeerAdded & NewAgent,
       const Theron::Address SessionLayerServer );
  
  // A similar behaviour but in the reverse order is done by the handler 
  // deleting producers. 
  
  void RemoveProducer( 
       const Theron::SessionLayerMessages::PeerRemoved & LeavingAgent,
       const Theron::Address SessionLayerServer );

	// There is a small interface function for the Actor Manager to obtain the 
	// selected producer address
	
public:
	
	inline Theron::Address GetSelectedProducer( void )
	{ 
		if ( State == ExecutionState::AwaitingAcknowledgement )
			return Theron::Address::Null();
		else
			return SelectedProducer; 
	}
	
private:
	
  // ---------------------------------------------------------------------------
  // Stochastic reinforcement learning of producers
  // ---------------------------------------------------------------------------
  //
  // A learning automaton has a set of actions to choose from probabilistically.
  // After making a choice for one of these actions, it awaits the feedback from 
  // the environment. Different types of learning automata uses different 
  // algorithms to update the probabilities of choosing the various actions in 
  // the future. The goal of the learning is to converge to the action with the 
  // highest probability for positive rewards. There is a learning constant 
  // in the open interval <0,1> deciding on how much the penalised probabilities 
  // will change. Hence a value close to unity implies slow learning with little 
  // change in probabilities, and a value close to zero implies quicker 
  // learning, but at the expense of less stability and higher variability 
  // in the decisions.
  // TODO: Make this learning constant a command line parameter.

  constexpr static double LearningConstant = 0.99;
  
  // The probability for selecting the grid producer should be discounted with 
  // respect to the probabilities of the other PV producers: Let there be N PV 
  // producers. The default initial probability would then be 1/(N + 1) for all 
  // producers, including the grid. Let L be the learning constant. A producer
  // that fails will have p(1) = L * p(0), and after n failures it will have 
  // the probability (L^n)*p(0). Hence, the initial grid probability will be 
  // set to (L^n)/(N + 1). This will allow, on average, the PV producers to be 
  // tried n times before the grid is selected. The default value for n is 
  // given by the below parameter, defaulting to 10.

  constexpr static unsigned int GridDiscountFactor = 10;

  // The consumer actor uses a learning automaton to learn the best producer and
  // select the producer to try if the one selected in last iteration failed.
	// However, an automata proposes its actions to an environment. Although 
	// this environment is here the real scheduling, it must be defined for a 
	// particular environment type. Given that the reward calculator gives the 
	// feedback as a reward in the interval [0,1], is is a strength environment
	// or S-Model. The learning environments are abstract classes because the 
	// evaluation function needs to be defined, and this must be done to 
	// instantiate the environment when creating the automata.
	
	class NeighbourhoodEnvironment
	: public LA::LearningEnvironment< LA::Model::S > 
	{
		// The Environment must declare the evaluation function, but since the 
		// reward calculator computes the reward, it will never be called, and 
		// will therefore just throw an error if it is used.
		
	public:
		
		virtual Response Evaluate( const Action & ChosenAction ) override
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Neighbourhood Environment has no way to evaluate a "
									 << "proposed action";
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
		
		// The constructor is just a way to set the number of actions this 
		// environment can accept which for the CoSSMic scheduler corresponds 
		// to the number of producers and this will then be passed on to the 
		// automata when it is constructed on the basis of this environment.
		
		NeighbourhoodEnvironment( const LA::ActionIndex ActionSetCardinality )
		: LA::LearningEnvironment< LA::Model::S >( ActionSetCardinality )
		{	}
		
		// Since the number of actions, must be given for a problem, it should 
		// not be possible to default construct an environment.
		
		NeighbourhoodEnvironment( void ) = delete;
	};
	
  // To ensure that we always relate to the same Learning automata in all 
  // functions, its type is first defined.
  // 
  // Some users considered it odd that the grid could be chosen as a provider on
  // first attempt, even before trying any of the PV producers as would happen 
  // if the full action set was used for the selection. The solution is to use 
	// a variable action set automata where the selection is first made from the 
	// set of PV producers, then from the set of batteries, and if these fail to
	// provide energy the grid will be used as a fall-back option.
	//
	// The S-Model version is use since the environment feedback will be a real 
	// number in the interval [0,1].
  
  using AutomataType = LA::VariableActionSet< 
										   LA::PoznyakNajim<  
										   LA::SubsetEnvironment< NeighbourhoodEnvironment > > >;
		
  // The automaton instance must be dynamically allocated as producers may come
  // and go, and the number of actions supported by the automata must correspond
  // to the number of possible producers, including the grid producer and the 
	// batteries.
  
  std::shared_ptr< AutomataType >  ProducerSelector;

  // There is one function to create create and initialise the automaton with 
  // as many actions as there are producers in the producers set. The 
  // probability vectors of the producers will take historical information 
  // about persisted probabilities into account when initialising the 
  // probability mass of the selectors.
  
  void CreateAutomaton( void );

  // There is also a need to make sure that the state of the automaton is stored.
  // In particular if a new producer comes on-line or a producer goes off-line, 
  // then the selectors must be re-created, but the learned knowledge should be
  // kept as much as possible.
  
  void LAStoreProbabilities( void );
  
	// In order to implement the policy of selecting the PV producers first, and 
	// then the batteries and leave the grid as the last option, there are two 
	// set of indices referring to the elements of the producer vector. Since 
	// these indices will not change unless new producers or new batteries become
	// available, they are maintained by the handlers adding and removing 
	// producers.
	//
	// There is a third set which is the active set of prioritised producers that
	// will change for each selection, and that will predominantly be maintained
	// by the handler selecting the next producer.
	
	std::set< LA::ActionIndex > PVProducers, Batteries, PriorityProducers;
	
  // To avoid looking up the index of the selected producer when mapping back 
	// to the automata action index, the selected value is stored between the 
	// selection and the received reward.

  LA::ActionIndex SelectedActionIndex;
	
  // The learned probabilities will be stored to a file when the consumer actor
  // terminates, and be read back when it starts again since the consumer actor
  // is used to run loads from a particular mode of a given appliance, it can
  // start from the probabilities of the previous run. The stored probabilities
  // is read into a map in the constructor to be available for initialisation 
  // when the currently active producers are known. The map is from the producer
  // actor addresses and the stored probability value.
  
  std::map< Theron::Address, Probability<double> > StoredProbabilities;

  // The reward is computed as by a reward calculator and sent to the consumer
  // as a message containing the value for the choice made by the consumer. If
  // the consumer has not been able to obtain a start time from a producer, 
  // then the reward is simply ignored. It is assumed that the reward calculator
  // is an actor on the same network endpoint as the consumer, so the message
  // does not support serialisation.
  
public:  
  
  class RewardMessage 
  {
  private:
    
		// The response of an environment is a class containing both the action 
		// and the reward for this action. This to support stateless automata. 
		// However, in this case, the reward calculator will not know the action 
		// as this is stored by the consumer, and the coupling of action index and 
		// reward must be done by the consumer when the reward is received. Thus
		// for the reward message it is sufficient to use the raw response type 
		// of the automata, and since the automata type is S-Model this is just 
		// a lengthy way to say 'double', but it allows other models to be used 
		// some time in the future.
		
    AutomataType::Environment::ResponseType Response;
    
  public:
    
    // Interface function to read the response
    
    inline AutomataType::Environment::ResponseType Reward( void ) const
    { return Response; }

    // The constructor simply takes the response and stores it.
    
    RewardMessage( const AutomataType::Environment::ResponseType & 
										     TheRewardValue )
    : Response( TheRewardValue )
    { }    
    
    // There is a copy constructor to ensure that messages can be safely passed
    // between threads
    
    RewardMessage( const RewardMessage & OtherMessage )
		: Response( OtherMessage.Response )
		{ }
  };
  
  // ---------------------------------------------------------------------------
  // Message handlers
  // ---------------------------------------------------------------------------
  // 
  // When a choice of producer has been made, the learning automata should 
  // have a feedback on the choice. This will typically be produced by the 
  // reward calculator on the network endpoint hosting the consumer and sent 
  // as a message back to the consumer

private:
  
  void Feedback2Selector( const RewardMessage & Response, 
												  const Theron::Address RewardCalculator );
  
  // When the load scheduler has decided on a schedule it sends back the 
  // assigned start time, and the consumer agent needs a handler for this 
  // message. Note that the message is sent from the producer on the 
  // a possibly remote node. This method is also responsible for rewarding 
  // or penalising the automata based on the start time assignment and 
  // which producer responding. 
  
  void SetStartTime( const Producer::AssignedStartTime & StartTime,
								     const Theron::Address TheProducer );
  
  // The effect of a schedule request sent to a producer is that the producer 
  // creates a proxy for this consumer and uses this proxy to schedule an 
  // acceptable start time. If no start time can be found or the consumer is 
  // refused by the consumer, it will send back an unassigned start time, and
  // the start time handler will ask the producer to kill the proxy. Once the 
  // proxy has been cleared, this producer will use the learning automata to 
  // select the next producer. However, it would create a race condition if 
  // the new producer is selected and a schedule request is sent to a producer 
  // at the same network endpoint as the previously selected producer. Then 
  // the new producer could create the proxy before the old producer has cleared
  // the previous proxy. We must therefore wait for an acknowledgement from 
  // the previous producer that the proxy has been killed before we can select 
  // the next proxy. The selection function is therefore formally a handler for
  // the proxy removal acknowledgement.
  
  void SelectProducer( const Producer::AcknowledgeProxyRemoval & Ack,
								       const Theron::Address ProducerActor );
   
  // ---------------------------------------------------------------------------
  // OUTBOUND ===> Task Manager
  // ---------------------------------------------------------------------------
  // There is an assigned start time message to be sent by the consumer agent 
  // to the task manager when the load has a valid start time. Note that the 
  // Consumer Agent for a load is always created on the same node as the task 
  // manager to manage the load, and therefore there should be no ambiguity 
  // with respect to which task manager to receive messages from the Consumer
  // Agent.

public:
  
  class StartTimeMessage : public Theron::SerialMessage
  { 
    // The message provides the ID of the load, the ID of the selected producer
    // and the assigned start time.
    
  private: 
    
    IDType 	 LoadID, ProducerID;
    Time   	 StartTime;
    unsigned int SequenceNumber;
    
  public:
    
    // Interface functions to read these variables
    
    inline IDType GetLoadID( void ) const
    { return LoadID; }
    
    inline IDType GetProducerID( void ) const
    { return ProducerID;}
    
    inline Time GetStartTime( void ) const
    { return StartTime;}
    
    // The two methods for serialising and de-serialising the message must 
    // exists since this message is for the Task Manager agent.
    
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool 
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;

    // There is a constructor taking a serialised payload and then calling the 
    // Deserialize method to initialize the elements based on this string.
    
    StartTimeMessage( 
		 const Theron::SerialMessage::Payload & Payload );
    
    // The standard constructor takes the parameters as input and initialises 
    // the internal variables.
    
    StartTimeMessage( const IDType & TheLoad, Time AssignedStartTime,
								      const unsigned int TheSequenceNumber,
								      const IDType & TheProducer );
		
		// The message has a copy constructor to facilitate sending the message to
		// a different thread.
		
		StartTimeMessage( const StartTimeMessage & OtherMessage )
		: LoadID( OtherMessage.LoadID ), ProducerID( OtherMessage.ProducerID ),
		  StartTime( OtherMessage.StartTime ), 
		  SequenceNumber( OtherMessage.SequenceNumber )
		{}
		
		// A serialised message must have a virtual destructor
		
		virtual ~StartTimeMessage( void )
		{ }
  };
  
  // Another message that is needed is to inform the task manager that a 
  // previously assigned start time has been cancelled. It is understood that 
  // the task manager automatically updates one start time with another 
  // upon receiving a start time message, but if the previously assigned 
  // start time is just cancelled, the task time manager must be informed 
  // that this change has happened by receiving the cancel start time 
  // message
  
  class CancelStartTime : public Theron::SerialMessage
  {
  private: 
    IDType LoadID;
    
  public:

    inline IDType GetLoadID( void ) const
    { return LoadID; }
    
    // Serialising and de-serialising
 
     
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool 
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;

    // The constructor simply saves the ID

    CancelStartTime( const IDType & ID )
    : LoadID( ID )
    { }
    
    // The copy constructor is similarly simple
    
    CancelStartTime( const CancelStartTime & OtherMessage )
		: LoadID( OtherMessage.LoadID )
		{}
    
    // The constructor taking the serialised message will merely invoke the 
    // Deserialize method to initialise the variables. A standard invalid 
    // argument exception will be thrown if this initialisation fails.
    
    CancelStartTime( Theron::SerialMessage::Payload & Payload );
		
		// A serialised message must provide a virtual destructor
		
		virtual ~CancelStartTime( void )
		{ }
  };
  
  // ---------------------------------------------------------------------------
  // INBOUND: Delete load 
  // ---------------------------------------------------------------------------
  // Clock synchronisation is a major problem, and the consumer agent will not 
  // try to estimate when its associated load has been completed. The reason is 
  // is also that the actual execution time is stochastic depending on varying 
  // environmental factors not being modelled by the load profile. Consequently, 
  // the task manager will tell the scheduler when the load has finished, and
  // provide the consumed energy and the providing producer in a message to the 
  // Actor Manager, which will distribute this information to the reward 
  // calculator and then kills the consumer agent. 
  // 
 public:
    
  virtual ~ConsumerAgent( void );
  
  // There is a small complication with the address space and distribution of 
  // actors. A receiver will wait for messages sent to the the receiver, but 
  // a receiver is not an Agent with a node external address and presence. On 
  // the other hand, the proxy could well be hosted by a producer on a remote
  // network endpoint. Hence it cannot address the receiver directly. The 
  // receiver could have been registered as an agent, but then there would have
  // been a further delay before the address had propagated to the producer's 
  // network endpoint. 
  //
  // A trick will be deployed here: Given that the Consumer Agent already is 
  // registered as an external agent, it will be possible to replace the 
  // message handler for the proxy removal acknowledgement with another handler
	// redirecting the acknowledgement from the proxy removal to the receiver
	// enabling the consumer's destructor to wait for this event.
  //
  // The message handler will read the address of the acknowledgement receiver
  // and if the receiver has not been initialised when called, it will throw
  // a standard logic error exception.

private:
    
  void ForwardAcknowledgement( const Producer::AcknowledgeProxyRemoval & Ack,
															 const Theron::Address Producer );
  
	// When a scheduling action is initiated, the remote producer will respond 
	// with a start time if this load could be scheduled, otherwise the assigned
	// start time is undefined. This causes the set start time message handler to
	// request the proxy to be deleted with the remote producer. In normal 
	// operation, this acknowledgement will be captured by the select producer 
	// handler that will select a new producer starting the operation again. 
	// If a shut down happens when the consumer is waiting for the proxy removal
	// acknowledgement, the above forward function will catch this.
	//
	// A special situation occurs if the shut down happens while the consumer is
	// waiting for a start time to be assigned. If the consumer assigns a start 
	// time, then the state shifts to the state of having a start time, but 
	// the consumer is shutting down so it should then immediately ask the 
	// producer to kill the proxy. In other words, the returned start time must 
	// be captured, and result in a kill proxy request sent to the producer 
	// irrespective of the start time value. A special message handler is 
	// defined for doing this, and it is registered by the destructor as a 
	// replacement for the start time message handler.
	
	void SendKillProxyCommand( const Producer::AssignedStartTime & StartTime,
														 const Theron::Address Producer );
	
	// Finally, there is a shut-down handler. It is necessary to ensure correct 
	// de-registration of the default handlers and registration of these two 
	// special termination handlers. The actor model guarantees that one and 
	// only one message will be processed at the same time, and by doing the 
	// registration of the termination handlers from within a message handler 
	// ensures that there will be no race condition.
	
	void ShutDown( const ActorManager::ShutdownMessage & ShutDownCommand,
								 const Theron::Address TheActorManager	);
	
	// In order to confirm the shut down back to the actor manager, its address
	// must be stored in the shut down handler.
	
	Theron::Address TheActorManager;

  // ---------------------------------------------------------------------------
  // Constructor 
  // ---------------------------------------------------------------------------

  // The constructor takes the parameters of the create load command and 
  // initialises its local parameters and reads the load file. It will then 
  // subscribe to the Session Layer to be notified about other actors with 
  // external addresses, among them the producers.

public:
    
  ConsumerAgent( const IDType & ID,
								 Time EST, Time LST, unsigned int TheSequence, 
								 const std::string & ProfileFileName,
								 const Theron::Address & LocalTaskManager   );  
};
  
};	// End Namespace CoSSMic
#endif 	// CONSUMER_AGENT
