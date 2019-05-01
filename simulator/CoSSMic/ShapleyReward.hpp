/*=============================================================================
  Shapley Value Reward
  
  The CoSSMic neighbourhood is modelled as a collaborative game of consumers 
  and producers. The reward of the game to the whole neighbourhood is the total 
  amount of grid energy not bought by the neighbourhood. The best fair solution 
  to distribute the reward is the Shapley value [1]. 
  
  A play for a consumer will be the selection of a candidate producer, and if 
  the consumer is refused by the producer it will make another play for 
  another. Given that taking power from the Grid is always an option, this 
  game will terminate with the successful association of the consumer with a 
  successful association of the consumer with a provider (the Grid provider 
  included). It should be noted that the association can change at any point 
  in time until the consumer actually starts the job depending on the producer's
  capacity to fulfil the assignment. However, once the job starts the consumer's
  association to the producer is frozen. The game epoch lasts until the consumer 
  has completed the job on the energy from the selected provider. 
  
  When an epoch ends, the Shapley value can be computed on the basis of 
  completed transactions. The Shapley value of a player is representing its 
  expected marginal contribution to any random coalition [2], and therefore 
  the reward for its decisions. The relations among the players are modelled 
  as weights on the edges of a graph where the edges are between a consumer 
  and the producers providing it with energy.
  
  It has been proven that computing the value exactly has exponential 
  complexity [2], and probabilistic Monte Carlo methods have been argued as 
  the preferred solution approach. However, for the special case where the 
  value of the game is captured as sum of bilateral values between two players,
  it has been shown that the Shapley value for a player can be computed with
  complexity linear to the number of associations the player has [3]. Given 
  the model in CoSSMic this amounts to adding together half the weights on the 
  edges incident to a vertex (consumer actor) in the energy transfer graph.
  
  A consequence of this is that it is only necessary to store the energy 
  exchange for consumers on the local network endpoint since energy consumption 
  by consumers on remote nodes will never affect the energy weight on any edge
  of the energy exchange graph incident to any consumer on the local node. 
  The local energy exchange weights are stored in a matrix where there is one
  row for each local consumer, and one column for each known producer.
  
  The rows are recorded as the consumers are created by the Actor Manager, and
  the columns are recorded once a load has finished execution and the Actor 
  Manager is requested by the Task Manager to delete the load, i.e. delete its
  associated Consumer Agent.
  
  When the load terminates, it also marks the end of an epoch in the game. 
  However, only one of the energy exchange weights will have changed, and this
  on an edge incident to the consumer agent representing this load. It is 
  assumed that all players (i.e. consumers) shall be rewarded by the end of an
  epoch, and this layout means that all but one consumer will receive
  the same reward as last time because none of the weights incident to these 
  nodes of the energy exchange graph have changed. 
  
  When an epoch ends, the reward calculator on the node hosting the consumer 
  agent for the terminating load will disseminate the recorded energy 
  consumption to all peer reward calculator, which will in turn reward all 
  consumers on each node. 
    
  REFERENCES:
  
  [1] Lloyd S. Shapley (1953): "A Value for n-person Games", Paper 17 in 
      "Contributions to the Theory of Games", H. W. Kuhn and A. W. Tucker (eds.)
      Annals of Mathematical Studies, Number 28, Vol. 2, pp. 307–317
  [2] U. Faigle and W. Kern (1992): "The Shapley value for cooperative games 
      under precedence constraints", International Journal of Game Theory,
      Vol. 21, No. 3, pp. 249-266
  [3] Xiaotie Deng and Christos H. Papadimitriou (1994): "On the Complexity of 
      Cooperative Solution Concepts", Mathematics of Operations Research,
      Vol. 19, No. 2, pp. 257-266
  
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef SHAPLEY_VALUE_REWARD
#define SHAPLEY_VALUE_REWARD

#include <unordered_map>	 // Mapping addresses to indices
#include <armadillo>		 // For matrix support

#include "RewardCalculator.hpp"  // The generic reward structure

namespace CoSSMic
{

// Since the agent and the de-serialising support are generic classes that may
// be inherited multiple times, they are declared as virtual also for the 
// reward calculator. Hence it is strictly not necessary to declare them again
// here, but it is less confusing since their constructors have to be explicitly
// involved by the class constructor.

class ShapleyValueReward : virtual public Theron::Actor,
													 virtual public Theron::StandardFallbackHandler,
												   virtual public Theron::DeserializingActor,
												   virtual public RewardCalculator
{
private:
  
  // ---------------------------------------------------------------------------
  // Variables
  // ---------------------------------------------------------------------------
  //
  // The Armadillo matrix library uses uword for indices, and it is defined 
  // a separate type for clarity. The same goes for the double matrix.
  
  typedef arma::uword 		    Index;
  typedef arma::Mat< double >	Matrix;
  
  // The energy edge weights are stored in a matrix as E(c,p) where c is 
  // the consumer index and p is the producer index. However, it will receive 
  // the reference for the consumer actor involved as a Theron address, and 
  // this address needs to be mapped to a legal index. A standard map is used 
  // since this should be faster when iterating over all local consumers to 
  // reward them. Lookup in an unordered map is of constant complexity, and 
  // it is used for fast lookup of the index.
  
  std::unordered_map< Theron::Address, Index > ConsumerIndex;
  
  // The producers are typically only known by their CoSSMic IDs, hence the 
  // map key is the ID for the producers.  
  
  std::unordered_map< IDType, Index > ProducerIndex;
   
  // The edge weights are the accumulated energy from a provider to a consumer,
  // and the size of the matrix will grow when new consumers become known to 
  // the system. Note that a consumer represents a device or a mode on a device,
  // and so the consumer agent will only exist as long as the load it represents
  // is active, but it may come back next time the same device wants to run a 
  // load. The matrix will therefore grow quite rapidly in the beginning when 
  // new consumers and producers are being defined in the system, and after some
  // time it should stabilise and only grow when new devices are added. 
  
  Matrix EnergyExchange;
  
  // The Shapley values will only change when a consumer on this node has 
  // finished its load, but the rewards to the consumers on this node is 
  // produced whenever an epoch ends in the neighbourhood, i.e. when any of 
  // the consumers in the neighbourhood has completed a consumption. The Shapley
  // values are therefore cached and update only when a consumer on this 
  // endpoint terminates its consumption
  
  arma::colvec ShapleyValues;
  
  // ---------------------------------------------------------------------------
  // New PV Energy message
  // ---------------------------------------------------------------------------
  //
  // The message handler extends the generic reward calculator by computing
  // the reward per consumer and dispatch this reward to the local consumers.

protected:
  
  virtual void NewPVEnergyValue( const NewPVEnergy & EnergyMessage, 
																 const Theron::Address Sender       );

  // ---------------------------------------------------------------------------
  // Add consumer message
  // ---------------------------------------------------------------------------
  //
  // The overloaded handler needs to check if this consumer has already a row 
  // in the energy exchange matrix, and add that row if this is the first time
  // that consumer is seen.
  
  virtual void NewConsumer( const AddConsumer & ConsumerRequest, 
												    const Theron::Address Sender );
  
  // ---------------------------------------------------------------------------
  // Computing the reward when the load has finished
  // ---------------------------------------------------------------------------
  //
  // The handler for the add energy message will add a column to the energy
  // exchange matrix if the producer ID has not been recorded previously. Then  
  // it will compute the reward and use the new PV energy value dispatcher 
  // handler to distribute the reward to all local consumers, before sending  
  // back the acknowledgement to the Actor Manager.
    
  virtual void NewEnergy( const AddEnergy & EnergyMessage, 
												  const Theron::Address Sender    );

  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
  //  
  // The constructor initialises the various parts of the reward calculator, 
  // and sets the name of the agent will be "RewardCalculator" with the endpoint 
	// domain name added.
  
public:
  
  ShapleyValueReward( const std::string & DomainName );
  
  // The destructor does nothing in this version, but it is a place holder to
  // ensure that the right destructor is called on the base class.
  
  virtual ~ShapleyValueReward()
  { }
};
  
}; 	// Name space CoSSMic
#endif	// SHAPLEY_VALUE_REWARD
