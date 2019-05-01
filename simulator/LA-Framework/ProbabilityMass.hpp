/*=============================================================================
  Probability mass
  
  A probability mass is a vector with real valued elements between 0 and 1,
  and where the sum of the elements is unity. It is defined in terms of a 
  standard vector of a defined probability class, but protects some of the 
  abilities to change elements of the vector after construction to ensure that 
  it stays a proper probability mass even if assignments are done to elements.

  Author: Geir Horn, University of Oslo, 2016, 2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPLv3.0
=============================================================================*/

#ifndef PROBABILITY_MASS
#define PROBABILITY_MASS

#include <vector>       // The main probability vector
#include <iterator>		  // Generic iterator base
#include <algorithm>    // To normalise the vector
#include <numeric>		  // To evaluate the sum of the vector
#include <cmath>		    // To evaluate the absolute value
#include <limits>		    // To check against the precision of the used type
#include <type_traits>  // To check that the type is really a real value
#include <stdexcept>    // Standard exceptions
#include <sstream>      // To format error messages
#include <iostream>     // To print error messages to cerr
#include <utility>		  // For the standard pair
#include <set>          // to select subsets of the vector
#include <functional>		// Standard functions
#include <optional>     // C++17 now includes optional as standard
#include <initializer_list>

// 2017 revision: Even though the probability mass is necessary for the Learning
// Automata framework, it is a general purpose tool which can be used without 
// any dependencies of the other files and classes in the LA Framework. It has 
// therefore been taken out of the LA name space

/*=============================================================================

 Probability

=============================================================================*/
//
// A probability is a real value that can only be in the interval [0,1] and 
// this limits the assignments and arithmetic operations that can be performed 
// on a probability. The idea with this class is that a probability value should
// be used and fully compatible with other values, even where the other values
// do not need to be probabilities. As an example consider the case where one 
// would like to compute the average of N probabilities by first adding them 
// together and then dividing on N. The sum of the probabilities can be any 
// real number in the interval [0,N], yet the average would again be a legal 
// probability.
  
template< typename RealType = double >
class Probability 
{
private:

  static_assert( std::is_floating_point< RealType >::value,
                 "Probability must be based on a real value type!");

  // Then the actual value field is of the real type. 
  
  RealType Value;
    
public: 
  
  // There is an interface function to obtain the value without changing it
  
  RealType GetValue( void ) const
  { return Value; }
  
  // Implicit conversion of the value to the its real type. This enables the 
  // definition of all other operators to take only a scalar type, as any 
  // probability will be converted into its real type. Declaring the cast 
  // to be to the basic types creates an ambiguity as all of the basic types
  // will match the operators and the compiler does not know which one to use.

  operator RealType() const
  { return static_cast< RealType >( Value ); }

  // There are interface methods to assign the value. The type is supposed to
  // be a basic type, and hence there is no need to pass it by reference as 
  // a copy is as efficient.
  
  template< typename OtherReal >
  void operator= ( const OtherReal NewValue )
  { 
    static_assert( std::is_floating_point< OtherReal >::value,
		  "Probability must be assigned a real value!");
    
    if ( (static_cast< OtherReal >(0.0) <= NewValue) && 
         (NewValue <= static_cast< OtherReal >(1.0) )     )
      Value = static_cast< RealType >( NewValue ); 
    else
    {
      std::ostringstream ErrorMessage;
      
      ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						       << NewValue << " is not a legal probability in [0,1]!";
      
      throw std::domain_error( ErrorMessage.str() );
    }
  }
    
  // Adding or subtracting values will not necessarily create a new probability
  // and there is no need to check that the produced values are in [0,1]. The 
  // given value does not even have to be a real value, as long as it can be 
  // cast to the real type used to store the probability. If two probabilities
  // are added there is no need for them to be a legal probability so the 
  // value is just returned. The same goes for division and multiplication.
  
  template< typename ValueType >
  inline RealType operator+ ( const ValueType OtherValue ) const
  { return Value + static_cast< RealType >( OtherValue );  }

  template< typename ValueType >
  inline RealType operator- ( const ValueType OtherValue ) const
  { return Value - static_cast< RealType >( OtherValue ); }
  
  template< typename ValueType >
  inline RealType operator* (const ValueType OtherValue ) const
  { return Value * static_cast< RealType >( OtherValue ); }
  
  template< typename ValueType >
  inline RealType operator/ (const ValueType OtherValue ) const
  { return Value / static_cast< RealType >( OtherValue ); }
  
  // Adding or subtracting a value to the existing probability is more risky 
  // as there is fair chance that the result will be outside the legal interval.
  // It should be noted that these are considered assignments that will not 
  // return any object.
  
  template< typename ValueType >
  void operator+= (const ValueType OtherValue )
  {
    RealType Term = static_cast< RealType >( OtherValue );
    
    if ( Value + Term <= 1.0 )
      Value += Term;
    else
    {
      std::ostringstream ErrorMessage;
      
      ErrorMessage  << __FILE__ << " at line " << __LINE__ << ": "
						        << "Cannot add " << Term << " to probability " 
									  << Value << " and get a probability!";
      
      throw std::domain_error( ErrorMessage.str() );
    }
  }
  
  template< typename ValueType >
  void operator-= (const ValueType OtherValue )
  {
    RealType Term = static_cast< RealType >( OtherValue );
    
    if ( 0.0 <= Value - Term )
      Value -= Term;
    else
    {
      std::ostringstream ErrorMessage;
      
      ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						       << "Cannot subtract " << Term << " from probability "
								   << Value << " and get a probability!"; 
		   
      throw std::domain_error( ErrorMessage.str() );
    }
  }
  
  // The multiplication and subtraction assignment operator can be designed 
  // similarly.
  
  template< typename ValueType >
  void operator *= (const ValueType OtherValue )
  {
    RealType Factor = static_cast< RealType >( OtherValue );
    
    if ( Value * Factor <= 1.0 )
      Value *= Factor;
    else
    {
      std::ostringstream ErrorMessage;
      
      ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						       << Factor << " cannot be multiplied with " << Value
								   << " and get a probability in [0,1]";
		   
      throw std::domain_error( ErrorMessage.str() );
    }
  }
  
  template< typename ValueType >
  void operator /= (const ValueType OtherValue )
  {
    if ( OtherValue > 0.0 )
      Value /= static_cast< RealType >( OtherValue );
    else
    {
      std::ostringstream ErrorMessage;
      
      ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						       << "Cannot divide a probability by a negative value ("
								   << OtherValue << ") and get a probability in [0,1]";
		   
      throw std::domain_error( ErrorMessage.str() );
    }
  }
  
  // Comparisons are more difficult. A real number has by definition limited 
  // accuracy so there is a high probability that a test like Sum == 1.0 will 
  // fail. Even more so because the number 1.0 is by default interpreted as a 
  // double and if the real type used for the probability vector is a float, 
  // the test will fail because float has less precision than a double. The 
  // provided test is therefore only testing equality to the precision of the 
  // real type used.
  
  template< typename ValueType >
  inline bool operator== ( const ValueType OtherValue ) const
  {
    return fabs( Value - static_cast< RealType >( OtherValue ) ) <=
	   std::numeric_limits< RealType >::epsilon();
  }
  
  template< typename ValueType >
  inline bool operator!= (const ValueType OtherValue ) const
  { return ! this->operator==< ValueType >( OtherValue ); }
  
  // The less-than or less-than-equal and greater-than or greater-than-equal 
  // tests do not require any special attention and can readily reuse standard
  // tests provided that the number given can be cast to the same type as the 
  // real value used for this probability class.
  
  template< typename ValueType >
  inline bool operator< (const ValueType OtherValue ) const
  { return Value < static_cast< RealType >(OtherValue); }
  
  template< typename ValueType >
  inline bool operator> (const ValueType OtherValue ) const
  { return Value > static_cast< RealType >(OtherValue); }
  
  template< typename ValueType >
  inline bool operator<= (const ValueType OtherValue ) const
  { return Value <= static_cast< RealType >(OtherValue); }
  
  template< typename ValueType >
  inline bool operator>= (const ValueType OtherValue ) const
  { return Value >= static_cast< RealType >(OtherValue); }
  
  // The constructors take the given probability value directly, or it will 
  // make a copy or a move of another probability value. For the direct value
  // it will use the assignment operator since this assures that the value is 
  // in the legal interval [0,1], whereas if another probability is given it 
  // may directly be copied by value.
  
  template< typename ValueType >
  Probability( const ValueType GivenProbability )
  { this->operator=< ValueType >( GivenProbability ); }
  
  template< typename OtherReal >
  Probability( const Probability< OtherReal > & Other )
  { Value = static_cast< RealType >( Other.Value ); }
  
  template< typename OtherReal >
  Probability( const Probability< OtherReal > && Other )
  { Value = static_cast< RealType >( Other.Value ); }
  
  // The default constructor simply sets the probability to zero
  
  Probability( void )
  { Value = 0.0; }
};

/*=============================================================================

	Probability Mass

=============================================================================*/
//
// The probability mass is a standard vector of probabilities with the 
// characteristic property that the sum of the probabilities is unity. It is 
// implemented as a standard vector of probabilities based on some real type
// (float, double, long double) with double defined as the default. The 
// probability mass inherits the vector's functionality as private to avoid
// that elements of the vector is changed not using the appropriate access 
// functions ensuring that the vector remains a probability mass.
//
// Changing elements of a probability mass implies that the other elements 
// must also change in order to preserve the characteristic property. The class
// therefore implements certain change policies. If an element of the mass is 
// explicitly assigned, it means that the user knows the real value of this 
// particular probability. The remaining probabilities are then normalised 
// such that their total probability mass is 1-p where p is the assigned 
// probability. 
//
// This assignment principle can be extended to the user giving multiple 
// probabilities. Then the remaining, not explicitly given, probabilities must
// be normalised to the probability mass of 1-sum(p_i) where p_i is a given 
// probability. 
// 
// Care must be taken since it is a major difference between assigning multiple
// probabilities one by one or as a block. If they are assigned one by one,
// then the first probability assigned will be adjusted by the normalisation 
// made when the second probability is given. In the block assignment, all 
// assigned probabilities do keep their assigned values in the final probability
// mass.

template< typename RealType = double >
class ProbabilityMass : private std::vector< Probability< RealType >  >
{
private:
  
  // The vector used for storing the probabilities is defined as a type to 
  // make the code more readable
  
  using ProbabilityVector = std::vector< Probability< RealType > >;
  
public: 

  static_assert( std::is_floating_point< RealType >::value,
		 "Probability vector must be based on a real value type!");
    
  // The type used for indexing the vector is defined as a standard size type
  
  using size_type = typename ProbabilityVector::size_type;
	  
  // The same type name can also be referred to as the index type
	  
  using IndexType = typename ProbabilityVector::size_type;

private:
  
  // ---------------------------------------------------------------------------
  // Vector management
  // ---------------------------------------------------------------------------

  // Since the base class vector depends on the template parameter, it is 
  // necessary to declare the methods that is used by this derived class.
  
  using ProbabilityVector::begin;
  using ProbabilityVector::end;
  using ProbabilityVector::push_back;
  
  // The most important functionality is the normalisation of a vector. This 
  // implies that the vector should sum to unity. Hence, first the sum of the 
  // vector is obtained, and if that is different from unity, the vector is 
  // normalised by dividing each element on this sum. However,  if the sum 
  // is close to zero or zero it may be numerically difficult to use it as a 
  // divisor. In this case all the infinitesimal probabilities are set to 
  // zero The interval of zero is taken to be the 10 times the smallest value
  // that can be represented in a double.
  
  void Normalise( void )
  {
    RealType Sum = std::accumulate( begin(), end(), RealType(0) );
    
    if ( Sum < 10 * std::numeric_limits<double>::epsilon() )
      std::transform( begin(), end(), begin(), 
        [](Probability<RealType> & aProbability)->RealType{ 
            return RealType(0); });
    else if ( (Sum < 1.0) || (Sum > 1.0) )
      std::transform( begin(), end(), begin(), 
		      [=](Probability<RealType> & aProbability)->RealType{ 
			  return aProbability / Sum;} );
  }
    
public:
  
  // Some useful functions from the base vector must be declared public to 
  // be available to users of the probability vector.
  
  using ProbabilityVector::at;  
  using ProbabilityVector::size;
  using ProbabilityVector::empty;
  using ProbabilityVector::cbegin;
  using ProbabilityVector::cend;
  using ProbabilityVector::clear;
  
  // The resize function supports only a specific number of elements, and there
  // are two alternatives: If the number of element is less than the current 
  // size of the probability mass, elements at the end will be dropped. If 
  // the size is larger than the current size, it will be extended with new 
  // elements. The new elements could be set to zero, if they are to be 
  // separately assigned later, or they can be assigned to any other value.
  // The default is to initialise them to 1/n as if the probability mass is 
  // uniform over all elements. However, in order to keep the knowledge 
  // already encoded in the existing probabilities, the mass will be 
  // re-normalised after removing elements or adding new elements. 
  
  void resize( size_type n )
  {
    ProbabilityVector::resize( n, 1.0 / static_cast<RealType>(n) );
    Normalise();
  }
  
  void resize( size_type n, Probability<RealType> & InitialValue )
  {
    ProbabilityVector::resize( n, InitialValue );
    Normalise();
  }
  
  // ---------------------------------------------------------------------------
  // Assignments
  // ---------------------------------------------------------------------------

  // The first assignment is really an initialisation, clearing the current 
  // content and setting all elements to the same default value 1/n. Note that
  // in contrast with the standard assign function, this does not actually take
  // the value to assign. This because the mass must be normalised after the 
  // assignment, and then the result would be 1/n independent of which value 
  // is given to this function.
  
  void assign( size_type n )
  {
    ProbabilityVector::assign( n, 1.0 / static_cast<RealType>(n) );
  }
  
  // If the assignment is made from a list of probabilities, it must also be
  // normalised since there is no assurance that the user gives a proper 
  // probability mass.
  
  template< typename OtherReal >
  void assign( std::initializer_list< OtherReal > InitialValues )
  {
    ProbabilityVector::assign( InitialValues );
    Normalise();
  }
  
  // Single value assignments will normally happen through the operator [] or 
  // the "at" function to be compatible with standard vectors. However, in a 
  // standard vector changing one element has no bearing on the other elements,
  // and a reference to the stored element can be returned and subsequently 
  // assigned. Here it is necessary to normalise the vector of the remaining 
  // elements before the assigned element is stored. The solution is to make 
  // the access operators return a class, derived from the Probability, to 
  // update the probability mass accordingly. 
  
private:
  
  friend class AssignSingle;
  
  class AssignSingle : public Probability< RealType >
  {
  private:
    
    // The class must store a pointer to the probability mass object owning 
    // the changed probability, and the index of the probability to assign
    
    ProbabilityMass< RealType > * This;
    size_type Index;
    
  public:
    
    // It is necessary to declare all the operators of the probability again 
    // in order to make the accessible for this class
    
    using Probability< RealType >::GetValue;    
    using Probability< RealType >::operator RealType;

    using Probability< RealType >::operator=;
    using Probability< RealType >::operator+;
    using Probability< RealType >::operator-;
    using Probability< RealType >::operator*;
    using Probability< RealType >::operator/;
    using Probability< RealType >::operator+=;
    using Probability< RealType >::operator-=;
    using Probability< RealType >::operator==;
    using Probability< RealType >::operator<;
    using Probability< RealType >::operator>;
    using Probability< RealType >::operator<=;
    using Probability< RealType >::operator>=;
    
    // The constructor takes the index and the probability mass pointer and 
    // makes sure that the probability value equals the probability at the 
    // index.
    
    AssignSingle( size_type i, ProbabilityMass< RealType > * ProbMass )
    : Probability< RealType >( ProbMass->ProbabilityVector::at(i) ),
      This( ProbMass )
    { Index = i;  }
    
    // The actual assignment should be handled by the assignment operators on 
    // the probability, but when this object is destructed it will normalise 
    // the remaining elements of the probability vector to the remaining 
    // probability mass after the removal of the value of this probability,
    // and then assign this probability to its correct position.
    //
    // Mathematically: The sum of the p untouched elements is 1-v where v is 
    // the value of the element to change before the re-assignment. If this 
    // element is assigned the value V then the the untouched elements must 
    // sum to 1-V. This implies that they should be multiplied with the factor
    // (1-V)/(1-v) since
    //	sum( p * (1-V)/(1-v) ) = (1-V)/(1-v)*sum(p) = (1-V)/(1-v) * (1-v) 
    //			       = 1-V.
    // An important assumption is that the past value would not be unity as the
    // weight would be infinite. In this case (1-V) should be equally divided 
    // among the other probabilities, currently zero.
    // 
    // Another test is to see if the probability mass has more than just this 
    // element. If this is the only element, the new value must be unity, and 
    // also the past value must be unity otherwise the vector would not have 
    // been a probability mass. Hence, if the attempted assignment is for 
    // a value not unity, an invalid argument exception will be thrown.
    
    ~AssignSingle( void )
    {
      if ( This->size() > 1 )
      {
       Probability<RealType> PastValue = This->ProbabilityVector::at( Index );
       RealType Weight 		      = 1.0 - GetValue();
       
       if ( PastValue == 1.0 )
       {
         Weight /= static_cast< RealType >( This->size() - 1 );
         This->ProbabilityVector::assign( This->size(), Weight );
       }
       else
       {
         Weight /= ( 1.0 - PastValue );

         // The old value is set to zero since it is not included in the sum 
         // and it may not be a probability when scaled. 
           
         This->ProbabilityVector::at( Index ) = 0.0;
         
         // The the weight is applied to all elements of the probability vector
         
         std::transform( This->begin(), This->end(), This->begin(),
             [=]( Probability< RealType > & p )->RealType{
               return p * Weight;
             });	
       }
       
       // Finally, the assigned value is stored in the vector.
          
       This->ProbabilityVector::at( Index ) = GetValue();
      }
      else if ( GetValue() != 1.0 )
      {
				// Normally this would lead to an exception being thrown, but since 
				// this is in the destructor and a destructor should not throw by 
				// convention in C++,  it is only possible to print the error message
				// and terminate the application.
				
        std::cerr << GetValue() << "Cannot be assigned to a probability "
                  << "mass with only one element";
                 
        exit( EXIT_FAILURE );
      }
    }
  };
  
public:
  
  // The "at" function will now return this class. Note that there is no 
  // explicit test that the index value is legal since the assign single uses
  // the "at" function of the underlying vector, and this will throw if the 
  // index is out of range.
  
  inline AssignSingle at( size_type i )
  { return AssignSingle( i, this );  }
  
  inline RealType at( size_type i ) const
  { return ProbabilityVector::at(i); }

  // The [] operator is identical and included for completeness.
  
  inline AssignSingle operator[] ( size_type i )
  { return AssignSingle( i, this ); }
  
  inline RealType operator[] ( size_type i ) const
  { return ProbabilityVector::at(i); }

  // In the case that many probabilities should be assigned, one cannot use 
  // repeatedly the single probability assignment since the second probability
  // assigned would re-normalise the first probability assigned so it may end 
  // up with a different value. This consideration implies that the 
  // probabilities to be assigned and their indices must be stored provided 
  // as a structure. The default assignment takes a vector of optional 
  // probabilities that must have the same size as the current probability 
  // mass, and must add up to a probability value less or equal to unity.
  
  using OptionalProbability   = std::optional< Probability< RealType > >;
 	using OptionalProbabilities = std::vector< OptionalProbability >;
  
  template< typename OtherReal > 
  void assign( const std::vector< std::optional< Probability< OtherReal > > > & GivenProbabilities )
  {
    // A simple size test is performed first to ensure that the given vector 
    // is legal.
    
    if ( GivenProbabilities.size() != size() )
    {
      std::ostringstream ErrorMessage;
      
      ErrorMessage  << "Size of given probability vector (" 
                    << GivenProbabilities.size() << ") must be equal to the "
                    << "size of the probability mass (" << size() << ")!";
		   
      throw std::invalid_argument( ErrorMessage.str() );
    }
    
    // The next test is to ensure that the sum of the given probabilities 
    // is less or equal to unity. This require a pass of the given vector.
    // and at the same time the mass of the probabilities unchanged by the 
    // assignment will be recorded for correct normalisation.
    
    RealType GivenMass     = 0.0,
             MassUnchanged = 0.0;
	     
    for ( size_type Index = 0; Index < size(); Index++ )
      if ( GivenProbabilities[ Index ] )
        GivenMass += GivenProbabilities[ Index ].value();
      else
        MassUnchanged += ProbabilityVector::at( Index );
    
    // Then the given probabilities can be assigned and the remaining 
    // probabilities normalised to the remaining probability mass. If the 
		// Given mass is larger than zero, the probability should be normalised 
		// to unity. 
    
    RealType UnchangedWeight = (1.0 - std::min(GivenMass, 1.0)) / MassUnchanged,
             GivenWeight     = std::max( 1.0, GivenMass );
    
    for ( size_type Index = 0; Index < size(); Index++ )
      if ( GivenProbabilities[ Index ] )
        ProbabilityVector::at( Index ) = GivenProbabilities[Index].value()
																						/ GivenWeight;
      else
        ProbabilityVector::at( Index ) *= UnchangedWeight;
  }
  
  // The probabilities to assign can also be stored as a pair given the 
  // index to change and the new probability to assign. The assignment is 
  // then based on iterators to a structure of such records.
  
  using ProbabilityRecord = std::pair< size_type, Probability<RealType> > ;
  
  template< class IteratorType >
  void assign( IteratorType Begin, IteratorType End )
  {
    static_assert( std::is_same< typename IteratorType::value_type, 
																 ProbabilityRecord >::value, 
    "Iterators must point to LA::ProbabilityMass::ProbabilityRecord elements" );

    // In line with the principle of not duplicating code, the given data is 
    // used to create a vector that may be passed to the previous version of 
    // the assign multiple method.
    
    OptionalProbabilities GivenProbabilities( size() );
    
    for (IteratorType aProbability = Begin; aProbability != End; ++aProbability)
      GivenProbabilities.at( aProbability->first ) = aProbability->second;
    
    // Then the probabilities can be assigned the standard way.
    
    assign( GivenProbabilities );
  }
  
  // There is also possible to generate the probabilities of the mass. In this
  // case it is necessary to normalise the vector since there is no guarantee 
  // that the generated probabilities will add up to unity.
  
  void assign( std::function< Probability<RealType>( IndexType )> & Generator )
	{
		for ( IndexType index = 0; index < size(); index++ )
			at( index ) = Generator( index );
		
		Normalise();
	}
  
  // Finally, an assignment can be made based on another probability mass. It 
  // is based on the underlying vector's assignment operator providing the 
  // same interface and return value; and exist both as a copy operator and 
  // as a move operator.
  
  template< typename OtherReal >
  ProbabilityMass< RealType > & operator= (
    const ProbabilityMass< OtherReal > & Other )
  {
    ProbabilityVector::operator= ( Other );
    
    return *this;
  }
  
  template< typename OtherReal >
  ProbabilityMass< RealType > & operator = (
    const ProbabilityMass< OtherReal > && Other )
  {
    ProbabilityVector::operator= ( Other );
    
    return *this;
  }
  
  // The final version is with the initialiser list for which the version of 
  // the assign method with the initialiser list will be used.
  
  template< typename OtherReal > 
  ProbabilityMass< RealType > & operator = (
		   std::initializer_list< OtherReal > InitialValues )
  { 
    assign( InitialValues ); 
    
    return *this;
  }

  // ---------------------------------------------------------------------------
  // Element operations
  // ---------------------------------------------------------------------------

  //  It may be necessary to partition the set of probabilities,  and then 
  //  accumulate the elements in a subset of elements. In order to ensure that
  //  each element is selected only once,  a set of indices is required as 
  //  input,  and this set must have a size less or equal to the number of 
  //  stored probabilities. If this is not the case a standard invalid argument
  //  exception will be thrown. Each element of the set must then correspond 
  //  to a legal index, and as the at function of the probability vector is 
  //  used, a standard out of range exception is thrown if this is not the 
  //  case.
  
  RealType accumulate( const std::set < size_type > & SubsetIndices )
  {
    RealType Sum( 0 );
    
    if ( SubsetIndices.size() > size() )
	 {
	   std::ostringstream ErrorMessage;
	   
	   ErrorMessage << SubsetIndices.size() <<  " indices given but only "
					    << size() <<  " probabilities are available";
      
      throw std::invalid_argument( ErrorMessage.str() );
    }
    
    for ( size_type Index :  SubsetIndices )
		 Sum += at( Index );
		 
	 return Sum;
  }
  
  // Similar to this it is possible to create another probability mass from 
  // this mass by taking some elements based on their indices. It will throw
  // an invalid argument exception of too many indices are given, and the 
  // elements will be copied on using the at function that will throw if one 
  // of the indices is out of range.
  
  ProbabilityMass < RealType > Take( 
                                  const std::set < size_type > & SubsetIndices )
  {
	 ProbabilityMass < RealType > SubsetMass;
	 
	 if ( SubsetIndices.size() > size() )
    {
      std::ostringstream ErrorMessage;
	   
	   ErrorMessage << SubsetIndices.size() <<  " indices given but only "
							    << size() <<  " probabilities are available";
      
      throw std::invalid_argument( ErrorMessage.str() ); 
	 }
	 
	 for ( size_type Index :  SubsetIndices )
       SubsetMass.push_back( at( Index ) );
       
    return SubsetMass;
  }
    
  // ---------------------------------------------------------------------------
  // Constructors
  // ---------------------------------------------------------------------------
  
  // A probability mass can be constructed from a standard vector, after 
  // potentially normalising the elements and verifying that they represent 
  // real probabilities. If they are not probabilities, then there is no way 
  // to recover the situation and a domain error is thrown. Since there is 
  // no copy constructor from a vector of the real values to a vector of 
  // probabilities, iterators must be used to initialise.
  
  template< class VectorAlloc >
  ProbabilityMass( const std::vector< RealType, VectorAlloc > & InitialValues )
  : ProbabilityVector( InitialValues.begin(), InitialValues.end() )
  { Normalise(); }
  
  // The probability mass can also be constructed from iterators, but since 
  // different types are possible, the constructor must be a template. The 
  // same checks are done on the provided values.
  
  template< class IteratorType >
  ProbabilityMass( IteratorType Begin, IteratorType End )
  : ProbabilityVector( Begin, End )
  { Normalise(); }
  
  // The probability mass can be copied or moved from another mass in a similar
  // way assuming that the floating point type of the other vector can be 
  // converted to the floating point type of this probability mass. There is 
  // no need for normalisation in this case since the given probability mass is
  // already normalised by definition. 
  
  template< typename OtherReal >
  ProbabilityMass( ProbabilityMass< OtherReal > & Other )
	: ProbabilityVector( Other )
	{ }
  
  template< typename OtherReal >
  ProbabilityMass( ProbabilityMass< OtherReal > && Other )
	: ProbabilityVector( Other )
	{ }
  
  // There is a constructor taking a number of elements in the vector and 
  // resize it accordingly.
  
  ProbabilityMass( size_type n )
  : ProbabilityVector()
  { assign( n ); }
  
  // There is also a constructor taking an initialiser list and assigning it
  // using the above assignment mechanisms. Since it is not possible to enforce
  // the requirement that these values will sum to unity, the probability mass 
  // must be normalised after the assignment.
  
  ProbabilityMass( std::initializer_list< RealType > InitialValues )
  : ProbabilityVector( InitialValues )
  { Normalise(); }
  
  // The default constructor leaves the probability mass empty
  
  ProbabilityMass( void )
  : ProbabilityVector()
  {}
  
}; // Class Probability Vector

/*=============================================================================

	Aliases

=============================================================================*/

using EmpiricalPDF = ProbabilityMass<double>;
using DiscretePDF  = ProbabilityMass<double>;

/*=============================================================================

	Probability Mass Generator functions

=============================================================================*/
//
// The main use of the probability vector is to ensure that a proper probability
// vector is received as input to various functions expecting a normalised 
// vector whose elements are probabilities in the interval [0,1]. One could 
// directly construct the template class in these cases, but the code becomes 
// more readable if a generator function is used. 
//
// The first function takes a vector of a real type, and return the 
// corresponding probability vector class.

template< typename RealType >
auto PDF( const std::vector<RealType> & InitialValues )
-> ProbabilityMass< RealType >
{
  static_assert( std::is_floating_point< RealType >::value,
		 "Probability mass must be based on a real value type!");

  return ProbabilityMass< RealType >( InitialValues );
}

// In case the values are not given as a vector, two iterators are taken and 
// in this case the standard allocator will be used for the probability vector,
// and the type of the vector will be double since all numerical values may 
// be cast to a double.

template< class IteratorType >
auto PDF( IteratorType Begin, IteratorType End )
-> ProbabilityMass< double >
{
  return ProbabilityMass< double >(Begin, End);
}

/*=============================================================================

	Output stream functions

=============================================================================*/

template< typename RealType >
std::ostream & operator<< ( std::ostream & out, 
                            Probability< RealType > & TheProbability )
{
  out << TheProbability.GetValue();
  return out;
}

template< typename RealType >
std::ostream & operator<< ( std::ostream & out, 
						                ProbabilityMass< RealType > & ThePDF )
{
  out << "[ ";

  for ( auto & TheProbability : ThePDF )
    out <<  TheProbability.GetValue() << " ";
      
  out << "]";

  return out;
}

#endif // PROBABILITY_VECTOR
