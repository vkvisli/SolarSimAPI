/*============================================================== ===============
  Shapley Value Reward

  This is the implementation of the Shapley Value Reward class. Please see the 
  header file for explanations.
  
  
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <stdexcept>		        // Standard exceptions
#include <armadillo>		        // For matrix support

#ifdef CoSSMic_DEBUG
  #include "ConsolePrint.hpp"   // Debug messages
#endif

#include "ConsumerAgent.hpp"	  // The receivers of the reward
#include "ShapleyReward.hpp"	  // The reward calculator

// When adding up elements in a matrix the sum can be made row wise or 
// column wise. For some reason Armadillo expects these to be given as numeric 
// values, whereas they should have been strongly typed enumerations. They 
// are defined here as normal enumerations because they must be converted to 
// the numeric values expected by the Armadillo functions, and strongly typed
// enumerations cannot be implicitly converted, even if the underlying storage
// type is explicitly specified.

namespace arma {
  enum Sum : uword
  {
    Columns = 0,
    Rows    = 1
  };
}

namespace CoSSMic 
{
/*****************************************************************************
  Messages and message handlers
******************************************************************************/

// -----------------------------------------------------------------------------
// New consumer created on the local node
// -----------------------------------------------------------------------------
// There are two cases to consider: The consumer is created for the first time, 
// and the energy exchange matrix should be extended with a row for this 
// consumer. On the other hand, if the consumer already has a row in the energy 
// exchange matrix, it means that the consumer actor is re-created to serve 
// another load. 

void ShapleyValueReward::NewConsumer( 
  const RewardCalculator::AddConsumer & ConsumerRequest, 
  const Theron::Address Sender)
{
  RewardCalculator::NewConsumer(ConsumerRequest, Sender);
  
  if (ConsumerIndex.find( ConsumerRequest.GetAddress() ) == ConsumerIndex.end())
  {
    // Store the index that this new consumer will have in the enlarged 
    // energy exchange matrix since the index is in the interval [0,n-1],
    // the number of rows (n) will be the index of the new row.
      
    ConsumerIndex.emplace(ConsumerRequest.GetAddress(), EnergyExchange.n_rows);
    
    // Then extend the energy exchange matrix with one row for this consumer
    
    EnergyExchange.resize( EnergyExchange.n_rows + 1, EnergyExchange.n_cols );
  }
}


// This message will make the receiving reward calculator compute the reward 
// for all consumers on the same network endpoint, i.e. the consumers stored in 
// the consumer index map.

void ShapleyValueReward::NewPVEnergyValue( 
     const ShapleyValueReward::NewPVEnergy & EnergyMessage, 
     const Theron::Address Sender )
{
  // First the global counters are updated by the reward calculator
  
  RewardCalculator::NewPVEnergyValue( EnergyMessage, Sender );
  
  // In order to compute the overall reward to the household the reward for 
  // the local customers must be added up.
  
  double TotalConsumerReward = 0.0;
   
  // Strictly speaking, the Shapley values are half of the weights on the 
  // incident edges because each edge will be counted for two vertices if the 
  // sum of the Shapley values for all vertices are added together. The 
  // reward value is in the interval [0,1], and it is produced by normalising 
  // the Shapley value for one vertex on the sum of all weights in the energy 
  // exchange graph, i.e. the total neighbourhood PV Energy consumed. If only 
  // half of the sum of incident energy exchange values is used, the maximum
  // reward will be 0.5. To see this consider the case where there is only 
  // one consumer and its PV energy exchange would be equal to the total PV 
  // energy of the neighbourhood. The reward is therefore defined to be twice
  // the normalised Shapley value for the consumer. The reward is computed and 
  // distributed in one pass of the Shapley values to the address part of the 
  // consumer index record.
  //
  // In order to find the associated reward for a consumer, the index to the 
  // consumer's row in the Shapley value vector is needed. Consequently, one 
  // may think that the indexes could be stored directly to avoid the lookup
  // in the consumer index map. However, a message should also be sent to 
  // this consumer, so both its address and its index is needed. It is then
  // faster to store the address since the lookup in the unordered map is 
  // assumed to be of constant complexity. Looking up based on the index is 
  // of linear complexity.
  
  const double NeighbourhoodEnergy = GetNeighbourhoodPVEnergy();
   
  for ( const Theron::Address & Consumer : GetConsumers() )
  {
    double Reward = ShapleyValues( ConsumerIndex.at( Consumer ) ) 
		    / NeighbourhoodEnergy;
		    
    TotalConsumerReward += Reward;
		    
    Send( ConsumerAgent::RewardMessage( Reward ), Consumer );
  }  
  
  // Then the reward for the household is computed. Since this takes into 
  // account both the consumer side and the producer side of the edge in the 
  // weighted energy graph, the reward must be divided by two. Furthermore,
  // the PV energy produced by this household need to be normalised on the 
  // neighbourhood PV energy to become a reward. 
  
  SaveRewardFile( 
      (TotalConsumerReward + GetSharedPVEnergy() / NeighbourhoodEnergy)/2.0 );
}

// -----------------------------------------------------------------------------
// New energy consumed when a load terminates
// -----------------------------------------------------------------------------
//
// When a load is terminated because it has finished execution, a message will 
// be sent from the Task Manager to the Actor Manager, which will in turn send 
// a message to the reward calculator containing the total consumed energy of
// the transaction. The message handler will first do the housekeeping 
// of the energy exchange matrix. If the producer is unknown, it will be added 
// to the producer store and one additional column will be added to the energy 
// exchange matrix. If the producer is known the energy of this consumption 
// will just be added to the weight of the producer consumer edge in the 
// consumption graph.
//
// Special treatment is given to the Grid producer since it should not be 
// recorded in the energy exchange matrix, and the consumer should not be 
// rewarded for taking energy from the grid.

void ShapleyValueReward::NewEnergy( 
     const ShapleyValueReward::AddEnergy & EnergyMessage, 
     const Theron::Address Sender)
{
  if ( EnergyMessage.Producer() != Grid::ID() )
  {
    // Matrix housekeeping is to check if the producer is already known. Since 
    // this is the normal situation, exception handling is used if the column 
    // index of the producer ID cannot be retrieved.
    
    Index ProducerColumn;
    
    try
    {
      ProducerColumn = ProducerIndex.at( EnergyMessage.Producer() );
    }
    catch ( std::out_of_range Exception )
    {
      ProducerColumn = EnergyExchange.n_cols;
      ProducerIndex.emplace( EnergyMessage.Producer(), ProducerColumn );
      EnergyExchange.resize( EnergyExchange.n_rows, EnergyExchange.n_cols + 1 );
    }
    
    // Then the row associated with the consumer is looked up. Note that the 
    // at function will throw an out of range error if the consumer's address 
    // cannot be found. This is not captured, because all consumers should be 
    // registered with the add consumer message when they are created, hence it 
    // is an indication of a serious error if the consumer is not found.
    
    #ifdef CoSSMic_DEBUG
	    Theron::ConsolePrint DebugMessage;

	    DebugMessage << "Shapley Reward records energy consumed by the consumer: "
	                 << EnergyMessage.Consumer().AsString() << std::endl;
    #endif
    
    Index ConsumerRow = ConsumerIndex.at( EnergyMessage.Consumer() );
    
    // The energy just consumed is then added to the weight of the edge between
    // the consumer and the producer in the energy exchange graph.
    
    EnergyExchange( ConsumerRow, ProducerColumn ) += EnergyMessage.Energy();

    // The Shapley value for a given consumer is the sum of all weights on the 
    // edges incident to that consumer's vertex in the PV energy consumption 
    // graph. Since these weights are stored in a matrix with as many rows as 
    // there are consumers on this local network endpoint and as many columns 
    // as there are PV producers used by these consumers, the Shapley value 
    // for a consumer is the sum of the elements in the corresponding row.
  
    ShapleyValues = sum( EnergyExchange, arma::Sum::Rows );
  
    // The rewards to the local consumers is computed and dispatched by the 
    // message handler for new PV Energy, so it is simply invoked directly.
    // The sender address is in this case not necessary, but out of courtesy
    // it is provided to be the address of this actor.
    
    NewPVEnergyValue( 
               NewPVEnergy( EnergyMessage.Energy(), EnergyMessage.Producer() ), 
               GetAddress() );    
  }
  
  // Finally, the generic reward calculator can do the housekeeping
  
  RewardCalculator::NewEnergy( EnergyMessage, Sender );
}


/*****************************************************************************
  Constructor and destructor
******************************************************************************/
//
// Because the actor and the de-serialising actor are virtual base classes their
// constructors must be explicitly called by all derived classes, and the 
// corresponding calls will be ignored for base classes.

ShapleyValueReward::ShapleyValueReward( const std::string & DomainName ) 
: Actor( RewardCalculator::NameRoot + DomainName ),
  StandardFallbackHandler( GetAddress().AsString() ),
  DeserializingActor( GetAddress().AsString() ),
  RewardCalculator( DomainName ),
  ConsumerIndex(), ProducerIndex(), EnergyExchange(),  ShapleyValues()
{ }


} // Name space CoSSMic
