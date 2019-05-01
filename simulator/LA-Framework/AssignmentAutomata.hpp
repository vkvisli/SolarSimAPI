/*=============================================================================
  Assignment Automata
  
  Consider a game of automata, each trying to decide the best value (action)
  for a parameter taken from a discrete set. The actions of all automata 
  constitutes the allocation vector, and the feedback is given for this whole
  vector and not for the individual elements. 
  
  When one of the automata chooses a new action, it means that the allocation
  vector changes, and the environment will give a feedback to all automata
  based on this new allocation vector. Note that this means that an automaton
  may receive multiple rewards for one choice of actions as the other automata
  makes their choices.
  
  This violates the assumptions of automata convergence. For example VSSAs
  will select an action according to a probability distribution, and update
  this distribution in response to the feedback. Assume that a sequence of 
  negative feedback is received making it very unlikely that the action would
  be chosen again, yet the automaton will continue to get feedback for this
  bad action until it is its turn to choose the next action.
  
  In general this calls for a novel type of automata to be used in such 
  games. This file provides extensions to existing automata allowing them 
  to be used in such collaborative allocation games.
  
  Author: Geir Horn, 2013-2017
  License: LGPL3.0
  Revision: Geir Horn 2017 - Environment defined types and actions
=============================================================================*/

#ifndef ASSIGNMENT_AUTOMATA
#define ASSIGNMENT_AUTOMATA

#include "LearningEnvironment.hpp"		// The automata types and action count
#include "LearningAutomata.hpp"       // The LA base types

namespace LA
{
/*==============================================================================

 S-Model wrapper

==============================================================================*/
//
// S-model automata can be readily reused for collaborative allocation games. 
// The argument is that each individual feedback on an action is in the interval
// [0,1] and as feedback is received for many different allocation vectors, 
// one fundamentally receives feedback on how the chosen action behaves on 
// average for different choices of the other elements of the allocation vector. 
// Averaging the feedback will therefore also give a feedback in the interval 
// [0,1] and this average feedback is representative for the average (expected) 
// goodness of the a particular action.


template < class AutomataClass >
class AssignmentAutomata 
: virtual public LearningAutomata< typename AutomataClass::Environment >,
  public AutomataClass
{
  // We do a compile time check to see that the given automata is of the 
  // correct model.
  
  static_assert( std::is_base_of< 
								   LearningEnvironment< Model::S >, 
									 typename AutomataClass::Environment >::value , 
		 "Assignment Automata must be based on an S-Model automata" );
	
	// The standard definitions of environment and number of actions.
	
	using Environment = typename AutomataClass::Environment;
	using LearningAutomata< Environment >::NumberOfActions;

private:
  
  // The class adds up the reward received since the last selection of
  // an action, and the number of rewards in order to compute the average 
  // reward received since last selection.
  
  typename 
  Environment::ResponseType TotalResponse;
  unsigned long             NumberOfResponses;
  
  // It is also necessary to remember the active action. This is normally
  // returned with the feedback function, but since the feedback function
  // does not update the probabilities, we need to remember the action to 
  // when it is needed for the update (i.e. when we select the next action)
  
  ActionIndex ActiveAction;

public:
  
  // The feedback function can be called many times for action choice and
  // simply adds together the total response. The actual update of 
  // probabilities will happen when the automaton is requested to select
  // the next action.
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    TotalResponse += Response.Feedback;
    NumberOfResponses++;
  };
   
  // The Select Action marks the point when the action changes. It will first
  // update the automata logic with the combined feedback receive since 
  // the action was selected, and then reset the counters before proceeding
  // with letting the automaton select the action.
  
  virtual ActionIndex SelectAction (void) override
  {
    // The action probabilities can only be updated if we have any feedback
    
    if ( NumberOfResponses > 0 )
    {
      // Provide the average feedback for the active action to the normal 
      // probability update function.
      
      AutomataClass::Feedback( ActiveAction, 
	      TotalResponse /  static_cast< typename Environment::ResponseType >( 
															        NumberOfResponses )  );
      
      // Then reset the feedback counters
      
      TotalResponse     = static_cast< typename Environment::ResponseType >(0);
      NumberOfResponses = 0;
    }
    
    ActiveAction = AutomataClass::SelectAction();
    
    return ActiveAction;
  };
 
  // The constructor takes an instantiation of the given Environment and the 
	// remaining arguments to forward to the encapsulated automata type 
	// constructor
  
  template < typename... AutomataClassTypes >
  AssignmentAutomata( const Environment & TheEnvironment,
								      AutomataClassTypes... BaseArguments )
    : LearningAutomata< Environment >( TheEnvironment ),
      AutomataClass( TheEnvironment, BaseArguments... )
  {
    TotalResponse     = static_cast< typename Environment::ResponseType >( 0 );
    NumberOfResponses = 0;
  }

	// There is a virtual destructor to ensure that all base classes and their 
	// variables are correctly removed.

	virtual ~AssignmentAutomata( void )
	{ }
};

}      // Name space LA
#endif // ASSIGNMENT_AUTOMATA
