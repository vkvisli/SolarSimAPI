/*=============================================================================
  Interpolation

  This file is the implementation of some of the more complex methods of the 
  Interpolation class that are not templated. All templated functions have to 
  be defined in the class definition in the header file.
  
  Author: Geir Horn, University of Oslo, 2014
  Contact: Geir.Horn [at] mn.uio.no
  License: GPL (LGPL3.0 without the GNU Scientific Library)
=============================================================================*/

#include "Interpolation.hpp"
#include <gsl/gsl_errno.h>
#include <sstream>
#include <vector>
#include <set>
#include <functional>
#include <fstream>
#include <stdexcept>

// ----------------------------------------------------------------------------
// Initialisation functions
//-----------------------------------------------------------------------------

void Interpolation::ComputeCoefficients(void)
{
  // First the state objects are initialised according to the type of 
  // interpolation desired
  
  std::unique_lock< std::mutex > Lock( AcceleratorLock );
  
  AcceleratorObject   = gsl_interp_accel_alloc();
  
  Lock.unlock();
	
	// Allocate the interpolation object based on the requested interpolation 
	// type.
	
	switch ( InterpolationType )
  {
		case Type::Linear :
			InterpolationObject = gsl_interp_alloc( gsl_interp_linear, 
																							Abscissa.size() );
			break;
		case Type::Polynomial :
			InterpolationObject = gsl_interp_alloc( gsl_interp_polynomial, 
																							Abscissa.size() );
			break;
		case Type::CubicSpline :
			InterpolationObject = gsl_interp_alloc( gsl_interp_cspline, 
																							Abscissa.size() );
			break;
		case Type::AkimaSpline :
			InterpolationObject = gsl_interp_alloc( gsl_interp_akima, 
																							Abscissa.size() );
			break;
		case Type::SteffenMethod :
			InterpolationObject = gsl_interp_alloc( gsl_interp_steffen, 
																							Abscissa.size() );
			break;
		case Type::PeriodicCubicSpline :
			if ( Ordinate.front() != Ordinate.back() )
			{
				std::ostringstream ErrorMessage;
	
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
										 << "Periodic interpolation requires first"
										 << " and last ordinate value equal";
										 
				throw std::length_error( ErrorMessage.str() );
			}
			else
				 InterpolationObject = gsl_interp_alloc( gsl_interp_cspline_periodic, 
																							   Abscissa.size() );
		  break;
		case Type::PeriodicAkimaSpline :
			if ( Ordinate.front() != Ordinate.back() )
			{
				std::ostringstream ErrorMessage;
	
				ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
										 << "Periodic interpolation requires first"
										 << " and last ordinate value equal";
										 
				throw std::length_error( ErrorMessage.str() );
			}
			else
				InterpolationObject = gsl_interp_alloc( gsl_interp_akima_periodic, 
																							  Abscissa.size() );
			break;
	}
  
  // Checking that there are more than the minimum number of samples required 
  // by the interpolation type
  
  if ( Abscissa.size() < gsl_interp_min_size( InterpolationObject ) )
	{
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "Not enough points for the interpolation type "
								 << gsl_interp_name( InterpolationObject );
								 
		throw std::length_error( ErrorMessage.str() );
	}
    
  // Finally the data vectors are used to solve the coefficients of the 
  // interpolation function and initialise the interpolation object

  gsl_interp_init( InterpolationObject, Abscissa.data(), Ordinate.data(), 
									 Abscissa.size() );
}

// The clean up function resets the object to an empty place holder.

void Interpolation::CleanUp( void )
{
  Abscissa.clear();
  Ordinate.clear();
  
  if ( InterpolationObject != nullptr )
    gsl_interp_free( InterpolationObject );
  
  if ( AcceleratorObject != nullptr )
  {
    std::unique_lock< std::mutex > Lock( AcceleratorLock );
    gsl_interp_accel_free( AcceleratorObject );
  }
  
  // The offset is off course zero since there are no data.
  
  Offset.x = 0.0;
  Offset.y = 0.0;
}

// ----------------------------------------------------------------------------
// Operators
//-----------------------------------------------------------------------------

// The first helper function constructs a common abscissa from two abscissae 
// vectors. It uses temporarily a set to ensure that the abscissa values are 
// unique and sorted. First the elements of the two abscissae are inserted
// into the set, and then the resulting values of the set are exported into
// the returned vector.

inline std::vector<double> Union( const std::vector<double> & First,
				  double FirstOffset,
				  const std::vector<double> & Second,
				  double SecondOffset	)
{
  std::set<double> Members;
  
  // Inserting the vectors' values adding the respective offsets
  
  for (double x : First ) 
    Members.insert( x + FirstOffset );
  
  for (double x : Second )
    Members.insert( x + SecondOffset );
  
  return std::vector<double>( Members.begin(), Members.end() );
}

// The second helper function computes the a vector of function values based
// on the combination of two interpolation functions and an abscissa vector. It 
// basically loops over all the arguments. If the argument is in the domain of
// only one of the interpolation functions, that function is used to compute 
// the ordinate value, but if it can be supported by both functions, the 
// combination function is used.
//
// Implementation note: a template is used instead of an inline function because
// the provided function to combine the values of the interpolation functions
// is probably a lambda, which can be efficiently passed as a template 
// (move = &&) argument. For a regular (inline) function, it would have to 
// encapsulated in a std::function object, which would lead to a virtual 
// function call at runtime (indirect function call), which is less efficient.
// See the very good note at:
// http://stackoverflow.com/questions/16111285/how-to-pass-and-execute-anonymous-function-as-parameter-in-c11

template < typename Function >
std::vector<double> ComputeOrdinates( 
  const std::vector<double> & Arguments,
  Interpolation & f, Interpolation & g,
  Function && Combination )
{
  std::vector<double> Ordinates;
  
  for ( double x : Arguments )
    if ( ! f.DomainQ(x) )
      Ordinates.push_back( g(x) );
    else if ( ! g.DomainQ(x) )
      Ordinates.push_back( f(x) );
    else
      Ordinates.push_back( Combination(x) );
    
  return Ordinates;
}

// The third helper function is used to select the interpolation type to use 
// when combining two interpolation functions. It will always select the most
// continuous version, or the periodic version if one of the two types is 
// periodic. It then tacitly assumes that the combined domain for the the two
// functions will support the requirement for periodicity, i.e. that the first 
// and the last ordinate values are identical.
//
// Implementation note: Since the use of the [] operator makes the priority 
// comparison elegant, we cannot use const on the priority map (even though it 
// is const) since the [x] operator will extend the map if x is not already a 
// key value. In our case we know that only enumerated types will match the 
// function signature and the map should contain all the enumerated types, 
// hence it will never be extended by the [] operator.

inline Interpolation::Type SelectInterpolationType (
  Interpolation::Type First, Interpolation::Type Second )
{
  static std::map< Interpolation::Type, unsigned int > Priority =
  {
    { Interpolation::Type::Linear,              1 },
    { Interpolation::Type::Polynomial,          2 },
    { Interpolation::Type::CubicSpline,         3 },
    { Interpolation::Type::AkimaSpline,         4 },
		{ Interpolation::Type::SteffenMethod,       5 },
    { Interpolation::Type::PeriodicCubicSpline, 6 },
    { Interpolation::Type::PeriodicAkimaSpline, 7 }
  };
  
  if ( Priority[ First ] >= Priority[ Second ] )
    return First;
  else
    return Second;
}

// All the operators will use these three helper functions with slightly 
// different arguments for the combination function. Thus, they could have been
// represented as a generic member function, but this would require that the 
// helper functions had to be defined in the header file, making the interface
// less readable, and with uncertain performance gains. Hence, we rather use 
// a parameter and a switch to decide how the ordinate values should be 
// computed. Note that in this case we only need to consider the offset when 
// computing the new, joint abscissa since the offset in the ordinate will be 
// handled when computing the values of the two interpolation functions.

Interpolation Interpolation::GenericOperator( 
  Interpolation& Other, Interpolation::BinaryType OperatorType) 
{
  Interpolation CombinedFunction;
  
  // First the final interpolation type for the combined object is found
  
  CombinedFunction.InterpolationType = SelectInterpolationType( 
    InterpolationType, Other.InterpolationType
  );
  
  // Then the combined abscissa is constructed from the two domains involved
  
  CombinedFunction.Abscissa = Union( Abscissa, Offset.x,
				     Other.Abscissa, Other.Offset.x );
  
  // The ordinates will depend on which operator type that is implemented. The 
  // only difference is the combination function passed to the computation 
  // function.
  
  switch ( OperatorType )
  {
    case BinaryType::Plus :
      CombinedFunction.Ordinate = ComputeOrdinates( 
	  CombinedFunction.Abscissa, *this, Other, 
	  [this, &Other](double x){ return this->operator()(x) + Other(x); } );
      break;
    case BinaryType::Minus :
      CombinedFunction.Ordinate = ComputeOrdinates( 
	  CombinedFunction.Abscissa, *this, Other, 
	  [this, &Other](double x){ return this->operator()(x) - Other(x); } );      
      break;
    case BinaryType::Multiply :
      CombinedFunction.Ordinate = ComputeOrdinates( 
	  CombinedFunction.Abscissa, *this, Other, 
	  [this, &Other](double x){ return this->operator()(x) * Other(x); } );
      break;
    case BinaryType::Divide :
      CombinedFunction.Ordinate = ComputeOrdinates( 
	  CombinedFunction.Abscissa, *this, Other, 
	  [this, &Other](double x){ return this->operator()(x) / Other(x); } );     
      break;
  }
  
  // Finally we can compute the actual interpolation over this new domain
  
  CombinedFunction.ComputeCoefficients();
  
  return CombinedFunction;
}

// The functor operator throws an error message if the provided argument value
// is outside the domain. This because the class does interpolation not 
// extrapolation. However, it is not as simple as testing the domain because 
// if the function is periodic, it should in theory extend to any arguments. 
// This can only be decided by the GSL library so here the error returned from 
// the GSL library is used to decide if the argument is valid or not.
//
// Calling the operator on an uninitialised interpolation, as determined by the 
// size of the abscissa, will throw an out of range error.

double Interpolation::operator()(double x)
{
  if ( Abscissa.empty() )
	{
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << "Evaluation of an empty interpolation object";
								 
		throw std::length_error( ErrorMessage.str() );
	}
  else
  {   
    double Value;
    
    std::unique_lock< std::mutex > Lock( AcceleratorLock );
    
    int Status = gsl_interp_eval_e( InterpolationObject, Abscissa.data(), 
			    Ordinate.data(), x - Offset.x, AcceleratorObject, 
			    &Value );
    
    Lock.unlock();
    
    if ( Status == GSL_EDOM )
    {
      std::stringstream ErrorMessage;
      
      ErrorMessage << __FILE__ << " at line " << __LINE__ << ": " 
									 << "Interpolation: Requested argument " << x 
							     << " is outside the interpolation range ["
							     << Abscissa.front() + Offset.x << "," 
							     << Abscissa.back() + Offset.x << "]";
		    
      throw std::out_of_range( ErrorMessage.str() );
    }
    else
      return Value + Offset.y;
  }
}

// The copy assignment operator copies the data set vectors and interpolation
// type and then computes its own interpolation. The latter could have been 
// implemented by a memcopy (in principle), but the objects dealt with are 
// C structures whose internal structure belongs to the GSL to define and 
// initialise.

void Interpolation::operator=(Interpolation & Other)
{
  // First we clean the data currently maintained by this interpolation object
  
  CleanUp();
  
  // Copy the data set vectors and the interpolation type
  
  Abscissa 	    = Other.Abscissa;
  Ordinate 	    = Other.Ordinate;
  InterpolationType = Other.InterpolationType;
  
  // Copy the offset
  
  Offset.x = Other.Offset.x;
  Offset.y = Other.Offset.y;
  
  // Reinitialise the interpolation objects
  
  ComputeCoefficients();
}

// The move operator first ensures that this object is completely emptied 
// before swapping elements with the other object and resetting its 
// interpolation objects.

void Interpolation::operator=( Interpolation && Other)
{
  // Clear this object
  
  CleanUp();  
  
  // Grab the content of the other object
  
  Abscissa.swap( Other.Abscissa );
  Ordinate.swap( Other.Ordinate );
  
  InterpolationObject = Other.InterpolationObject;
  AcceleratorObject   = Other.AcceleratorObject;
  
  InterpolationType   = Other.InterpolationType;
  
  Offset.x            = Other.Offset.x;
  Offset.y            = Other.Offset.y;
  
  // Clear the other object
  
  Other.InterpolationObject = nullptr;
  Other.AcceleratorObject   = nullptr;
}

// ----------------------------------------------------------------------------
// Restrict domain
//-----------------------------------------------------------------------------
// 
// The domain can be restricted to a sub-interval of the current interval and
// then only the interior points will be kept from the original abscissa, and 
// new data points will be added for the extreme values of the changed domain.

void Interpolation::RestrictDomain( double NewLowerLimit, double NewUpperLimit )
{
  // The main check of validity of the domain limits requires this to be a 
  // subset of the current interval. Note that if the new domain limits equal 
  // the existing domain, nothing will be done and no error thrown.
  
  if ( (DomainLower() < NewLowerLimit) && (NewUpperLimit < DomainUpper()) )
  {
    // The data points will be stored in a map for the re-initialisation of 
    // of this interpolation object
    
    std::map< double, double > DataPoints;
    
    // The constructed data point at the start and at the end of the new 
    // interval is stored
    
    DataPoints.emplace( NewLowerLimit, this->operator()( NewLowerLimit ) );
    DataPoints.emplace( NewUpperLimit, this->operator()( NewUpperLimit ) );

    // Then the interior points of the existing abscissa and ordinate values 
    // are inserted taking into account the possible offsets.
    
    for ( unsigned int i = 0; i < Abscissa.size(); i++ )
    {
      double x = Abscissa[i] + Offset.x;
      
      if ( (NewLowerLimit < x) && (x < NewUpperLimit) )
	DataPoints.emplace( x, Ordinate[i] + Offset.y );
    }
    
    // Computing the interpolation over the new domain is the final step of 
    // restricting the domain.
    
    InitialiseData( DataPoints.begin(), DataPoints.end(), InterpolationType );
  }
  else if ( (NewLowerLimit < DomainLower()) || (DomainUpper() < NewUpperLimit) )
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage << "Interpolation: The new domain [" << NewLowerLimit << ","
		 << NewUpperLimit << "] is not a sub-domain of the existing ["
		 << DomainLower() << "," << DomainUpper() << "]";
		 
    throw std::invalid_argument( ErrorMessage.str() );
  }
}


// ----------------------------------------------------------------------------
// Constructors
//-----------------------------------------------------------------------------

// The constructor that reads the data points from a file first creates the 
// data vector and then initialises the interpolation objects. Note that no 
// error check is done while reading successive pairs of values from the file,
// and so errors are not captured if the file format is incorrect (does not 
// contain an even number of doubles). Again, a map is used to ensure that 
// the abscissa values are unique

Interpolation::Interpolation( const std::string Filename, 
			      Interpolation::Type DesiredInterpolationType )
: Abscissa(), Ordinate(),
  InterpolationObject( nullptr ), AcceleratorObject( nullptr ), 
  AcceleratorLock()
{
  std::map< double, double > DataPoints;
  std::ifstream DataFile( Filename );
  
  if ( DataFile ) 
    while( !DataFile.eof() )
    {
      double x, y;
      
      DataFile >> x >> y;
      DataPoints.insert( std::make_pair(x,y) );
    }
    
  DataFile.close();

  // Then initialise the interpolation
  
  InitialiseData( DataPoints.begin(), DataPoints.end(), 
		  DesiredInterpolationType );
}

// ----------------------------------------------------------------------------
// Miscellaneous
//-----------------------------------------------------------------------------

// The integral function simply calls the corresponding function in the GSL 
// and return the value if successfully updated. Note that the error variants 
// are used, and the message is thrown if something goes wrong. The offset has
// no effect on derivation, but an ordinate offset will give a constant bias 
// on the integral value.

double Interpolation::FirstDerivative(double x)
{
  double Value;
  
  std::unique_lock< std::mutex > Lock( AcceleratorLock );
  
  int Status = gsl_interp_eval_deriv_e( InterpolationObject, 
		  Abscissa.data(), Ordinate.data(), x - Offset.x, 
		  AcceleratorObject, &Value               );
  
  Lock.unlock();
  
  if ( Status != GSL_SUCCESS )
	{
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << gsl_strerror( Status );
								 
		throw std::length_error( ErrorMessage.str() );
	}
  else
    return Value;
}

double Interpolation::SecondDerivative(double x)
{
  double Value;
  std::unique_lock< std::mutex > Lock( AcceleratorLock );
  
  int Status = gsl_interp_eval_deriv2_e( InterpolationObject, 
		  Abscissa.data(), Ordinate.data(), x - Offset.x, 
		  AcceleratorObject, &Value               );

  Lock.unlock();
  
  if ( Status != GSL_SUCCESS )
	{
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << gsl_strerror( Status );
								 
		throw std::length_error( ErrorMessage.str() );
	}
  else
    return Value;
}

double Interpolation::Integral(double From, double To)
{
  double Value;
  std::unique_lock< std::mutex > Lock( AcceleratorLock );
  
  int Status = gsl_interp_eval_integ_e( InterpolationObject, 
		  Abscissa.data(), Ordinate.data(), 
		  From -Offset.x, To - Offset.x, 
		  AcceleratorObject, &Value               );
  
  Lock.unlock();
  
  if ( Status != GSL_SUCCESS )
	{
		std::ostringstream ErrorMessage;

		ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								 << gsl_strerror( Status );
								 
		throw std::length_error( ErrorMessage.str() );
	}
  else
    return Value + Offset.y * (To - From);
}
