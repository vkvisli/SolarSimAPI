/*=============================================================================
  Interpolation
  
  This class is a functor aimed at interpolating a univariate function. It is
  fundamentally only an interface to the algorithms of the GNU Scientific 
  Library [1]. User should be aware that the GSL is released under GPL so this 
  code cannot be used in commercial applications. GSL has been chosen because 
  it is packaged with most Linux distributions, well tested after years of use,
  compatible with many platforms, and has a backing community.
  
  Another good alternative without the license restrictions would have been to
  use E. J. Mahler's SplineLibrary [2], recently released as open source. It 
  should provide the same functionality as this class entirely, but it is 
  not well documented and its popularity and developer support remains unknown.
  
  A final alternative would be to use ALGLIB [3]. The free version has 
  performance issues for larger systems, it is also released under GPL so it 
  suffers the same issues with commercial exploitation as the GSL. Finally, it 
  is a part of a larger library so one would need to compile the the full 
  library even if only the Spline interpolation algorithms will be used.
  
  ALGORITHM:
  
  Interpolation can be done in a multitude of ways, from the very simple 
  zero-order (flat sample and hold), via linear interpolation fitting a straight
  line between any two neighbouring points, to advanced polynomial methods and 
  splines. Polynomial methods works by fitting a polynomial to the data, but 
  it easily introduces oscillations for larger data sets. Splines generally 
  fit cubic polynomials to each pair of points along the function, matching the
  first and second order derivatives in the given data points. However, these
  splines can oscillate near outliers [4], and a more robust alternative would 
  be to use Akima splines [5]. This variant uses local information only, basing 
  the interpolation on the five successive data points. One should note that the 
  Akima interpolation of a sum of two functions does not equal the sum of the 
  interpolation of the two functions. 
  
  Steffen's method [6] guarantees monotonicity in the interpolation and has 
  therefore been chosen as the default method here, with the Akima spline 
  as a good candidate to consider if additivity is desired.
  
  REFERENCES:
  
  [1] https://www.gnu.org/software/gsl/
  [2] https://github.com/ejmahler/SplineLibrary
  [3] http://www.alglib.net/interpolation/spline3.php
  [4] Jerrold Fried and Stanley Zietz: "Curve fitting by spline and Akima 
      methods: possibility of interpolation error and its suppression",
      Physics in Medicine and Biology, Vol. 18, No. 4, July 1973
  [5] Hiroshi Akima: "A New Method of Interpolation and Smooth Curve Fitting 
      Based on Local Procedures", Journal of the ACM, Vol. 17, No. 4, pp. 
      589–602, October 1970
  [6] M. Steffen: "A Simple Method for Monotonic Interpolation in One 
		  Dimension", Astronomy & Astrophysics, Vol. 239, No. 1-2, pp. 443-450,
			November 1990
      
  Author: Geir Horn, University of Oslo, 2014
  Contact: Geir.Horn [at] mn.uio.no
  License: GPL (LGPL3.0 without the GNU Scientific Library)
=============================================================================*/

#ifndef INTERPOLATION
#define INTERPOLATION

#include <string>

#include <map>
#include <vector>
#include <type_traits>
#include <iterator>
#include <utility>
#include <stdexcept>
#include <sstream>

#include <mutex>

// The GNU Scientific Library (GSL) defines some functions as in-line if that is 
// supported by the compiler. However, they are not declared in-line by default
// since the GSL is designed to support standard C

#include <gsl/gsl_interp.h> 

class Interpolation
{
public:
  
  // The different types of interpolation supported by the GSL is enumerated
  // to ensure that they are used correctly in C++ programs.
  
  enum class Type
  {
    Linear,
    Polynomial,
    CubicSpline,
    PeriodicCubicSpline,
    AkimaSpline,
    PeriodicAkimaSpline,
		SteffenMethod
  };
  
protected:
  
  // The interpolation object remembers which type of interpolation is used.
  // This is necessary in order to correctly add two interpolation functions 
  // as they will return a new interpolation function corresponding to the 
  // "most advanced" interpolation method of the two functions. Thus, if a 
  // linearly interpolated function is added to a spline interpolated function
  // a spline function results.
  
  Type InterpolationType;
  
private:
  
  // It is necessary for the class to store the abscissa and ordinate values
  // (data points) of the function to interpolate.
  
  std::vector <double> Abscissa, Ordinate;
  
  // The GSL needs an accelerator object holding the state of searches and an 
  // interpolation object holding the static state (coefficients) computed from 
  // the data. Both are dynamically allocated by the initialiser and deleted 
  // by the destructor
  
  gsl_interp * InterpolationObject;
  
  // The accelerator object contains information about the interpolation 
  // function that may speed up some computations.
  
  gsl_interp_accel * AcceleratorObject;
  
  // Various functions using the accelerator object may actually write to it, 
  // and therefore it must be protected in order to allow the same 
  // interpolation object to be used from concurrent threads. The interpolation 
  // structure is passed as a constant to every function, so parallelism 
  // should not be a problem for this object.
  
  std::mutex AcceleratorLock;

  // These objects are initialised when the interpolation coefficients are
  // computed, after the data vectors have been filled, and the interpolation
  // type set.
  
  void ComputeCoefficients (void);
  
  // It is however possible to shift the interpolated function along either of 
  // the two axes without recomputing it since such a shift corresponds to 
  // adding a constant either to the abscissa or to the computed ordinate value.
  // The current offsets are stored in the next structure.
				  
  struct
  {
    double x, y;
  } Offset;

  // When initialising the interpolation function, or when assigning another 
  // interpolation function to this object, it is necessary to clean the 
  // current state, and there is a dedicated function for this.
  
  void CleanUp( void );
  
  // The Initialiser does the common work of all constructors. In order to 
  // ensure that all the abscissae are unique and correctly sorted a map is 
  // used to hold the data points in the constructor. The start and end 
  // iterators of this map is passed to the initialiser that first sets up the
  // data point vectors, and then initialises the GSL objects.
  
  template < class MapIterator >
  void InitialiseData( MapIterator Begin, MapIterator End,
    Type DesiredInterpolationType = Type::SteffenMethod      )
  {
    // The assumption is that these types are arithmetic, i.e. that they 
    // can be transformed into doubles (potentially with a loss of accuracy).
    // A compilation error is created if the types are not arithmetic.
    
    static_assert( std::is_arithmetic< 
					typename std::iterator_traits< MapIterator>::value_type::first_type 
					>::value && std::is_arithmetic< 
					typename std::iterator_traits< MapIterator >::value_type::second_type 
					>::value,
		"Only numeric types supported for interpolation" );
    
    // This object is first cleaned
    
    CleanUp();
    
    // Then the data vectors can be populated with the given data points
    
    for ( auto DataPoint = Begin; DataPoint != End; ++DataPoint )
    {
      Abscissa.push_back( static_cast< double >( DataPoint->first  ) );
      Ordinate.push_back( static_cast< double >( DataPoint->second ) );
    }
    
    // The interpolation type is stored for future reference
    
    InterpolationType   = DesiredInterpolationType;

    // Finally, it is possible to compute the interpolation coefficients
    
    ComputeCoefficients();
  }
  
  // Binary operators are obvious when it comes to the value of two 
  // interpolated functions, e.g. f(x) + g(x) has a clear meaning if x is 
  // a legal value for both f and g. However, what is to be understood by 
  // adding two interpolated functions, e.g. f + g? This implementation 
  // understand this as the domain of the new interpolated function resulting 
  // from this operation will cover the total range of the domains of the two
  // interpolated functions involved, thus if the domain of f is [fa,fb] and 
  // the domain of g is [ga,gb] then the new domain will be [min(fa,ga), 
  // max(fb,gb)]. The the data points on the joint abscissa will be the union
  // of the data points on the two abscissae, and new ordinate values computed 
  // by using the binary operation. As an example: if x is a data point on the 
  // abscissa of f, but outside of the domain of g, then the new ordinate value
  // is f(x) regardless of binary operator. If x is in the domain of g then 
  // the value for the plus operator will be f(x) + g(x), even if g(x) in this
  // case may be the interpolated value of g at x. With all the ordinate values
  // computed for the union of the abscissae data points of the two functions, 
  // a new interpolation is made over these points using the most continuous 
  // interpolation type of the two involved functions. It should be noted that
  // this procedure is rather computationally intensive, and one could be 
  // better off simply combining the values of the involved functions. Beware
  // that this may not always be desirable, in particular for the Akami 
  // interpolation since the sum of two interpolated values will in general 
  // not correspond to the value of the interpolated Akami function constructed
  // according to the outlined procedure, thus in this case one should rather 
  // use (f+g)(x).
  
  enum class BinaryType
  {
    Plus,
    Minus,
    Multiply,
    Divide
  };
  
  Interpolation GenericOperator ( Interpolation & Other, 
																  BinaryType OperatorType      );
      
protected:
  
  // In order to have a more general syntax for derivatives and integration,
  // these functions are defined to be protected, with friend interface 
  // functions defined below. These are straight forward encapsulations of 
  // similar functions in the GSL.
  
  double FirstDerivative  (double x);
  double SecondDerivative (double x);
  double Integral (double From, double To);

public:
  
  // The first is a cast to a boolean enabling a simple test to see if the 
  // interpolated object is valid or not. It is taken to be valid if the 
  // abscissa has values.
  
  inline operator bool() const
  {
    if ( Abscissa.empty() )
      return false;
    else
      return true;
  }
  
  // This object is a functor whose main functionality is to obtain a new 
  // ordinate value for a given abscissa value. Note that all interpolation 
  // types supported by GSL produces a known ordinate value for a known abscissa
  // value, i.e. the interpolated curve passes always through the given data 
  // points. Note that calling this on an uninitialised object will throw a 
  // standard out of range error.
  
  double operator() (double x);

  // There is a public function to translate the interpolation function by 
  // a constant offset in both directions.
  
  inline void Translate ( double xOffset, double yOffset )
  {
    Offset.x = xOffset;
    Offset.y = yOffset;
  }
  
  // The binary operators are all defined in terms of the generic operator
    
  inline Interpolation operator+ (Interpolation & Other)
  {
    return GenericOperator( Other, BinaryType::Plus );
  }
  
  inline Interpolation operator- (Interpolation & Other)
  {
    return GenericOperator( Other, BinaryType::Minus );
  }
  
  inline Interpolation operator* (Interpolation & Other)
  {
    return GenericOperator( Other, BinaryType::Multiply );
  }
  
  inline Interpolation operator/ (Interpolation & Other)
  {
    return GenericOperator( Other, BinaryType::Divide );
  }
  
  // Assignment operators: Care must be taken when setting two interpolation 
  // functions equal. If the right hand side is an allocated object, a copy 
  // will be made, but if the right hand side is a temporary object a move
  // will be made. The latter leaves the moved object in a void state.

  void operator= ( Interpolation &  Other ); // Make a deep copy of Other
  void operator= ( Interpolation && Other ); // Move the Other's data
  
  // Then it is possible to define operators that work relative to this 
  // interpolation function as a combination of the others.
  
  inline void operator+= ( Interpolation & Other )
  {
    this->operator=( GenericOperator( Other, BinaryType::Plus ) );
  }
  
  inline void operator-= ( Interpolation & Other )
  {
    this->operator=( GenericOperator( Other, BinaryType::Minus ) );
  }
  
  inline void operator*= ( Interpolation & Other )
  {
    this->operator=( GenericOperator( Other, BinaryType::Multiply ) );
  }
  
  inline void operator/= ( Interpolation & Other )
  {
    this->operator=( GenericOperator( Other, BinaryType::Divide ) );
  }

  // There are two small functions to let the user check the domain limits. 
  // Calling the 'front' or the 'back' function on an empty abscissa has 
  // 'undefined behaviour' which will here be defined by throwing a standard 
  // exception. This could have been solved by letting the functions return 
  // an uninitialised std::optional, but throwing an exception is cleaner 
  // for the use cases when it is clear that the interpolation object has been 
  // properly initialised. A range error is thrown to indicate the problem.
  
  inline double DomainLower (void) const
  {
    if ( Abscissa.empty() )
		{
			std::ostringstream ErrorMessage;

			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Domain error: Interpolation object is empty";
									 
			throw std::range_error( ErrorMessage.str() );
		}
    else
      return Abscissa.front() + Offset.x;
  }
  
  inline double DomainUpper (void) const
  {
    if ( Abscissa.empty() )
		{
			std::ostringstream ErrorMessage;

			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "Domain error: Interpolation object is empty";
									 
			throw std::range_error( ErrorMessage.str() );
		}
    else
      return Abscissa.back() + Offset.x;
  }
    
  // Then there is a small helper function to check if a given argument is 
  // valid for this interpolation function. Attention: It is assumed that the 
  // periodic function interpolation supports any argument, also outside of the 
  // dataset used to create the interpolation function.
  
  inline bool DomainQ (double x) const
  {
    if ( ( InterpolationType == Type::PeriodicCubicSpline ) ||
				 ( InterpolationType == Type::PeriodicAkimaSpline )    )
      return true;
    else
      return (DomainLower() <= x) && (x <= DomainUpper());
  }

  // There is a function to restrict the domain of the interpolation function
  // to a sub-interval of the original interval. The function will throw an 
  // invalid argument exception if this criterion is not fulfilled. It will 
  // change the abscissa and ordinate vector keeping only data points interior
  // to the new domain, and add new points at the domain boarders whose ordinate
  // value is the interpolated value. The actual interpolation function will in 
  // this case not be recomputed.
  
  void RestrictDomain( double NewLowerLimit, double NewUpperLimit );
  
  // CONSTRUCTORS II: The copy constructor copies the data from the other 
  // object, but computes the interpolation once again. The move constructor 
  // moves every thing from the other object and leaves it in a void state. The
  // latter are actually implemented in terms of the assignment operators.
  
  inline Interpolation ( Interpolation & Other )	// Copy constructor
  {
    this->operator=( Other );
  }
  
  inline Interpolation ( Interpolation && Other )     	// Move constructor
  {
    this->operator=( Other );
  }
  
  // CONSTRUCTORS III: In order to make sense the interpolation function 
  // must be constructed on a data set. Iterators must be given to the data 
  // set and there are two variants: one constructor takes four iterators for 
  // the start and the end of the abscissa and ordinate ranges respectively.
  // As normal, it is assumed that the 'end' iterators points to the element 
  // after the last element to insert.
  
  template < class AbscissaIterator, class OrdinateIterator >
  Interpolation ( AbscissaIterator xFirst, AbscissaIterator xLast, 
		  OrdinateIterator yFirst, OrdinateIterator yLast, 
		  Type DesiredInterpolationType = Type::SteffenMethod  )
  : Abscissa(), Ordinate(),
    InterpolationObject( nullptr ), AcceleratorObject( nullptr ), 
    AcceleratorLock()
  {
    // In order to ensure that all the abscissae values are unique and sorted  
    // we first build a map of these values.
    
    std::map< typename std::iterator_traits< AbscissaIterator >::value_type, 
				      typename std::iterator_traits< OrdinateIterator >::value_type > 
				      DataPoints;

    AbscissaIterator xValue = xFirst;
    OrdinateIterator yValue = yFirst;
    
    while ( (xValue != xLast) && (yValue != yLast) )
    {
      DataPoints.insert( std::make_pair( *xValue, *yValue ) );
      ++xValue;
      ++yValue;
    }
    
    // The rest of the initialisation is left to the data initialiser 
    // function
    
    InitialiseData( DataPoints.begin(), DataPoints.end(), 
								    DesiredInterpolationType );
  }

  // Alternatively, if only two iterators are provided, they must be to a 
  // a structure holding pairs already. However, we do not know if the data
  // are unique or correctly sorted, which would require the iterators to 
  // be to a map. In order to ensure uniqueness and a sorted abscissa 
  // we copy the data to a map before passing it to the initialiser.
  
  template < class DataPointIterator >
  Interpolation ( DataPointIterator First, DataPointIterator Last,
		  Type DesiredInterpolationType = Type::SteffenMethod )
  : Abscissa(), Ordinate(),
    InterpolationObject( nullptr ), AcceleratorObject( nullptr ), 
    AcceleratorLock()
  {
    std::map< 
    typename std::iterator_traits< DataPointIterator >::value_type::first_type,
    typename std::iterator_traits< DataPointIterator >::value_type::second_type>
    DataPoints( First, Last );
    
    InitialiseData( DataPoints.begin(), DataPoints.end(), 
								    DesiredInterpolationType );
  }
  
  // If the user really has prepared the data in a map before constructing 
  // the interpolation function, it is recommended to use the next constructor,
  // which is merely an interface to the initialiser, thus saving one copy 
  // of the data.
  
  template < typename Key, typename Value, class Comparator, class Allocator >
  Interpolation ( std::map< Key, Value, Comparator, Allocator >  
		  & DataPoints, 
		  Type DesiredInterpolationType = Type::SteffenMethod )
  : Abscissa(), Ordinate(),
    InterpolationObject( nullptr ), AcceleratorObject( nullptr ), 
    AcceleratorLock()
  {
    InitialiseData( DataPoints.begin(), DataPoints.end(), 
								    DesiredInterpolationType );
  }
  
  // The file constructor takes a string indicating a file containing 
  // the data in multiple lines where each line contains the abscissa and 
  // the ordinate values. The file is read until end of file.
  
  Interpolation ( const std::string Filename, 
								  Type DesiredInterpolationType = Type::SteffenMethod );
  
  // Finally there is a void constructor for which the interpolation object 
  // is left useless. Calling any function on this is bound to fail as a 
  // nullptr will be accessed... However it can be useful if there are classes
  // that needs to prepare the abscissa and ordinate values before the 
  // object can be initialised. In this case it will be necessary to use the
  // assignment operator (=) to set the empty place holder object equal to the 
  // temporary object created based on properly formatted data.
  
  Interpolation( void )
  : Abscissa(), Ordinate(),
    InterpolationObject( nullptr ), AcceleratorObject( nullptr ), 
    AcceleratorLock()
  {
    InterpolationType   = Type::Linear;
    Offset.x            = 0.0;
    Offset.y            = 0.0;
  }
  
  // DESTRUCTOR: frees the interpolation object and the accelerator object 
  // that were both dynamically allocated by the initialiser.
  
  virtual ~Interpolation (void)
  {
    CleanUp();
  };
  
  // Friend functions to make the interface to integration and derivation more
  // appealing from a syntax view
  
  friend double Derivative ( Interpolation & Function, double x );
  friend double Derivative2( Interpolation & Function, double x );
  friend double Integral   ( Interpolation & Function, double LowerLimit, 
			     double UpperLimit );
};

// The definitions of the derivation and integration functions are trivial

inline double Derivative ( Interpolation & Function, double x )
{
  return Function.FirstDerivative(x);
}

inline double Derivative2( Interpolation & Function, double x )
{
  return Function.SecondDerivative(x);
}

inline double Integral( Interpolation & Function, double LowerLimit, 
			double UpperLimit )
{
  return Function.Integral( LowerLimit, UpperLimit );
}


#endif // INTERPOLATION
