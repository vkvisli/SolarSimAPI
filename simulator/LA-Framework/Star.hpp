/*=============================================================================
  STAR like Fixed Structure Stochastic Automata
  
  This file defines a family of automata that are best described by the 
  STack ARchitecture automaton [1]: There is a set of ordered states associated 
  with each action. The number of states for each action is called the 
  "automaton depth". One of the states is taken as an initial state, from 
  which the automaton can switch to the initial state of another action. 
  If an action is rewarded the automaton moves to the next state in the set
  of states for this action. Similarly, if the automaton is penalised it moves
  to the previous state in the succession of states for the chosen action.
  
  Different types of automata in this family are distinguished by the way 
  the transitions are done, and how the next action is chosen when the 
  automaton moves out of an initial state upon a penalty.
  
  TODO: verify that this implementation is complete!
  
  REFERENCES:
  
  [1] Anastasios A. Economides and Athanasios Kehagias (2002): "The STAR 
      automaton: expediency and optimality properties", IEEE Transactions 
      on Systems, Man, and Cybernetics, Part B: Cybernetics, Vol. 32, 
      No. 6, pp. 723-737, December 2002
  
  Author: Geir Horn, 2013
  Revision: Geir Horn, 2017 - Environment learning models
  Lisence: LGPL3.0
=============================================================================*/

#ifndef STAR_AUTOMATA
#define STAR_AUTOMATA

#include <set>			     // To store the states
#include <type_traits>   // To test if given types are the same

#include "LearningEnvironment.hpp"
#include "LearningAutomata.hpp"

namespace LA
{

/******************************************************************************
 STAR: STack ARchitecture automata

 The STAR automaton defined in [1] was not the first of this type of automata,
 but the one that most clearly illustrates the idea and the one that is best
 supported by analytical convergence results. 
 
*******************************************************************************/
//
// The states are labelled with two indices: the action to which it belongs,
// and its order in the sequence, its depth.

class StateLabel
{
public: 
  
	const ActionIndex    Action;
  const FSSAStateIndex Depth;

	// These values are set by the constructor
  
  inline StateLabel( ActionIndex TheAction, FSSAStateIndex	TheDepth )
	: Action( TheAction ), Depth( TheDepth )
  { }
  
  inline StateLabel( const StateLabel & Other )
	: StateLabel( Other.Action, Other.Depth )
	{ }
  
  // The default constructor should not be generated.
  
  StateLabel( void ) = delete;
  
  // In order to be able to sort these states there must be a less-than
  // operator. It first sorts the states based on their action indices,
  // and then based on their depth.
  
  bool operator < ( StateLabel & OtherState )
  {
    if ( Action < OtherState.Action )
      return true;
    else if ( (Action == OtherState.Action) && (Depth < OtherState.Depth) )
      return true;
    else
      return false;
  };	  
};
	
// The STAR automaton as defined in [1] is only defined for the P-model

template< class StochasticEnvironment >
class STARAutomaton 
: virtual public LearningAutomata < StochasticEnvironment >,
  virtual public FSSA < StochasticEnvironment, StateLabel >
{
private:
	
	using FSSA_Base = FSSA < StochasticEnvironment, StateLabel >;
	
public:
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< StochasticEnvironment >::NumberOfActions;
	
	// It should be verified that the environment is really a P-model environment
	
	static_assert( std::is_base_of< LearningEnvironment< Model::P >, 
																  Environment >::value, 
								 "STAR Automata will only work with P-model environments" );
	
	// Then a check that the global state index is really the same state index 
	// as defined by the FSSA base automata. Note that this test should never 
	// trigger unless someone has made some seriously wrong patches
	
	static_assert( 
		std::is_same< FSSAStateIndex, typename FSSA_Base::StateIndex >::value, 
		"STAR Automata FSSA state index does not match"  );
	
private:
	
  // In order to know when to do the special transitions of the initial 
  // states, their indices are stored as a set to ensure uniqueness and 
  // fast look-up.
  
  std::set < FSSAStateIndex >  InitialStates;
  
public:
  
  // The general constructor takes the depth of the automata. Note that this 
	// depth cannot be zero.
  
  STARAutomaton( const Environment & TheEnvironment, FSSAStateIndex	Depth )
  : LearningAutomata < Environment >( TheEnvironment ),
    FSSA_Base( TheEnvironment, Depth ), InitialStates()
  {
    // The states are created action by action where we store the 
    // first state created as the initial state.
    
    for ( ActionIndex Action = 0; Action < NumberOfActions; Action++ )
			InitialStates.insert( 
		    FSSA_Base::DefineState( StateLabel( Action, 0 ), Action ) );
  };
  
};

}      // Name space LA
#endif // STAR_AUTOMATA
