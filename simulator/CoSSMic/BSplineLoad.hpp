/*=============================================================================
  B-Spline Load
  
  The energy load profiles are inherently stochastic. Consider for instance a
  washing machine running a programme A. The energy consumption of several 
  runs of this programme will depend on factors like how much the machine is 
  loaded and the temperature of the inlet water. In essence each run will 
  represent a unique profile. In order to schedule the loads, it is necessary 
  to have an average profile, and preferably some confidence bounds in order
  to schedule for a worst case situation etc.
  
  With time regular sampling, there will be a sample of the cumulative energy 
  consumed by the device mode at fixed time intervals. Each run will result in
  a time series of time-energy values, and the time stamps will be the same 
  for all runs, for instance seconds since start of the task. An average of all
  runs can then be produced by the average sample at a each sample time, and 
  the variance of the samples at that sample time can be used to establish 
  a confidence interval around the mean value. The average load profile can 
  easily be represented as the interpolation of the average sample values at 
  the sample points.
  
  Energy is however often measured using event driven sampling. This means that
  the sample is recorded whenever a certain amount of energy is consumed, and 
  the interval between two samples will not be the same. With different time 
  values it is not possible to construct the average profile as the curve 
  through the average values of the sample times. A different approach must be
  taken and a consistent way of doing this based on B-Splines is outlined in 
  [1] and encoded here. The actual steps of the algorithm is explained in the 
  implementation. 
  
  This module defines the B-Spline name space and the base polynomial class to
  represent the interpolation polynomials between the adjacent "knots" of the 
  curve. The Curve itself is defined as a separate class using the base 
  polynomials and a set of control points to produce the B-Spline curve. The 
  Regression is a B-Spline curve, but constructed from time series data, i.e. 
  either a file or a container containing time-value pairs. 
  
  There are multiple BSpline implementations available. The Gnu Scientific 
  Library (GSL) has B-spline routines implemented in C [2]. This implies that
  it uses a series of functions and heap allocated scratch areas to produce 
  the spline. It also has its own matrix format. Encapsulating these routines 
  would be possible. An alternative implementation considered is the unsupported
  Eigen spline curve [3]. The implementation of the Base functions and the 
  curve itself is inspired by this, but since the learning framework uses 
  Armadillo [4] as the linear algebra library, a reuse of the implementation 
  for the Eigen [5] linear algebra library would necessitate both libraries 
  installed. Armadillo was selected since its interface is slightly closer to 
  the STL, and it is used in other machine learning projects like MLPACK [6].
  Part of the motivation for this implementation is therefore to provide a 
  B-Spline implementation using Armadillo. A final option would have been to 
  reuse the self-contained library SPLINTER [7]. However SPLINTER uses its own
  regression methodology, and it is not clear if it could be modified to support
  the methodology described from [1]. Furthermore, the purpose of SPLINTER is 
  to do 2D and 3D approximations, hence it may be seen as an over-kill to use
  this library for a strict 1D regression curve.
  
  REFERENCES:
  
  [1] Geir Horn, Salvatore Venticinque, and Alba Amato (2015): "Inferring 
      Appliance Load Profiles from Measurements", in Proceedings of the 8th 
      International Conference on Internet and Distributed Computing 
      Systems (IDCS 2015), Giuseppe Di Fatta et al. (eds.), Lecture Notes in 
      Computer Science, Vol. 9258, pp. 118-130, Windsor, UK, 2-4 September 2015
  [2] http://www.gnu.org/software/gsl/manual/html_node/Basis-Splines.html#Basis-Splines
  [3] http://eigen.tuxfamily.org/dox/unsupported/classEigen_1_1Spline.html
  [4] http://arma.sourceforge.net/
  [5] http://eigen.tuxfamily.org/index.php?title=Main_Page
  [6] http://mlpack.org/
  [7] https://github.com/bgrimstad/splinter
        
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/
  
#ifndef BSPLINE_REGRESSION
#define BSPLINE_REGRESSION

#include <vector>
#include <initializer_list>
#include <type_traits>

namespace BSpline {
  
/******************************************************************************
  Base polynomial
*******************************************************************************/

class Basis
{
protected:
  
  std::vector< double > Knots;
  
public:
  
  // The operator takes the indices (i,j) and the parameter t for which the 
  // polynomial is to be evaluated, and produces the recursive solution.
  
  double operator() (unsigned int KnotInterval, unsigned int Degree, double t);
  
  // The constructor basically gets the knots either directly as a vector or 
  // as iterators to some container, or as an initialiser list. In all cases 
  // the actual initialisation will be done by the standard 
  
  Basis( const std::vector< double > GivenKnots )
  : Knots( GivenKnots )
  { }
  
  template< class IteratorType >
  Basis( IteratorType Begin, IteratorType End )
  : Knots( Begin, End )
  { 
    static_assert( 
      std::is_floating_point< typename IteratorType::value_type >::value,
      "Iterator must point to a real type" );
  }
  
  Basis( const std::initializer_list< double > & InitialValues )
  : Knots( InitialValues )
  { }
  
  // There is also a default constructor to be used when the knot values will 
  // be assigned later, and therefore an assignment operator is provided.
  
  Basis( void )
  : Knots()
  { }
  
  void operator= ( const std::vector< double > & GivenKnots )
  {
    Knots = GivenKnots;
  }
};

/******************************************************************************
  The Curve
  
  The curve extends the basis with control point information, and basically 
  implement De Boor's algorithm for computing the curve points.
  
*******************************************************************************/

class Curve : private Basis
{
public:
  
  using Basis::operator();  
};

}	// Name space BSpline
#endif	// BSPLINE_REGRESSION