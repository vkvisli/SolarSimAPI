/*=============================================================================
 Estimator Automata

 This is a class of automata algorithms using a two step procedure to update
 the probabilities:
 
 1) The feedback from the environment is used to update reward estimates for
    each possible action
 2) The reward estimates are then used to update the actual probabilities
 
 In principle these two processes are independent, and so every estimator 
 that allows the identification of the better estimate can be used.
 
 Estimator algorithms are historically motivated by the observation that 
 convergence could be slow with many actions. To remedy this problem, the 
 first estimator algorithms would update all actions that had higher reward
 estimates than the chosen action. In other words, this could lead to the 
 probability of the chosen action to be decreased even if it was rewarded if
 all the other actions have higher reward probabilities than the chosen action
 even after the estimator update.
 
 Author: Geir Horn, 2013-2017
 License: LGPL3.0
 
 Revisions: Geir Horn, 2017 - The Environment define the automata type.
===============================================================================*/

#ifndef ESTIMATOR_AUTOMATA
#define ESTIMATOR_AUTOMATA

#include <vector>                     // Standard vectors
#include <algorithm>                  // Algorithms operating on structures
#include <numeric>                    // To sum elements of a container
#include <stdexcept>                  // Standard error exceptions

#include "LearningEnvironment.hpp"    // The number of actions and response type
#include "LearningAutomata.hpp"       // The fundamental automata structures
#include "RewardEstimators.hpp"       // The estimators to be used

namespace LA
{

/*==============================================================================

 Estimator Automata

==============================================================================*/
//
// The generic estimator automata base class provides an estimator to be used 
// by the derived algorithms. It cannot be instantiated on its own as it does
// not provide the necessary virtual functions for processing feedback signals.
// For this reason, its constructor is protected and the default constructor is 
// deleted.

template < class StochasticEnvironment, class EstimatorType >
class EstimatorAutomata 
: virtual public LearningAutomata< StochasticEnvironment >, 
  virtual public VSSA< StochasticEnvironment >
{
public:
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
		
protected:
  
  EstimatorType Estimator;
  
	// The constructor simply initialises the base classes with the environment 
	// instance
	
  EstimatorAutomata( const Environment & TheEnvironment )
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA< Environment >( TheEnvironment ), 
    Estimator( TheEnvironment )
  {};
	
	// The default constructor should not be created by the compiler, and it has 
	// a virtual destructor to allow the correct destruction of the virtual base
	// classes.
  
public:
	
	EstimatorAutomata( void ) = delete;
	
	virtual ~EstimatorAutomata( void )
	{ }
};


/*==============================================================================

 Generalised Thathachar-Sastry Estimator Automata (GTSE)

==============================================================================*/
//
//  The first estimator algorithm was proposed for the P-model by Thathachar and 
//  Sastry [1], and it was using equal weights for the probability updates. This 
//  was subsequently vectorised by Agache and Oommen [2], who showed that there 
//  was not strictly necessary to use equal weights in the update and 
//  thereby they generalised the algorithm leaving the original algorithm as a 
//  special case.
//  
//  The algorithm uses a distance function to weight the difference between the 
//  estimated reward for the action whose probability is being updated, and the 
//  estimated reward for the chosen action. This function should be a monotonic,
//  increasing function defined on the interval [-1,1] and returning a value in 
//  the same interval [-1,1] with the condition that f(0)=0. By default, the 
//  identity function is used so f(x)=x.
//  
//  The weights are represented by a function returning a vector of weights to
//  be used successively for each of the actions having the estimated reward 
//  larger than the chosen action. In the worst case this could be all the other 
//  actions, except the one chosen, so Thathachar and Sastry took the weights 
//  all equal to 1/(r-1) where r is the number of actions. However, it can be 
//  anything as long as the sum of the weights is less or equal to unity. The 
//  default weight function returns a vector whose elements are all equal and 
//  sum exactly to unity.
//  
//  The original estimator algorithm of [1] was subsequently extended to the 
//  S-model by the same authors in [3]. However, the only thing they did was
//  changing the estimator allowing it to accumulate fractional environment 
//  responses (*). This allows both the P-model and the S-model to be supported 
//  by the same generic implementation. This means that the implementation 
//  template takes the automata type as parameter. 
//  
//  The other parameters to the implementation template are the type of 
//  estimator to use, which is by default set to the maximum likelihood 
//  estimator. The third argument is the function weighting the difference in 
//  reward estimate between the chosen action and the action to update, which is
//  by default the identity function. Finally, there is an argument for to give 
//  the vector function setting the probability weights, which is by default set
//  to equal weights for the actions having reward estimates higher than the 
//  currently chosen action. 
//  
//  (*) It should be noted that [3] also changes the distance weighting function
//  to weighting the two reward estimates independently and then take the 
//  difference of their functional values. This has been captured in this 
//  implementation by requiring the distance weight function to take two 
//  arguments: The reward estimate of the chosen action, and the reward estimate
//  of the action whose probability is updated. The default implementation 
//  simply subtracts these two.
//  
//  REFERENCES:
//  
//  [1] M. A. L. Thathachar and P. S. Sastry (1983): "A class of rapidly 
//      converging algorithms for learning automata", Proceedings of the 
//      International Conference on Systems, Man and Cybernetics, 
//      29 December 1983 - 7 January 1984, Bombay and New Delhi, India. 1983
//      
//  [2] Mariana Agache and B. John Oommen (2004): "Generalized TSE: A New
//      Generalized Estimator-based Learning Automaton", Proceedings of 
//      the 2004 IEEE Conference on Cybernetics and Intelligent Systems, 
//      Singapore, 1-3 December, pp. 245-251, 2004
//      
//  [3] M. A. L. Thathachar and P. S. Sastry (1985): "A New Approach to the 
//      Design of Reinforcement Schemes for Learning Automata", IEEE Transactions 
//      on Systems, Man, and Cybernetics, vol. SMC-15, no. 1, pp. 168-175, 
//      January/February, 1985
// 
// -----------------------------------------------------------------------------
// Weights for the individual estimates
// -----------------------------------------------------------------------------
//
// The only requirements on the functor for the probability weights is that 
// it has an index operator [ ] that returns the weight for the indexed 
// probability. The default version is a vector, and therefore the vector 
// provides the index operator. 
//
// The weights should be set only for the actions whose reward estimate is 
// larger than the currently chosen action, so the constructor requires a
// boolean indicator vector where the elements are set to true for the actions
// that have larger reward estimates and that should have a weight assigned.
//
// By default the weight is set to 1/K where K is the number of actions to be
// weighted.

class GTSEWeights : public std::vector< double > 
{
public:
  
  GTSEWeights( std::vector< bool > & ActionIndicator )
  : std::vector< double >( ActionIndicator.size(), 0.0 )
  {
    ActionIndex NumberOfLargerActions = count( ActionIndicator.begin(), 
																							 ActionIndicator.end(), 
																							 true );
    
    if ( NumberOfLargerActions > 0 )
    {
			double Weight = 1.0 / static_cast< double >( NumberOfLargerActions );
		      
			for ( ActionIndex Index = 0; Index < ActionIndicator.size(); Index++ )
	      if ( ActionIndicator[ Index ] )
					at( Index ) = Weight;
    };
  };
};

// -----------------------------------------------------------------------------
// Distance function
// -----------------------------------------------------------------------------
//
// The distance function takes the difference of the estimated reward of the 
// action to be updated with the reward estimate of the chosen action and 
// returns a weight in the range [-1,1]. The function should be monotonic,
// increasing and satisfy f(0)=0. The default version is the identity function

class GTSEDistanceWeight
{
public:
  
  double operator() ( double EstimateChosenAction, double EstimateOtherAction )
  {
    return EstimateChosenAction - EstimateOtherAction;
  };
};

// -----------------------------------------------------------------------------
// Generalised Thathachar-Sastry Estimator Automata (GTSE)
// -----------------------------------------------------------------------------
//
// The actual automata is defined only for the P-model and uses the MLE reward 
// estimator by default.

template< class StochasticEnvironment,
	  class EstimatorType         		= MLE< StochasticEnvironment >,
	  class DistanceWeightFunction    = GTSEDistanceWeight,
	  class ProbabilityWeightFunction = GTSEWeights >
class GTSE 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >, 
  public EstimatorAutomata< StochasticEnvironment, EstimatorType >
{
public:
	
	static_assert( std::is_base_of< LearningEnvironment< Model::P >, 
																  StochasticEnvironment >::value, 
								 "P-Model GTSE requires a P-model environment" );
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
	
protected:
	
	// Then the definitions for the VSSA base class and its action probabilities 
	// are given for convenience
	
	using VSSA_Base = VSSA< Environment >;
	using VSSA_Base::ActionProbabilities;
	
	// The estimator will be reused from the base class 
	
	using EstimatorAutomata< Environment, EstimatorType >::Estimator;

private:
	
  // The distance weight is function which is stored for future 
  // invocations - it is called through the operator and not through the 
  // object constructor. 
  
  DistanceWeightFunction DistanceWeight;
  
  // There is also a learning constant 
  
  double LearningConstant;
  
public:
  
  // The probability updates are handled by the feedback function 
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    // The estimator is updated first - this is different from the version
    // found in the literature where the estimates are updated after the 
    // probabilities.  
    
    Estimator.Update( Response );
    
    // Based on this update the expected reward for this action is computed 
		// by the estimator.
    
    double RewardEstimateChosenAction = 
												     Estimator.RewardEstimate( Response.ChosenAction );
    
	  // Then the indicator and weight vectors are initialised
														 
    std::vector< bool >    Indicator(    NumberOfActions, false );
    std::vector< double >  RewardWeight( NumberOfActions, 0.0   );
    
    for ( ActionIndex Action = 0; Action < NumberOfActions; Action++ )
    {
      double ActionEstimate = Estimator.RewardEstimate( Action );
      
      RewardWeight[ Action ] = DistanceWeight( RewardEstimateChosenAction, 
																							 ActionEstimate );

      // NOTE that the indicator is REVERSED with respect to the S_ij found 
      // in Agache's and Oommen's paper [2] (i.e. it is representing their S_ji)
      // However, this is consistent with the notation used in [4], which
      // essentially calls the same algorithm, with the identity function as
      // the fixed distance weight function, for a generalised pursuit 
      // algorithm although it is an estimator algorithm.
      
      if ( ActionEstimate > RewardEstimateChosenAction )
				Indicator[ Action ] = true;
    };
      
    // We can then decide how to distribute the increased probability on 
    // the "better" actions (based on the indicators)
    
    auto ProbilityScale = ProbabilityWeightFunction( Indicator );
    
    // Finally we can make a pass over all actions updating them according
    // to three conditions: They have a reward estimate larger than the 
    // current action, they have a reward estimate less than the chosen 
    // action, or it is the chosen action.
    
    double ChosenActionProbability = ActionProbabilities[Response.ChosenAction],
           CummulativeUpdate       = 0.0;
    
    for ( ActionIndex Action = 0; Action < NumberOfActions;  Action++ )
      if ( Indicator[ Action ] )
      {
				// The action has a larger reward estimate than the current action
				
				double Update = LearningConstant * RewardWeight[ Action ] * 
												ChosenActionProbability * ProbilityScale[ Action ] * 
								        ( 1 - ActionProbabilities[ Action ] );
				
				ActionProbabilities[ Action ] -= Update;
				CummulativeUpdate    			    += Update;
      }
      else if ( Action != Response.ChosenAction )
      {
				// The action has an inferior reward estimate than the current action
				
				double Update = LearningConstant * RewardWeight[ Action ] *
									      ActionProbabilities[ Action ];
						
				ActionProbabilities[ Action ] -= Update;
				CummulativeUpdate	      	    += Update;
      };
			      
	    // Then we can finally update the probability of the current action
			    
			ActionProbabilities[ Response.ChosenAction ] += CummulativeUpdate;        
  };
  
  // The constructor takes the learning environment and the learning constant
  // and initialises the base classes.
  
  GTSE( const Environment & TheEnvironment, double Lambda ) 
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment ), 
    EstimatorAutomata< Environment, EstimatorType >( TheEnvironment ),
    DistanceWeight()
  {
		if ( (Lambda > 0.0) && (Lambda < 1.0) )
      LearningConstant = Lambda;
    else
      throw std::invalid_argument("Illegal GTSE learning constant");
  };
	
	// The default constructor is not allowed
	
	GTSE( void ) = delete;
  
	// The destructor is virtual to avoid proper destruction of the virtual 
	// base classes.
	
	virtual ~GTSE( void )
	{ }
};

/*==============================================================================

 Generalised Pursuit Automata (GPA)

==============================================================================*/
//
//  Agache and Oommen [4] give a simplification of this algorithm where the 
//  weight function is fixed to the identity function and where the weights are
//  identically distributed. Another noticeable difference is that the chosen
//  action is counted among the actions that does not receive an increase unless
//  it is the action with the highest reward estimate (i.e. there are no other 
//  actions that have better estimates).
//  
//  The algorithm has never been expressed as valid for the S-model, but given
//  that the reward is only used in the estimator, there is no reason why it 
//  should not be useful also for the S-model and the implementation below 
//  supports both models.
//  
//  REFERENCE:
//  
//  [4] Mariana Agache and B. John Oommen (2002): "Generalized pursuit learning 
//      schemes: new families of continuous and discretized learning automata", 
//      IEEE Transactions on Systems, Man and Cybernetics, Part B, Cybernetics, 
//      vol. 32, no. 6, Dec. 2002
     
template< class StochasticEnvironment,
				  class EstimatorType = MLE< StochasticEnvironment > >
class GPA 
: virtual public LearningAutomata< StochasticEnvironment >, 
  virtual public VSSA< StochasticEnvironment >,
  public EstimatorAutomata< StochasticEnvironment, EstimatorType >
{
public:
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
	
protected:
	
	// Then the definitions for the VSSA base class and its action probabilities 
	// are given for convenience
	
	using VSSA_Base = VSSA< Environment >;
	using VSSA_Base::ActionProbabilities;
	
	// The estimator will be reused from the base class 
	
	using EstimatorAutomata< Environment, EstimatorType >::Estimator;

private:
  
  double LearningConstant;
  
protected:
  
  // The probability updates are handled by the feedback function 
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    // The estimator is updated first - this is different from the version
    // found in the literature where the estimates are updated after the 
    // probabilities. It seems off not to use the feedback before after one
    // iteration, and so we start by updating the estimates. 
    
    Estimator.Update( Response );
    
    double RewardEstimateChosenAction = 
													    Estimator.RewardEstimate( Response.ChosenAction );
    
    // Then the indicators are defined. 
    
    std::vector< bool > Indicator( NumberOfActions, false );
    
    for ( ActionIndex Action = 0; Action < NumberOfActions; Action++ )
      if ( Estimator.RewardEstimate( Action ) > RewardEstimateChosenAction )
				Indicator[ Action ] = true;
      
    // Setting the probability increase weights
      
    auto ProbilityScale = GTSEWeights( Indicator );
 
    // Then the probabilities are updated
      
    for ( ActionIndex Action = 0; Action < NumberOfActions; Action++ )
      if ( Indicator[ Action ] )
				ActionProbabilities[ Action ] = 
					(1 - LearningConstant) * ActionProbabilities[ Action ] 
																 + LearningConstant * ProbilityScale[ Action ];
      else
				ActionProbabilities[ Action ] *= (1 - LearningConstant);
    
    // The chosen action is treated specially since it normalises the 
    // probability vector by setting it to unity minus the sum of the already
    // updated probabilities (after first setting the probability of the 
    // chosen action to zero so it will not be included in the sum)
      
    ActionProbabilities[ Response.ChosenAction ] = 0.0;
    ActionProbabilities[ Response.ChosenAction ] = 
	    1.0 - std::accumulate( ActionProbabilities.begin(), 
												     ActionProbabilities.end(), 0.0 );      
  };

public:
  
  // The constructor takes the learning environment and the learning constant
  // and initialises the base classes.
  
  GPA( const Environment & TheEnvironment, double Lambda ) 
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment ), 
    EstimatorAutomata< Environment, EstimatorType >( TheEnvironment )
  {
		if ( (Lambda > 0.0) && (Lambda < 1.0) )
      LearningConstant = Lambda;
    else
      throw std::invalid_argument("Illegal GPA learning constant");
  };

	// The default constructor should not be used
	
	GPA( void ) = delete;
	
	// The destructor must be virtual to allow the base classes to destruct 
	// properly
	
	virtual ~GPA( void )
	{ }
};

/*==============================================================================

 Discrete Generalised Pursuit Automata (DGPA)

==============================================================================*/
//
//  Agache and Oommen [4] provide a discrete probability version of the GPA, 
//  which is implemented next. It is formulated for the P-model in the original
//  paper, but again the rewards are only used to update the estimator, so the 
//  automata is therefore implemented as model agnostic. 

template< class StochasticEnvironment,
				  class EstimatorType = MLE< StochasticEnvironment > >
class DGPA 
: virtual public LearningAutomata< StochasticEnvironment >, 
  virtual public VSSA< StochasticEnvironment >,
  public EstimatorAutomata< StochasticEnvironment, EstimatorType >
{
public:
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
	
protected:
  
	// Then the definitions for the VSSA base class and its action probabilities 
	// are given for convenience
	
	using VSSA_Base = VSSA< Environment >;
	using VSSA_Base::ActionProbabilities;
	
	// The estimator will be reused from the base class 
	
	using EstimatorAutomata< Environment, EstimatorType >::Estimator;

private:
  
  // Based on the resolution parameter, the step size of the algorithm is 
	// derived and it is assigned its value in the constructor.
  
  double StepSize;
  
public:
  
  // The probability updates are handled by the feedback function 
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    // The estimator is updated first - this is different from the version
    // found in the literature where the estimates are updated after the 
    // probabilities. It seems off not to use the feedback before after one
    // iteration, and so we start by updating the estimates. 
    
    Estimator.Update( Response );
    
    double RewardEstimateChosenAction =
			     Estimator.RewardEstimate( Response.ChosenAction );
    
    // Then the indicators are set and counted
    
    std::vector< bool > Indicator( NumberOfActions, false );
    ActionIndex NumberOfLargerActions = 0;
    
    for ( ActionIndex Action = 0; Action < NumberOfActions; Action++ )
      if ( Estimator.RewardEstimate( Action ) > RewardEstimateChosenAction )
      {
				Indicator[ Action ] = true;
				NumberOfLargerActions++;
      };
    
    // Then the probabilities are updated
    
    for ( ActionIndex Action = 0; Action < NumberOfActions; Action++ )
      if ( Indicator[ Action ] )
				ActionProbabilities[ Action ] = std::min( 1.0, 
					ActionProbabilities[ Action ] + StepSize / NumberOfLargerActions );
      else
				ActionProbabilities[ Action ] = std::max( 0.0, 
					ActionProbabilities[ Action ] 
						- StepSize / ( NumberOfActions - NumberOfLargerActions ) );
      
    // The chosen action is treated specially since it normalises the 
    // probability vector by setting it to unity minus the sum of the already
    // updated probabilities (after first setting the probability of the 
    // chosen action to zero so it will not be included in the sum)
    
    ActionProbabilities[ Response.ChosenAction ] = 0.0;
    ActionProbabilities[ Response.ChosenAction ] = 
	    1.0 - std::accumulate( ActionProbabilities.begin(), 
														 ActionProbabilities.end(), 0.0 );      
  };

  // The constructor takes the learning environment and the learning constant
  // and initialises the base classes.
  
  DGPA( const Environment & TheEnvironment, unsigned long ResolutionParameter ) 
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment ), 
    EstimatorAutomata< Environment, EstimatorType >( TheEnvironment )
  {
    // The probability space [0,1] is discretized by the resolution parameter
    // and this will influence the accuracy of the algorithm, but this is 
    // reciprocal to the efficiency of the algorithm.
    
    StepSize = 
    1.0 / static_cast< double >( NumberOfActions * ResolutionParameter );
  };
  
	// The automata should not be default constructed
	
	DGPA( void ) = delete;
	
	// The destructor is virtual to properly destruct also the base classes
	
	virtual ~DGPA( void )
	{ }
	
};

}      // Name space LA
#endif //ESTIMATOR_AUTOMATA
