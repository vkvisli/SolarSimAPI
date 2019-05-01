/*=============================================================================
  Pursuit Automata
  
  Pursuit automata are really estimator automata with the exception that the 
  update of the probability happens only for the action with the best reward 
  estimate.
  
  The implementations here broadly follows the versions of the algorithms 
  provided by Oommen and Agache [1].
  
  REFERENCE:
  
  [1] B. John Oommen and Mariana Agache (2001): "Continuous and discretized 
      pursuit learning schemes: various algorithms and their comparison",  
      IEEE Transactions on Systems, Man and Cybernetics, Part B, Cybernetics, 
      vol. 31, No. 3, Jun 2001
  
  Author: Geir Horn, 2013 - 2017
  Lisence: LGPL v. 3.0
=============================================================================*/

#ifndef PURSUIT_AUTOMATA
#define PURSUIT_AUTOMATA

#include <numeric>      // To accumulate vectors
#include <algorithm>    // To have portable maximum and minimum
#include <type_traits>  // Meta programming type checking

#include "LearningEnvironment.hpp"
#include "LearningAutomata.hpp"
#include "EstimatorAutomata.hpp"

namespace LA
{
/*******************************************************************************
 Continuous Pursuit Reward Penalty
 
 The algorithm for the pursuit is quite simple since all probabilities are 
 discounted and the remaining probability is added to the probability of the
 action having the largest reward estimate. The generic estimator base class 
 fundamentally adds a function to pick out the best estimate. The 
 implementation is therefore quite straight forward.
 
******************************************************************************/

template < class StochasticEnvironment, class EstimatorType 
												= MLE< typename StochasticEnvironment::ResponseType > >
class ContinuousPursuitRP 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >,
  virtual public EstimatorAutomata< StochasticEnvironment, EstimatorType >
{
public:
		
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
	
private:

	// A shorthand for the estimator automata and the VSSA base class are needed
	
	using EstimatorLA = EstimatorAutomata< StochasticEnvironment, EstimatorType >;
	using VSSA_Base   = VSSA< StochasticEnvironment >;
	
	// The learning constant (lambda) is the one that decides on the discount of 
	// the action probabilities for the actions not selected if the currently 
	// selected automata is rewarded.
	
  double LearningConstant;
  
public:
    
  // The probability updates are handled by the feedback function 
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    // The estimator is updated first (although it is updated last in the 
    // algorithms in the literature.
    
    EstimatorLA::Estimator.Update( Response );
    
    // Then all probabilities are discounted by a factor
    
    for ( double & probability : VSSA_Base::ActionProbabilities )
      probability *= (1 - LearningConstant);
    
    // Finally the probability of the action with the highest reward 
    // estimate can be increased.
    
    VSSA_Base::ActionProbabilities[
				    EstimatorLA::Estimator.BestEstimatedAction() ] += LearningConstant;
  };
  
	// The automata must be created on an instance of the environment it is 
	// interacting with
	
  ContinuousPursuitRP( const Environment & TheEnvironment, double Lambda ) 
  : LearningAutomata< Environment >( TheEnvironment ), 
    VSSA_Base( TheEnvironment ), EstimatorLA( TheEnvironment ), 
    LearningConstant( Lambda )
  { };
	
	// The default constructor must be deleted to prevent it from being used
	
	ContinuousPursuitRP( void ) = delete;
	
	// The virtual destructor will allow the virtual base classes to be correctly
	// destructed.
	
	virtual ~ContinuousPursuitRP( void )
	{ }
};

/*******************************************************************************
 Continuous Pursuit Reward-Ignore
 
 This is a very simple derivative of the reward-penalty version, defined only
 for the P-model since that is the only model for which it is possible to 
 know when the feedback is a penalty.
 
*******************************************************************************/

template < class StochasticEnvironment, class EstimatorType 
												= MLE< typename StochasticEnvironment::ResponseType > >
class ContinuousPursuitRI
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >,
  virtual public EstimatorAutomata< StochasticEnvironment, EstimatorType >,
  virtual public ContinuousPursuitRP< StochasticEnvironment, EstimatorType >
{
public:
  // Since this is only defined for the P-model a check must be made for the 
	// given environment type.
	
	static_assert( std::is_base_of< LearningEnvironment< Model::P >, 
																  StochasticEnvironment >::value,  
								 "Continuous Pursuit RI requires a P-Model environment" );
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;;
	
private:
	
	// There is a shorthand for the estimator and pursuit base automata
	
	using EstimatorLA = EstimatorAutomata  < Environment, EstimatorType >;
	using PursuitLA   = ContinuousPursuitRP< Environment, EstimatorType >;
	
public:
	
  // The probability updates are handled by the feedback function 
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    if ( Response.Feedback == PModelResponse::Reward )
      PursuitLA::Feedback( Response );
  };
  
	// The constructor is fundamentally trivial with all base classes templates 
	// based on the same environment.
	
  ContinuousPursuitRI( const Environment & TheEnvironment, double Lambda ) 
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA< Environment >( TheEnvironment ), 
    EstimatorLA( TheEnvironment ), 
    PursuitLA( TheEnvironment, Lambda )
  { };

	// The default constructor must again be deleted.
	
	ContinuousPursuitRI( void ) = delete;
	
	// The virtual destructor ensures that the virtual base classes are correctly
	// destructed
	
	virtual ~ContinuousPursuitRI( void )
	{ }
};

/*******************************************************************************
 Discrete Pursuit Reward-Penalty
 
 The discrete version decreases the unfavourable probabilities with a fixed 
 step, otherwise it is quite similar to the continuous version. This algorithm
 was first proposed by Lanctôt and Oommen [2].
 
 REFERENCE
 
 J. Kevin Lanctôt and B. John Oommen (1992): "Discretized estimator learning 
 automata", IEEE Transactions on Systems, Man and Cybernetics, vol. 22, 
 no. 6, November/December, pp. 1473-1483
 
******************************************************************************/

template < class StochasticEnvironment, class EstimatorType 
												= MLE< typename StochasticEnvironment::ResponseType > >
class DiscretePursuitRP 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >,
  virtual public EstimatorAutomata< StochasticEnvironment, EstimatorType >
{
public:
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;;

private:

	// Then shorthand notations for the base classes
	
	using VSSA_Base   = VSSA< Environment >;
	using EstimatorLA = EstimatorAutomata< Environment, EstimatorType >;
	
	// The algorithm has a parameter giving the discrete step
	
  double StepSize;
  
public:
  
  // The probability updates are handled by the feedback function 
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
		EstimatorLA::Estimator.Update( Response );
		
    ActionIndex BestAction = EstimatorLA::Estimator.BestEstimatedAction();
    
    if ( VSSA_Base::ActionProbabilities[ BestAction ] < 1.0 )
    {
      // Downscale all probabilities
      
      for ( double & probability : VSSA_Base::ActionProbabilities )
				probability = std::max( probability - StepSize, 0.0 );
      
      // Then normalise the probability vector with by setting the action 
      // with the largest reward probability, after first setting it to 
      // zero to avoid that it influences the probability sum.
      
      VSSA_Base::ActionProbabilities[ BestAction ] = 0.0;
      VSSA_Base::ActionProbabilities[ BestAction ] = 1.0 
      - std::accumulate( VSSA_Base::ActionProbabilities.begin(), 
								         VSSA_Base::ActionProbabilities.end(), 0.0  );
    };
  };
  
  // The step size is based on the resolution parameter deciding on how
  // probability space is discrete
  
  DiscretePursuitRP( const Environment & TheEnvironment, 
										 unsigned long Resolution ) 
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment ), EstimatorLA( TheEnvironment )
  {
    StepSize = 1.0 / static_cast<double>( NumberOfActions * Resolution);
  };
  
	// The default constructor must be deleted to prevent it from being used
	
	DiscretePursuitRP( void ) = delete;
	
	// There is a virtual destructor to allow the base classes to be correctly 
	// deleted.
	
	virtual ~DiscretePursuitRP( void )
	{ }
};

/*******************************************************************************
 Discrete Pursuit Reward-Inaction
 
 Similar to the continuous case, the discrete reward inaction automata can 
 only be defined for the P-model since it is only for the P-model the 
 penalty is defined. The implementation is therefore defined as a specialisation
 over the 
 
******************************************************************************/

template < class StochasticEnvironment, class EstimatorType 
												= MLE< typename StochasticEnvironment::ResponseType > >
class DiscretePursuitRI
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >,
  virtual public EstimatorAutomata< StochasticEnvironment, EstimatorType >,
  virtual public DiscretePursuitRP< StochasticEnvironment, EstimatorType >
{
public:
	
	// An explicit test for the P-model must be made.

 	static_assert( std::is_base_of< LearningEnvironment< Model::P >, 
																  StochasticEnvironment >::value, 
								 "Discrete Pursuit RI requires a P-Model environment" );
	
	// Then the standard definitions based on the environment can be given
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;

private:
	
	// And the shorthand notations for the base classes
	
	using VSSA_Base   = VSSA< Environment >;
	using EstimatorLA = EstimatorAutomata< Environment, EstimatorType >;
	using PursuitLA   = DiscretePursuitRP< Environment, EstimatorType >;
	
public:
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    if ( Response.Feedback == PModelResponse::Reward )
      PursuitLA::Feedback( Response );
  };
  
	// The constructor initialises all the base classes making sure they are all
	// instantiated on the same environment instance.
	
  DiscretePursuitRI( const Environment & TheEnvironment, 
										 unsigned long Resolution ) 
  : LearningAutomata< Environment >( TheEnvironment ),
	  VSSA_Base( TheEnvironment ), EstimatorLA( TheEnvironment ), 
	  PursuitLA( TheEnvironment, Resolution )
  {};
	
	// The default constructor is not used
	
	DiscretePursuitRI( void ) = delete;
	
	// The virtual destructor is necessary for correct invocation of each base 
	// class virtual destructor
	
	virtual ~DiscretePursuitRI( void )
	{ }
};

}      // Name space LA
#endif // PURSUIT_AUTOMATA
