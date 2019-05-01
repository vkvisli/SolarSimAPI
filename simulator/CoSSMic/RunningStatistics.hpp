/*=============================================================================
  Running Statistics
  
  This defines a class that is a simple C++ encapsulation of the Gnu Scientific 
  Library's functions for computing the running statistics. The compiled code
  should therefore be linked with the GSL library available with most platforms.
  
	Author and Copyright: Geir Horn, 2017
	License: LGPL 3.0
=============================================================================*/

#ifndef GSL_RUNNING_STATISTICS
#define GSL_RUNNING_STATISTICS

#include <stddef.h>						// For size_t
#include <cmath>						  // Standard mathematical functions
#include <gsl/gsl_rstat.h>		// Estimators for running statistics
#include <gsl/gsl_roots.h>    // For solving Chebychev's inequality
#include <gsl/gsl_errno.h>		// For error reporting
#include <sstream>						// For reporting errors
#include <ostream>						// For inserting data 
#include <stdexcept>					// For throwing errors
#include <map>		  				  // For keeping quantiles
#include <initializer_list>   // For defining quantiles

#include <iostream>
namespace GSL
{

class RunningStatistics
{
private:
	
	gsl_rstat_workspace * Accumulator;
	
  // ---------------------------------------------------------------------------
  // Quantiles 
  // ---------------------------------------------------------------------------
  //
	// Quantiles of the distribution requires that a special data structure is
	// allocated for each quantile that should be estimated. Since there can be 
	// more quantiles to be estimated, the probability and data structure is kept
	// in a map for quick lookup and to ensure that only one quantile structure
	// will be used per probability
	
	class QuantileData : public std::map< double, gsl_rstat_quantile_workspace * >
	{
	public:
		
		// Testing the given probability is an activity that all interface 
		// function must do, and it throws out of range if the given probability is
		// illegal.
		
		bool LegalProbability( double Probability )
		{
			if ( (0.0 < Probability) && ( Probability < 1.0) )
				return true;
			else
 		  {
		    std::ostringstream ErrorMessage;
		    
		    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								     << "Quantile probability must be in (0.0, 1.0)";
				 
		    throw std::out_of_range( ErrorMessage.str() );
		  }
		}
		
		// Returning a quantile estimate requires a lookup based on the probability
		// and it throws an invalid argument if the probability cannot be found, 
		// and an out of range if the probability is invalid.
		
		inline double Get( double Probability )
		{ 
			if ( LegalProbability( Probability ) )
			{
				auto QData = find( Probability );
				
				if ( QData != end() )
					return gsl_rstat_quantile_get( QData->second ); 
				else
				{
				  std::ostringstream ErrorMessage;
					
					ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
											 << "No quantile known for p = " << Probability;
					
					throw std::invalid_argument( ErrorMessage.str() );
				}
			}
			else
				return 0.0;
		}
		
		// A new data point should be added to all quantile data stored. 
		
		inline void Add( double data )
		{ 
			for ( auto QData = begin(); QData != end(); ++QData )
				gsl_rstat_quantile_add( data, QData->second ); 
		}
		
		// A reset implies that all the quantile data structures are freed and 
		// new data structures will be allocated for the stored probabilities.
		
		inline void Reset( void )
		{ 
			for ( auto QData = begin(); QData != end(); ++QData )
		  {
			  gsl_rstat_quantile_free( QData->second );
				QData->second = gsl_rstat_quantile_alloc( QData->first );
			}
		}
		
		// The constructor allocates the data structure for an initialiser list of 
		// quantiles, and the destructor clears the data structures.
		
		QuantileData( const std::initializer_list< double > & Probabilities )
		: std::map< double, gsl_rstat_quantile_workspace * >()
		{
			for ( double Probability : Probabilities )
				if ( LegalProbability( Probability ) )
					insert( std::make_pair( Probability, 
																	gsl_rstat_quantile_alloc( Probability ) ));
		}
		
		~QuantileData( void )
		{
			for ( auto QData = begin(); QData != end(); ++QData )
				gsl_rstat_quantile_free( QData->second );
		}
		
	} Quantiles;
	
public:

  // ---------------------------------------------------------------------------
  // Interface functions 
  // ---------------------------------------------------------------------------
  //	
	// Data characterisation: number of samples, min and max samples.
	
	inline size_t N( void )
	{ return gsl_rstat_n( Accumulator ); }
	
	inline double Min( void )
	{ return gsl_rstat_min( Accumulator ); }
	
	inline double Max( void )
	{ return gsl_rstat_max( Accumulator ); }
	
	// Mean and Median
	
	inline double Mean( void )
	{ return gsl_rstat_mean( Accumulator ); }
	
	inline double Median( void )
	{ return gsl_rstat_median( Accumulator ); }
	
	// Variance and derived quantities
	
	inline double Variance( void )
	{ return gsl_rstat_variance( Accumulator ); }
	
	inline double StandardDeviation( void )
	{ return gsl_rstat_sd( Accumulator ); }
	
	inline double StandardDeviationOfMean( void )
	{ return gsl_rstat_sd_mean( Accumulator ); }
	
	// Higher order moments: skew and kurtosis
	
	inline double Skewness( void )
	{ return gsl_rstat_skew( Accumulator ); }
	
	inline double Kurtosis( void )
	{ return gsl_rstat_kurtosis( Accumulator ); }
	
	// There is a function to clear the data and restart the accumulator
	
	inline void Clear( void )
	{
		gsl_rstat_reset( Accumulator );
	}
	
	// Finally there is a way to get an estimated quantile. Note that this will 
	// throw if there is no quantile for the given probability, i.e. it was not 
	// passed in the initialiser list to the constructor of the class, or if 
	// the given probability is not in the range (0.0, 1.0)
	
	inline double Quantile( double Probability )
	{
		return Quantiles.Get( Probability );
	}
	
	// Adding data is done via the stream input operator. It is declared as a 
	// template so that it can accept any kind of argument that can be converted
	// to a double.
	
	template< typename DataType >
	void operator << ( const DataType & Value )
	{
		gsl_rstat_add( static_cast< double >(Value), Accumulator );
		Quantiles.Add( static_cast< double >(Value) );
	}
	
  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
  //
	// The constructor and destructor allocates and deallocates the accumulator 
	// object
	
	RunningStatistics( const std::initializer_list<double> & Probabilities = {} )
	: Quantiles( Probabilities )
	{
		Accumulator = gsl_rstat_alloc();
	}
	
	~RunningStatistics( void )
	{
		gsl_rstat_free( Accumulator );
	}
};

/*=============================================================================

 Chebychev bounds
 
 One use of the sample statistics is to compute bounds on the spread of the 
 data. The original result by Chebychev [1] required exact knowledge about
 the underlying mean and variance of the unknown distribution of the data. 
 Hence, the result had little practical value before a similar bound was 
 derived using the unbiased estimator for the mean and variance based on 
 actual samples from the unknown distribution [2]. This result has recently 
 been generalised to multivariate distributions [3]. Although the singular 
 version of this result could be used, a simpler form is given by Kabán [4]:
 
 P( |x-m| < k*s ) <= [(n-1)/k²+1]/sqrt( n(n+1) )
 
 Here k is the fractional number of sample standard deviations s such that the 
 probability that a sample value exceeds the right hand side. For example, if 
 we want to make sure that 99% of the data is within k*s, we must solve the 
 right hand side for a k such that the right hand side is less or equal to 1%.
 Once we know k and s, we can find the 99% threshold for the largest sample 
 value by adding the mean to the product of the two: x < m + k*s with 99% 
 probability.
 
 If the distribution of x is known or can be estimated, a bound based on the 
 actual distribution will always be tighter than the Chebychev bound. For small
 sample sizes, Samuelson's inequality [5] will provide a better bound
 
 k = sqrt( n-1 )
 
 Inserting this in the upper sample Chebychev bound above gives
 
 [(n-1)/k²+1]/sqrt( n(n+1) ) = [(n-1)/(n-1)+1]/sqrt(n(n+1)) = 2/sqrt(n(n-1))
 
 Since Samuelson's bound is hard, i.e. is states that all samples will be 
 within this bound, only in the limit when n -> infinity will the Chebychev
 bound be as strong. However, the Chebychev bound is essentially decreasing with 
 sample size whereas Samuelson's bound is increasing proportionally to the 
 square root of the sample size. Furthermore, the point of the Chebychev bound
 is to give a probabilistic bound, i.e. there will be some low probability p 
 that a sample may exceed the bound. Interpreting the "no samples above the 
 bound" of Samuelson as "low probability", say p, for exceeding the bound allows
 us to solve 
 
 p <= 2/sqrt(n(n-1))
 p*sqrt(n(n-1)) <= 2
 
 then squaring both sides
 
 p²(n²-n) <= 4
 p²n²-p²n-4 <= 0
 
 and the roots of the left hand side polynomial is when rounding up to the 
 nearest integer n
 
 n = ceil([ -p² +- p * sqrt(16 + p²) ]/(2p²))

 then if "low probability" is understood as p = 0.1 (or 1%) then n = 20, and if
 it is taken as p = 0.05 (or 0.5%) then n = 40, and if p = 0.01 (or 0.1%) then 
 n = 200. Hence, the rule of thumb that the Samuelson bound is tighter when 
 n < 100. 
 
 In the below implementation which bound to use will be determined by the user's
 requested confidence level, and the tightest bound will be returned.
  
 References:
 
 [1] Tchebichef, P. (1867): "Des valeurs moyennes". Journal de mathématiques 
		 pures et appliquées. Vol 2, No.12, pp.177–184.
 [2] John G. Saw, Mark C. K. Yang, Tse Chin Mo (May 1984): "Chebyshev Inequality 
     with Estimated Mean and Variance", The American Statistician, Vol. 38, 
     No. 2, pp. 130-132
 [3] Bartolomeo Stellato, Bart P. G. Van Parys, Paul J. Goulart (2016): 
     "Multivariate Chebyshev Inequality with Estimated Mean and Variance",
	   The American Statistician (accepted manuscript)
 [4] Ata Kabán (2012): "Non-parametric detection of meaningless distances in 
     high dimensional data", Statistics and Computing, Vol. 22, No. 2, 
		 pp. 375-385, March 2012
 [5] Paul A. Samuelson (1968): "How Deviant Can You Be?", Journal of the 
	   American Statistical Association, Vol. 63, No. 324, pp. 1522-1525

=============================================================================*/

class ChebychevBound : public RunningStatistics
{
public:
	
	// The upper bound for the probability is a function of the distance parameter
	// k and it simply calculates the right hand side of the equation using the 
	// sample data statistics.
	
	inline double ProbabilityBound( double k )
	{
		double n = static_cast< double >( N() );
		
		return ((n-1)/(k*k) + 1)/sqrt( n*(n+1)  );
	}
	
	// The Samuelson bound is also computed since it is only depending on the 
	// number of observations
	
	inline double SamuelsonBound( void )
	{
		return sqrt( static_cast< double >( N() - 1 ) );
	}
	
	// The purpose is to solve this by the GSL root finding algorithm since 
	// the this pointer has to be explicitly passed as the function parameter.
	// Hence, as static function is defined for indirectly calling the above 
	// function based on the pointer to the parameter structure passed.
	
private:
	
  struct RootFunctionParameters
  {
		ChebychevBound * This;
		double 					 TheProbability;
	};

	static double kValue( double k, void * Param )
	{
		RootFunctionParameters * Parameters 
												= reinterpret_cast< RootFunctionParameters * >( Param );
		
		return Parameters->This->ProbabilityBound( k ) - Parameters->TheProbability;
	}
	
	// This can be solved for a given k using a standard root finding procedure
	
public:
		
	double Spread( double Probability )
	{
		if( (Probability <= 0.0) || (1.0 <= Probability) )
	  {
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Given probability bound " << Probability 
									 << " is not a valid probability in (0.0, 1.0)";
									 
		  throw std::out_of_range( ErrorMessage.str() );
		}
		else
	  {
			// The Samuelson bound is evaluated and compared with the probability 
			// value if it is used as the Chebychev bound. If the Chebychev bound is
			// larger than the requested probability, then the Samuelson bound is used 
			// as it is absolute and tighter. 
			
			double SamuelsonSpread = SamuelsonBound();
			
			if ( ProbabilityBound( SamuelsonSpread ) > Probability )
				return SamuelsonSpread;
			else
	    {
				// The right spread factor will be found as the k solving the 
				// equation ProbabilityBound(k) == Probability using the Brent-Dekker 
				// root bracketing method provided by the GSL.
				
				// Allocating the solver
				
			  gsl_root_fsolver * Solver = 
															 gsl_root_fsolver_alloc( gsl_root_fsolver_brent );
				
				// Setting up the function for k and the parameters needed to evaluate
			  // this function.
			  
			  RootFunctionParameters Parameters;
				
				Parameters.This 				  = this;
				Parameters.TheProbability = Probability;
				
				gsl_function k;
								
				k.function = &kValue;
				k.params   = &Parameters;
			
				// The function and the solver is then bound together with the lower 
				// and upper range of the search space. The upper range will always be
				// the Samuelson spread.

				gsl_root_fsolver_set( Solver, &k, 1.0, SamuelsonSpread );
				
			  // We have to iterate the solver until it has converged or the maximum 
				// number of iterations are exceeded. The current iteration limit is 
				// 1000 which should be sufficient in most cases.
	  
			  int IterationCounter = 0,
			      Status;
			  
			  do
			  {
			    IterationCounter++;
			    gsl_root_fsolver_iterate( Solver );
			    Status = gsl_root_test_interval( gsl_root_fsolver_x_lower( Solver ),
							     gsl_root_fsolver_x_upper( Solver ),
							     0, 0.001 );
			  }
			  while ( (Status == GSL_CONTINUE) && (IterationCounter < 1000) );
	  
				// Then the found solution is verified and returned if acceptable
				
				double Solution = gsl_root_fsolver_root( Solver );
				gsl_root_fsolver_free( Solver );
				
				if ( ProbabilityBound( Solution  ) <= Probability )
					return Solution;
				else
					return SamuelsonSpread;
			}
		}
	}
	
	// The actual bound is computed based on a given probability for exceeding 
	// this bound (typically a small probability). It should be noted that 
	// the standard deviation can only be computed if the number of samples is 
	// larger than 2. Unfortunately, there is no alternative to test this for 
	// every invocation. It is undocumented what the GSL will return in this 
	// case, but since it is not C++ it is not an exception.
	
	double Bound( double Probability )
	{
		if ( N() > 1 )
			return Mean() + Spread( Probability ) * StandardDeviation();
		else
			return Mean();
	}
	
	// The constructor takes the set of quantile probabilities and passes them 
	// on to the running statistics class to produce the desired quantiles. The 
	// default is not to record any quantile data.
	
	ChebychevBound( 
						const std::initializer_list<double> & QuantileProbabilities = {} )
	: RunningStatistics( QuantileProbabilities )
	{}
	
};

} 			// End name space Gnu Scientific Library
#endif	// GSL_RUNNING_STATISTICS
