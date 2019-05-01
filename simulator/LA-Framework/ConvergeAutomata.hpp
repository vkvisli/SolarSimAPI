/*=============================================================================
 Converge Automata
 
 This encapsulation allows an automaton to run until it has converged, i.e. its
 maximum probability exceeds a given value. Since this applies to various 
 automata types, but the logic is the same, it is defined as a template. For
 this reason it is necessary that the given automata type is based on a virtual 
 VSSA automata base class, and the compilation of the below template will
 fail with a message that VSSA is not a base class... if it is called with an
 automata type that is not based on the VSSA.
 
 The template takes two parameters: The type of the environment and the 
 VSSA automata class that does the job and must be instantiated on the same 
 environment type.
 
 This class should be enhanced once the convergence theory has developed in 
 order to figure out convergence without testing against a simple threshold.
  
 Author: Geir Horn, 2013-2017
 License: LGPL3.0
 Rewritten: Geir Horn 2017 - Environment defines the model type and actions
=============================================================================*/

#ifndef CONVERGE_AUTOMATA
#define CONVERGE_AUTOMATA

#include <type_traits>                 // For testing types at compile time

#include "ProbabilityMass.hpp"         // For testing probabilities
#include "LearningEnvironment.hpp"
#include "LearningAutomata.hpp"

namespace LA
{
/*==============================================================================

 Common base class

==============================================================================*/
//
// -----------------------------------------------------------------------------
// Template definition
// -----------------------------------------------------------------------------
//
// The basic class defines a template that must be specialised based on how 
// the convergence is recorded. It can be recorded as a number of iterations
// or it can be recorded in absolute time as the derived classes implements. 
// This is decided by the third parameter to the template

template< class StochasticEnvironment, class AutomataClass, 
					typename TimeType, typename Enable = void >
class ConvergeAutomata;

// -----------------------------------------------------------------------------
// Base class specialisation
// -----------------------------------------------------------------------------
//
// The base class is a specialisation of the convergence automata template 
// where the time type is given as void.

template< class StochasticEnvironment, class AutomataClass >
class ConvergeAutomata< StochasticEnvironment, AutomataClass, void > 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >,
	public AutomataClass
{
public:
	
	static_assert( std::is_base_of< VSSA< StochasticEnvironment >, 
																  AutomataClass >::value,
								 "Converge automata must encapsulate a VSSA automata" );
	
	// The standard definition of environment
	
	using Environment = StochasticEnvironment;
	
protected:
  
	// Then the definitions for the VSSA base class
	
	using VSSA_Base = VSSA< Environment >;

private:
	
  // There is a flag indicating if the automata has converged or not 
  
  bool Converged;
  
	// The actual convergence is checked with a function that can be overloaded
	// to implement more elaborate decisions. 
	
protected:
	
	virtual bool CheckConvergence( void ) = 0;
	
public:
  
	// Convergence can be tested by an external entity by the following function
	
	inline bool HasConverged( void )
	{ return Converged; }
	
  // The feedback function will always be called even if the events for 
  // this automata are set from an external master. Convergence is taken as
  // the "first passage time" for a probability with respect to the threshold,
  // and to avoid oscillations with respect to this decision, further updates
  // of the probability will be blocked.
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    if ( !Converged )
    {
      // Do the normal update expected by the automata
			
      AutomataClass::Feedback( Response );

			// The convergence is detected. It is up to the derived class to decide
			// what to do with this decision.
			
      Converged = CheckConvergence();
    }
  };
  
  // The constructor basically takes the learning environment, the threshold, 
  // and the set of other parameters expected by the base class (in order)
  // Note that we have to explicitly instantiate the virtual base classes as 
  // this should be virtual in the automata class to ensure that only one 
  // probability vector will be instantiated. Note that this constructor is 
	// protected to prevent direct construction of this base class.
  
protected:
	
  template< typename... BaseClassTypes >
  ConvergeAutomata ( const Environment & TheEnvironment,
								     BaseClassTypes... BaseArguments) 
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment ),
    AutomataClass( TheEnvironment, BaseArguments... )
  {
    Converged = false;
  };

public:
	
	// The destructor is virtual to allow the correct deconstruction of the 
	// virtual base classes although it does not need to do anything 
	
	virtual ~ConvergeAutomata( void )
	{ }
	
};

/*==============================================================================

 Probability limit

==============================================================================*/
//
// The simplest specialisation is the one that takes a threshold and concludes 
// that the automata has converged when this threshold is exceeded by the 
// largest probability of the action probability vector.

template< class StochasticEnvironment, class AutomataClass, typename RealType >
class ConvergeAutomata< StochasticEnvironment, AutomataClass, 
												Probability< RealType > >
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >,
  public ConvergeAutomata< StochasticEnvironment, AutomataClass, void >
{
public:
	
	// The standard definition of environment
	
	using Environment = StochasticEnvironment;
	
protected:
  
	// Then the definitions for the VSSA base class
	
	using VSSA_Base = VSSA< Environment >;
	
private:
	
  // The variable to store the threshold of convergence given as the 
  // second parameter to the constructor. 
  
  Probability< RealType > ConvergenceThreshold;  

protected:

	virtual bool CheckConvergence( void ) override
	{
		return VSSA_Base::BestAction().second >= ConvergenceThreshold;
	}
	
	// The constructor takes the environment instance and  passes it on to 
	// the base classes. It also takes the arguments expected after the 
	// environment by the encapsulated automata.
	
public:
	
  template< typename... BaseClassTypes >
  ConvergeAutomata ( const Environment & TheEnvironment,
										 Probability< RealType > Threshold,
								     BaseClassTypes... BaseArguments) 
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment ),
    ConvergeAutomata< StochasticEnvironment, AutomataClass, void >( 
	    TheEnvironment, BaseArguments... ),
	  ConvergenceThreshold( Threshold )
  { }
	
	// The virtual destructor will ensure that the virtual base classes are 
	// properly destroyed
	
	virtual ~ConvergeAutomata( void )
	{ }
	
};

/*==============================================================================

 Iteration counter

==============================================================================*/
//
// If the time type template argument is given as an integral type, the number
// of feedback updates will be counted until the automata is marked as converged

template< class StochasticEnvironment, class AutomataClass, 
					typename TimeType >
class ConvergeAutomata< StochasticEnvironment, AutomataClass, TimeType, 
		  typename std::enable_if< std::is_integral< TimeType >::value >::type > 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >,
	public ConvergeAutomata< StochasticEnvironment, AutomataClass, void >
{
public:
	
	// The standard definitions of environment and number of actions.
	
	using Environment = StochasticEnvironment;
	
protected:
  
	// Then the definitions for the VSSA base class and its action probabilities 
	// are given for convenience
	
	using VSSA_Base = VSSA< Environment >;

private:

	// The class maintains a single counter, and the threshold for the number of
	// iterations.
	
	TimeType IterationLimit, 
					 IterationCounter;
	
protected:
	
	// This counter is updated by the check convergence function and checked 
	// against the limit.
	
	virtual bool CheckConvergence( void ) override
	{ 
		return ++IterationCounter >= IterationLimit;
	}
	
public:
	
	// The constructor simply needs to initialise the base classes, store the 
	// threshold and reset the counter
	
  template< typename... BaseClassTypes >
  ConvergeAutomata ( const Environment & TheEnvironment,
										 TimeType Limit,
								     BaseClassTypes... BaseArguments) 
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment ),
    ConvergeAutomata< StochasticEnvironment, AutomataClass, void >( 
	    TheEnvironment, BaseArguments... ),
	  IterationLimit( Limit ), IterationCounter( 0 )
  { }

  // The destructor does nothing but is a place holder to ensure the correct 
  // destruction of the base classes
  
  virtual ~ConvergeAutomata( void )
	{ }
};

}      // name space LA 
#endif // CONVERGE_AUTOMATA
