/*=============================================================================
Learning Automata

A learning automaton is a state machine that has probabilities for 
transferring from one state to the next. In a Fixed Structure Stochastic 
Automaton these probabilities are fixed, and in a Variable Structure Automaton
the transition probabilities are updated based on the feedback.

The automaton interacts with an environment proposing an action to the 
environment, and then it receives a feedback from the environment. Based on the
feedback the automaton will move to the next state, and potentially also update 
the action probabilities. This feedback can either be taken from a set of 
possible feedbacks, or it can be a value in the range [0,1]. Please see the
Automata Type header file for available types.

Author: Geir Horn, 2013 - 2017
Contact: Geir.Horn [at] mn.uio.no
License: LGPL3.0

Revision: Geir Horn, 2015 - Separated the Automata and actor concept so that
          they can be used independently with the Automata Actor as a wrapper
          around any valid automata class.
          Geir Horn, 2016 - Introduced the LA name space
          Geir Horn, 2017 - New environment definitions
=============================================================================*/

#ifndef LEARNING_AUTOMATA
#define LEARNING_AUTOMATA

#include <string>         // Standard strings
#include <vector>         // To keep probability values
#include <numeric>        // To accumulate probability values
#include <map>            // To map Markov chain states
#include <functional>     // For functional programming
#include <sstream>        // Readable error messages
#include <algorithm>      // Various standard algorithms
#include <iterator>       // To get distances of iterators

#include <armadillo>

#include "ProbabilityMass.hpp"
#include "RandomGenerator.hpp"
#include "LearningEnvironment.hpp"

namespace LA
{

/*****************************************************************************
 The Learning Automata

 Note that the automata model class given as an argument to the template is 
 the "bolt" on which everything hinges since it defines the types of the 
 expected feedback structures and the callback functions. The automata model 
 helps the compiler to ensure consistency through the class hierarchy as
 the top level class implementing the learning algorithm passes the 
 automata model to all base classes and eventually it is used here for the 
 learning automata base class. Please see the header Automata Type for more 
 details on the different automata models. 

******************************************************************************/

// Automata can either have a fixed structure or a variable structure
// and the structure of a given automaton can be tested using the 
// Structure variable.
  
enum class AutomataStructures
{
    Fixed,
    Variable,
    Unknown
};

// The base class for all automata types deals mainly with the generalised 
// interface to the learning automata class. It defines standard methods that
// other automata must provide, and ensures that all the automata in the 
// stack derived from this act as the same automata model type.

template < class StochasticEnvironment >
class LearningAutomata 
{
public:
	
	// The environment is defined first to make sure that derived automata knows
	// its properties.
	
	using Environment = StochasticEnvironment;
	
	// The structure can be tested using static_assert on the structure
  // parameter at compile time.

  static constexpr AutomataStructures Structure = AutomataStructures::Unknown;

  // The automaton maintains must maintain the number of actions available,  
  // and this must match the number of actions supported by the learning 
	// environment. It is therefore initialised by the constructor based on the 
	// instance of the learning environment. 

  const ActionIndex NumberOfActions;

		// Each environment type defines a special type of action object to ensure 
	// that automata returns the right type of action expected by the environment
	// type and to ensure that this is within the bounds expected by the given 
	// environment instance. The latter is necessary in the case there are more 
	// than one environment simultaneously responding to disjunct set of automata.
	// It is therefore an action generating function that must be set by the 
	// environment instance, and that is used for creating the return value 
	// after the automata has chosen an action by its index.
	
protected:
	
	const std::function< typename Environment::Action( const ActionIndex & ) >
	Action;

  // The actual actions are selected by the following function, which 
  // must be defined by a derived class. The default operation should be that
  // the returned action index is passed to the environment's Evaluate 
	// function. It is expected that the action index chosen can be used for 
	// action vector lookup, and consequently is an integral number in the set 
	// {0,...,N-1} for the N actions of the automata.
 
public:
	
  virtual typename Environment::Action SelectAction (void) = 0;
	
  // The learning automata must maintain a feedback function taking
  // care of the feedback from the environment. A typical behaviour is for 
	// the automaton to update its states or action probabilities in this 
	// function.

  virtual void Feedback ( const typename Environment::Response & Response ) = 0;
	
	// The constructor takes an instance of the environment as parameter and 
	// uses this to initialise the number of actions and the action generating 
	// function.
	
	LearningAutomata( const Environment & TheEnvironment )
	: NumberOfActions( TheEnvironment.NumberOfActions ),
	  Action( TheEnvironment.ActionGenerator() )
	{ }
	
	// The default constructor is explicitly deleted to ensure that the previous 
	// constructor will be used.
	
	LearningAutomata( void ) = delete;
	
	// Even though it is strictly not necessary, a virtual destructor is given 
	// to ensure that the derived classes will also use a virtual destructor
	
	virtual ~LearningAutomata( void )
	{ }
};

/*****************************************************************************
 The Variable Structure Stochastic Automaton (VSSA)
 
 This automata is characterised by having a vector of action probabilities,
 and the actions are selected according to this vector. After proposing an
 action the probabilities are updated based on the response from the 
 environment. The type of automaton is defined by the algorithm performing
 this update.
 
 Note also that the feedback functions must be defined by a derived class
 since this is where the actual update of the action probability vector 
 will take place.
  
*******************************************************************************/

template < class StochasticEnvironment >
class VSSA : virtual public LearningAutomata < StochasticEnvironment >
{
public:
	
	// The environment and number of actions must be made visible. This is a 
	// consequence of using constant expressions instead of an actual variable.
	
	using Environment = StochasticEnvironment;

protected:
  
  // The action probabilities are kept in a vector. One could think that it would
  // be natural to use the probability mass for this storage, however,  a 
  // probability mass will always keep its probabilities normalised (sum to 
  // unity). This is impractical when a learning algorithm may need to iterate
  // over the probabilities and changing them one after the other.
  
  std::vector < double > ActionProbabilities;
	
	// The action generating function of the Learning Automata is reused here
	
	using LearningAutomata < Environment >::Action;
  
public:

  // First indicate that this is a variable structure automata
  
  static constexpr AutomataStructures Structure = AutomataStructures::Variable;

	// The number of actions is a public parameter that is accessible for this 
	// class through the base class
	
	using LearningAutomata< Environment >::NumberOfActions;
	
  // One design criteria for this library is to separate structure from 
  // algorithms. As an example: the L_RP is a combination of the L_RI and
  // L_IP algorithms, but both working on the same probability space - but 
  // they can also be used independently. To avoid duplication of the structure
  // the base class defining the probability space, VSSA or FSSA, these
  // classes will in general be included as virtual. However, the problem is
  // that virtual base classes must be constructed before other derived 
  // classes. In order to remedy this, the structure is defined as a typedef
  // so that knowledge about the base structure is not necessary.

  using StructureType = VSSA< Environment >;
  
  // The actual action is selected according to the empirical probability
  // distribution given by the action probabilities and sent to the 
  // environment by the Select Action function. It should therefore not be
  // necessary to overload this function for derived classes.
  //  
  // The price paid for the having the action probabilities as a plain 
  // vector of doubles is that it must be converted to a probability mass 
  // before the random index method can be used. This because the selection 
  // is made according to the empirical density function, and it is mandatory 
  // that this is a proper probability mass.

  virtual typename Environment::Action SelectAction (void) override
  {
    return Action( Random::Index( EmpiricalPDF( ActionProbabilities ) ) );
	} 

  // There is a function to initialise the probabilities with a probability 
  // vector in case uniform initial probabilities are not desired. In order to
  // ensure that the probability vector fulfils the requirements of a 
  // probability vector, it is required to be a probability mass object 
  // that ensures proper normalisation (see the Probability mass header)
  // The size of the probability mass must match the number of actions, since 
  // the number of actions must correspond with the the environment.

  template< typename RealType, class Allocator >
  void InitialiseProbabilities( 
       const ProbabilityMass< RealType, Allocator > & NewProbabilities )
  {
		if ( NewProbabilities.size() == NumberOfActions )
	    ActionProbabilities.assign( NewProbabilities.cbegin(), 
	                                NewProbabilities.cend()    );
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Size of new action probability vector ("
									 << NewProbabilities.size() << " must equal the number of "
									 << "actions (" << NumberOfActions << ")";
									 
		  throw std::invalid_argument( ErrorMessage.str() );
		}
  }

  // There is also a function that gives visibility to the current status of 
  // the probability vector, and it is important that this is represented as
  // a proper probability mass.
  
  EmpiricalPDF GetProbabilities( void )
  {
    return EmpiricalPDF( ActionProbabilities );
  }
  
  // When evaluating convergence of an  automaton one will need to check the
  // value of the largest element of the probability vector to decide if it is
  // close "enough" to unity. Other times one would like to know which action 
  // is the best. This this function returns a pair of values where the first
  // value is the action index an the second value is its probability. Note
  // that if two or more actions have the same probability the first one in 
  // the vector is returned.
  
  std::pair< ActionIndex, double > BestAction (void)
  {
		auto MaxElement = std::max_element( ActionProbabilities.begin(), 
																				ActionProbabilities.end() );
    return { std::distance( ActionProbabilities.begin(), MaxElement ), 
						 *MaxElement };
  };
  
  // There is also an operator to print out the content of the vector, see
  // below the name space as this operator is not defined as a part of the 
  // name space.
  
  template< class EnvironmentType >
  friend std::ostream & operator<< (std::ostream & out, 
				    VSSA< EnvironmentType > & Automaton);
	
  // The standard constructor takes the environment instance and passes this 
  // on to the base class Learning Automata before setting the initial 
  // probabilities all equal to 1/r
  
  VSSA ( const Environment & TheEnvironment )
  : LearningAutomata< Environment >( TheEnvironment ),
    ActionProbabilities( NumberOfActions, 
												 1.0/static_cast<double>( NumberOfActions )  )
  {  };
  
  // There is also a constructor that takes an initial probability vector and 
  // uses this to initialise the action probabilities

  template< typename RealType, class Allocator >
  VSSA( const Environment & TheEnvironment,
		    const ProbabilityMass<RealType, Allocator> & GivenProbabilities )
  : LearningAutomata< Environment >( TheEnvironment ),
    ActionProbabilities(GivenProbabilities.cbegin(), GivenProbabilities.cend())
  { }
  
  // There is no standard constructor because it does not make sense to 
  // construct an automata without an environment.
  
  VSSA( void ) = delete;
	
	// Since it has virtual functions it needs a virtual constructor, even though
	// the standard destruction for the class members is sufficient.
	
	virtual ~VSSA( void )
	{ }
};

/******************************************************************************
 Fixed Structure Stochastic Automata
 
 The fixed structure automata have a given set of states and associated 
 transition probabilities among these states. Furthermore, the action 
 selected depends on the states of the automata, and typically a set of 
 states correspond to the one action.
 
 The states have two representations: One is the one given making sense for
 a particular automata and the other is an internal direct index making the 
 states consecutively numbered. The user defined reference must be sortable,
 i.e. it must have a "less than" operator allowing the states to be sorted.
 
 The matrix (linear algebra) template framework Armadillo [1,2] has been 
 chosen to represent the probability matrices for the following reasons:
 
 1) It has an interface compatible with the STL, and in particular support
    for iterator access to matrix elements. This makes it compatible with
    other many useful algorithms, and here in particular our random 
    generator, and
 2) It has been selected by the MLPACK [3], a library of machine learning 
    algorithms developed by the Fundamental Algorithmic and Statistical 
    Tools laboratory (FASTLab) at Georgia Tech. MLPACK has currently no
    support for Learning Automata, however the present framework could be
    contributed for inclusion at one point in time. It would then be 
    helpful if the matrix frameworks used are compatible.
    
 Armadillo is similar in functionality and implementation strategy to the
 Eigen [4] matrix library, which could have been used equivalently although
 it lacks the STL interface and it has issues with the lazy evaluation if 
 the same matrix appears on both sides of an assignment, i.e. 
 A = Identity(A) + A.transpose() where an "evaluate" operator had to be used
 on the right hand side to achieve the correct result. Armadillo also uses
 lazy evaluation, but does not report any similar evaluation issues.
 
 REFERENCES:
 
 [1] Conrad Sanderson (2010): "Armadillo: An Open Source C++ Linear Algebra 
     Library for Fast Prototyping and Computationally Intensive Experiments."
     Technical Report, National ICT Australia (NICTA).
 [2] http://arma.sourceforge.net/
 [3] http://mlpack.org/
 [4] http://eigen.tuxfamily.org/
 
*******************************************************************************/

// The index of the current state. This is also the index used to 
// access transition probabilities in the armadillo framework, which 
// uses 'uword' indexes. We therefore first define this so that it can be 
// changed later if needed.
  
using FSSAStateIndex = arma::uword;
  
// Similar to the other automata types, the FSSA is a template taking the
// type of the automata as the first argument. In addition it requires
// the type used as the external reference for the states.

template< class StochasticEnvironment, class StateReference >
class FSSA : virtual public LearningAutomata < StochasticEnvironment >
{
public:
	
	// The environment and the number of actions defined by it is imported first
	
	using Environment = StochasticEnvironment;

	// An alias is given for the state index
	
	using StateIndex = FSSAStateIndex;
	
  // Then indicate that this is a variable structure automata
  
  static constexpr AutomataStructures Structure = AutomataStructures::Fixed;
	
	// The number of actions are also inherited from the automata base class
	
	using LearningAutomata< Environment >::NumberOfActions;
  
  // The structure is also defined as a type for derived classes to make it
  // easier to create wrappers and derived classes (see comment in the VSSA
  // class.
				  
  using StructureType = FSSA< Environment, StateReference >	;
				  
private:
  
  // We also need to store the current state.
  
  StateIndex CurrentState;
  
  // Associated with each state there must be a reference to the corresponding
  // action to do in that state. This mapping is stored as a simple vector
  // indexed by the state.
  
  std::vector< ActionIndex > StateAction;

  // Similarly it should be possible to look up the user label of a state 
  // based on the current state index. A vector ensure this mapping.
  
  std::vector< StateReference > StateLabel;
  
  // Finally there is a mapping from the user provided state reference to
  // the corresponding internal index of the state. These are kept separate
  // since it is assumed that this is mainly used and accessed during 
  // initialisation. A standard map is used because all the state references
  // must be unique.
  
  std::map< StateReference, StateIndex > State;
  
  // Then we can define the transition probabilities among the states. These
  // are kept in two different matrices: one for the case of a reward and 
  // the second to be used in the case of a penalty.
  
  arma::mat RewardTransition, PenaltyTransition;
  
  // Finally there is a flag to block further state definitions once one 
  // starts to define the probabilities.
  
  bool AllStatesDefined;
  
protected:
  
  // It is assumed that a particular FSSA first defines the states so that
  // one knows later for which states the probabilities are defined. The
  // method returns the internal state index for the special situation where
  // a derived automaton would need to do some special processing based on 
  // the current state, e.g. it could record the index of the states of 
  // special interest.
  
  StateIndex DefineState( StateReference & NewState, 
												  ActionIndex ActionToDo )
  {
    if ( AllStatesDefined )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "States cannot be defined after transition probabilities";
			
			throw std::logic_error( ErrorMessage.str() );
		}
    else if ( StateAction.size() == NumberOfActions )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "There cannot be more states than the number of actions ("
									 << NumberOfActions << ") supported by the environment";
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
		else
    {
      auto Result = State.emplace( NewState,  StateAction.size() );
      
      // If the state was a new state and inserted with no problems we 
      // can simply insert its corresponding action. Otherwise, we take that
      // the action defined should overwrite the action already stored for 
      // this particular state. [The first element of the insert Result 
      // will be an iterator to the existing record, and the second part
      // of this record will be the state index].
      
      if ( Result.second == true )
      {
        StateAction.push_back( ActionToDo );
        StateLabel.push_back( NewState );
        
        return StateAction.size()-1;
      }
      else
      {
        StateAction.at( Result.first->second ) = ActionToDo;
        StateLabel.at(  Result.first->second ) = NewState;
        
        return static_cast< StateIndex >( Result.first->second );
      }
    }
  };
  
  // There is also a support function for a derived class to check the 
  // current state of the automaton
  
  inline StateReference GetCurrentState (void)
  { return StateLabel.at( CurrentState ); };
  
  // For each pair of states the transition probabilities are defined both
  // in the case of rewards or penalties. If the all states flag is not 
  // set, it means that the probability matrices should be initialised to
  // zero.
  
  void Transition( StateReference & FromState, StateReference & ToState, 
								   double RewardProbability  = 0.0, 
								   double PenaltyProbability = 0.0           )
  {
    if ( !AllStatesDefined )
      AllStatesDefined = true;
    
    // We then look up the internal references for the given states
    
    auto FromStatePtr = State.find( FromState ), 
				 ToStatePtr   = State.find( ToState   );
    
    if ( FromStatePtr == State.end() )
      throw std::invalid_argument( "From State is not a known state" );
    
    if ( ToStatePtr == State.end() )
      throw std::invalid_argument( "To State is not a known state" );
    
    // Knowing that we have valid states, the 'second' field points to
    // the internal index of the state and we can proceed to set the 
    // related probabilities
    
    RewardTransition( FromStatePtr->second, ToStatePtr->second )
											= RewardProbability;
    PenaltyTransition( FromStatePtr->second, ToStatePtr->second )
											= PenaltyProbability;
  };
  
  // The initial state must also be selected by the specific automata. If 
  // the state given is illegal, i.e. it is not already defined as a state
  // the initial state is set to the zero state (whatever that is).
  
  inline void SetState( StateReference & InitialState )
  {
    auto StatePtr = State.find( InitialState );
    
    if ( StatePtr == State.end() )
      CurrentState = static_cast< StateIndex >(0);
    else
      CurrentState = StatePtr->second;
  };
  
  // Some derived classes might need to know the unique ID of the current
  // state. Note that if states are still being defined this will return
  // zero as the current state.
  
  inline StateIndex CurrentStateID( void )
  { return CurrentState; };
  
  // The number of states will be returned as zero if states are still 
  // being defined.
  
  inline StateIndex NumberOfStates( void )
  {
    if ( AllStatesDefined )
      return static_cast< StateIndex >( StateAction.size() );
    else
      return static_cast< StateIndex >(0);
  };
	
	// The action index must be converted to the action object expected by the 
	// environment. The standard conversion function is inherited from the 
	// learning automata base class
	
	using LearningAutomata< Environment >::Action;
  
public:
  
  // Selecting an action is rather trivial since it is simply a matter of 
  // returning the action associated with the current state.
  
  virtual typename Environment::Action SelectAction (void) override
  { return Action( StateAction.at( CurrentState ) ); };
  
protected:
  
  // When the feedback from the environment is received, the feedback 
  // function will be called. The main task is to move from the current 
  // current state to the next state based on the transition probabilities.
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {    
    if ( Response.Feedback == PModelResponse::Reward )
			CurrentState = Random::Index( 
				EmpiricalPDF( RewardTransition.begin_row( CurrentState ), 
											RewardTransition.end_row(   CurrentState ) ) 	);
    else
	    CurrentState = Random::Index( 
			    EmpiricalPDF( PenaltyTransition.begin_row( CurrentState ), 
												PenaltyTransition.end_row(   CurrentState ) ) );
  };
  
public:
  
  // The constructor takes an instance of the environment and initialises 
	// the empty transition probability matrices.
  
  FSSA( const Environment & TheEnvironment )
  : LearningAutomata< Environment >( TheEnvironment ),
    StateAction(), StateLabel(), State(),
    PenaltyTransition( NumberOfActions, NumberOfActions, arma::fill::zeros ), 
    RewardTransition(  NumberOfActions, NumberOfActions, arma::fill::zeros )
  {
    CurrentState = static_cast< StateIndex >( 0 );
  };
	
	// The full constructor must be used, and the default constructor is 
	// therefore deleted.
	
	FSSA( void ) = delete;
	
	// The virtual base class and virtual methods of the class makes it necessary
	// to have a virtual destructor, even though it does not do anything in 
	// particular to destruct this class.
	
	virtual ~FSSA( void )
	{ }
};

} // name space LA

/******************************************************************************
 Support functions
*******************************************************************************/

// The vector is printed as a "row" vector, formatted with square brackets

template< class EnvironmentType >
std::ostream & operator<< ( std::ostream & out, 
                            LA::VSSA< EnvironmentType > & Automaton )
{
  out << "[ ";
  
  for ( const auto & ProbabiltiyValue :  Automaton.ActionProbabilities )
      out << ProbabiltiyValue << " ";
    
  out << "]";
  
  return out;
};


#endif // LEARNING_AUTOMATA
