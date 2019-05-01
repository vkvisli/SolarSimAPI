/*=============================================================================
  Variable Action Set
  
  A variable action set automata supports situations where only a subset of 
  the action set should be used when selecting the next action, and where this
  subset is context dependent. Consider for example a situation where there 
  are A actions, but for a given situation only a < A actions would make sense.
  
  The fundamental idea is then that the probabilities of the applicable actions
  form a temporary probability mass used to select the action and updated as 
  a normal automata. After the update of the this sub-automata, the updated 
  probabilities are scaled back to their original probability mass and moved
  into the correct positions of the larger probability vector.
 
  The automata is a VSSA, and it takes a care-of automata that is used to 
  select the subset action, and update the subset probabilities based on 
  its normal update algorithm. This is a generalisation of Thathachar's and 
  Harita's Variable Action Set L_RI P-model automata [1]. 
  
  Although the generalisation suggests that any sub-set updating automata 
  could be used, the convergence is only proved in [1] for the case where 
  the feedback is binary (P-Model) and a linear reward-inaction update scheme 
  is used. Convergence of the automata must be verified if other update 
  schemes are used. However, Poznyak and Najim [2] has proven convergence for 
  a special S-Model update scheme implemented below.
  
  There is one important assumption: The automata must not remember the size 
  of the action set, but take this from the length of the action probability 
  vector, so that it is possible to re-initialise the probability vector when 
  a new subset of actions is chosen.
  
  REFERENCES:
  
  [1] Mandayam A. L. Thathachar and Bhaskar R. Harita (1987): "Learning automata 
      with changing number of actions",  IEEE transactions on systems, man, and 
      cybernetics, Vol. SMC-17, No. 6, pp. 1095–1100, November 1987
      
  [2] Alexander Semenovich Poznyak and Kaddour Najim (1996): "Learning automata 
      with continuous input and changing number of actions", International 
      Journal of Systems Science, Vol. 27, No. 12, pp. 1467-1472
 
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPLv3.0
=============================================================================*/   

#ifndef VARIABLE_ACTION_SET_AUTOMATA
#define VARIABLE_ACTION_SET_AUTOMATA

#include <set>										 // The set of selected automata
#include <vector>									 // For index mapping
#include <type_traits>						 // Ensuring the use of a VSSA
#include <stdexcept>							 // Standard exception types
#include <sstream>								 // Error messages
#include <limits>								   // Numeric limits of types
#include <memory>                  // For smart pointers
#include <algorithm>               // To operate on standard containers
#include <iterator>                // For iterator arithmetic
#include <functional>              // Functional programming
#include <tuple>                   // subset automata constructor arguments
#include <utility>                 // Index sequence to unpack arguments

#include "LearningAutomata.hpp"    // The LA and VSSA definition
#include "LinearLA.hpp"						 // For the P-model specialisation

namespace LA
{

/*==============================================================================

 Variable Action Set automata

==============================================================================*/
//
// The Variable Action Set automata encapsulates another standard automata,
// and this encapsulated automata defines the probability update algorithm. 
// The variable action set automata provides the standard select action 
// function, which will select an action based on the full set of possible 
// actions. However, there is a select action overloaded function taking a 
// set of action indices defining the set of actions to select from.
//
// An instance of the encapsulated automata is created based on the 
// probabilities of the selected actions after normalisation.The feedback 
// from the environment is then passed on to the instance of the care of 
// automata, which will update the probabilities according to the normal 
// algorithm. Finally, the probabilities for the selected sub-set of actions
// will be used to update the corresponding probabilities of the full action 
// set probabilities. 
//
// The concept of Environment is however slightly more difficult. The type
// of the encapsulated automata must be defined on an environment. However,
// the type of the encapsulated automata is a black box when given as argument 
// to the variable action set automata. Since the care of automata will be 
// instantiated based on a subset of actions, its environment type must be a 
// subset environment. However, the variable action set automata need the 
// full action set environment encapsulated by the subset environment. This 
// is achieved with a default template argument derived from the encapsulated
// automata's environment. Based on the normal substitution rules, this will 
// fail to compile if the encapsulated automata is not defined on a subset 
// environment. It would have been nicer to be able to give a good error 
// message in this (like in a static assert), but hopefully this 
// explanation may help to understand the compile errors errors.
	
template < 
  class CareOfAutomata,
	typename = std::enable_if_t< std::is_base_of< 
							 SubsetEnvironment< 
								 typename CareOfAutomata::Environment::FullSetEnvironment >, 
						   typename CareOfAutomata::Environment >::value 
					    > >
class VariableActionSet 
: virtual public LearningAutomata< 
								 typename CareOfAutomata::Environment::FullSetEnvironment >,
  virtual public VSSA < 
							   typename CareOfAutomata::Environment::FullSetEnvironment >
{
  // ---------------------------------------------------------------------------
  // Basic definitions
  // ---------------------------------------------------------------------------

public: 
	
	// Since the second template argument will only match if the environment of 
	// the care of automata really is a subset environment, 

	using Environment = typename CareOfAutomata::Environment::FullSetEnvironment;
	
  // Then a verification that the care of automata is a VSSA and derived 
  // from the VSSA class.
  
  static_assert( std::is_base_of< VSSA< typename CareOfAutomata::Environment >,
                                  CareOfAutomata >::value,
        "Variable Action Set: The Care-of-automata must be a VSSA automata!"  );
  	
private:
  
	// Since the template is compiled before its instantiation, it is necessary
	// to qualify the inherited variables, and a shorthand alias is defined 
	// to facilitate this
	
	using VSSA_Base = VSSA< Environment >;

protected:
	
	// The function converting the global action index to the action object 
	// expected by the environment is inherited from the VSSA base class
	
	using VSSA_Base::Action;
	
	// The action probabilities are also reused
	
	using VSSA_Base::ActionProbabilities;
	
	// The number of actions is also exported for possible public use.
	
public:
	
	using VSSA_Base::NumberOfActions;
	
	// The VSSA function to initialise the probabilities must also be initialised
	
	using VSSA_Base::InitialiseProbabilities;

  // ---------------------------------------------------------------------------
  // Variables
  // ---------------------------------------------------------------------------
  //
  // There will fundamentally be two probability vectors: The universal one
  // over all possible actions and the selected subset vector. When an action
  // is chosen it is selected according to the latter,  but it will be known 
  // externally under the index of the universal vector. It is therefore 
  // necessary to map the indices of the subset probabilities back to the 
  // universal index.
  
private:
	
  std::vector< ActionIndex > SubsetIndexMap;
  
  // The selected probabilities has a certain mass in the overall probability 
  // vector and this mass should not change when the probabilities are 
  // written back to universal probability vector. This is therefore 
  // computed when the selection is made, and used when the feedback has 
  // arrived for the choice.
  
  double SelectedMass;
	
  // ---------------------------------------------------------------------------
  // Subset automaton
  // ---------------------------------------------------------------------------
  //
  // The care of automata will be initialised with the probabilities from the 
	// given subset when an action should be selected, and it will then select 
	// the actual action according to its normal algorithm. Its normal 
	// probability updating algorithm is then again used when the action is 
	// rewarded by the environment, and the updated probabilities written back 
	// to the full vector of probabilities.
	//
	// Since the subset automation is created for each new subset, it is stored 
	// in a shared pointer so that the automaton for the previous subset will be
	// properly deleted when the new one is created. 
  
  std::shared_ptr< CareOfAutomata > SubsetAutomaton;
	
	// Since this has to be generated for each subset of actions, there must be 
	// generator function. This will be defined as a class with a virtual method
	// to return a constructed subset automaton
	
	class SubsetCreatorFunction
	{
	public:
		
		virtual std::shared_ptr< CareOfAutomata > 
		NewAutomaton( const std::vector< ActionIndex > & SubsetIndexMap ) = 0;
		
		// There is no need for a constructor since the class has no data, 
		// but the destructor must be virtual to ensure that derived classes can 
		// be destroyed via a base class pointer.
		
		virtual ~SubsetCreatorFunction( void )
		{ }
	};
	
	// The actual generator is a managed pointer to this function to be 
	// initialised by the variable action set constructor below
	
	std::shared_ptr< SubsetCreatorFunction > SubsetAutomataGenerator;
	
	// A problem with creating the subset automata of a generic type is to pass
	// the necessary construction parameters. For instance, the linear algorithms 
	// require a learning constant. In general, it is not possible to know which 
	// parameters a special algorithm will require in the future, and in general 
	// these parameters can only be given to the constructor of this 
	// encapsulating variable set automata.
	//
	// This implies that the argument types and their values must be cached and 
	// reused to create the above shared care of automata pointer. There is a 
	// standard way of doing this using a tuple to store the values and then 
	// an integer sequence to unpack the values stored in the tuple. Although 
	// supported by the standard, the implementation is a little obscure. The 
	// following is based on Oktalist's anwer on the Stack Overflow thread
	// https://stackoverflow.com/questions/14897527/constructor-arguments-from-tuple
	// A good explanation for variadic argument and their unpacking is Murray 
	// Cumming's blog post at
	// https://www.murrayc.com/permalink/2015/12/05/modern-c-variadic-template-parameters-and-tuples/
	// and a full implementation of the needed mechanism is shown in 0x499602D2's 
	// response on the Stack Overflow thread
	// https://stackoverflow.com/questions/16868129/how-to-store-variadic-template-arguments
	//
	// The fundamental idea is that the subset automaton generator is a template 
	// class defined for the additional care of automata constructor argument,
	// and it offers the function operator to create the shared pointer on an 
	// instance of this class.
	
	template< class... SubSetLAArgumentTypes >
	class AutomataGenerator : public SubsetCreatorFunction
	{
	private:
		
		// Since the subset automaton requires a subset environment encapsulating 
		// the full action set environment, it is necessary to store a copy of the 
		// full set environment given to the constructor of the variable action 
		// set automata, and it is assumed that the environment type has a copy
		// constructor to initialise the environment copy. 
		
		Environment FullSetEnvironment;
		
		// The given argument values are stored in a standard tuple to be unpacked
		// when the subset automaton is constructed.
		
		std::tuple< SubSetLAArgumentTypes... > ArgumentValues;
		
		// The actual generator function takes an argument index sequence and 
		// creates the subset automaton based on a reduced set environment and 
		// the subset index map.
		
		template< std::size_t... ArgumentIndices >
		std::shared_ptr< CareOfAutomata >
		Generator( const std::vector< ActionIndex > & SubsetIndexMap, 
							 std::index_sequence< ArgumentIndices... > )
		{
			SubsetEnvironment< Environment > 
			RestrictedSetEnvironment ( FullSetEnvironment, SubsetIndexMap );
			
			return std::make_shared< CareOfAutomata >( 
								RestrictedSetEnvironment, 
						    std::get< ArgumentIndices >( ArgumentValues )...  );
		}
		
	public:
		
		// Functional operator to construct the care of automata object taking 
		// the subset index map as argument
		
		virtual std::shared_ptr< CareOfAutomata >
		NewAutomaton( const std::vector< ActionIndex > & SubsetIndexMap ) override
		{
			return 
			Generator( SubsetIndexMap, 
								 std::index_sequence_for< SubSetLAArgumentTypes... >() );
		}
		
		// The constructor ensures that the argument values to be passed when 
		// the automaton is created will be properly stored in the tuple.
		
		AutomataGenerator( const Environment & TheEnvironment, 
										   SubSetLAArgumentTypes... SubSetLAArgumentValues )
		: FullSetEnvironment( TheEnvironment ),
		  ArgumentValues( SubSetLAArgumentValues... )
		{ }
		
		// In order to support proper destruction of the full set environment 
		// and the tuple, the destructor must be virtual so it can be called 
		// from the subset automata generator base class pointer when the 
		// variable action set automata is deleted.
		
		virtual ~AutomataGenerator( void )
		{ }
	}; 
  
	// It would be desirable to directly instantiate this generator class, but 
	// it is not possible as the subset automata constructor argument types taken 
	// as template arguments must be given and they will not be known before the 
	// variable action set automata constructor taking these additional arguments. 
	// For this reason it will be dynamically allocated in the constructor for 
	// the Variable Action Set below, and the subset automata generator will 
	// point to this instance in order to ensure that the right argument values 
	// will be passed each time a new subset automata is needed.
    
  // ---------------------------------------------------------------------------
  // Selecting actions
  // ---------------------------------------------------------------------------
	
public:
	
  virtual typename Environment::Action SelectAction( void ) override
  {
    return 
    Action( Random::Index( EmpiricalPDF( ActionProbabilities ) ) );
  }
  
	// The function selecting an action takes a set of action indices,  selects
  // these from the action vector and initialises the subset automaton with 
  // these probabilities.
  
  typename Environment::Action 
  SelectAction( const std::set< ActionIndex > &  SubsetIndices )
  {
    // The parameters of the previous selection is cleared first.

    SubsetIndexMap.clear();
    SelectedMass = 0.0;

    // A legality check to ensure that the selected index set does not contain
    // too many indices; or alternatively that the set is empty.
    
    if ( SubsetIndices.size() > NumberOfActions )
    {
      std::ostringstream ErrorMessage;
      
      ErrorMessage << "Subset index set size " << SubsetIndices.size()
                   << " is larger than the number of allowed actions, "
                   << NumberOfActions;
                   
      throw std::invalid_argument( ErrorMessage.str() );
    }
    else if ( SubsetIndices.size() ==  0 )
    { 
      std::ostringstream ErrorMessage;
      
      ErrorMessage << "The given subset must contain at least one candidate "
                   << "action";
                   
      throw std::invalid_argument( ErrorMessage.str() );
    }

    // The index map is initiated together with the vector of selected 
    // probabilities.
    
    std::vector< double > SelectedProbabilities;
        
    for ( ActionIndex index : SubsetIndices )
    {
      double TheProbability = ActionProbabilities.at( index );
      SelectedProbabilities.push_back( TheProbability );
      SelectedMass += TheProbability;
      SubsetIndexMap.push_back( index );
    }
    
    // Since a meaningful selection can be made, the subset automaton is re-
    // initialised to select the action, whose universal action index is 
    // returned. In the case that the subset contained only one index,  the 
    // subset automaton will be initialised with the complete universal 
    // probability vector, and the single action of the subset will be 
    // selected. The reason for this is that there is no way to update the 
    // universal action probabilities since the only update algorithm know to 
    // this subset automata is the one used to update the subset. Hence the 
    //  "subset" must be equal to the entire action universe.
    
    if ( SelectedProbabilities.size() > 1 )
    {
	    // Theoretically it could happen that the probability mass of the selected 
	    // set is zero, for which no meaningful selection can be made. The only 
	    // way to recover is to ask for a new subset of actions.
	    
	    if ( SelectedMass < 10 * std::numeric_limits< double >::epsilon() )
	      throw std::underflow_error("Selected subset probability mass is zero!");

			// The subset automaton is created and initialised with the selected 
			// probabilities.
			
			SubsetAutomaton = SubsetAutomataGenerator->NewAutomaton( SubsetIndexMap );
			SubsetAutomaton->InitialiseProbabilities( 
											 ProbabilityMass< double >( SelectedProbabilities ) );
			
			// Then the subset automata can select an action from its action set, 
			// but to the outside the original action index is presented as the 
			// selected action. Hence one would expect that the choice of the 
			// subset automaton should be mapped back to the original indices by 
			// 		return SubsetIndexMap[ SubsetAutomaton->SelectAction() ];
			// However, since the subset automaton has been constructed on the 
			// basis of a subset environment, its action selector function will 
			// already take care of this mapping to ensure that a subset automata 
			// will return a legal index for the subset. Hence, it is just to ask 
			// the automata to select normally the action
			
			return SubsetAutomaton->SelectAction();
    }
    else
    {
			// The selected subset map is in this case the identity map, and it must
      // be initialised, and the subset mass set equal to the mass of the 
      // universal action probability vector, i.e. unity.
      
      SubsetIndexMap.clear();
      SelectedMass = 1.0;
      
      for ( ActionIndex index = 0; index < NumberOfActions; index++ )
        SubsetIndexMap.push_back( index );
			
			// No actions were selected and the decision will be based on the full 
			// action set.
			
			SubsetAutomaton = SubsetAutomataGenerator->NewAutomaton( SubsetIndexMap );
			SubsetAutomaton->InitialiseProbabilities( 
											 ProbabilityMass< double >( ActionProbabilities ) );
			
			// In this case the subset indices had only one member, the selected 
			// action and this action is therefore chosen with probability one.
			
			return *( SubsetIndices.cbegin() );
    }    
  }

  // ---------------------------------------------------------------------------
  // Getting feedback
  // ---------------------------------------------------------------------------
  // The feedback function will pass the environment reward through to the 
  // subset automaton leaving it to update the probabilities of the actions in 
  // the subset. After this has been done, the updated probabilities will be 
  // scaled with the probability mass they have in the universal probability 
  // vector since the subset probabilities were normalised when the subset
  // automation was initialised. 
  // 
  // The update using the subset automaton will only happen if the selected 
  // mass is larger than zero. 
  
  virtual 
  void Feedback ( const typename Environment::Response & Response ) override
  {
    // The response contains the full action set index, and the corresponding 
    // subset index must be identified. 

    auto SubsetAction = std::find( SubsetIndexMap.begin(), SubsetIndexMap.end(), 
																	 Response.ChosenAction );
		
		// If this was found in the map, then the feedback to the subset automaton 
		// is set up where the subset index is given by the distance between the 
		// first subset action and the found action.
		
		if ( SubsetAction != SubsetIndexMap.end() )
		{
		  typename Environment::Response 
		  SubsetResponse( std::distance( SubsetIndexMap.begin(), SubsetAction ), 
											Response.Feedback );
			
			SubsetAutomaton->Feedback( SubsetResponse );
			
	    // Getting the normalised subset probabilities
    
	    auto SubsetProbabilities( SubsetAutomaton->GetProbabilities() );
	    
	    // Scaling with the original probability mass of the subset is necessary 
	    // when writing these updated probabilities back to the universal 
	    // probabilities.
	    
	    for ( ActionIndex index = 0; index < SubsetProbabilities.size(); index++ )
	      ActionProbabilities[ SubsetIndexMap[ index ] ] = 
	          SelectedMass * SubsetProbabilities.at( index );
		}
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "The chosen action " << Response.ChosenAction 
									 << " is not a part of the subset actions [ ";
									 
		  for ( ActionIndex idx : SubsetIndexMap )
				ErrorMessage << idx << " ";
			
			ErrorMessage << "]";
			
			throw std::invalid_argument( ErrorMessage.str() );
		}    
  }
  
  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
  // The subset automaton has to be constructed for each selected subset of the 
  // indices of available actions (selected from the the full set). Depending 
  // on the type of automata instantiated for the subset automation, it will 
  // typically require additional arguments like a learning parameter and 
  // similar. The only place these parameters can be given is to the constructor
  // which is a variadic template supporting zero or more arguments of various 
  // types with the intention to forward these to the subset automaton. 
  //
  // The easiest way to preserve the given parameters forwarded to the subset
  // automaton constructor is to wrap up the construction of the subset 
  // automaton in a generator function that defines the construction based 
  // on the provided additional arguments for the subset automaton. 
  //
  // 
  //
  // Finally, given that any automata must be constructed on an instantiated 
  // environment defining the actions and the response type, a temporary 
  // subset environment must be constructed based on the given full action 
  // vector environment, but restricted to the selected subset of actions.
  
  template < class... SubSetLAArgumentTypes >
  VariableActionSet( const Environment & TheEnvironment, 
										 SubSetLAArgumentTypes... SubSetLAArgumentValues )
  : LearningAutomata< Environment >( TheEnvironment ),
    VSSA_Base( TheEnvironment ),
    SubsetIndexMap(), SelectedMass( 0 ),
    SubsetAutomaton() 
	{
		SubsetAutomataGenerator = std::make_shared< 
		  AutomataGenerator< SubSetLAArgumentTypes... > >
			  ( TheEnvironment, SubSetLAArgumentValues... );
	}
  
  // The destructor is merely a place holder ensuring that all the variables 
  // are correctly destroyed
  
  virtual ~VariableActionSet( void )
  { }
};

/*==============================================================================

 Variable action set P-Model automata 

==============================================================================*/
//
//   Thathachar's and Harita's automata follows from using the standard Linear 
//   Reward-Inaction automata with this variable action set wrapper automata. 
//   The following definitions allows a definition like
//   
// 	  VariableActionSet< ThathacharHarita >
// 	  
//   for their particular update mechanism

using ThathacharHarita = LinearRI< LearningEnvironment< Model::P >, Model::P >;

/*==============================================================================

 Variable action set S-Model automata 

==============================================================================*/
//
//   Poznyak's and Najim's S-model [2] changes the update function of the subset 
//   probabilities. It is therefore a special care-of automata for the generalised
//   variable action set. 
//   
//   It should be noted that in [2], the reward is a 'non-penalty' extending from 
//   zero representing a reward to unity representing a penalty. This is the 
//   inverse of the standard notation used by this library and the formula 
//   implemented below is consequently deviating from the one found in [2].
//   
//   The update equation for a feedback r(k) in [0,1] is
//   
//   p_i(k+1) = p_i(k) + gamma * D(i,s) - gamma * p_i(k) 
// 						   + gamma * r(k) * (1-N*D(i,s))/(N-1)
// 
//   here s is the index of the chosen action and D(i,s) is the Kronecker's 
//   delta function which is unity if i = s, otherwise zero. Hence,  this can be 
//   split into one update equation for the actions not chosen
//   
//   p_i(k+1) = (1-gamma) * p_i(k) + gamma * r(k)/(N-1)    when i is not equal to s
//   
//   and the update for the chosen action
//   
//   p_s(k+1) = (1-gamma) * p_s(k) + gamma * r(k)/(N-1)
//              + gamma + gamma * r(k)*N/(N-1)
//              
//   Consequently,  the first line of the update for the chosen action is the same
//   as the update for the not chosen actions. Furthermore, writing 
//   
// 		  lambda = 1-gamma
//   
//   identifies lambda as the standard learning constant of this library, and 
//   the equations are simplified by introducing 
//    
// 		 zeta = (1-lambda) * r(k)/(N-1)
// 		 
//   and the general update equation is then 
//   
//   p_i(k+1) = lambda * p_i(k) + zeta
//   
//   and then a correction is added to the selected action after the update
//   
//   p_s(k+1)' = p_s(k+1) + (1-lambda) + zeta * N

template< class StochasticEnvironment >
class PoznyakNajim 
: virtual public LearningAutomata< StochasticEnvironment >,
  virtual public VSSA< StochasticEnvironment >
{
public:
	
	static_assert( std::is_base_of< LearningEnvironment< Model::S >, 
																  StochasticEnvironment >::value, 
		 "The Poznyak-Nadim automata will only work with an S-model environment" );
	
	// First the standard definitions for the environment and its number of 
	// actions
	
	using Environment = StochasticEnvironment;
	using LearningAutomata< Environment >::NumberOfActions;
	
  
	// The VSSA function for initialising the probabilities is allowed also 
	// for this automata
	
	using VSSA< Environment >::InitialiseProbabilities;
	
	// There is also a shorthand for the VSSA base class action probability 
	// vector
	
protected:
	
	using VSSA< Environment >::ActionProbabilities;
	
	// There is a standard learning constant to be used when updating the 
	// probabilities based on the environment's feedback. It should be noted 
	// that the paper [2] defines this as the inverse of the learning 
	// constant in this library where a value close to unity implies slow 
	// learning. 
	
	double LearningConstant;
	
	// The feedback function updates the subset probabilities 
	
public:
	
 virtual void Feedback ( const typename Environment::Response & Response )
  {
		double zeta = (1.0-LearningConstant) * Response.Feedback / 
																	static_cast<double>( NumberOfActions - 1 );
		
		for ( double & probability : ActionProbabilities )
			probability = LearningConstant * probability + zeta;
		
		ActionProbabilities[ Response.ChosenAction ] += (1.0 - LearningConstant) 
														+ zeta * static_cast<double>( NumberOfActions );
	}	
	
	// The constructor takes the number of actions to initialise the VSSA base
	// class, and the learning constant.
	
	PoznyakNajim( const Environment & TheEnvironment, double lambda	)
	: LearningAutomata< Environment >( TheEnvironment ),
	  VSSA< Environment >( TheEnvironment )
	{
		if ( (0.0 < lambda) && (lambda < 1.0))
			LearningConstant = lambda;
		else
			throw std::invalid_argument("Poznyak-Najim: illegal learning constant");
	}
};

}         // name space LA                                                  
#endif    // VARIABLE_ACTION_SET_AUTOMATA
