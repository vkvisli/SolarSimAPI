/*=============================================================================
 Reward Estimators
 
 An important class of automata uses an estimator for expected reward, and 
 then update the probabilities based on this estimated reward. This file 
 defines the estimators that can be used with the automata.
 
 The rewards to be estimated for the S-model is quite straight forward since
 the rewards are doubles. However for the Q-model, what does it mean if a 
 given action A one time received the feedback index 2 and the next time the 
 feedback index 3? The standard maximum likelihood approach is to add the 
 feedback indices together and divide by the number of times the action has
 been tried. Otherwise one could use the median number, i.e. the most 
 frequent response given for the action. In either case, the estimator will 
 only work on the response indices and not what they actually mean in terms of
 response set values.
 
 Author: Geir Horn, 2013 - 2017
 License: LGPL3.0
==============================================================================*/

#ifndef REWARD_ESTIMATORS
#define REWARD_ESTIMATORS

#include <cmath>        // For mathematical functions
#include <vector> 	    // Recording total rewards per action
#include <stdexcept>    // To throw standard exceptions
#include <type_traits>  // To test types at compile time
#include <numeric>      // Accumulation of values

#include "LearningAutomata.hpp"
#include "LinearLA.hpp"

namespace LA
{
/*******************************************************************************
 Reward Estimator
 
 This is the interface class to be used as the base class for all the 
 other estimators. Note that the constructor requires a pointer to the 
 learning environment for which the reward estimator is going to be used. 
 Fundamentally it only needs the number of actions, but this must be consistent
 with the number of actions supported by the learning environment, so the 
 number of actions is taken from the environment rather than being passed as
 and explicit parameter.
*******************************************************************************/

template< class StochasticEnvironment, 
					typename EstimateType = typename StochasticEnvironment::ResponseType >
class RewardEstimator
{
public:
  
  // The environment type and the number of actions are defined from the given
	// environment.
  
	using Environment = StochasticEnvironment;
	const ActionIndex NumberOfActions;

  // There is an interface function to record the rewards received for the 
  // given action tried. Note that the response from the environment will 
	// contain both the index of the tried action and the actual response value.
  
  virtual void Update ( const typename Environment::Response & Response ) = 0;

  // Then there are two interface functions to retrieve the reward estimate 
  // for a given action (index). One should observe that it is possible for
  // a derived class to specify that the returned estimate should be 
  // different from the rewards received. This happens when the Q-model simply
  // averages over received response indices to find the "average" response
  // index (as a double).
  
  virtual EstimateType RewardEstimate( ActionIndex TheAction ) = 0;
  
  EstimateType operator[] ( ActionIndex TheAction )
  { return RewardEstimate( TheAction );  };
  
  // Finally there is a function to find the estimated best action, i.e. it
  // should return the index of the action with the maximum reward estimate.
  
  ActionIndex BestEstimatedAction (void)
  {
    ActionIndex  BestAction      = 0;
    EstimateType HighestEstimate = static_cast< EstimateType >(0);
    
    for ( ActionIndex Action = 0; Action < NumberOfActions; Action++ )
		{
		  EstimateType TheEstimate = RewardEstimate( Action ); 
		  
		  if ( TheEstimate > HighestEstimate )
		  {
		    HighestEstimate = TheEstimate;
		    BestAction      = Action;
		  };
		};
    
    return BestAction;
  };
	
	// The constructor takes an instance of the Stochastic Environment and 
	// initialises the number of actions
	
	RewardEstimator( const StochasticEnvironment & TheEnvironment )
	: NumberOfActions( TheEnvironment.NumberOfActions )
	{ }
	
	// All initialisation must use this constructor and so the default constructor
	// is deleted
	
	RewardEstimator( void ) = delete;
};

/*******************************************************************************
 Maximum Likelihood Estimator
 
 This is the conceptually simplest estimator that simply maintains two 
 vectors with as many elements as there are actions. The first vector counts
 the number of times the corresponding action has been tried, and the second
 adds up the total reward for that action. The estimate is then returned as a
 double (always).
 
*******************************************************************************/

template< class StochasticEnvironment >
class MLE : public RewardEstimator< StochasticEnvironment, double >
{
public:
	
	// The standard definitions of environment and actions
	
	using Environment = StochasticEnvironment;
	using RewardEstimator< StochasticEnvironment, double >::NumberOfActions;
	
private:
  
  // The count for the number of times an action has been tried. 
  
  std::vector < unsigned long int > TriedCount;
  
  // There is also a vector to accumulate the rewards given for each action
  
  std::vector < typename Environment::ResponseType > AccumulatedReward;
  
public:
  
  // The constructor takes the an instance of the stochastic environment and 
	// passes this on to the Reward Estimator base class. Then the counters are 
	// initialised to zero
  
  MLE ( const StochasticEnvironment & TheEnvironment )
  : RewardEstimator< Environment, double >( TheEnvironment ),
    TriedCount( NumberOfActions, 0 ),
    AccumulatedReward( NumberOfActions, 
								       static_cast< typename Environment::ResponseType >(0) )
    {};
		
	// The default constructor is not to be used
		
	MLE( void ) = delete;
    
  // The update function simply increments the number an action has been tried
  // and accumulates the rewards received for that action.
    
  virtual 
  void Update ( const typename Environment::Response & Response ) override
  {
    TriedCount.at( Response.ChosenAction )++;
    AccumulatedReward.at( Response.ChosenAction ) += Response.Feedback;
  };
  
  // The estimator is simply the ratio of the count and the total reward
  // after casting both values to double. Not that we have to check not to 
  // divide by zero. If an action has not been tried its accumulated reward
  // must also be zero so we have a "0/0" situation, which is here interpreted
  // as zero.
  
  virtual double RewardEstimate( ActionIndex TheAction ) override
  {
    if ( TriedCount.at( TheAction ) > 0 )
    	return static_cast< double >( AccumulatedReward.at( TheAction ) ) /
						 static_cast< double >( TriedCount.at( TheAction ) );
    else
      	return 0.0;
  };
			  
};

/*******************************************************************************
 Relative Reward Estimator
 
 The Maximum Likelihood estimator gives the expected reward per action, 
 however it does not say anything about the probability of a given action to 
 receive a reward: Consider for example an action that rarely gets rewarded,
 but when it receives a reward it is a big reward. Thus its reward per try
 ratio will make it a good action, although the probability for being 
 rewarded is low.
 
 One can therefore argue that in order to estimate the best action one should
 rather look at the ratio of the total reward received for this action to the
 total reward received for all actions. Consequently, an action that constantly
 receives many small rewards could be ranked as a better action to try than 
 the one receiving infrequently big rewards. After all, one is normally trying
 to select the action that will maximise the future reward.
 
 That said, the above per-action MLE is used frequently with the P-model where
 there are no "big or small" rewards, only a reward or nothing. In this case
 the reward estimate of the MLE corresponds to the probability of being 
 rewarded; and as such it should be equivalent to the relative reward estimator.
 
 The relative reward estimator implemented in the following has the property
 that the sum over all estimates is unity, and therefore the estimates can be
 interpreted as the reward estimate for a given action.
 
*******************************************************************************/

template< class StochasticEnvironment >
class RelativeReward 
: virtual public RewardEstimator< StochasticEnvironment, double >
{
public:
	
	using Environment = StochasticEnvironment;
	using RewardEstimator< StochasticEnvironment, double >::NumberOfActions;

private:
  
  // The total reward received for each action is accumulated in a vector
  // as for the maximum likelihood estimator
  
  std::vector < typename Environment::ResponseType > AccumulatedReward;
  
  // Instead of adding together this vector for each estimate we need to 
  // report, we keep a variable accumulating the total reward for all 
  // actions.
  
  typename Environment::ResponseType TotalReward;
  
public:
  
  // The constructor is simpler than for the MLE since there is only one 
  // vector to initialise.
  
  RelativeReward( const StochasticEnvironment & TheEnvironment )
  : RewardEstimator< Environment, double >( TheEnvironment ),
    AccumulatedReward( NumberOfActions, 
											 static_cast< typename Environment::ResponseType >(0) )
  {
    TotalReward = static_cast< typename Environment::ResponseType >(0);
  };
  
	// The default constructor should not be used
	
	RelativeReward( void ) = delete;
	
  // The updating function simply accumulates the per action reward and the
  // total reward
  
  virtual 
  void Update ( const typename Environment::Response & Response ) override
  {
    AccumulatedReward.at( Response.ChosenAction ) += Response.Feedback;
    TotalReward += Response.Feedback;
  };
 
  // Producing the reward probability estimates is now straight forward,
  // although we need to handle the initial situation that absolutely no
  // reward has been received as a special case to test for.
  
  virtual 
  double RewardEstimate( ActionIndex TheAction )
  {
    if ( TotalReward > 0 )
      return static_cast< double >( AccumulatedReward.at( TheAction ) ) /
				     static_cast< double >( TotalReward );
    else
      return 0.0;
  };
  
};

/*******************************************************************************
 Exponentially Weighted Moving Average
 
 The previous estimators should work well in situations where the environment
 is stationary, i.e. its action probability vector does not change over time.
 If the environment is non-stationary, and estimator will exhibit inertia with 
 many samples needed in order to correctly reflect the new reward probability
 vector. Two strategies have normally been proposed to overcome this situation:
 one can either make the probabilty estimates based on a "window" of the 
 past N observations (for some value N), or one can downscale old values such
 that new values weights more when computing the estimates. This latter 
 strategy is known as Exponentially Weighted Moving Average (EWMA). 
 
 The estimator works analogous to the MLE above were it tries to estimate the
 average reward obtained from selecting a particular action. It can be
 expressed as follows where r(k) is the current estimate for the reward for 
 a given action, with a scale factor 0 < lambda < 1:
 
 r(k) = r(k-1) + lambda * ( Reward(k) - r(k-1) )
      = (1-lambda)r(k-1) + lambda * Reward(k)
 
 so if lambda is close to one, we will quickly forget old samples, and if 
 lambda is close to zero a new reward value will hardly be taken into 
 account. In order to be be sensitive to small shifts in the underlying reward
 probabilities, a small lambda must be used, and in order to react quickly to 
 large shifts in the probabilities, a large lambda must be used. This fact led
 Capizzi and Masarotto [1] to propose an Adaptive Exponentially Weigthed 
 Moving Average (AEWMA) - essentially they proposed to replace lambda in 
 the first equation with a "function of the error" and rewrite it to 
 
 r(k) = r(k-1) + Phi( Reward(k) - r(k-1) )
 
 The requirements on this function is 
 1) It should be monotonicly increasing in the error
 2) It should be negative symmetric ( Phi(e) = -Phi(-e) )
 3) It should be zero for a zero error
 
 It should be noted that if we set
 
 lambda = Phi(Reward(k) - r(k-1)) / (Reward(k) - r(k-1))
 
 In the last equation we get the AEWMA form of the error function update, and
 so the AEWMA can be seen as an update of the weighting factor. 
 
 The below implementation is a generalisation of this idea. The estimator
 takes a functor whose () operator takes three parameters: The index of the 
 action for which lambda is wanted, the current value of the estimate, and the
 the current reward. The action index is needed to allow the general case 
 where there are different evaluations used for each action.
 
 One final note: Arguably the situation is multivariate and a multivariate 
 estimator should be used. However, the multivariate extension of the EWMA 
 proposed by Lowry et al. [2] uses a diagonal weighting matrix, thus the 
 individual estimates are updated individually. On the other hand, Lowry et 
 al. show how the estimate vector can be used to detect deviations from the 
 expected mean vector, which can be useful in some applications.
 
 REFERENCES:
 
 [1] Giovanna Capizzi and Guido Masarotto (2003): "An Adaptive Exponentially 
     Weighted Moving Average Control Chart", Technometrics, Vol. 45, 
     No. 3, pp. 199--207, August, 2003
 
 [2] Cynthia A. Lowry and William H. Woodall and Charles W. Champ and 
     Steven E. Rigdon (1992): "A Multivariate Exponentially Weighted Moving 
     Average Control Chart", Technometrics, Vol. 34, No. 1, pp. 46-53, 
     February 1992
  
*******************************************************************************/

// The first lambda function is the constant function that produces a 
// constant oblivion factor based on the value given at construction time. 
// The various oblivion factors all require the number of actions as the first 
// parameter to their constructor even though it may not be used in most cases
// because this allows a unique interface to all oblivion factors in the 
// estimator. 

class ConstantOblivion
{
protected:
  
  double OblivionFactor;
  
public:
  
  ConstantOblivion( ActionIndex NumberOfActions, double lambda )
  {
    if ( (lambda > 0) && (lambda < 1) )
      OblivionFactor = lambda;
    else
      throw std::invalid_argument("Oblivion factor out of range");
  };
  
  virtual double operator() ( ActionIndex Action, 
															double CurrentRewardEstimate, double RewardValue )
  { return OblivionFactor; };
  
};

// The next function is the one that Capizzi and Masarotto attributes to the
// book of P. J. Huber (1981), Robust Statistics, New York: Wiley, and it is
// therefore named accordingly

class HuberOblivion : public ConstantOblivion
{
protected:
  
  double MaxError;
  
public:
  
  HuberOblivion( ActionIndex NumberOfActions, 
								 double lambda, double MaxAbsoluteError ) 
  : ConstantOblivion( NumberOfActions, lambda )
  {  MaxError = MaxAbsoluteError; };
  
  virtual 
  double operator() ( ActionIndex Action, double CurrentRewardEstimate, 
											double RewardValue ) override
  {
    double Error = RewardValue - CurrentRewardEstimate;
    
    if ( Error == 0.0 )
      return 0.0;
    else  if ( Error < -MaxError )
      return ( Error + (1.0 - OblivionFactor) * MaxError ) / Error;
    else if ( Error > MaxError )
      return ( Error - (1.0 - OblivionFactor) * MaxError ) / Error;
    else
      return OblivionFactor;
  };
};

// The bi-square function is, according to Capizzi and Masarotto based on 
// Tukey's bi-square function, see A. E. Beaton and J. W. Tukey (1974) "The 
// Fitting of Power Series, Meaning Polynomials, Illustrated on 
// Band-Spectroscopic Data," Technometrics, Vol. 16, pp. 147-185.

class BiSquare : public HuberOblivion
{
public:
  
    BiSquare( ActionIndex NumberOfActions, 
							double lambda, double MaxAbsoluteError )
    : HuberOblivion( NumberOfActions, lambda, MaxAbsoluteError )
    {};
 
    virtual 
    double operator() ( ActionIndex Action, double CurrentRewardEstimate,
								        double RewardValue ) override
    {
      double Error = RewardValue - CurrentRewardEstimate;
      
      if ( std::abs( Error ) < MaxError )
				return 1.0 - (1.0 - OblivionFactor) *
								std::pow( 1.0 - std::pow( Error / MaxError, 2), 2);
      else
				return 1.0;
    };
};

// The Adaptive Exponential Smoothing Method (AESM) was proposed by Sotiris N. 
// Pantazopoulos and Costas P. Pappis (1996): "A new adaptive method 
// for extrapolative forecasting algorithms", European Journal of 
// Operational Research, Vol. 94, No. 1, pp. 106-111, October 1996. It is 
// based on adjusting the oblivion factor according to the relative error 
// of the last two observations. It is therefore necessary to store an
// oblivion factor for each action, as well as the last reward received for
// this action and the previous estimate. 

class AESM
{
private:
  
  class AESMData
  {
  public:
    
    double OblivionFactor,
           Reward,
				   Estimate;
	   
    AESMData( double lambda, double beta, double hat )
    {
      OblivionFactor = lambda;
      Reward	     = beta;
      Estimate 	     = hat;
    };
  };
  
  std::vector < AESMData > History;
  
public:
  
  // The constructor simply takes the number of actions as parameter and
  // initialises the last oblivion factor and reward to zero for all actions.
  
  AESM( ActionIndex NumberOfActions )
  : History( NumberOfActions, AESMData( 0.0, 0.0, 0.0 ))
  {};
  
  // The update of the oblivion factor is based on the relative error of
  // the previous reward to the existing estimate. The at function is used in 
	// the first access to the history vector to ensure that it is within the 
	// legal range.
  
  double operator() ( ActionIndex Action, 
											double CurrentRewardEstimate, double RewardValue )
  {
    double OldError = History.at( Action ).Reward - History[ Action ].Estimate;
    
    if ( OldError != 0.0 )
    {
      History[ Action ].OblivionFactor = 
		      std::abs( ( RewardValue - History[ Action ].Estimate ) / OldError );
					
      if ( History[ Action ].OblivionFactor > 1.0 )
					 History[ Action ].OblivionFactor = 1.0;
    }
      
    History[ Action ].Reward   = RewardValue;
    History[ Action ].Estimate = CurrentRewardEstimate;
    
    return History[ Action ].OblivionFactor;
  };
  
};

// The Exponentially Weighted Moving Average class takes the type of 
// lambda generator as template parameter, and passes on to that 
// generator all other parameters given to the constructor. The only thing
// it needs is the number of actions in order to set up the estimator array.

template< class StochasticEnvironment, class OblivionGenerator >
class EWMA 
: virtual public RewardEstimator< StochasticEnvironment, double >
{
private:
  
  OblivionGenerator OblivionFactor;
  
protected:
  
  std::vector< double > Estimate;
  
public:
  
	// The standard definitions
	
	using Environment = StochasticEnvironment;
	using RewardEstimator< StochasticEnvironment, double >::NumberOfActions;

  // The constructor must know the number of actions and then it passes the 
  // other arguments to the oblivion factor generator.
  
  template< typename... OblivionGeneratorTypes >
  EWMA( const StochasticEnvironment & TheEnvironment, 
				OblivionGeneratorTypes... GeneratorArguments )
  : RewardEstimator< Environment, double >( TheEnvironment ),
    OblivionFactor( NumberOfActions, GeneratorArguments... ),
    Estimate( NumberOfActions, 0.0 )
    {};
		
	// The default constructor should not be used
		
	EWMA( void ) = delete;
    
  // The update function generates the lambda and updates the current 
  // reward estimate.
    
  virtual 
  void Update ( const typename Environment::Response & Response ) override
  {
    double lambda = OblivionFactor( Response.ChosenAction, 
																		Estimate.at( Response.ChosenAction ),
				       static_cast<double>( Response.Feedback ) );
    
    Estimate[ Response.ChosenAction ] = 
	    (1.0-lambda) * Estimate[ Response.ChosenAction ] 
		    + lambda * Response.Feedback;
  };

  // Since the estimates are stored there is no computation needed when the 
  // estimate is used.

  virtual 
  double RewardEstimate( ActionIndex TheAction ) override
  { return Estimate.at( TheAction );  };
  
};

/*******************************************************************************
 Relative Exponentially Weighted Moving Average
 
 This is an estimator derived from the EWMA estimator with two changes: 
 	1) It keeps a global total reward, and 
 	2) it make the reward estimate relative to this total reward. 
 It is then possible to interpret the estimates as probabilities and use it 
 as an automata, which was done by McMurty and Fu [4] in the paper defining 
 the VSSA automata as a probability vector. 
 
 [3] G. McMurtry and K. S. Fu (1966): "A variable structure automaton used 
     as a multimodal searching technique", IEEE Transactions on Automatic 
     Control, Vol. AC-11, No. 3, pp. 379-387, July 1966

*******************************************************************************/

template< class StochasticEnvironment, class OblivionGenerator >
class RelativeEWMA 
: virtual public RewardEstimator< StochasticEnvironment, double >,
  virtual public EWMA< StochasticEnvironment, OblivionGenerator >
{
public:
	
	// The standard definitions of the environment and the number of actions

	using Environment = StochasticEnvironment;
	using RewardEstimator< StochasticEnvironment, double >::NumberOfActions;
	
private:
  
	// There is a shorthand for the base class
	
	using EWMA_Base = EWMA<Environment, OblivionGenerator>;
	
  // Given that the moving average discount old observations, the total 
  // reward can not be kept as a cumulative sum of all the rewards received
  // until now. It is therefore necessary to recompute the total reward 
  // as the sum of the individual reward estimates at the time the estimate
  // is requested.
  
  typename Environment::ResponseType TotalReward;
  
public:
  
  // The update simply invalidates the total reward and let the EWMA update
  // as normally.
  
  virtual 
  void Update ( const typename Environment::Response & Response ) override
  {
    TotalReward = static_cast< typename Environment::ResponseType >(0);
    EWMA_Base::Update( Response.ChosenAction, Response.Feedback );
  };
  
  // The estimate function will recompute the total reward if there has 
  // been an update of the estimate vector since the last time an estimate 
  // was requested, otherwise it will simply reuse the available value in 
  // the computation of the estimate.
  
  virtual 
  double RewardEstimate( ActionIndex TheAction ) override
  {
    if ( TotalReward == static_cast< typename Environment::ResponseType >(0) )
      TotalReward = std::accumulate(  
      	EWMA_Base::Estimate.begin(), EWMA_Base::Estimate.end(),
				static_cast< typename Environment::ResponseType >(0)
      );
    
    return EWMA_Base::Estimate.at( TheAction ) / TotalReward;
  };
  
  // The constructor initialises the total reward and the base class, 
  // taking into account that the oblivion generator may need additional
  // arguments.

  template< typename... OblivionGeneratorTypes >
  RelativeEWMA( const StochasticEnvironment & TheEnvironment,
								OblivionGeneratorTypes... GeneratorArguments )
  : RewardEstimator< StochasticEnvironment, double >( TheEnvironment ),
    EWMA_Base( TheEnvironment, GeneratorArguments... )
  { TotalReward = static_cast< typename Environment::ResponseType >(0);  };
  
	// The default constructor should not be used
	
	RelativeEWMA( void ) = delete;
};

/*******************************************************************************
 Stochastic Learning Weak Estimator
 
 The Stochastic Learning Weak Estimator (SLWE) was proposed by Oommen and 
 Rueda [3] for estimation of relative occurrences of a set of observations 
 when the underlying distribution is time variate. This can be understood 
 as the reward probabilities of under the P model where each action tried 
 can result in either a penalty or a reward if one assumes that a reward 
 corresponds to the "observation" of the associated action. The estimator will
 give weak estimates for the relative occurrences of the "observations", thus
 estimate the reward probabilities of the actions.
 
 The update of the estimate, r(k), for the observed action with index "a" is 
 given by Oommen and Rueda based on the oblivion factor lambda as
 
 r_a(k+1) = r_a(k) + (1-lambda) sum( r_j(k), j!=a )
 
 however since the sum over all estimates is unity in this scheme, the 
 sum( r_j(k), j!=a ) = (1 - r_a(k)) so 
 
 r_a(k+1) = r_a(k) + (1-lambda) * (1-r_a(k)) 
          = r_a(k) + (1-r_a(k)) - lambda * (1-r_a(k))
          = 1 - lambda * (1-r_a(k))
          = (1 - lambda) + lambda * r_a(k)
      
 Which is exactly the Linear Reward-Inaction automata, see LinearLA.hpp.
 Furthermore, also the update for the actions not observed is identical, 
 see Oommen and Rueda, Equation (30). The SLWE is therefore implemented in
 terms of the P-model Linear Reward-Inaction automata. Note that it exists
 only as a specialisation for this type.
 
 REFERENCES
 
 [3] B. John Oommen and Luis Rueda (2006): "Stochastic Learning-based Weak 
 Estimation of Multinomial Random Variables and Its Applications to Pattern 
 Recognition in Non-stationary Environments", Pattern Recognition, 
 vol. 39, pp. 328 – 341
 
*******************************************************************************/

template<  class StochasticEnvironment > 
class SLWE 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >,
  virtual public LinearRI< StochasticEnvironment, Model::P >, 
  virtual public RewardEstimator < StochasticEnvironment, double >
{
public:
	
	// The automata is only defined for P-model environments
	
	static_assert( StochasticEnvironment::Type != Model::P, 
								 "The SLWE is only defined for P-Model environments" );
  
	// The standard definitions for automata are given first
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< StochasticEnvironment >::NumberOfActions;

private:
	
	// A shorthand definition for the Linear RI automata used
	
	using LRI = LinearRI< Environment, Model::P >;
	
  // The update now simply translates to a reward for the tried action.
  // In order to make sure that the update is correct we ignore the reward
  // given and force a P-model reward.
  
public:
	
  virtual 
  void Update ( const typename Environment::Response & Response ) override
  {
    LRI::Feedback( Response );
  };
  
  // The reward estimate is simply the current action probability of the 
  // L_RI automaton
  
  virtual 
  double RewardEstimate( ActionIndex TheAction ) override
  {
    return LRI::ActionProbabilities.at( TheAction );
  };
  
  // Initially all estimated probabilities will be set to 1/r where r is the
  // number of actions. Actions that have no rewards will be downscaled with
  // a fraction called the oblivion factor (lambda) for each iteration. 
  // Thus after k iterations the probability will be
  //		lambda^k * (1/r)
  // Although the user can use any value of lambda, the default value can be
  // set by assuming that we should do at least r iterations before the 
  // resulting probability has been reduced by 95%. This corresponds to 
  // solving
  //		lambda^r * (1/r) = 0.05*(1/r)
  //      		lambda^r = 0.05
  //	    	  r * ln(lambda) = ln(0.05)
  //			  lambda = e^(ln(0.05)/r)
  // The following function implements this equation and is used to set the 
  // default lambda value in the constructor below.
  //
  // Note that the VSSA constructor must be explicitly called here since it
  // is a virtual base for the L_RI class, and the LRI constructor is given a 
	// legal, but useless reward constant as the real value will be calculated 
	// in the constructor body.
  
  SLWE ( const StochasticEnvironment & TheEnvironment, 
				 double OblivionFactor = 0.0  )
  : LearningAutomata< StochasticEnvironment >( TheEnvironment ),
    VSSA< Environment >( TheEnvironment ), LRI( TheEnvironment, 0.5 ),
    RewardEstimator< Environment, double>( TheEnvironment )
  {
    // An oblivion  factor of 0.0 indicates that the default value of the
    // oblivion factor should be computed, otherwise we check to see if it 
    // is a valid learning constant.
    
    if ( OblivionFactor == 0.0 )
      LRI::RewardConstant = 
	      exp( log(0.05) / static_cast<double>( NumberOfActions ) );
    else if ( (OblivionFactor > 0.0) && (OblivionFactor < 1.0) )
      LRI::RewardConstant = OblivionFactor;
    else
      throw std::invalid_argument("SLWE: Illegal oblivion factor");
  };
	
	// The default constructor should not be used
	
	SLWE( void ) = delete;
};

}      // Name space LA
#endif // REWARD_ESTIMATORS
