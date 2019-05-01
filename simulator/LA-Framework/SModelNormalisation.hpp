/*=============================================================================
 S-Model Normalisation
 
 The S-model automata defined as part of this framework assumes that the 
 feedback is a real number in the unit interval [0,1]. Many applications 
 do not ensure this, and the actual application response to an action must be
 scaled or normalised to the interval [0,1]. This file discusses ways to 
 do this, based on learning automata theory, and the provided classes 
 should be used to convert the application response to the S-model feedback.
   
 Author: Geir Horn, 2013 - 2017
 Lisence: LGPL3.0
=============================================================================*/

#ifndef S_MODEL_NORMALISATION
#define S_MODEL_NORMALISATION

#include <stdexcept>
#include <limits>

#include "RandomGenerator.hpp"
#include "LearningEnvironment.hpp"

namespace LA
{
/******************************************************************************
 Basic Normalisation
 
 If one knows the maximum and minimum response possible the normalisation is 
 just to divide the given response on the length of this response interval. 
 The normalisation class simply stores the bounds and then uses these in the 
 normalisation.
 
 This basic normalisation is shown by Viswanathan and Narendra [1] to maintain
 the optimality of P-model automata when they are extended to the S-model by
 their method. This normalisation is therefore preferred for the S-model Linear 
 Reward-Inaction automata, see LinearLA.h
 
 REFERENCE:
 
 [1] R. Viswanathan and Kumpati S. Narendra (1973): "Stochastic Automata 
 Models with Applications to Learning Systems", IEEE Transactions on 
 Systems, Man and Cybernetics, Vol. SMC-3, No. 1, pp. 107-111, 
 January 1973
 
******************************************************************************/

// The normalisation is based on the response type offered by an S model 
// environment, and hence this must be given.

template < class StochasticEnvironment >
class BasicNormalisation
{
public:
	
	static_assert( StochasticEnvironment::Type != Model::S, 
								 "Normalisation requires an S-Model environment" );
	
	// The shorthand for the environment is defined
	
	using Environment = StochasticEnvironment;
	
protected:
  
  typename Environment::ResponseType MaxValue, MinValue;
  
public:
  
  // The constructor simply saves the bounds
  
  BasicNormalisation( typename Environment::ResponseType LowerBound,
								      typename Environment::ResponseType UpperBound  )
	: MinValue( LowerBound ), MaxValue( UpperBound )
  { };
  
	BasicNormalisation( void ) = delete;
	
  // The actual normalisation is done by the functor operator based on
  // the given response. It is resilient against the singular case where
  // the both bounds are equal and the interval has zero length. In this
  // case a division by zero will occur, but at the same time the response
  // has to be equal to the bounds, i.e. one could also interpret this as a
  // normalisation to unity. In this implementation the response is simply
  // random.
  
  virtual typename Environment::ResponseType 
  operator () ( typename Environment::ResponseType TheResponse )
  {
    if ( (MinValue <= TheResponse) && (TheResponse <= MaxValue) )
    {
      typename Environment::ResponseType IntervalLength = MaxValue - MinValue;
      
      if ( IntervalLength != 0.0 )
				return TheResponse / IntervalLength;
      else
				return Random::Number();
    }
    else
      throw std::out_of_range("Response out of normalisation bounds");
  };
};

/******************************************************************************
 Dynamic Normalisation

 If the bounds are not known a priori, Viswanathan and Narendra [1] 
 conjectured that setting the bounds dynamically based on the actual 
 responses would lead to an asymptotically optimal automata. This claim
 was supported by simulations, but is difficult to assert theoretically.
 
******************************************************************************/

template < class StochasticEnvironment >
class DynamicNormalisation : public BasicNormalisation< StochasticEnvironment >
{
private:
	
	// The short hand for the normalisation is defined
	
	using StaticNormalisation = BasicNormalisation< StochasticEnvironment >;
	
public:
  
	// The shorthand for the environment is defined
	
	using Environment = StochasticEnvironment;
	
  // It is a problem that the first two responses are needed in order to
  // set the bounds correctly. This is approached by allowing both the 
  // maximum and the minimum value to be set upon first response. Although 
  // this creates an interval of zero length, the basic normalisation will 
  // be robust against this and simply return a random number over the unit 
  // interval.
  
  virtual typename Environment::ResponseType 
  operator () ( typename Environment::ResponseType TheResponse ) override
  {
    if ( TheResponse > StaticNormalisation::MaxValue )
      StaticNormalisation::MaxValue = TheResponse;
    
    if ( TheResponse < StaticNormalisation::MinValue )
      StaticNormalisation::MinValue = TheResponse;
    
    return StaticNormalisation::operator()( TheResponse );
  };
  
  // The constructor simply sets both bounds such that they will be certainly
  // updated by the first response. This means that the lower bound will be 
  // set to the maximum value of response type, and the upper bound to the 
  // minimum value of the response type.
  
  DynamicNormalisation( void ) 
  : StaticNormalisation(
    std::numeric_limits< typename Environment::ResponseType >::max(), 
    std::numeric_limits< typename Environment::ResponseType >::lowest() )
  {};
};

}      // Name space LA  
#endif // S_MODEL_NORMALISATION
