/*=============================================================================
  Consumer Agent

  The consumer agent is created when the user schedules a new load. It uses
  a two-level learning automata (LA) to select the energy producer to ask for
  energy. The probabilities are increased if the selected producer is able to 
  accommodate the load, and decreased if the load is refused by the producer.
  
  The loads are attached to modes of devices. Consider a washing machine as 
  an example. This has many different programmes, and the load required to 
  execute one programme will be stochastic as it depends on the amount of 
  cloths in the machine, and other factors like the temperature of the inlet
  water. The load profile for the run is therefore updated after every 
  execution of that run according to the methodology described in [1], and 
  the resulting profile is represented as a parametrised B-Spline curve.
  [NOTE: As of January 2016 support for B-Splines have not been included, and 
  the load is received as a standard time series.]
  
  Since the load represents the mode of an appliance, it would be natural to 
  persist (store) the learned probabilities between each time this load is 
  scheduled in order to speed up the convergence of the stochastic learning.
  There is correspondingly one file for each consumer agent stored in the 
  directory "Probabilities". The file is read from the actor's constructor and
  written in the actor's destructor.
  
  REFERENCES:
  
  [1] Geir Horn, Salvatore Venticinque, and Alba Amato (2015): "Inferring 
      Appliance Load Profiles from Measurements", in Proceedings of the 8th 
      International Conference on Internet and Distributed Computing 
      Systems (IDCS 2015), Giuseppe Di Fatta et al. (eds.), Lecture Notes in 
      Computer Science, Vol. 9258, pp. 118-130, Windsor, UK, 2-4 September 2015
        
  Author: Geir Horn, University of Oslo, 2015-2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPLv3.0
=============================================================================*/

#include <map>		             // Used for key-value pairs
#include <memory>	             // shared pointers and its like
#include <algorithm>	         // Algorithms working on STL containers
#include <numeric>             // For normalising probability vectors
#include <stdexcept> 			     // For throwing standard exceptions
#include <sstream>					   // For formatted error messages
#include <fstream>					   // For persisting probabilities 
#include <limits>						   // For storing maximum double precision probabilities
#include <sys/stat.h> 			   // makedir
#include <cerrno>						   // C-style error reporting
#include <cmath>						   // For computing the grid's initial probability

#include "ConsumerAgent.hpp"   // The description of the agent
#include "Producer.hpp"        // The generic producer class
#include "Grid.hpp"            // The infinite Grid producer
#include "PVProducer.hpp"      // Photo voltaic producers
#include "Battery.hpp"         // Batteries - not implemented yet
#include "CSVtoTimeSeries.hpp" // To parse CSV files

#ifdef CoSSMic_DEBUG
  #include <iterator>
  #include "ConsolePrint.hpp"
#endif
  
namespace CoSSMic {

// For the same reason is the Grid discount factor defined

const unsigned int ConsumerAgent::GridDiscountFactor;

/******************************************************************************
  Constructor & Destructor
*******************************************************************************/

ConsumerAgent::ConsumerAgent( const IDType & ID, Time EST, Time LST,
												      unsigned int TheSequence,
												      const std::string & ProfileFileName,
												      const Theron::Address & LocalTaskManager  )
: Actor( ( ValidID( ID ) ?
			   "consumer" + std::string( ID ) : std::string() ) ),
  StandardFallbackHandler( GetAddress().AsString() ),
  DeserializingActor( GetAddress().AsString() ),
  TaskManager( LocalTaskManager ), 
  Producers(), ProducerSelector(), 
  PVProducers(), Batteries(), PriorityProducers(),
  StoredProbabilities(), TheActorManager()
{
  // Initialisation of the local variables
  
  LoadID 	      	    = ID;
  EarliestStartTime   = EST;
  LatestStartTime     = LST;
  SequenceNumber      = TheSequence;
  State               = ExecutionState::Idle;
 
  // When the Actor Manager kills the Consumer Agent it must make sure that the 
  // remote proxy is killed by its current producer. The acknowledgement of the 
  // proxy removal now poses a problem since the Consumer Agent's destructor 
  // must wait for the acknowledgement before the consumer actor can terminate. 
  // The mechanism used is as follows: The destructor, which is called from the 
  // Actor Manager (thread) directly, waits for this receiver to get the the 
	// proxy delete acknowledgement. 
 
 // AcknowledgementReceiver = std::make_shared< WaitForProxyRemoval >();
	
  // The grid is inserted as a valid producer. The global grid address function
  // will return the address of the local grid actor receding on this network 
  // endpoint, if any, or the global grid address defined as standard. If there
  // is a node local grid actor, it should be created before any consumers for 
  // the grid address function to be valid and correct.
  
  Producers.push_back( Grid::Address() );
  
  // Then we register the message handlers so that we are able to receive 
  // messages from the other actors.
  
  RegisterHandler(this, &ConsumerAgent::SetStartTime      );
  RegisterHandler(this, &ConsumerAgent::SelectProducer    );
  RegisterHandler(this, &ConsumerAgent::AddProducer       );
  RegisterHandler(this, &ConsumerAgent::RemoveProducer    );
  RegisterHandler(this, &ConsumerAgent::Feedback2Selector );
	RegisterHandler(this, &ConsumerAgent::ShutDown 				  );
	
  // Read the profile data file using the CSV parser
  
  auto Profile( CSVtoTimeSeries( ProfileFileName ) );
  
  // The duration is simply the abscissa value (time stamp) of the last value
  // in the datafile, and the total energy is the energy value of the same 
  // value since the profile is cumulative.
  
  Duration    = (Profile.rbegin())->first;
  TotalEnergy = (Profile.rbegin())->second;

  // Then the producer-probability map is read from the file, provided that
  // the file exist and can be opened for reading.
  
  std::ifstream PersistedProbabilities( 
		std::string("Probabilities/") + GetAddress().AsString()+".dta");
  std:: string  GridName( Grid::Address().AsString() );
  
  if ( PersistedProbabilities.good() )
    while ( !PersistedProbabilities.eof() )
    {
      std::string ProducerID;
      double 	  Probability = 0.0;
      
      PersistedProbabilities >> ProducerID >> Probability;
      
      if( !ProducerID.empty() ) 
             StoredProbabilities.emplace( Theron::Address( ProducerID.data() ), 
				     Probability   );
    }
    
  // Finally, it requests the list of peer actors supported for the 
  // communication. It should be noted that this will produce a sequence of 
  // messages to the Add Producer handler that will initiate the selection of 
  // a producer to ask for energy. For this reason it must be invoked after 
  // the profile has been processed, and the persisted probabilities loaded.
  
  Theron::Address TheLayer(Theron::Network::GetAddress( Theron::Network::Layer::Session ));
  
  Send( Theron::SessionLayerMessages::NewPeerSubscription(), 
				TheLayer );
}


// The role of the shut down message handler is to de-register the handlers 
// dealing receiving proxy removal acknowledgement and start time in a safe 
// way.

void ConsumerAgent::ShutDown( 
		 const ActorManager::ShutdownMessage & ShutDownCommand,
	   const Theron::Address HouseholdActorManager )
{
  // Since the consumer is closing the subscription to be informed about new 
  // peers from the session layer must be cancelled first. This should avoid 
  // further subscription messages to be fired at the Consumer Agent.
  
  Send( Theron::SessionLayerMessages::NewPeerUnsubscription(),
        Theron::Network::GetAddress( Theron::Network::Layer::Session ) );
  
	// The current handler of the Consumer Actor for proxy deletion 
  // acknowledgement is first replaced to forward the acknowledgement to the 
	// receiver. 
  //
	// This could pose a danger if the remote producer has already been asked to 
	// remove the proxy, and the acknowledgement arrives between the removal of 
	// the message hander and the registration of the new handler. Theron will 
	// crash if a message arrives and no handler is registered. It is however 
	// safe to register a second handler, for which both handlers will be called
	// so it should be safe to register the acknowledgement handler first, and 
	// then de-register the select producer. There is however possible that the 
	// second registration of a handler with the same function signature will be
	// optimised away by the compiler. 
	// 
	// If both handlers are registered, even for a short time, they can both be 
	// invoked, and in this case it is necessary to let the select producer 
	// handler complete its job, so the forward handler must check if the 
	// select producer handler is registered. However, this may mistakenly 
	// return 'true' if for some optimising compiler. This race condition may 
	// then be managed with a default handler, although not this is not evident.
	//
	// The only safe mechanism is to do this in a message handler, since the actor 
	// model guarantees that only one message handler is invoked at the same time.
  
  DeregisterHandler( this, &ConsumerAgent::SelectProducer );
  RegisterHandler  ( this, &ConsumerAgent::ForwardAcknowledgement );
	
	// This will take care of the situation where the consumer is waiting for an 
	// acknowledgement to arrive. However, if it is in the Scheduling state it is
	// waiting for a start time to arrive. It is therefore necessary to make sure 
	// that the scheduled time will lead to a kill proxy command being issued. 
	// This is achieved by replacing the normal handler for start times with 
	// the special termination handler.
	
	DeregisterHandler( this, &ConsumerAgent::SetStartTime );
	RegisterHandler  ( this, &ConsumerAgent::SendKillProxyCommand );
	
  // The only time a kill proxy command is explicitly sent to the producer is 
	// if the state is Start Time, indicating that the consumer has a start time
	// and it just awaits for the task manager to kill the load once the appliance 
	// has run to completion.

  if ( State == ExecutionState::StartTime )
    Send( Producer::KillProxyCommand(), SelectedProducer );
	
	// Finally, the address of the actor manager is stored for the consumer to 
	// be able to confirm the deletion of the proxy when the producer's 
	// acknowledgement is received.
	
	TheActorManager = HouseholdActorManager;
}

// The destructor has the role of disconnecting the Consumer Agent from the 
// system, and ensure that another consumer agent may be created with the same 
// name after the termination. One important concern is the remote Consumer 
// proxy that must be deleted, and the acknowledgement from the remote 
// producer that the proxy has been removed should not trigger the selection of
// a new producer as it normally does.
//
// A second concern is feedback messages. When the Actor Manager receives a 
// message to terminate a consumer it will pass the total energy consumed and
// the provider of that energy (used producer) to the Reward Calculator. This
// will in turn trigger a feedback to all active consumers, inclusive the one 
// that is terminating, and then this consumer will be removed from the 
// Reward Calculator's list of active consumers to prevent future rewards to 
// be sent to deleted consumers. When the Actor Manager receives the 
// acknowledgement from the Reward Calculator that the reward has been 
// dispatched, it can proceed to delete the consumer and implicitly invoke the 
// destructor. Given that the Reward Calculator only rewards consumers on the 
// local node (network endpoint), no network transfer will be involved when 
// sending the feedback message. Still, it is impossible to know when Theron 
// will schedule the Consumer Agent to handle the feedback message since this 
// depends on the availability of a thread to run the Consumer Agent actor. 
//
// The Consumer Agent's destructor will however run in the same thread as the 
// Actor Manager, a thread that has been stalled waiting for the Reward 
// Calculator to complete in a different thread. Hence, it is conceivable that
// the destructor will execute before the pending feedback message has been 
// consumed.
  
ConsumerAgent::~ConsumerAgent( void )
{
  // At the end, the probabilities are written to a file for use next time a 
  // consumer with the same ID is started. We try to make the directory, and 
  // most likely it will fail because it exists. This is however OK and 
  // sufficient to create or replace the file already there. If there is 
  // any other error, the probabilities will not be stored.
  
  if ( (mkdir("Probabilities", S_IRWXU | S_IRWXG | S_IRWXO) == 0) ||
       (errno == EEXIST) )
  {
    // The probabilities are first stored in the internal map of probabilities
    
    LAStoreProbabilities();

    // Next the file to persist these probabilities is opened. First the file 
    // name for this consumer must be set up.

    std::string Filename("Probabilities/");
    Filename += GetAddress().AsString();
    Filename += ".dta";

    // Open the file and set the flag to write the probabilities in fixed 
    // format at the maximum precision supported by the double probabilities.
    // Fixed format is used since a probability will be between zero and one,
    // and can therefore be correctly represented without using scientific 
    // representation, and fixed representation may be more human friendly.
    
    std::ofstream PersistentProbabilities( Filename );
    
    PersistentProbabilities.setf( std::ofstream::fixed );
    PersistentProbabilities.precision( std::numeric_limits<double>::digits10 );
    
    // The probabilities are written out to this file in a simple loop over 
    // the stored probabilities.
    
    for (auto & ProbabilityRecord : StoredProbabilities )
      PersistentProbabilities << ProbabilityRecord.first.AsString() << " "
												      << ProbabilityRecord.second << std::endl;

    // There is strictly no need to close the file since it will be closed 
    // when the file class goes out of scope and is deleted. However, for 
    // readability it is done explicitly.
			      
    PersistentProbabilities.close();
  }
  
  #ifdef CoSSMic_DEBUG
    Theron::ConsolePrint DebugMessage;
    DebugMessage <<  "Consumer agent " << GetAddress().AsString()
                 <<  " has closed successfully" << std::endl;
  #endif
}
  
/******************************************************************************
  Learning Automata related functions
*******************************************************************************/
//
// First a small utility function to look up the address of a producer in the 
// set of known producers, and map it to its index in the probability vector
// (i.e. action index). The standard find function is used as there is no 
// dedicated find function for vectors. It uses iterator arithmetic to compute 
// the index, and throws an exception if the given producer address was not 
// found. The calling method should catch this if it has a reasonable way to 
// manage the error.

LA::ActionIndex
ProducerIndex( const std::vector< Theron::Address > & ProducerSet,
               const Theron::Address & GivenProducer )
{
  auto ProducerPosition = std::find( ProducerSet.cbegin(), ProducerSet.cend(), 
                                     GivenProducer );
    
  if ( ProducerPosition != ProducerSet.cend() )
    return ProducerPosition - ProducerSet.cbegin();
  else
  {
    std::ostringstream ErrorMessage;
    
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "The producer " << GivenProducer.AsString()
                 << " does not exist in the given producer set!";
		 
    throw std::invalid_argument( ErrorMessage.str() );
  }
}

// The selector for the producers is challenging to initialise as the 
// probability values must be taken from the stored values. A further 
// complication dealing with past probabilities is that they might have been 
// stored at various points in time as a snapshot of a single value 
// of the probability mass at that time. Hence they are potentially from 
// different probability masses, and therefore they may not add up to unity.
// 
// A direct approach is therefore taken: It is assumed that the current 
// probabilities, if any, are stored prior to calling this function. This is 
// indeed the case, see the Add Producer message handler below. The default 
// probability would be 1/r where r is the number of known producers, and it 
// is assumed that r is larger than 1 since the grid is always a producer. 
// The grid probability is given a discount factor. 
// 
// If a producer is one that has a stored probability from the past, this 
// probability is just added. If a producer has no value the default 1/r is 
// used, and if the producer is the grid, this 1/r is discounted. 

void ConsumerAgent::CreateAutomaton( void )
{
	// The new, initial probabilities are just collected in a vector, which is 
	// as long as there are producers, and the initial value for all is set to 
	// 1/r. This will be overwritten for producers that has historical 
	// probabilities
	
	std::vector< double > 
	InitialProbabilities( Producers.size(), 
												1.0 / static_cast< double >( Producers.size() )  );

	// A pointer to the probability position is needed as the values will be 
	// set while iterating over the producer addresses. The Grid address is 
	// retrieved and stored for convenience since it can be any address of the 
	// producer vector and it is no need to call the function repeatedly.
	
  auto ProbabilityPosition = InitialProbabilities.begin();
  auto GridAddress         = Grid::Address();
  
	// The producers are then checked one by one to see if it is the grid or 
	// if it has a stored probability value that can overwrite the default 
	// initial value. 
	
  for( Theron::Address & ProducerAddress : Producers )
  {
    if ( ProducerAddress == GridAddress )
    {
      // The grid probability is (re)set according to the discussion in the 
      // header file, provided that the grid exist as a producer (it should do 
      // and it is a critical run-time exception if it does not.)   
      //
      // The initial probability for the Grid will then be set to 
      // 		(L^n)/(N + 1)
      // where L is the learning constant and n is the average number of times 
      // the other producers will be tried before the grid is tried for the 
      // first time. N is the number of PV producers, and N + 1 is consequently
      // the number of all producers including the grid.
      
      double GridProbability = std::pow( LearningConstant, GridDiscountFactor ) 
													     / Producers.size();

      *ProbabilityPosition   = GridProbability;
    }
    else
    {
      // The address belongs to a different producer and it is necessary to 
      // check if there is a probability stored for this producer from a 
      // previous run and reuse this probability if it exists. If no 
      // probability is found, the default initialisation is kept.
      
      auto PastProbability = StoredProbabilities.find( ProducerAddress );
      
      if ( PastProbability != StoredProbabilities.end() )
				*ProbabilityPosition = PastProbability->second;
    }
    
    ++ProbabilityPosition;
  }
  
  // Then the probability mass can be created and assigned the probabilities 
  // that were known from the previous run. The constructor will ensure that 
  // the probabilities are properly normalised to unity.
  
  ProbabilityMass< double > Probabilities( InitialProbabilities.begin(), 
																					 InitialProbabilities.end() );
  
  // Finally, the selector automaton can be created and initialised with this 
  // probability mass. 
	// 
	// Implementation node: The automata must be created on the basis of an 
	// instantiated environment with the correct number of actions and specifying 
	// the response type. The Neighbourhood Environment is constructed for this 
	// role, and the variable action size automata caches the environment and 
	// it can therefore be allocated on the stack (and be removed when the 
	// function terminates). This is passed to the constructor of the automata 
	// so it knows the number of actions, and that the environment is of the 
	// S-model type.
	
	NeighbourhoodEnvironment TheEnvironmet( Producers.size() );
  
  ProducerSelector = std::make_shared< AutomataType >( TheEnvironmet, 
                                                       LearningConstant );

  ProducerSelector->InitialiseProbabilities( Probabilities );
  
  #ifdef CoSSMic_DEBUG
    Theron::ConsolePrint DebugMessage;
    DebugMessage << GetAddress().AsString() << "'s producer probabilities = ";
    std::copy( Probabilities.cbegin(), Probabilities.cend(),
	      std::ostream_iterator<double>(DebugMessage," ") );
		DebugMessage << std::endl;
  #endif
}

// Based on the above discussion it does not make sense to change the number of  
// actions after the initialisation of the automaton. Consider a Variable  
// Structure Stochastic Automata, VSSA, that has a probability vector whose  
// elements represents the probability of choosing the action with the same 
// index as the probability. For each iteration these probabilities will be 
// increased for choices leading to positive feedback, and decreased for the 
// others. If a new action becomes available it essentially invalidates these 
// updates, because it does need some probability to be selected at all (an 
// action with zero probability will never be chosen). How to re-distribute 
// the probability to give a fair probability? This is a real research question!
//
// On the other hand, since the stored probabilities from the previous run is 
// used to update the probabilities after creation of the automata, it is 
// not clear that it will be any better to use these historical probabilities 
// instead of the new ones, which may have been updated over the course of 
// multiple plays of the game before the new producer arrives. 
// 
// The idea is that the stored probabilities are updated with the probabilities
// known for the current producers, and then the selection automata can be
// re-created. The reason for re-creating them is that the learning 
// constants and other parameters may be a function of the number of producers,
// and the automata state should therefore be re-initialised.
// 
// The next method stores the current probabilities allowing the list of 
// producers to change before the selection automata are re-created. It is 
// based on the assumption that new producers might have been added to the 
// set of producers, and so only the first producers corresponding to
// probability values will be stored.

void ConsumerAgent::LAStoreProbabilities(void)
{
  auto CurrentProbabilities( ProducerSelector->GetProbabilities() );
	auto ProducerAddress = Producers.cbegin();
	
	for ( auto TheProbability  = CurrentProbabilities.cbegin();
		         TheProbability != CurrentProbabilities.cend(); ++TheProbability )
  {
		StoredProbabilities[ *ProducerAddress ] = *TheProbability;
		++ProducerAddress;
	}
}

/******************************************************************************
  Message handlers
*******************************************************************************/
//
// Selecting a producer also involves sending a request to be scheduled to that
// producer, before awaiting the response to be returned in terms of a scheduled
// assigned start time which will be processed by the set start time handler.
//
// The function is formally a handler that responds to a killed remote proxy 
// indicating that we can schedule on another producer, and possibly have it 
// to create a proxy for this agent. 
// 
// Given the policy that the PV producers should be asked first, and then the 
// batteries before the grid is tried as the last possibility, the current 
// set of producers is split according to their category. The two sets are 
// initialised if they are empty when this handler is invoked.  

void ConsumerAgent::SelectProducer( 
     const Producer::AcknowledgeProxyRemoval & Ack,
     const Theron::Address ProducerActor )
{ 
	// The random selection of the next producer is limited to the set of 
	// priority producers. It could however happen that the probability mass 
	// of the priority producers equals zero. A typical situation is when the 
	// automata is close to convergence and knows the best producers, but if 
	// these cannot provide energy in a given situation, the remaining producers
	// can have zero selection probability. This exceptional situation will be 
	// detected by the select action function, and it will throw an underflow
	// error. In this case the priority producers will be set to the next level 
	// priority and the selection re-initiated by recursion.
	
	try
	{
		SelectedActionIndex = ProducerSelector->SelectAction( PriorityProducers );
	  SelectedProducer    = Producers.at( SelectedActionIndex );
	} 
	catch( std::underflow_error & Error )
	{
		if ( Producer::CheckAddress< PVProducer >( 
															   Producers.at( *PriorityProducers.begin() ) ) )
			PriorityProducers = Batteries;
		else 
		{
			PriorityProducers.clear();
			PriorityProducers.insert( ProducerIndex( Producers, Grid::Address() ) );
		}
		
		// Then try to select a new producer with these priority producers, and 
		// as this will succeed for the grid since selecting among a subset of 
		// actions will always select the given action if the cardinality of the 
		// set of priority producers is unity.
		
		SelectProducer( Ack, ProducerActor );
		
		// As the previous statement took care of starting the scheduling no 
		// further processing should be done here.
		
		return;
	}
	
  // After successfully selecting a producer, a scheduling request must be sent
  // to the selected producer and the state  of the actor changes to awaiting 
  // scheduling.

  Send( Producer::ScheduleCommand( EarliestStartTime, LatestStartTime, 
                                   Duration, TotalEnergy ), 
        SelectedProducer );
        
  State = ExecutionState::Scheduling;
	
	// The selected producer should not be tested again, and it is therefore 
	// removed from the set of priority producers if the set has cardinality 
	// larger than unity. If it is the last remaining priority producer, the 
	// set of priority producers should be shifted to the class of producers 
	// at the next priority level.
	//
	// If the selected producer was a PV panel, then electricity should be 
	// searched among the batteries. If the selected producer was a battery,
	// then the prioritised producer will be the grid. 
	//
	// Note that there is no reason to test for the battery since this must 
	// be the situation if the producer is not a PV producer.
	
	if ( PriorityProducers.size() > 1 )
		PriorityProducers.erase( SelectedActionIndex );
	else if ( Producer::CheckAddress< PVProducer >( SelectedProducer )
				    && (Batteries.size() > 0)	)
			PriorityProducers = Batteries;
	else //if ( Producer::CheckAddress< Battery >( SelectedProducer ) )
	{
		PriorityProducers.clear();
		PriorityProducers.insert( ProducerIndex( Producers, Grid::Address() ) );
	}

  // If debugging log messages should be produced the selection is reported
	
  #ifdef CoSSMic_DEBUG
	  Theron::ConsolePrint DebugMessage;

	  DebugMessage << GetAddress().AsString() << " selected " 
								 << SelectedProducer.AsString() << " at index " 
								 << SelectedActionIndex << ". "
								 << "Remaining Priority Producers (" 
								 <<  PriorityProducers.size() << ") = ";
								 
		for ( auto index : PriorityProducers )
		 DebugMessage <<  index << " ";
		
	  DebugMessage << std::endl;			
  #endif
}

// The select producer is the default handler for the proxy removal 
// acknowledgement, and it could have tested whether the consumer agent is 
// closing, by verifying the shared pointer at each invocation. However, this
// will lead to multiple unnecessary tests. Instead a special handler will 
// replace the standard proxy removal acknowledgement handler with a handler 
// just forwarding the acknowledgement to the acknowledgement receiver when 
// the consumer agent is closing down.

void ConsumerAgent::ForwardAcknowledgement( 
     const Producer::AcknowledgeProxyRemoval & Ack,
		 const Theron::Address Producer )
{
	Send( ActorManager::ConfirmShutDown(), TheActorManager );
}

// Giving the feedback on a producer simply forwards this to the feedback 
// selector assuming that this exist.
//
// A configuration of the energy game is a particular association of consumers 
// with producers, i.e. consumers that have start times set. If one consumer 
// changes its association, it means that there is a completely new 
// configuration of the energy game, even if all the other consumers stay with 
// their original association. This new configuration is rewarded. Hence, a 
// consumer agent may receive many rewards without choosing a new producer.
//
// This reward is computed by the reward calculator when one of the involved 
// consumers terminates. This could raise a concern about which producer 
// selection is really rewarded. Consider the situation where this consumer 
// has selected a producer that will end up NOT providing electricity to this 
// consumer, but the scheduling is still ongoing when the reward update arrives.
// It is still correct to reward this choice since it is the current 
// configuration, i.e. the joint decision of all consumers that is rewarded 
// independent of whether this configuration is valid and able to provide all 
// consumers with electricity. 
//
// In the more normal situation when the selection has been successful and the 
// consumer is just waiting for the job to start or finish consumption, it is 
// obviously correct to  reward the decision. For this reason it is necessary 
// to verify that this consumer has a valid decision before rewarding the 
// selection.
//
// The Feedback function of the automata expects a response as given by the 
// environment for which it was created. The response class contains the 
// action index of the evaluated action. This does not make much sense here,
// and the reward should be given for the selected action.

void ConsumerAgent::Feedback2Selector( const RewardMessage & Response, 
                                       const Theron::Address RewardCalculator  )
{	
  if ( ProducerSelector && ( State == ExecutionState::StartTime ) )
    ProducerSelector->Feedback( 
    NeighbourhoodEnvironment::Response( SelectedActionIndex, 
																				Response.Reward() ) );
}

// The Set Start Time hander receives the assigned start time from a producer 
// actor, possibly on a remote producer node. It should be noted that this 
// start time may be unassigned, and in this case the consumer agent will 
// try to schedule its load on a different producer. It should be noted that  
// the whole idea is that the producer can at any time decide on a new start  
// time for the load, or even decide that it cannot supply energy to this load 
// at all and hence send back an unassigned start time.
  
void ConsumerAgent::SetStartTime( 
	   const Producer::AssignedStartTime & StartTime, 
		 const Theron::Address ProducerActor )
{
  #ifdef CoSSMic_DEBUG
    Theron::ConsolePrint DebugMessage;
    DebugMessage << GetAddress().AsString() << " got start time " 
								 << StartTime << std::endl;
  #endif
  
  if ( StartTime && ( State != ExecutionState::InvalidScheduling ) )
  {
    // By the convention used in CoSSMic, the name of the producer was 
    // constructed by pre-pending the word "producer" to the ID, and to recover
    // the ID from the actor name we need to take the part of the name string 
    // starting at character 8 (as there are 8 characters in "producer", but 
    // the indices in the string starts at character zero). Theron returns 
    // the name as a standard C-string, so we do the conversion in two steps:
    // First the C-string is converted to a string, then we find the start 
    // of the ID part of the string. The ID is [x]:[x]:[x] so searching for the 
    // "[" letter in the ID will discard any part of the actor name not related
    // to the ID.
   
    std::string ProducerID( ProducerActor.AsString() );
    
    ProducerID = ProducerID.substr( ProducerID.find_first_of("[") ); 
    
    // Use this start time to run the load = send an assigned start time message 
    // to the task manager. 
    
    Send( StartTimeMessage( LoadID, *StartTime, SequenceNumber, 
			    IDType( ProducerID ) ), TaskManager );
    
    // Then it is noted that the start time has been set so that we can 
    // inform the task manager if this time is cleared without no new start 
    // time to be given.
    
    State = ExecutionState::StartTime;
  }
  else
  {
    // The start time could not be initialised on this producer, and the 
    // consumer should find another producer. This means that we should first 
    // kill the proxy that was created on the remote producer node for the 
    // scheduling operation by sending a kill message back to the remote 
    // producer. When this proxy removal is acknowledged, the message is 
    // captured by the select producer message handler, which will select the 
    // next producer to try.
    
    Send( Producer::KillProxyCommand(), ProducerActor );
		
		// If we had a start time that has now been cleared by this event, it is 
    // necessary to inform the task manager that we do no longer have any 
    // start time.
    
    if ( State == ExecutionState::StartTime )
      Send( CancelStartTime( LoadID ), TaskManager );
      
    State = ExecutionState::AwaitingAcknowledgement;
  }
}

// As described in the header, this should simply be replaced with with a kill
// proxy request if the consumer actor is shutting down 

void ConsumerAgent::SendKillProxyCommand( 
	   const Producer::AssignedStartTime & StartTime, 
		 const Theron::Address ProducerActor )
{
	Send( Producer::KillProxyCommand(), ProducerActor );
	State = ExecutionState::AwaitingAcknowledgement;
}

// The add producer is the message handler called when the session layer sends
// a message that a new producer agent has been added, or after a subscription 
// as a sequence of multiple messages to inform the consumer of the producer 
// agents already known to the session layer.

void ConsumerAgent::AddProducer( 
  const Theron::SessionLayerMessages::NewPeerAdded & NewAgent, 
  const Theron::Address SessionLayerServer)
{
  // Only agents that are "producers" will be considered. By the naming 
  // conventions in CoSSMic a producer actor will have the address string 
  // "producer<id>" so it is sufficient to check if the address string 
  // contains "producer", and we add the actor if we do not have the address
  // in our directory already. For further actions we will need to know if 
  // a producer was added or not.
  
  LA::ActionIndex NumberOfProducers = Producers.size();
  
  // It should be noted that the New Agent given could in fact be a set of 
  // agents added since last notification, and we need to handle all of them.
	// The agent is added to the list of known producers if it has one of the 
	// known producer types and it is not known already.
  
  for ( const Theron::Address & TheAgentAddress : NewAgent )  
    if ( std::find( Producers.begin(), Producers.end(), TheAgentAddress )
	       == Producers.end() )  
    {
			// The agent address is not stored from before, and it should be stored
			// if it belongs to one of the known producer categories this consumer
			// will use to source energy. Note that the index of the added 
			// producer address will be the number of currently known producers since
			// the producers are indexed from 0..n-1, i.e. the next producer added
			// will have index n.
			
			if ( Producer::CheckAddress< PVProducer >( TheAgentAddress ) )
			{
				PVProducers.insert( Producers.size() );	// Index of new producer
				Producers.push_back( TheAgentAddress );
			}
			else if ( Producer::CheckAddress< Battery >( TheAgentAddress ) )
			{
				Batteries.insert( Producers.size() );
				Producers.push_back( TheAgentAddress );
			}      
		}

  // If any producer IDs were received, the appropriate actions must be taken.
  
  if ( Producers.size() > NumberOfProducers )
  {
		// A new producer should lead to a new search for a provider of electricity
		// starting from the set of PV producers. In theory there should be 
		// either a PV producer known, a battery known, but if both are void it is 
		// only possible to use the grid
		
		if ( ! PVProducers.empty() )
			PriorityProducers = PVProducers;
		else if ( ! Batteries.empty() )
			PriorityProducers = Batteries;
		else
		{
			PriorityProducers.clear();
			PriorityProducers.insert( ProducerIndex( Producers, Grid::Address() ) );
		}
		
		// Depending on whether this is first time producers are discovered or if 
		// new producers are added to an already existing set, it is necessary to 
		// re-initiate the learning automaton.
		
    if ( ! ProducerSelector ) 
    {
      // If the learning actor does not exist it will be created, and a producer 
      // selected. It is safe to invoke the selection of producers since we   
      // cannot have an ongoing scheduling operation if the producer selector 
      // has not been initialised.
      
      CreateAutomaton();
      SelectProducer( Producer::AcknowledgeProxyRemoval(), 
                      Theron::Address::Null() );
    }
    else 
    {
      // Producers were added and there is already a producer selector. It 
      // means that its state should be saved before we recreate the selector
      // with a probability mass corresponding to the new set of producers. 
      
      LAStoreProbabilities();
      CreateAutomaton();
			
      // Since one of the already known producers may be involved with an  
      // ongoing scheduling operation, we cannot start a new one before that 
      // operation has finished according to the standard protocol. Hence we 
      // cannot select any new producer at this point. However, this means that
			// the previously selected producer is no longer valid, and the state 
			// is marked invalid if a scheduling is ongoing. If a start time has 
			// been received, it should still be valid, and so the change only affects
			// the case where the consumer is waiting for a start time from a remote
			// producer.

			if ( State == ExecutionState::Scheduling )
				State = ExecutionState::InvalidScheduling;
    } 
  }
}

// The next handler is the inverse of the previous handler and called when 
// one of the agents are no longer present and useful as observed by the 
// Session Layer. Note the invariant that the learning actor must exist and in 
// this case since we cannot remove a producer that has not been added, so 
// if the producer is found we can simply rescale the producer selector 
// automaton

void ConsumerAgent::RemoveProducer(
  const Theron::SessionLayerMessages::PeerRemoved & LeavingAgent, 
  const Theron::Address SessionLayerServer)
{
  // First we must find the producer in the list of existing producers, and 
  // it is likely that this may fail because this handler is invoked whenever 
  // an agent goes off-line, which includes all types of actors not only the 
  // producers.
  
  auto Position = std::find( Producers.begin(), Producers.end(), 
												     LeavingAgent.GetAddress() );
  
  if ( Position != Producers.end() )
  {
    // The leaving agent is a producer and should be removed both from the 
    // directory of producers and from the producer learning automata. This 
    // has to be done in three steps: First the current probabilities must be
    // stored, then the leaving producer will be removed. It must be removed 
		// from the list of PV producers, batteries, and priority producers if 
		// it is part of that set.
    
    LAStoreProbabilities();
		
		auto ProducerIndex = Position - Producers.begin();
		
		if ( ! PVProducers.empty()       ) PVProducers.erase( ProducerIndex );
		if ( ! Batteries.empty()         ) Batteries.erase(   ProducerIndex );
		if ( ! PriorityProducers.empty() ) PriorityProducers.erase( ProducerIndex );
		
    Producers.erase( Position );

		// Finally the automata can be recreated for the reduced set of available 
		// producers.
		
		CreateAutomaton();
    
    // Note that a new producer cannot be selected at this point because
    // there are three options: First, the closing producer has this consumer as 
    // a client, and in this case the producer will have cancelled the load by 
    // calling the set start time handler with an unassigned start time which 
    // will trigger a selection of the next producer to try anyway. Second, if
    // another producer than the one leaving had been selected, this change 
    // will be taken into account if this producer cancels the current agreement
    // or at the end of the load. Finally, the more esoteric situation occurs
		// when the producer removal happens while the consumer is waiting for a 
		// start time. Then the actual selection has been invalidated and the 
		// consumer can only reject whatever start time it receives. The scheduling
		// operation is marked as invalid in this case.
		
		if ( State == ExecutionState::Scheduling )
			State = ExecutionState::InvalidScheduling;
  }
}

/******************************************************************************
  Messages
*******************************************************************************/

// Serialising the start time message is straightforward 

Theron::SerialMessage::Payload 
ConsumerAgent::StartTimeMessage::Serialize( void ) const
{
  std::ostringstream Message;
  
  Message << "ASSIGNED_START_TIME " << LoadID << " " << SequenceNumber 
          << " " << StartTime 
	  << " " << ProducerID << std::endl;
	  
  return Message.str();
}

// The reverse direction is also similar, although there is a verification that
// the keyword is correct. All fields of the message must be present, otherwise
// the parsing will fail.

bool ConsumerAgent::StartTimeMessage::Deserialize( 
     const Theron::SerialMessage::Payload & Payload )
{
  std::istringstream Message( Payload );
  std::string 	     Command;
  
  // Since failure is an exception, it will be treated as such.
  
  Message.exceptions( std::istringstream::eofbit | std::istringstream::badbit |
		      std::istringstream::failbit );
  
  // Then the command is verified, and if it corresponds, then the rest of the 
  // message is read.
  
  Message >> Command;
  
  if ( Command == "ASSIGNED_START_TIME" )
  try
  {
    Message >> LoadID
				    >> SequenceNumber
				    >> StartTime
				    >> ProducerID;
	    
    return true;
  } catch ( std::istringstream::failure Error ) 
  {
    return false;
  }
  else return false;
}

// The constructor based on a serialised payload simply calls the above method 
// de-serialise the payload and initialise the message elements.

ConsumerAgent::StartTimeMessage::StartTimeMessage(
		  const Theron::SerialMessage::Payload & Payload )
{
  if ( ! Deserialize( Payload ) )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "Start Time Message != " << Payload;
	
	  throw std::invalid_argument( ErrorMessage.str() );
	}
}

// The message is normally constructed by giving the values of the message 
// fields directly. Since the ID Type can potentially be a class, the ID fields
// are initialised as classes. Time is likely a standard type, by to be on 
// the safe side, also this will be initialised as a class.

ConsumerAgent::StartTimeMessage::StartTimeMessage(
  const IDType & TheLoad, Time AssignedStartTime, 
  const unsigned int TheSequenceNumber, const IDType & TheProducer)
: LoadID( TheLoad ), ProducerID( TheProducer ), StartTime( AssignedStartTime ),
  SequenceNumber( TheSequenceNumber )
{ }

// The message to cancel a start time simply insert the keyword into the 
// serialised string

Theron::SerialMessage::Payload 
ConsumerAgent::CancelStartTime::Serialize( void ) const
{
  std::ostringstream Message;
  
  Message << "DELETE_SLA " << LoadID << std::endl;
  
  return Message.str();
}

// Reverting the process is not much more complicated, and the command is 
// verified separately first.

bool ConsumerAgent::CancelStartTime::Deserialize(
  const Theron::SerialMessage::Payload & Payload)
{
  std::istringstream Message( Payload );
  std::string Command;
  
  Message >> Command;
  
  if ( Command == "DELETE_SLA")
  {
    Message >> LoadID;
    return true;
  }
  else return false;
}

// The constructor of the message from a serialised string is simply calling 
// the de-serialising method, and throws if this fails

ConsumerAgent::CancelStartTime::CancelStartTime( 
  Theron::SerialMessage::Payload & Payload)
{
  if ( ! Deserialize( Payload ) )
	{
		std::ostringstream ErrorMessage;
		
		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "Cancel start time != " << Payload;
								 
	  throw std::invalid_argument( ErrorMessage.str() );
	}
}


}

