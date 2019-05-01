/*=============================================================================
 The Learning Environment
 
 The actions are defined in the environment since the actions will only make
 sense when interpreted by the environment. The action is evaluated by the 
 environment which gives a feedback of a type that makes sense to the automata 
 based on this action.
 
 This means that the automata type must match the environment (response) type, 
 and how can the compiler ensure this? The environment object can be serving 
 multiple automata using different algorithms as long as they all expect the 
 same response type. 
 
 One approach would be to let the automata constructor require a pointer or 
 a reference to the environment object. However, this would not allow the 
 definition of certain types in of the automata to match the same types in 
 the given environment. For this the environment type should be a template 
 parameter. The downside of a pure template parameter is that everything has 
 to be fixed at compile time. Hence, both approaches is necessary.
 
 The clearest illustration of this is the number of actions. This could be a 
 compile time template parameter, like the size of standard arrays. However, 
 this would require a recompilation every time the number of actions needs to 
 be changed. Consider for instance an application written to learn which book 
 in a library to recommend. The number of books would be a constant in the 
 source code and could not be a parameter given at the command line. This is 
 clearly impractical and the number of actions must be a parameter to the 
 constructor of the environment. Consequently, the constructors of the automata
 must take some kind of reference to the environment to be able to copy the 
 read-only parameter for the number of actions.
 
 A related topic is how to ensure that the automata corresponds to the 
 environment. Consider the case where there are two environments with A1 and A2
 number of actions respectively, and these numbers are not equal. Automata are
 created with each of the two environments as template arguments and constructor
 arguments. Then it could be perfectly possible to send an action from an 
 automata created for the first environment to the second environment, and the 
 chosen action index may be larger than A2 causing a run-time exception. 
 
 There are several possible approaches to force this consistency:
 
 1) The automata stores a smart pointer to the environment, and the environment
    makes the automata base class a friend that can call the protected 
    evaluation function of that environment.
 2) The automata stores a function object that is created by a creator on the 
    environment instance, and this function object is used to send the chosen 
    action to the right environment.
 3) The action evaluation function of the environment does not accept an action,
    but requires an automata pointer on which is calls the Select Action 
    function. Then the signature of the automata pointer can ensure that only 
    automata created with the correct environment as template parameter can 
    be given.
 4) The automata are dynamically allocated by a generator function of the 
    environment, and the environment keeps track of existing automata and only 
    evaluates actions from these. The automata must de-register with the 
    environment when they close.
 5) The action is not an index into the action vector, but an object that is 
    relative to the given environment. It must be relative to the instance 
    since two environments may be of the same type, just with different numbers 
    of actions. The environment only accept to evaluate actions of this kind. 
 
 Alternatives 1,3, and 4 all requires shared memory between the automata and 
 the environment and can be hard to extend to distributed applications where 
 the automata and the environment are located in different memory domains (and 
 actions are typically communicated via messages). This restriction can be 
 circumvented by having an environment proxy on each memory domain, which is 
 anyway needed because the automata constructor must have a reference to its 
 local environment.
 
 The learning loop is essentially:
 
 action   = LearningAutomata::SelectAction()
 response = Environment::Evaluate( action )
 LearningAutomata::ProcessFeedback( response ) 
 
 Alternatives one and three merge all these three steps into one function, and 
 for some applications it could be useful to separate them; for instance if one 
 needs to log the actions selected by the automata. 
 
 Alternative 4 should be avoided as it is wasteful in terms of memory, and 
 more complex in implementation.
 
 Alternative 5 has a type-instance problem in that the action object must be 
 defined for an instance of the environment. However, this may not be a major 
 restriction since also the number of actions must be obtained from an 
 environment instance, and an action object creator function can be obtained 
 on automata construction.  
 
 Hence, it is similar in complexity to alternative two at the exception that in 
 alternative two the evaluation function must be called on the automata, and 
 it is this call that decides which environment to evaluate the given action. 
 This may make it more difficult to follow the application logic. The main 
 draw back is that the action may need additional processing before it can be 
 given to the environment. Assume, as an example a distributed application 
 where the action needs to be serialised before transmitted over the network 
 and then de-serialised at the receiving end. The serialisation must either 
 be encoded in the transmission function, or the action must be given to a 
 proxy environment. The response is given to the automata from the proxy 
 environment, but this approach may be slightly more difficult to implement 
 than Alternative 5 that strictly respects the learning loop, and maintains 
 a clear separation between the functionality of the learning automata and the 
 environment model. Consequently, approach 5 is implemented.
 
 Author: Geir Horn, 2013 - 2017
 Contact: Geir.Horn [at] mn.uio.no
 License: LGPL3.0
=============================================================================*/

#ifndef LEARNING_ENVIRONMENT
#define LEARNING_ENVIRONMENT

#include <vector>				// Q-model response values and reward probabilities
#include <sstream>      // For error messages
#include <stdexcept>    // For standard exceptions
#include <functional>   // For action generation functions
#include <memory>       // For smart pointers
#include <type_traits>  // For metaprogramming

#include "RandomGenerator.hpp"
#include "ProbabilityMass.hpp"

namespace LA
{
/*==============================================================================

 Basic definitions

==============================================================================*/
//
// A core concept is the environment model that essentially defines the 
// feedback the automata provides

enum class Model
{
	S,   // Continuous feedback over the interval [0,1]
	Q,   // Discrete feedback over a given set of possible values
	P,   // A degenerate Q-model with binary response: Penalty or reward.
	Base // The common base class for all environments
};

// Actions: These must be defined for the given environment and a
// particular action is selected by a learning agent by passing a message
// with the action index to the environment. Please note that the action
// index starts at zero, so the actions are in the range 0..n-1 where 
// n is the number of actions. Since actions are normally stored in vectors,
// the action index type is identical to the vector's index type (normally 
// size_t, but it could be defined something else).

using ActionIndex = std::vector< double >::size_type ;

/*==============================================================================

 Generic learning environment

==============================================================================*/
//
// -----------------------------------------------------------------------------
// Learning environment base
// -----------------------------------------------------------------------------
// 
// There is a common specialisation for the learning environment defining the 
// action type, the action generator, and the response type expected by all 
// environments. 

template< typename FeedbackType >
class LearningEnvironmentBase
{
protected:
	
	// There is an interesting issue with the environment type since it defines 
	// the environment and it should not change. However, derived classes may 
	// want to set this to a different value. The problem is better explained 
	// with the following code extract:
	//
	// const Model Type;
	// 
	// This can only be set by the constructor, i.e. the constructor of the Base
	// Now assume that there is a function 
	//
	// f( LearningEnvironment< Model::Base > & TheEnvironment )
	// 
	// Any instance of a class derived from the Base model can be passed to this 
	// function, but what will Type be? If it is declared as constant in the base 
	// class, it can only be written by the constructor of the Base model 
	// environment, and it will be Model::Base for all environments! Hence, a 
	// constant member of the base class cannot be used. 
	// 
	// Another approach would be to make it a constant expression
	//
	// static constexpr Model Type = Model::Base;
	//
	// The benefit of this is that it can be tested at compile time, but there 
	// is a problem at run-time if one needs to test the environment type for an 
	// object. For this to work at compile time, derived classes must remember to 
	// 'use' this definition from its parent class for it to be testable on the 
	// derived class. Thus the only option to test at compile time is to check 
	// if the right learning environment for the needed feedback model is a 
	// base class of the given environment type (at least until 'concepts' are 
	// generally supported by compilers).
	// 
	// It is no option making the Type a non constant public variable, 
	// because then it could accidentally be changed somewhere. One approach 
	// would be to keep the type variable as a protected data element that 
	// derived classes may set in the constructor:
	
	Model EnvironmentType;
	
	// This variable could then be returned by a public interface function. This
	// will always work. However, the function call may seem strange to get a 
	// type information. A neat trick is provided by Rob in the Stack Overflow 
	// thread https://stackoverflow.com/questions/5424042/class-variables-public-access-read-only-but-private-access-read-write/5424521#5424521
	// Essentially, it provides a constant reference. This will prevent default 
	// assignment of an environment to another environment, but calling the 
	// copy constructor will still work.
	
public:
	
	const Model & Type;

	// The number of actions may only be defined at run-time, and it is therefore
	// stored as a parameter defined by the constructor.
	
	const ActionIndex NumberOfActions;

  // ---------------------------------------------------------------------------
  // Evaluation
  // ---------------------------------------------------------------------------
  // 
  // The actual action provided from the automata is an object because that 
  // will ensure at compile time that the type of the action exactly as 
  // expected by the environment. This will simply store the chosen action as
	// a data field
	
	class Action
	{
	public:
		
		const ActionIndex ChosenAction;
		
		// There is an operator that converts this action to an action index by 
		// simply returning the stored action
		
		inline operator ActionIndex() const
		{ return ChosenAction; }
		
		// The constructor simply stores the action as the chosen action
		
		inline Action( const ActionIndex & SelectedAction )
		: ChosenAction( SelectedAction )
		{ }
		
		inline Action( const Action & Other )
		: Action( Other.ChosenAction )
		{ }
		
		inline Action( const Action && Other )
		: Action( Other.ChosenAction )
		{ }
		
		// Finally, an action cannot be default constructed.
		
		Action( void ) = delete;
	};

	// Automata creating this action has to use a function to generate the action
	// and this function also checks that the selected action is within the bounds
	// given by the number of actions
	
	virtual std::function< Action( const ActionIndex ) > 
	ActionGenerator( void ) const
	{
		return [ ActionCardinality = NumberOfActions ]
					 ( const ActionIndex SelectedAction )->Action{
			if( SelectedAction < ActionCardinality )
				return Action( SelectedAction );
			else
		  {
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
										 << "Selected action index " << SelectedAction
										 << " must be less than the number of actions "
										 << ActionCardinality;
										 
			  throw std::invalid_argument( ErrorMessage.str() );
			}
		};
	}

	// ---------------------------------------------------------------------------
	// The response type
	// ---------------------------------------------------------------------------
	// 
	// The actual response is a structure containing the chosen action index, and
	// the response value. It is defined as a template conditioned on the feedback 
	// type providing two read-only fields holding the given values. The default 
	// constructor is deleted to force initialisation through the normal 
	// constructor. Trivial move and copy constructors are provided.

	class Response
	{
	public:
		
		const ActionIndex  ChosenAction;
		const FeedbackType Feedback;
		
		Response( ActionIndex TheAction, FeedbackType TheFeedback )
		: ChosenAction( TheAction ), Feedback( TheFeedback )
		{ }
		
		Response( Response & Other )
		: Response( Other.ChosenAction, Other.Feedback )
		{ }
		
		Response( Response && Other )
		: Response( Other.ChosenAction, Other.Feedback )
		{ }
		
		Response( void ) = delete;
	};
  
	// The Feedback type is also defined as the response type of the environment
	
	using ResponseType = FeedbackType;
	
  // ---------------------------------------------------------------------------
  // Evaluation
  // ---------------------------------------------------------------------------
  //
	// All environments must provide a way to evaluate the given actions. 
	// The evaluation function takes an action index as parameter and returns 
	// the response class. How the response is computed is highly application 
	// dependent, and it must be implemented by a derived environment class.

	virtual Response Evaluate( const Action & ChosenAction ) = 0;

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
  // 
	// The constructor only initialises the number of actions. It should not be 
	// possible to instantiate a base class learning environment directly, and 
	// therefore the constructor is protected so it can only be used by derived 
	// classes.
	
protected:
	
	LearningEnvironmentBase( const ActionIndex ActionSetCardinality )
	: EnvironmentType( Model::Base ), Type( EnvironmentType ), 
	  NumberOfActions( ActionSetCardinality )
	{ }
	
	// There is also a constructor for copying a basic environment - which is 
	// basically identical to the normal constructor. However, there is a minor 
	// point related to inheritance: The Other environment could potentially be 
	// an environment derived from the base model, and therefore having set the 
	// type to its model type (see the explanation above)
	
	LearningEnvironmentBase( 
			const LearningEnvironmentBase & Other )
	: EnvironmentType( Other.EnvironmentType ), Type( EnvironmentType ),
	  NumberOfActions( Other.NumberOfActions )
	{ }

public:
	
	// There is no default constructor to enforce that the base environment cannot
	// be instantiated as a separate object.
	
	LearningEnvironmentBase( void ) = delete;
	
	// There is also a public virtual destructor to ensure correct de-allocation 
	// as this class should be inherited.
	
	virtual ~LearningEnvironmentBase( void )
	{}
};

// -----------------------------------------------------------------------------
// Standard template environment 
// -----------------------------------------------------------------------------
// 
// The learning environment takes a model type as parameter, and defines the 
// corresponding types. The Q model environment will need the type of the 
// feedback values type, and the S model environment may want a different 
// accuracy than double. The feedback type is therefore declared as a double 
// by default, and required by the Q model environment, and fixed by the P model
// environment.

template< Model EnvironmentModel, typename FeedbackType = double >
class LearningEnvironment;


/*==============================================================================

 S-Model Environment

==============================================================================*/
//
// The Strength model (S-model) provides the feedback as a real number, and 
// it is a partial specialisation of the generic environment. 
// 
// It should be noted that the S-model environment is an abstract environment 
// since there is not possible to define a generic way to give a response for 
// a chosen action. Hence, the evaluation function must be defined by the 
// application using the S-model environment.

template< typename FeedbackType >
class LearningEnvironment< Model::S, FeedbackType >
: public LearningEnvironmentBase< FeedbackType >
{	
	// The default value for the feedback type is double, but if it is explicitly
	// given it could be anything, and it is necessary to check at compile 
	// time that the feedback type given is a real number (float, double, or
	// long double depending on the desired precision)
	
	static_assert( std::is_floating_point< FeedbackType >::value, 
								 "S model environment feedback type must be a real number!" );
	
private:
	
	using EnvironmentBase = LearningEnvironmentBase< FeedbackType >;
		
public:
	
	// The various definitions of the base class is reused
	
	using EnvironmentBase::Type;
	
	using Action       = typename EnvironmentBase::Action;
	using Response     = typename EnvironmentBase::Response;
	using ResponseType = typename EnvironmentBase::ResponseType;
		
  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
  // 
	// The constructor only initialises the number of actions
	
	LearningEnvironment( const ActionIndex ActionSetCardinality )
	: EnvironmentBase( ActionSetCardinality )
	{	EnvironmentBase::EnvironmentType = Model::S;	}
	
	// The copy constructor is trivial as it just transfers the number of actions,
	// and sets the environment type to the type of the other since it could be 
	// a type derived from the S-model.
	
	LearningEnvironment( 
		const LearningEnvironment< Model::S, FeedbackType > & Other )
	: LearningEnvironment( Other.NumberOfActions )
	{ EnvironmentBase::EnvironmentType = Other.EnvironmentType; }
	
	// The default constructor has no meaning.
	
	LearningEnvironment( void ) = delete;
	
	// There is a virtual destructor to ensure that the derived classes properly
	// clean up
	
	virtual ~LearningEnvironment( void )
	{}
};

/*==============================================================================

 Q-Model Environment

==============================================================================*/
//
// The specialisation for the Q-Model is slightly more complicated in that it
// needs to deal with a given set of response values. These values will have 
// types that needs to be given

template< typename QResponseType >
class LearningEnvironment< Model::Q, QResponseType >
: public LearningEnvironmentBase< QResponseType >
{
private:
	
	using EnvironmentBase = LearningEnvironmentBase< QResponseType >;
	
public:
	
	// The type is defined as an alias the base type, as well as the response 
	// type and the response class.
	
	using EnvironmentBase::Type;
	
	using Action       = typename EnvironmentBase::Action;
	using Response     = typename EnvironmentBase::Response;
	using ResponseType = typename EnvironmentBase::ResponseType;
		
  // ---------------------------------------------------------------------------
  // Evaluation
  // ---------------------------------------------------------------------------
	//
	// The environment will select a feedback from a vector of possible responses
	
private:
	
	std::vector< QResponseType > ResponseValue;
	
	// For each action there is a vector of selection probabilities, and the 
	// response is selected according to this probability distribution. This 
	// N x M matrix is stored as an array of probability masses in order to 
	// ensure that each row is a proper probability mass, and to facilitate the 
	// reuse of the random selection functions for probability masses.
	
	std::vector< ProbabilityMass< double > > SelectionPDF;
	
	// There is an evaluation function that takes an action and returns the 
	// response class for that action. It uses the chosen action to look up 
	// the right selection probabilities, and then generates a random index 
	// from these. This could be one complex statement, but it has been broken 
	// up for readability.
	
public:
	
	virtual Response Evaluate( const Action & ChosenAction ) override
	{
		ActionIndex 
		ResponseIndex = Random::Index( SelectionPDF.at( ChosenAction ) );
		
		return Response( ChosenAction, ResponseValue.at( ResponseIndex ) );
	}

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
	//
	// The number of responses may only be defined at run-time, and it is 
	// therefore stored as a parameter defined by the constructor. 
	
	const ActionIndex ResponseSetSize;

	// The constructor is given the set of response values and the selection 
	// probability masses, and essentially copies these to the internal structures
	// The number of actions is deduced from the length of the reward 
	// probabilities, and it is subsequently verified that all of the probability
	// masses have the same number of probabilities.
	
	LearningEnvironment( 
			const std::vector< QResponseType >             & ResponseSet, 
			const std::vector< ProbabilityMass< double > > & RewardProbabilities )
	: LearningEnvironmentBase< QResponseType >( RewardProbabilities.size() ),
	  ResponseValue( ResponseSet ), SelectionPDF( RewardProbabilities ),
	  ResponseSetSize( ResponseSet.size() )
	{	
		for ( const auto & ResponseDistribution : RewardProbabilities )
			if( ResponseDistribution.size() != ResponseSetSize )
		  {
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ":"
										 << "Q-Model environment: All response selection "
										 << "probability density function must have the same "
										 << "length (" << ResponseSetSize << ")";
										 
			  throw std::invalid_argument( ErrorMessage.str() );
			}
			
		EnvironmentBase::EnvironmentType = Model::Q;
	}
	
	// The copy constructor simply copies the probabilities using the above 
	// constructor.
	
	LearningEnvironment( 
		const LearningEnvironment< Model::Q, QResponseType > & Other )
	: LearningEnvironment( Other.ResponseValue, Other.SelectionPDF )
	{ EnvironmentBase::EnvironmentType = Other.EnvironmentType; }
	
	// There is no default constructor for the Q-Model
	
	LearningEnvironment( void ) = delete;
	
	// There is a virtual destructor to ensure that the derived classes properly
	// clean up
	
	virtual ~LearningEnvironment( void )
	{}
};

/*==============================================================================

 P-Model Environment

==============================================================================*/
//
// Since the P-model is a specialisation of the Q-model for binary actions, 
// the response type must be defined first.

enum class PModelResponse : bool
{
	Penalty = false,
	Reward  = true
};

// The specialisation for the P-model _is_ a Q-model, and it will just extend
// that environment.

template< >
class LearningEnvironment< Model::P >
: public LearningEnvironment< Model::Q, PModelResponse >
{
private:
	
	using EnvironmentBase = LearningEnvironment< Model::Q, PModelResponse >;
	
public:
	
	// The type is defined as an alias the base type, as well as the action and 
	// the responses
	
	using EnvironmentBase::Type;
	
	using Action       = typename EnvironmentBase::Action;
	using Response     = typename EnvironmentBase::Response;
	using ResponseType = typename EnvironmentBase::ResponseType;
			
private:
	
	// The constructor will get a vector of reward probabilities and this will 
	// be expanded for the number of actions supported with one probability mass
	// for each possible action. Note that since this will be used to initialise 
	// the constructor of the Q-model base class, which will define the number 
	// of actions, the number of actions must be taken from the probability of 
	// rewards.
	
	inline std::vector< ProbabilityMass< double > >
	RewardProbabilitiesPerAction( 
			const std::vector< Probability< double > > & ProbabilityOfReward  )
	{
		std::vector< ProbabilityMass< double > > 
				 RewardProbabilities( ProbabilityOfReward.size() );
		
		for ( ActionIndex idx = 0; idx < ProbabilityOfReward.size(); idx++ )
			RewardProbabilities[ idx ] = 
								{ Probability< double>( 1 - ProbabilityOfReward[ idx ] ), 
									Probability< double >( ProbabilityOfReward[ idx ] ) };
								
	  return RewardProbabilities;
	}
	
	// The constructor will then take the vector of probabilities and expand this 
	// to probabilities per action with the aid of above function.
	
public:
	
	LearningEnvironment( 
			const std::vector< Probability< double > > & RewardProbabilities )
	: EnvironmentBase( { PModelResponse::Penalty, PModelResponse::Reward },
		RewardProbabilitiesPerAction( RewardProbabilities )  )
	{ EnvironmentBase::EnvironmentType = Model::P; }
	
	// The copy constructor simply calls the Q-model constructor.
	
	LearningEnvironment( const LearningEnvironment< Model::P > & Other )
	: EnvironmentBase( Other )
	{ EnvironmentBase::EnvironmentType = Other.EnvironmentType; }
	
	// The default constructor is deleted
	
	LearningEnvironment( void ) = delete;
	
	// There is a virtual destructor to ensure that the derived classes properly
	// clean up
	
	virtual ~LearningEnvironment( void )
	{}
};

/*==============================================================================

 Action subset environment

==============================================================================*/
//
// An automaton may be confined to a subset of the possible actions of the 
// environment. The subset environment is a wrapper for the original automata 
// type, but it provides its own action generator mapping the subset action 
// indices to the indices of the wrapped environment. 
//
// It should be noted that the environments may use reward probabilities in 
// action evaluation. When selecting a subset of the actions one could either 
// copy only the reward probabilities connected with those action, or copy 
// all actions but ensure that only the actions of the subset will be selected.
// The benefit of the latter approach is that one can use the copy constructor 
// without any knowledge of the automata type, and therefore this approach is 
// taken.
//
// A typical scenario is to create automata working on different subsets that 
// are permutations of the full set of actions. In such a scenario, the cost 
// of always copying the reward probabilities for each subset could be an 
// efficiency overhead. If the subset environment is a wrapper for a basic 
// environment, then the actual automata characteristics will not be copied, 
// and the subset environment will not have an Evaluate function. It is then 
// the responsibility of the application to ensure that the evaluation function
// is called on some other environment. 

template< class StochasticEnvironment >
class SubsetEnvironment : public StochasticEnvironment
{
public:

	// The stochastic environment encapsulated by the subset environment is 
	// stored for reference by automata needing to know the real environment 
	// class type.
	
	using FullSetEnvironment = StochasticEnvironment;
		
	// The type of the environment is taken from the care-of environment. 
	
	using FullSetEnvironment::Type;
	
	// The number of subset actions will be stored and therefore used by the 
	// automata as this will shadow the definition in the care-of environment
	
	const ActionIndex NumberOfActions;
	
private:
	
	// The mapping between subset action indices and real indices are stored as 
	// a simple map. However, since this will be used in the action generation 
	// function, it must remain available for as long as any automata based on 
	// this subset environment remains available. It can therefore either be 
	// copied entirely to the action generation function, which may lead to 
	// multiple copies, or it can be referenced from them. A smart pointer is 
	// used to store the reference so that the map can be destroyed when it is 
	// no longer referenced.
	//
	// It should be noted that the map is efficiently implemented as a vector
	// with the same length as the cardinality of the subset of the actions, 
	// and the stored values are the indices of the corresponding actions in the 
	// full vector. The given map must be unique, and this must be ensured by 
	// the user as checking this is relatively expensive. 
	
	std::shared_ptr< std::vector< ActionIndex > > SubsetMap;
	
public:
	
	// The Action object is the same as the action object of the care-of 
	// environment
	
	using Action = typename FullSetEnvironment::Action;
	
	// With this action mapping, it is relatively easy to define the action 
	// generator, and this returns an action in the full set of actions that 
	// can be generated by the full action set evaluation function.
	
	virtual std::function< Action( const ActionIndex ) > 
	ActionGenerator( void ) const override
	{
		return [ SubsetMapCopy = SubsetMap, ActionCardinality = NumberOfActions ]
					 ( const ActionIndex SelectedAction )->Action{
			if( SelectedAction < ActionCardinality )
				return Action( SubsetMapCopy->at( SelectedAction ) );
			else
		  {
				std::ostringstream ErrorMessage;
				
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
										 << "Selected subset action index " << SelectedAction
										 << " must be less than the number of subset actions "
										 << ActionCardinality;
										 
			  throw std::invalid_argument( ErrorMessage.str() );
			}
		};
	}
	
	// The constructor takes an instance of the care-of environment and the 
	// action mapping vector. The care-of environment is copied to the base 
	// class, and the number of actions is set based on the number of elements 
	// in the mapping vector. 
	
	SubsetEnvironment( const FullSetEnvironment & GivenEnvironment, 
										 const std::vector< ActionIndex > & SubsetMapping )
	: FullSetEnvironment( GivenEnvironment ),
	  NumberOfActions( SubsetMapping.size() )
	{
		FullSetEnvironment::EnvironmentType = GivenEnvironment.Type;
		
		if ( NumberOfActions <= GivenEnvironment.NumberOfActions )
		  SubsetMap = std::make_shared< std::vector<ActionIndex> >( SubsetMapping );
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Number of actions in the subset of actions ("
									 << NumberOfActions << ") must be less or equal to the "
									 << "number of actions in the full set ("
									 << GivenEnvironment.NumberOfActions << ")";
									 
		  throw std::invalid_argument( ErrorMessage.str() );
		}
	}
};

} 		 // name space LA
#endif // LEARNING_ENVIRONMENT
