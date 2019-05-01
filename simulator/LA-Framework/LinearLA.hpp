/*=============================================================================
 Linear automata
 
 This file defines several linear automata types. Linear automata are the most
 used ones, and also the simpler variable structure automata.
 
 WARNING: Please note that throughout this work the learning parameter is 
 a scale parameter such that a value close to unity implies slow learning, and
 a value close to zero implies quick learning. Some authors use the 1-lambda
 for the learning parameter, so care should taken when using these algorithms
 to verify the results of (old) papers.
 
 Author: Geir Horn, 2013-2017
 License: LGPLv3.0
 
 Revision: Geir Horn 2016 - introduced the LA name space
           Geir Horn 2017 - Environment response types introduced
 =============================================================================*/

#ifndef LINEAR_AUTOMATA
#define LINEAR_AUTOMATA

#include <numeric>                    // For the accumulate function
#include <algorithm>                  // Standard algorithms
#include <stdexcept>                  // For standard error messages
#include <type_traits>                // Supporting meta programming

#include "LearningEnvironment.hpp"    // The Environment definitions
#include "LearningAutomata.hpp"       // The basic automata definitions

namespace LA
{
	
/*==============================================================================

 Linear Reward-Inaction

==============================================================================*/
//
// A Linear Reward-Inaction automata changes the action probabilities when it 
// gets a reward, and does nothing when it receives a penalty. There are 
// different algorithms depending on the environment's feedback type, and these
// are implemented as template specialisations.

template< class StochasticEnvironment, LA::Model AutomataModel > 
class LinearRI;

// -----------------------------------------------------------------------------
// P-model
// -----------------------------------------------------------------------------
//
//  This automata type reacts only on rewards and forgets the penalties in the
//  update of the probability vector. It can be combined with other automata 
//  types that ignores the reward response.
//  
//  In the case of a reward, all other actions than the one we tried should be
//  multiplied with the learning constant (RewardConstant), and the probability
//  mass we gain by this operation should be added to the probability of the 
//  selected action, p(a). After this operation, the probability vector must 
//  still sum to unity. Therefore:
//  
//  Downscaling of all = sum( RewardConstant * p(i) )
// 								    = RewardConstant * sum( p(i) )
// 								    = RewardConstant
//  
//  because the sum of all probabilities in the current vector is unity. Then
//  
//  Downscaling of only the ones not chosen = 
//  "downscaling of all" - RewardConstant * p(a) =
//  RewardConstant - RewardConstant * p(a) =
//  RewardConstant * (1-p(a))
//  
//  so when normalising this we get
//  
//  Unity = "new p(a)" + "Downscaling of only the ones not chosen" 
//        = "new p(a)" + RewardConstant * (1-p(a))
//  
//  which implies that 
//  
//  new p(a) = 1 - RewardConstant * (1-p(a))
// 				  = (1 - RewardConstant) + RewardConstant * p(a)
//  
//  The last form is useful because it shows that the last term is just the 
//  downscaling of the selected probability, plus the constant term 
//  (1-RewardConstant). In other words, this can most efficiently be implemented
//  as a downscaling of all probabilities, and then just adding this constant
//  term back to the probability of the selected action.
//  
//  The automata L_RI automata was first introduced by 
//  
//  [1] I. Joseph Shapiro and Kumpati S. Narendra (1969): "Use of Stochastic 
//      Automata for Parameter Self-Optimization with Multimodal Performance 
//      Criteria", IEEE Transactions on Systems Science and Cybernetics, 
//      Vol. SSC-5, No. 4, October 1969
	 
template< class StochasticEnvironment >
class LinearRI< StochasticEnvironment, Model::P > 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >
{
public:
	
  static_assert( std::is_base_of< LearningEnvironment< Model::P >, 
																  StochasticEnvironment >::value, 
								 "P-Model LinearRI requires a P-model environment" );
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
	
protected:
  
	// Then the definitions for the VSSA base class and its action probabilities 
	// are given for convenience
	
	using VSSA_Base = VSSA< Environment >;
	using VSSA_Base::ActionProbabilities;
	
  // The probability scaling constant multiplied with the actions that 
  // should have reduced the action probability. It is a value in the open 
  // interval zero (corresponding to quick learning) and unity (corresponding 
  // to slow learning).
  
  double RewardConstant;

public:
  
  // The probability updates are handled by the feedback function which is
  // here only defined for the P-model.
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    if ( Response.Feedback == PModelResponse::Reward )
    {
      for ( double & Probability : ActionProbabilities )
        Probability *= RewardConstant;
      
      ActionProbabilities[ Response.ChosenAction ] += 1 - RewardConstant;
    }
  }; 

public:
  
  // The constructor takes a reference to the learning environment and the
  // learning parameter 0 < lambda < 1. The assumption is that a larger
  // constant means faster learning (less change in probabilities as the
  // learning constant is multiplied with the current probabilities).
  
  LinearRI ( const Environment & TheEnvironment, double LearningConstant )
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment )
  {
    if ( (LearningConstant > 0.0) && (LearningConstant < 1.0) )
      RewardConstant = LearningConstant;
    else
      throw std::invalid_argument("Illegal LinearRI learning constant");
  };
  
	// The default constructor should not be used, and it is therefore explicitly
	// deleted.
	
	LinearRI( void ) = delete;
	
	// The destructor is virtual to ensure that the virtual base classes can 
	// properly delete the probability vector.
	
	virtual ~LinearRI( void )
	{ }
};

// -----------------------------------------------------------------------------
// S-model
// -----------------------------------------------------------------------------
//
//   Viswanathan and Narendra [1] developed a methodology for extending P-model
//   automata for the S-model, and applies this to the L_RI automata. It is a 
//   direct extension that weights the probability update according to the 
//   normalised environment response. Fundamentally, if the response at time k
//   is r(k), then the S-model is a linear combination of the penalty update
//   T(p(k),0,i) and the reward update T(p(k),1,i) as follows
//       T(p(k),r(k),i) = (1-r(k))T(p(k),0,i) + r(k)T(p(k),1,i)
//   
//   Philosophically, one can ask if it make sense defining 'penalty' for the
//   S-model except for the case where r(k)=0. This implies that the L_RI 
//    automata for the S-model can be directly based on the P-model L_RI with 
//   a time variable learning constant equal to RewardConstant*r(k). As such it 
//   could have been based directly on the P-model L_RI, however this would 
//   create a conflict in the invariant that the automata type should be the 
//   same all through the inheritance chain.
//   
//   The formula used here is equation (2.2.2) on page 56 of [2]
//   
//   REFERENCE:
//   
//   [1] R. Viswanathan and Kumpati S. Narendra (1973): "Stochastic Automata 
//       Models with Applications to Learning Systems", IEEE Transactions on 
//       Systems, Man and Cybernetics, Vol. SMC-3, No. 1, pp. 107-111, 
//       January 1973
//   [2] Mandayam A. L. Thathachar and P. S. Sastry (2004): "Networks of  
//       Learning Automata: Techniques for Online Stochastic Optimization", 
//       Kluwer Academic, Boston, MA, USA, ISBN 1-4020-7691-6

template< class StochasticEnvironment >
class LinearRI< StochasticEnvironment, Model::S > 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >
{
public:
	
  static_assert( std::is_base_of< LearningEnvironment< Model::S >, 
																  StochasticEnvironment >::value, 
								 "S-Model LinearRI requires a S-model environment" );
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
		
protected:
  
	// Then the definition for the VSSA base class, and the action probability 
	// vector for convenience
	
	using VSSA_Base = VSSA< Environment >;
	using VSSA_Base::ActionProbabilities;
	
  // The probability scaling constant multiplied with the actions that 
  // should have reduced the action probability. It is a value in the open 
  // interval zero (corresponding to quick learning) and unity (corresponding 
  // to slow learning).
  
  double RewardConstant;
  
  // The feedback function handles the reward from the environment and 
  // implements the probability update rules. Note that the scale factor
  // is a*beta(k) where a is a small number for slow learning whereas 
  // we here use lambda close to unity for slow learning, thus a=1-lambda
  // Furthermore, the weight factor is 1-a*beta(k)

public:
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    double ScaleFactor = 1.0 - RewardConstant * Response.Feedback; 
    
    for ( double & Probability : ActionProbabilities )
      Probability *= ScaleFactor;
      
    ActionProbabilities[ Response.ChosenAction ] += 1 - ScaleFactor;
  };
   
  // The constructor takes a reference to the learning environment and the
  // learning parameter 0 < lambda < 1. The assumption is that a larger
  // constant means faster learning (less change in probabilities as the
  // learning constant is multiplied with the current probabilities).
  
  LinearRI( const Environment & TheEnvironment, double LearningConstant )
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment )
  {
    if ( (LearningConstant > 0.0) && (LearningConstant < 1.0) )
      RewardConstant = LearningConstant;
    else
      throw std::invalid_argument("Illegal LinearRI learning constant");
    
  };
  
	// It should not be possible to default construct a linear RI automata, and 
	// so the default constructor is explicitly deleted.
	
	LinearRI( void ) = delete;
	
	// There is a virtual destructor to ensure that the virtual base classes are
	// properly removed.
	
	virtual ~LinearRI( void )
	{ }
};

/*==============================================================================

 Linear Ignore-Penalty (P-Model only)

==============================================================================*/
//
//   This automata type reacts only on penalties and forgets the rewards in the
//   update of the probability vector. It can be combined with other automata 
//   types that ignores the penalty response.
//   
//   In case of a penalty we do not know which action is the right one, so all
//   the actions should be downscaled with the penalty constant:
//   
//   p(k+1) = PenaltyConstant * p(k)
//   
//   However, the probabilities have to sum to one, and after this downscaling
//   they sum to PenaltyConstant, and we must add back an amount of 
//   (1-PenaltyConstant) to normalise the probability vector. However, since we 
//   know that the action tried is not appreciated, we exclude this from the
//   update, so that for all the other actions we will add 
//   
//   (1-PenaltyConstant)/(NumberOfActions-1)
//   
//   Algorithmically, we will do this by adding the same constant to all and 
//   then correct by subtracting the constant from the tried action in the end. 
//   Furthermore, we will do the downscaling and addition in one pass of the 
//   probability vector.

template< class StochasticEnvironment, LA::Model AutomataModel > 
class LinearIP;

// There is currently only one specialisation for the P-model

template< class StochasticEnvironment >
class LinearIP< StochasticEnvironment, Model::P > 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >
{
public:
	
  static_assert( std::is_base_of< LearningEnvironment< Model::P >, 
																  StochasticEnvironment >::value, 
								 "LinearIP requires a P-model environment" );
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
		
protected:
  
	// Then the definition for the VSSA base class and its action probability 
	// vector for convenience
	
	using VSSA_Base = VSSA< Environment >;
	using VSSA_Base::ActionProbabilities;

  // The probability scaling constant multiplied with the actions that 
  // should have reduced the action probability. It is a value in the open 
  // interval zero (corresponding to quick learning) and unity (corresponding 
  // to slow learning).
  
  double PenaltyConstant;
  
  // The probability updates are done by the feedback function 

public: 
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    if (  Response.Feedback == PModelResponse::Penalty )
    {
      double pIncrease    = ( 1.0-PenaltyConstant ) /
			    static_cast<double>( NumberOfActions()-1 );
      
      for ( double & Probability : ActionProbabilities )
          Probability = PenaltyConstant * Probability + pIncrease;
      
      ActionProbabilities[ Response.ChosenAction ] -= pIncrease;
    }
  };
    
public:
  
  // The constructor basically only checks and stores the learning 
  // constant, and uses the given environment to initialise the number of 
	// actions in the base classes.
  
  LinearIP (  const Environment & TheEnvironment, double LearningConstant )
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment )
  {
    if ( (LearningConstant > 0.0) && (LearningConstant < 1.0) )
      PenaltyConstant = LearningConstant;
    else
      throw std::invalid_argument("Illegal LinearIP learning constant");
  };
  
	// Only the above constructor should be used, and so the default constructor 
	// is explicitly deleted
	
	LinearIP( void ) = delete;
	
	// The virtual base classes and functions mandates a virtual destructor
	
	virtual ~LinearIP( void )
	{ }
};

/*==============================================================================

 Linear Reward-Penalty automata (P-model only)

==============================================================================*/
//
//  The general Linear Reward-Penalty automata type is simply the combination
//  of the Linear Reward-Ignore and the Linear Ignore-Penalty automata. It 
//  therefore needs both constants, and invokes the probability updates of 
//  one of the two base class automata depending on the received response.
 
template< class StochasticEnvironment, LA::Model AutomataModel > 
class LinearRP;

template< class StochasticEnvironment >
class LinearRP< StochasticEnvironment, Model::P > 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >,
  public LinearRI< StochasticEnvironment, Model::P >, 
  public LinearIP< StochasticEnvironment, Model::P >
{
public:
	
  static_assert( std::is_base_of< LearningEnvironment< Model::P >, 
																  StochasticEnvironment >::value, 
								 "LinearRP requires a P-model environment" );
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
		
protected:
  
	// Then the definition for the VSSA base class
	
	using VSSA_Base = VSSA< Environment >;

public:
  
  // The feedback function simply detects if we got a reward or a penalty and 
	// forward the actual update to the feedback functions in the base classes.
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
   {
    if ( Response.Feedback == PModelResponse::Reward )
      LinearRI< Environment, Model::P >::Feedback( Response );
    else
      LinearIP< Environment, Model::P >::Feedback( Response );
  };
      
  // The constructor simply leaves everything to the base classes. Note that
  // we have to explicitly initialise the a virtual base classes
  
  LinearRP ( const Environment & TheEnvironment, 
				     double RewardConstant, double PenaltyConstant )
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment ),
    LinearRI< Environment, Model::P >( TheEnvironment, RewardConstant  ),
    LinearIP< Environment, Model::P >( TheEnvironment, PenaltyConstant )
  {  };
  
	// Only the full constructor should be used, and the default constructor is 
	// explicitly deleted.
	
	LinearRP( void ) = delete;
	
	// There is a need for a virtual destructor since the class has virtual 
	// base classes and virtual functions
	
	virtual ~LinearRP( void )
	{ }
};

/*==============================================================================

 Discrete Linear Reward Inaction automata (P-model)

==============================================================================*/
//
//  Surprisingly enough there is not a proven multi-action discrete version of
//  the linear automata by Thatachar and Oommen [2]. The idea is that when an 
//  action is rewarded, all the other action probabilities are reduced by a 
//  fixed (discrete) amount. Finally, the removed probabilities are added back
//  to the rewarded action probability. 
//  
//  The implementation here is modified in the same way as the other discrete
//  algorithms, in which the step size is a function of the number of actions.
//  Essentially, this is just setting Thatachar's and Oommen's number of 
//  partitions, N, equal to r*n where n is the new resolution parameter.
//  
//  The two-action version of this automata was proven epsilon-optimal by 
//  Oommen and Hansen [3], and the two-action Discrete Linear Reward-Penalty 
//  automata was proposed and proven epsilon-optimal by Oommen and 
//  Christensen [4], and conjectured to hold for the general case although the
//  general case was never stated and there are no subsequent analysis to confirm
//  the general Discrete Linear Reward-Penalty automata, so it is consequently 
//  not implemented.
//  
//  REFERENCES
//  
//  [2] Mandayam A. L. Thathachar and B. John Oommen (1979): "Discretized 
//      reward-inaction learning automata", Journal of Cybernetics and 
//      Information Sciences, Vol. 2, No. 1, pp. 24–29, 1979
//      
//  [3] B. John Oommen and Eldon Hansen (1984): "The Asymptotic Optimality of 
//      Discretized Linear Reward-Inaction Learning Automata", IEEE Transactions 
//      on System, Man, and Cybertics, Vol. SMC-14, No. 3, pp.  542-545 , 1984
//      
//  [4] B. John Oommen and J. P. R. Christensen (1988): "epsilon-Optimal 
//      Discretized Linear Reward-Penalty Learning Automata", IEEE Transactions 
//      on Systems, Man and Cybernetics, vol. 18, no. 3, May/June, 1988, 
//      pp. 451-457

template< class StochasticEnvironment, LA::Model AutomataModel > 
class DiscreteLRI;

template< class StochasticEnvironment >
class DiscreteLRI< StochasticEnvironment, Model::P > 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >
{
public:
	
  static_assert( std::is_base_of< LearningEnvironment< Model::P >, 
																  StochasticEnvironment >::value, 
								 "DiscreteLRI requires a P-model environment" );
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
		
protected:
  
	// Then the definition for the VSSA base class and its probability vector
	// for convenience.
	
	using VSSA_Base = VSSA< Environment >;
	using VSSA_Base::ActionProbabilities;
	
private:
  
  double StepSize;
  
public:
  
  // The feedback function subtracts the step size form all the probabilities
  // not rewarded, before normalising the vector with the rewarded 
  // probability. Because of the max operator we cannot automatically assume
  // that a probability has been downscaled, so we have to add up the 
  // existing probabilities in the normalisation.
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    if ( Response.Feedback == PModelResponse::Reward )
    {
      for ( double & Probability : ActionProbabilities )
          Probability = std::max( Probability - StepSize, 0.0 );
      
      // The selected action probability is reset to zero so that it is 
      // possible to sum over the other changed probabilities
      
      ActionProbabilities[ Response.ChosenAction ] = 0.0;
      ActionProbabilities[ Response.ChosenAction ] = 
		      1.0 - std::accumulate( ActionProbabilities.begin(), 
																 ActionProbabilities.end(), 0.0 );
    }
  };
  
	// The constructor takes the resolution parameter and computes the step size 
	// based on this.
	
  DiscreteLRI(  const Environment & TheEnvironment, unsigned long Resolution )
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment )
  {
    StepSize = 1.0 / static_cast<double>( NumberOfActions * Resolution );
  };
	
	// Since the step size is needed for using the automata, the default 
	// constructor must be deleted
	
	DiscreteLRI( void ) = delete;
	
	// A virtual destructor is provided to ensure that the virtual base classes 
	// are properly destructed.
	
	virtual ~DiscreteLRI( void )
	{ }
};

}      // name space LA
#endif // LINEAR_AUTOMATA
