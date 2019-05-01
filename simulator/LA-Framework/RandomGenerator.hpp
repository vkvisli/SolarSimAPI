/*=============================================================================
  Random generator
  
  Many conclusions drawn on the basis of Monte Carlo experiments have been 
  invalidated by the use of poor random generators. A common mistake is to use 
  linear congruential pseudo-random generators, or to use multiple generators 
  since it is always better to generate one, long sequence of random numbers 
  from one generator than several short series from a set of generators. 

  In 2004 the below framework of functions was completed based on an 
  implementation of the random number generator found in "Numerical recipes
  in C++". This file transfers the structure to use the new random number 
  generator framework of C++, and otherwise maintains backward compatibility.

  Thus in the ideal situation one would use a complex generator like the 
  Marsenne Twister [1], and the let all random numbers of an application use 
  one single instance of this generator. 
   
  Note that the random number generator is defined as a global class and it 
  is therefore not thread safe without the necessary mutex encapsulation which
  is work to be done. The reason for using a global generator is that the 
  quality of a long sequence of random numbers is normally better than the 
  quality of many short sequences (even with different seeds). 

  References:
  
  [1] M. Matsumoto and T. Nishimura (1998): "Mersenne twister: a 
		  623-dimensionally equidistributed uniform pseudo-random number generator" 
		  ACM Transactions on Modeling and Computer Simulation. 8 (1): 3–30. 
  
  First version: Geir Horn, SINTEF, 2011
  Revised: Geir Horn, University of Oslo, 2013 (C++11 features)
	     		 Geir Horn, University of Oslo, 2014 (thread safe version)
	     		 Geir Horn, University of Oslo, 2015 (name space version)
	     		 Geir Horn, University of Oslo, 2016 (Index probability mass)
 					 Geir Horn, University of Oslo, 2017, Major revision:
							- Changed the encapsulation of the engine
						  - Better integration with the standard distributions

  Author and Copyright: Geir Horn, 2011-2017
  License: LGPLv3
=============================================================================*/

#ifndef RANDOM_GENERATOR
#define RANDOM_GENERATOR

// First a we disable the standard macro definitions of max and min provided
// by Microsoft Visual Studio if the code is compiled with VC++

#ifdef max
  #undef max
#endif

#ifdef min
  #undef min
#endif

#include <algorithm>   				// Correct maximum and minimum definitions
#include <numeric>					  // To sum vectors
#include <random>							// The standard random generators
#include <mutex>							// To protect the random generator engine
#include <type_traits>				// Essential for meta-programming
#include <sstream>					  // For advanced error reporting
#include <stdexcept>				  // Standard exceptions

#include <vector>							// For the random vector probabilities 
#include <map>							  // For the initialisation of random vectors
#include <boost/numeric/interval.hpp>  // Intervals

#include "ProbabilityMass.hpp" // Probability mass vectors

namespace Random
{
/*=============================================================================

 Generator

=============================================================================*/
//
// Following the above discussion it is mandatory to have only one generator 
// engine for for all random variates. Ideally this could be encapsulated as 
// a static element of the random variate, but the variate class has to be 
// conditioned on the type of distribution it is drawn from, and therefore 
// it will be a template. A local variable in a template class is bound to
// the template argument, and for this reason the generator cannot be embedded 
// in the random variate class.
//
// There is only one generator on each endpoint. Fundamentally, the random 
// generator only protects the access to the generator engine and ensures that 
// it is properly seeded with the high resolution system clock. 

extern
class GeneratorEngine 
{
private:
	
	// The standard instantiation of the Mersenne Twister is used for the 
	// generation
	
	#if __x86_64__ || __ppc64__ || _WIN64
    std::mt19937_64 MersenneTwister;
  #else
    std::mt19937 MersenneTwister;
  #endif

	// Access to this is protected by a mutex
		
	std::mutex Access;

public:
		
	// The constructor initialises the Mersenne Twister with the value of 
	// the current time of the system clock.
	
	GeneratorEngine( void )
	: MersenneTwister( 
						std::chrono::system_clock::now().time_since_epoch().count() ),
		Access()
	{ }
	
	// There are many different distributions that may be used with the engine 
	// to generate a given number. In order to simplify the syntax, a template 
	// operator is defined taking the distribution as argument and returning 
	// the result type of the given distribution.
	
	template< class Distribution >
	typename Distribution::result_type
	operator() ( Distribution & DensityFunction )
	{
		std::lock_guard< std::mutex > Lock( Access );
		
		return DensityFunction( MersenneTwister );
	}
	
} Generator;

/*=============================================================================

 Random Variates

=============================================================================*/
//
// The user object is the random number which is a template class taking the 
// corresponding distribution as argument. Its constructor forwards the given 
// arguments to the constructor of the distribution.
//
// Once constructed the class offers an operator () to generate a random number 
// according to the distribution used to construct the random variate.

template< class Distribution >
class Variate : public Distribution
{
public:
	
	using result_type = typename Distribution::result_type;
  using Distribution::operator();
	
	// ---------------------------------------------------------------------------
	// Computing numbers
	// ---------------------------------------------------------------------------
	
	inline result_type operator() ( void )
	{
		return Generator( *this );
	}
	
	// An alias is defined for convenience
	
	inline result_type Value( void )
	{
		return Generator( *this );
	}

	// ---------------------------------------------------------------------------
	// Constructor
	// ---------------------------------------------------------------------------
	//
	// The constructor simply forwards the given parameters to the constructor of
	// the given distribution.
	
	template< typename ... DistributionParameters >
	Variate( DistributionParameters && ... Parameters )
	: Distribution( std::forward< DistributionParameters >( Parameters )... )
	{ }
};

/*=============================================================================

 Standard aliases

=============================================================================*/
//
// In order to facilitate easy use of the above random variate template, certain 
// partial and complete aliases are defined. These uses the default template 
// parameters for the return values.
//
// Uniform distributions - partial specialisation

template< class DoubleType >
using UniformReal = Variate< std::uniform_real_distribution<DoubleType> >;

template< class IntegerType >
using UniformIntegral = Variate< std::uniform_int_distribution<IntegerType> >;

// Uniform distributions - full specialisation

using Double  = UniformReal< double >;
using Integer = UniformIntegral< long >;

// The other types are named by the distribution, and normally using double as
// the return types.

using Bernoulli   = Variate< std::bernoulli_distribution >;
using Binomial    = Variate< std::binomial_distribution<> >;
using Caucy       = Variate< std::cauchy_distribution<> >;
using ChiSquare   = Variate< std::chi_squared_distribution<> >;
using Exponential = Variate< std::exponential_distribution<> >;
using FisherF     = Variate< std::fisher_f_distribution<> >;
using Gamma       = Variate< std::gamma_distribution<> >;
using Geometric   = Variate< std::geometric_distribution<> >;
using LogNormal   = Variate< std::lognormal_distribution<> >;
using NegBinomial = Variate< std::negative_binomial_distribution<> >;
using Normal      = Variate< std::normal_distribution<> >;
using Poisson     = Variate< std::poisson_distribution<> >;
using StudentT    = Variate< std::student_t_distribution<> >;
using Weibull     = Variate< std::weibull_distribution<> >;

/*=============================================================================

 Beta distribution

=============================================================================*/
//
// The beta distribution has been left out of the collection of distributions
// probably because it can be defined by two Gamma variates. 

template< typename RealType = double >
class beta_distribution
{
public:
	
	static_assert( std::is_floating_point< RealType >::value, 
								 "The Beta distribution must return a real number" );
	
	using result_type = RealType;
	
private:
	
	// There are two gamma distributions used to generate the Beta distribution
	
	std::gamma_distribution< RealType > GammaAlpha, GammaBeta;
	
public:
	
	// The generation of the beta distributed number will be done in the 
	// standard operator taking the generator engine as input. It should be 
	// noted that the access to the generator is locked when this operator is 
	// called, and so it is most efficient to generate both gamma variates at the 
	// same time.
	
	template< class Engine >
	result_type operator() ( Engine & TheGenerator )
	{
		result_type gAlpha = GammaAlpha( TheGenerator ),
								gBeta  = GammaBeta ( TheGenerator );
								
		return gAlpha / ( gAlpha + gBeta );
	}
	
	// The constructor takes the parameter for each of the gamma functions, and
	// a scale parameter. 
	
	beta_distribution( RealType alpha, RealType beta, RealType scale = 1.0 )
	: GammaAlpha( alpha, scale ), GammaBeta( beta, scale )
	{}
	
};

// Then the standard alias can be defined also for the beta distribution

using Beta = Variate< beta_distribution<> >;

/*=============================================================================

 Empirical density functions

=============================================================================*/
// 
// There is a separate class that can take any set of numbers and create an 
// empirical density function. However, this class builds the distribution from
// scratch and this overhead can be avoided if the distribution is already 
// available as a probability mass with the convenient property that it is 
// normalised. 
//
// The typical problem is that one would like to use this probability mass to 
// select randomly an index of this probability mass according to the 
// probabilities. Consider as an example three probabilities p[1] = 0.2, 
// p[2] = 0.5, p[3] = 0.3. Selecting randomly an index according to this 
// empirical distribution should give "2" in about 50% of the draws, "3" in 
// 30% of the draws and "1" in 20% of the draws. The following function 
// takes a probability mass as argument and returns a random index based on 
// this mass.
//
// Since the probability mass is a template on the real value type and the 
// allocator type used for the vector, also the index function is formally a 
// template on the same parameters, but these should be automatically deduced 
// by the compiler from the given probability mass.

template< class RealType >
auto Index( const ProbabilityMass< RealType > & PDF )
-> typename ProbabilityMass< RealType >::IndexType
{
  // The algorithm uses the standard discrete distribution to generate the 
	// index of the probability mass in the set {0,..,n-1}
	
	Variate< 
		std::discrete_distribution< 
			typename ProbabilityMass< RealType >::IndexType > >
					ElementIndex( PDF.cbegin(), PDF.cend() );
  
  return ElementIndex();
}

/*=============================================================================

 Functional forms

=============================================================================*/

// First the normal way to generate a random double in the interval [0,1) it 
// simply delegates the computation to the generator object. One could think 
// that the generator could be called Number to avoid this function, however
// the idea is to have other Number generating functions, and you cannot 
// overload an object with functions. For this reason we do need this 
// delegation.

inline double Number (void)
{
	Double Value(0.0, 1.0);
  return Value();
}

// If a uniform random number in a different interval is desired, one could 
// imagine to use a random number in the interval [0,1) and then scale it 
// to the correct value. Although this is what the standard uniform 
// distributions might do, they could apply more elaborate algorithms based 
// on the quality and range of the generator engine used. For instance the 
// Marsenne twister generates integers by default and it seems a waste to 
// first convert this to a double, scale it, before converting it back to 
// an integer. For this reason we construct the transformation object and 
// then use it to generate and scale the random number in one operation.
//
// The following code is based on the idea of Luc Touraille in his Stack
// Overflow response at 
// http://stackoverflow.com/questions/3458510/how-to-check-that-templates-parameter-type-is-integral
// A good compiler should be able to remove the unused branch of the run-time 
// version in its optimisation step since the actual value of the
// condition is inferred at compile time. However, as Touraille notes, if
// the code in the branches are very different for the variants, then one 
// should rather use template specialisation. C++17 allows a compile time 
// "if constexpr" that removes the need for the specialisation. 
//
// The many different version of standard types does create a minor problem 
// as they confuse the compiler. For instance if one wants a random integer 
// between 'unsigned int a' and 'long b' one should obtain a long integer. 
// For this reason the function is defined with two possibly unequal types 
// and the common type is used for the return value.

template< typename Lower, typename Upper,
					typename ReturnType = typename std::common_type_t< Lower, Upper > >
inline ReturnType Number( Lower LowLimit, Upper HighLimit )
{
	static_assert( std::is_arithmetic< ReturnType >::value, 
								 "Non-numeric type given to Random::Number" );
	
	if constexpr ( std::is_integral< ReturnType >::value )
  {
		UniformIntegral< ReturnType > U(LowLimit, HighLimit);
		return U();
	}
	else if constexpr ( std::is_floating_point< ReturnType >::value )
	{
		UniformReal< ReturnType > U(LowLimit, HighLimit);
		return U();
	}
}

// If a boost interval is given the above number function is called with the 
// interval limits.

template< typename Type, class IntervalPolicies >
inline Type 
Number( const boost::numeric::interval< Type, IntervalPolicies > & Interval )
{
	return Number( Interval.lower(), Interval.upper() );
}

// The standard random shuffle uses a special form of the random generators
// taking an integer n as argument and returning a random number in the range
// [0,(n-1)]. Note that in many cases on will have a more readable code if one 
// uses the lambda function
// 	[] (unsigned int n)->unsigned int { Random::Number(0, n-1); }
// where the Number function used is defined below.
//
// The function is defined in terms of a size_t variable since this is the 
// normal definition of a std::vector index.

std::size_t IndexShuffle (std::size_t n)
{
  UniformIntegral< std::size_t > U(0, n-1);
  return U();
}

/*=============================================================================

 Probability vector

=============================================================================*/
//
// A random probability vector is characterised by having all elements in the 
// interval [0,1], and in addition they all belong to the N-1 simplex since 
// they should add to unity. 
// 
// Generating such a random vector is based on a series of observations:
//
// 1) The Dirichlet distribution is uniform on the N simplex if all the 
//    distribution parameters are unity.
// 2) A random point of a K-dimensional Dirichlet distribution can be 
//    generated by generating K Gamma distributed variables, one for each 
//    of the Dirichlet distribution parameters, and then normalise. 
// 
// Combining the two observations let us generate the random vector by first 
// generating the K gamma variates with parameter unity, and then normalise.
//
// Ths approach is inspired by the response at
// https://stats.stackexchange.com/questions/14059/generate-uniformly-distributed-weights-that-sum-to-unity

template< typename RealType = double >
class ProbabilityVector : public ProbabilityMass< RealType >
{
	static_assert( std::is_floating_point< RealType >::value, 
								 "A probability vector must contain real numbers" ); 

public:
	
	ProbabilityVector( 
	const typename ProbabilityMass< RealType >::IndexType VectorSize )
	: ProbabilityMass< RealType >( VectorSize )
	{
		Gamma G(1.0, 1.0);
		RealType VariateSum(0);
		
		// Creating the random variates
		
		std::vector< RealType > VariateValues( VectorSize );
		
		for ( RealType & Value : VariateValues )
		{
			Value = G();
			VariateSum = Value;
		}
			
		// Then the generator version of the assignment can be used to set
		// the probabilities returned from a normalising lambda function
		
		assign( [&]( typename ProbabilityMass< RealType >::IndexType index )
								->Probability< RealType >{
								return VariateValues[ index ] / VariateSum;
		});
	}		
};

/*=============================================================================

 Random vectors

=============================================================================*/
// 
// The basic random vector functional takes a vector sum and vector of element
// ranges as input. The element range is taken literately so that if the asked 
// sum is larger than what can be achieved setting all elements to their 
// maximum value, then the functional throws an exception. The same goes for
// the minimum value. If the requested sum is impossible to achieve with all
// values set to their minimum value, an exception is thrown. This interval is 
// open since a vector of only the maximum or minimum elements is not a random 
// vector. The length of the returned vector equals the number of element
// ranges given.

template< typename RealType = double >
class Vector : public std::vector< RealType >
{
public:
	
	static_assert( std::is_floating_point< RealType >::value, 
								 "A random vector must contain real numbers" ); 

	using IndexType = typename std::vector< RealType >::size_type;
	
	// ---------------------------------------------------------------------------
	// Main constructor
	// ---------------------------------------------------------------------------
	//
  // The main constructor building the vector based on two vectors giving 
	// the upper and lower bounds for each element in the resulting vector. 
	// After checking the legality of the input vectors, it will calculate 
	// the range of each element, i.e. the differences between the upper and 
	// lower limits. 

  Vector ( RealType VectorSum, 
			     std::vector< RealType > & LowerLimits,
			     std::vector< RealType > & UpperLimits )
	: std::vector< RealType > ( LowerLimits.size(), 0.0 )
  {
    // Error checking: The size of the lower and upper limits vectors 
		// must equal.
		
	  if ( LowerLimits.size() != UpperLimits.size() ) 
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << " : "
									 << "Random::Vector size of lower limit vector (" 
									 << LowerLimits.size() << ") is different from the "
									 << "size of the upper limit vector ("
									 << UpperLimits.size() << ")";
									 
			 throw std::invalid_argument( ErrorMessage.str() );
		}
		
		// Computing the total value of the upper limits to chat this is 
		// larger than the given vector sum. If not, then it is not possible 
		// to construct a vector with the given sum even by setting all elements
		// to their maximal values.
		
		RealType TotalUpper = std::accumulate( UpperLimits.begin(), 
																					 UpperLimits.end(), 
																					 RealType(0) );
		
		// Error checking: Verifying that the vector is constructable
		
		if ( TotalUpper < VectorSum )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << " : "
									 << "Random::Vector cannot be constructed because the "
									 << "sum of the upper limits (" << TotalUpper 
									 << ") is less than the requested sum of the vector ("
									 << VectorSum << ")";
									 
			 throw std::invalid_argument( ErrorMessage.str() );
		}

		// In the special case that the the vector sum only can be achieved by 
		// setting all values to the upper bound, a simple copy is performed 
		// and the vector has been constructed.
		
		if ( TotalUpper == VectorSum )
		{
			std::copy( UpperLimits.begin(), UpperLimits.end(), this->begin() );
			return;
		}
		
		// Error checking: The same test is done to see if the sum of the lower
		// bounds are larger than the desired vector sum, in which case the 
		// vector is not constructable 
		
		RealType TotalLower = std::accumulate( LowerLimits.begin(), 
																					 LowerLimits.end(), 
																					 RealType(0) );
		
		if ( VectorSum < TotalLower )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << " : "
									 << "Random::Vector cannot be constructed because the "
									 << "requested sum of the vector (" << VectorSum
									 << ") is less than the sum of the lower limits ("
									 << TotalLower << ")";
									 
			throw std::invalid_argument( ErrorMessage.str() );
		}
		
		// The lower limits are assigned as the initial value of the elements.
		
		std::copy( LowerLimits.begin(), LowerLimits.end(), this->begin() );
		
		// In the case that the desired vector sum only can be achieved by 
		// setting the values to their lower limits, there is nothing more to 
		// be done.
		
		if ( VectorSum == TotalLower ) return;
		
		// Since the lower limits have to be allocated to all elements, then 
		// the amount left for random distribution is really only 
		
		VectorSum -= TotalLower;
		
		// The elements should be assigned a random value in the interval 
		// [ Lower_i, Upper_i ] which is equivalent to draw a random variable 
		// in the range [0,R_i] where R_i = Upper_i - Lower_i is the range of 
		// the element, and then assign the element the value Lower_i + R_i
		// These ranges are stored in a map sorted on the range values.
		
		std::map< IndexType, RealType >	Ranges;
		
		// This map is populated by iterating the limit vectors checking that 
		// The upper limits is as least as large as the lower limit for each 
		// element, and not storing elements whose range is zero.
		
		for ( IndexType i = 0; i < UpperLimits.size(); i++ )
		{
			RealType TheRange = UpperLimits[i] - LowerLimits[i];
			
			if ( TheRange > 0 )
				Ranges.emplace( i, TheRange );
			else if ( TheRange < 0 )
		 {
			 std::ostringstream ErrorMessage;
			 
			 ErrorMessage << __FILE__ << " at line " << __LINE__ << " : "
									  << "Random::Vector requires that the upper limits are "
										<< "greater or equal to the lower limits. This is not "
										<< "the case for element " << i;
			 
			 throw std::invalid_argument( ErrorMessage.str() );
		 }
		}
		
		// The basic algorithm is now to compute a random partition of the 
		// requested vector sum, and then allocate this to the respective 
		// elements. This process is repeated until the vector sum has been 
		// completely allocated.
		
		while ( (VectorSum > 0) && (!Ranges.empty()) )
		{
			// First find the random portions of the vector sum over the 
			// available elements that have non-zero ranges.
			
			ProbabilityVector< RealType >	RandomDistribution( Ranges.size() );
			
			// The elements are then processed one by one and the random share 
			// allocated to the corresponding element.
			
			auto 		 RelativeShare = RandomDistribution.begin();
			RealType TotalAllocation(0);
			
			for ( auto & Element : Ranges )
			{
				RealType RandomAllocation = std::min( Element.second, 
																							*RelativeShare * VectorSum );
				
				// Add the allocation to the corresponding element. Since Ranges is
				// a map, the elements are pairs where the first element is the 
				// index and the second element is the range value
				
				at( Element.first ) += RandomAllocation;
				
				// Decrease the remaining range for this element and check if it 
				// should participate to the next round of allocations
				
				Element.second -= RandomAllocation;
				
				if ( Element.second <= 0 )
					Ranges.erase( Element.first );
				
				// Finally making ready for the next iteration by recording the 
				// amount allocated to this element, and advance the share iterator
				// to the next element
				
				TotalAllocation += RandomAllocation;
				++RelativeShare;
			}
			
			// Then the remaining vector sum is computed
			
			VectorSum -= TotalAllocation;
		}
		
		// At this point the remaining amount to allocate should be zero, or 
		// there is a serious problem with the above algorithm....A special
		// test for this invariant could have been done, but it should only 
		// be a waist of processing power. 
  }

	// ---------------------------------------------------------------------------
	// Other constructor variants
	// ---------------------------------------------------------------------------
	//
  // The second version is an interface to the first one and may be used if 
  // all elements are to have the same range (like [0..1]). Note that in this
  // case the length of the vector must be given as the first parameter.

  inline Vector ( unsigned int Size,   RealType VectorSum,
							    RealType LowerLimit, RealType UpperLimit  )
  : Vector( VectorSum, std::vector< RealType >( Size, LowerLimit ), 
											 std::vector< RealType >( Size, UpperLimit )  )
  { }

  // Then there are two variants of the above where either the lower limit
  // or the upper limit is given as a vector and the other is a scalar.

  inline Vector ( RealType VectorSum,
							    RealType LowerLimit, 
							    std::vector< RealType > & UpperLimits  )
  : Vector( VectorSum, std::vector<RealType>( UpperLimits.size(), LowerLimit ), 
											 UpperLimits )
  { }

  inline Vector ( RealType VectorSum,
							    std::vector< RealType > & LowerLimits, 
							    double UpperLimit  )
	: Vector( VectorSum, LowerLimits, 
					  std::vector< RealType >( LowerLimits.size(), UpperLimit) )
  { }

  // It is possible to achieve the same with iterators for the limits.
  // Also in this case will local vectors be produced whose elements are
  // set to the values obtained from the iterators. Note that it is assumed
  // that the vectors pointed to by the iterators have the same size.

  inline Vector ( RealType VectorSum,
							    typename std::vector< RealType >::iterator LowerBegin, 
							    typename std::vector< RealType >::iterator LowerEnd,
							    typename std::vector< RealType >::iterator UpperBegin )
  : Vector( VectorSum, std::vector< RealType >( LowerBegin, LowerEnd ), 
						std::vector< RealType >( UpperBegin, 
																		 UpperBegin + ( LowerEnd - LowerBegin ) ) )
  { }

};

} // Name space Random

#endif //RANDOM_GENERATOR
