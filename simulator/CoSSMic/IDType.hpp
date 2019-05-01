/*=============================================================================
  ID Type
  
  The address of an actor is the address used on the local node (network 
  endpoint) and in the multi agent system used in CoSSMic this actor address 
  will appear as the resource field in the Jabber IDs used by the XMPP protocol.
  
  Thus, and actor called "producer15" will, as an agent, send messages to other
  agents as "actormanager@host.domain/producer15". This address mapping is done
  at the transport layer of the protocol stack, and hence everything but the 
  resource field will be removed from the address on incoming messages to 
  preserve complete transparency between the actor system and the agent system.
  
  The crux is then that the actor IDs must be unique across the whole multi 
  agent system if they are to appear as agents. In other words, it cannot be an
  actor called "producer15" on network endpoint A and another "producer15" on 
  network endpoint B. This would only be possible if they only send messages, 
  since a receiving actor will see this message as coming from actor 
  "producer15" not knowing which node that has "producer15".
  
  When a Producer actor and agent is created it is created with an ID. By 
  convention it takes the actor ID "producer<ID>". Hence in the previous 
  example the ID is 15. Originally it was thought that an integer would
  be sufficient for the ID addresses, but it may be necessary to change it to 
  some other format. 
  
  This file basically defines a set of definitions for the ID that can be 
  changed should it be necessary to change the ID format.

  After some discussion it has been decided that the ID type is a string of 
  the form [HouseholdID]:[ApplianceID]:[ModeID] or [HouseholdID]:[DeviceID]
  The first form is used for loads where each appliance may have several 
  operational modi (think of different programs for a washing machine). The 
  latter form is used for producers. The best type to store these IDs is 
  a simple string. 

  Author: Geir Horn, University of Oslo, 2015-2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef ID_TYPE_DEFINITIONS
#define ID_TYPE_DEFINITIONS

#include <string>								// For useful strings
#include <sstream> 							// For building the ID from numeric values
#include <cstdio>  							// for sscanf
#include <stdexcept>						// For standard error reporting
#include <functional>						// For the hash function
#include <optional>

namespace CoSSMic
{
// The ID type is a string, but with added functionality and protecting its 
// string base from direct access

class IDType : public std::string
{
private:
  
  // The numeric fields are stored for quick reference. Note that the mode
  // is an optional field that might not be given.
  
  unsigned long int Household, Device;
  std::optional< unsigned long int > Mode;
  
public:
  
  // ---------------------------------------------------------------------------
  // Operators
  // ---------------------------------------------------------------------------
  //
  // It is easy to test the validity of an ID based on whether the string has
  // a value or not. It is actually implemented as a boolean type cast.
  
  operator bool() const
  {
    if ( empty() )
      return false;
    else
      return true;
  }

  // It is necessary to compare to IDs for equality. There is a template version
  // able to handle any type of right hand side that can be converted into an 
  // ID, and then a specialisation for the ID type.

  bool operator== ( const IDType & Other ) const
  {
    if ( empty() || Other.empty() ) 
      return false;
    else 
      return (Household == Other.Household) &&
	     (Device == Other.Device) &&
	     (Mode == Other.Mode);
  }

  template< class ConvertibleType >
  bool operator== ( const ConvertibleType & Other ) const
  { 
    return this->operator==( IDType( Other ) ); 
  }
   
  template< class ConvertibleType >
  bool operator!= ( const ConvertibleType & Other ) const
  {
    return ! this->operator==( Other );
  }
 
  // For sorting IDs in maps and sets there must be a way to sort them in 
  // lexicographical order. It is first sorted on the household ID and then on
  // the device ID and finally on the mode.

  bool operator< ( const IDType & Other ) const
  {
    if ( Household < Other.Household )
      return true;
    else if ( Household == Other.Household )
    {
      if ( Device < Other.Device )
	return true;
      else if ( (Device == Other.Device) && ( Mode < Other.Mode ) )
	return true;
    }
    
    return false;
  }

  template< class ConvertibleType >
  bool operator< ( const ConvertibleType & Other ) const
  { 
    return this->operator< ( IDType( Other ) ); 
  }
  
  bool operator> ( const IDType & Other ) const
  {
    return Other < *this;
  }
  
  template< class ConvertibleType >
  bool operator> ( const ConvertibleType & Other ) const
  {
    return IDType( Other) < *this;
  }
  
  bool operator>= ( const IDType & Other )
  {
    return ! this->operator<( Other );
  }
  
  template< class ConvertibleType >
  bool operator>= ( const ConvertibleType & Other ) const
  {
    return ! this->operator<( IDType( Other ) );
  }
  
  bool operator<= ( const IDType & Other )
  {
    return ! this->operator>( Other );
  }
  
  template< class ConvertibleType >
  bool operator<= ( const ConvertibleType & Other )
  {
    return ! this->operator>( IDType( Other ) );  
  }
  
  // ---------------------------------------------------------------------------
  // Utility functions
  // ---------------------------------------------------------------------------
  //
  // There is a clear function to reset the ID to an empty state
  
  inline void Clear( void ) 
  {
    clear();
    Household = 0;
    Device    = 0;
    Mode      = std::optional< unsigned long int >();    
  }
  
  // Read-only access to the various numeric sub-fields is provided through 
  // the corresponding "Get" functions
  
  inline unsigned long int GetHousehold( void ) const
  { return Household; }
  
  inline unsigned long int GetDevice( void ) const
  { return Device; }
  
  inline std::optional< unsigned long int > GetMode( void ) const
  { return Mode; }

  // The plus operator should basically reuse the plus operator for strings, 
  // and therefore return a string. There are two options: The ID can be the 
  // left hand side (lhs) of the operator or it can be the right hand side (rhs)
  // of the operator. Both cases must be defined for the compiler to pick the 
  // right version.

  template< class RHStype >
  friend std::string operator+ ( const IDType & ID, const RHStype & rhs )
  {
    return static_cast< std::string >(ID) + std::string(rhs);
  }

  template< class LHStype >
  friend std::string operator+ ( const LHStype & lhs, const IDType & ID )
  {
    return std::string(lhs) + static_cast< std::string >(ID);
  }

  // In order to use the ID in structures with hashed keys, a hash function 
  // will be defined in the standard name space for this type. This will be 
  // reusing the standard hash function for strings, and in order to get access
  // to the underlying string type, the hash class must be defined as a friend.
  
  friend struct std::hash< IDType >;

  // ---------------------------------------------------------------------------
  // Assignment operators
  // ---------------------------------------------------------------------------
  //
  // The copy assignment simply copies the fields one by one, with the ID string
  // as the last element
  
  IDType & operator= ( const IDType & Other )
  {
    Household = Other.Household;
    Device    = Other.Device;
    Mode      = Other.Mode;
    
    assign( Other );
    
    return *this;
  }
  
  // The move assign operator is identical, but the move assign should be used
  // by the compiler.
  
  IDType & operator= ( IDType && Other )
  {
    Household = Other.Household;
    Device    = Other.Device;
    Mode      = Other.Mode;
    
    assign( Other );
    
    return *this;
  }
  
  // ---------------------------------------------------------------------------
  // Constructors
  // ---------------------------------------------------------------------------
  //
  // The default constructor simply behaves similar to the Clear method above,
  // and the other constructors delegates the basic initialisation to this 
  // constructor.
  
  inline IDType( void )
  : std::string(), Mode()
  {
    Household = 0;
    Device    = 0;
  }
  
  // The string constructor parses a string to check if it is a valid ID and 
  // then stores both the value fields and the ID string. The sscanf function 
  // is used for parsing the given string. There should be a better C++ way 
  // to do this. Boost::Qi might be an alternative but looks like an overkill.
  // Note that the constructor will never fail. If the provided string is not 
  // a valid ID, the constructed ID will be left invalid. 
  
  inline IDType( const std::string & TheID )
  : IDType()
  {
		// There are two variables, one to hold the optional mode and one to 
		// count the number of fields contained in the string. 
		
    unsigned long int ModeID;
		unsigned int      Count = 0;
								 
	  // The ID is taken to start with the first occurrence of the [ character in 
	  // the string
								 
	  std::string::size_type IDPosition = TheID.find("[");
		
		if ( IDPosition != std::string::npos )
			Count = std::sscanf( TheID.substr( IDPosition ).data(), 
													 "[%lu]:[%lu]:[%lu]", 
													  &Household, &Device, &ModeID  );
		
	  // If there are no ID part of the string or if it does not confirm to the 
		// right format, an error message is thrown
			
		if ( ( IDPosition == std::string::npos ) || ( Count < 2 ) )
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
									 << "The given ID string \"" << TheID << "\" does not "
									 << "define a valid ID of the format "
									 << "[Household]:[Device]:[Mode] with optional mode. Cannot "
									 << "construct the ID";
									 
		  throw std::invalid_argument( ErrorMessage.str() );
		}
		else
		{
		  // Then the ID string is formed ignoring whatever additional information 
		  // that was provided in the string, i.e. retaining only the parsed values
			 
			std::ostringstream IDString;
			
			IDString << "[" << Household << "]:[" << Device << "]";
			 
	    // The fields are set depending on how many fields that could be read from
	    // the provided ID string. Part of the behaviour is similar for two and 
	    // three fields, and therefore the situation of three fields given is 
	    // handled first.
			 
	    switch ( Count )
	    {
	      case 3:
					Mode = ModeID;
					IDString << ":[" << ModeID << "]";
	      case 2:
				  assign( IDString.str() );	
					break;
	      default:
					Clear();
					break;
	    }
		}
  }
  
  // In case the string is given as a C-type string, there is a separate 
  // constructor delegating the actual construction to the above string based
  // constructor.
  
  inline IDType( const char * CStringID )
  : IDType( std::string( CStringID ) )
  { }
  
  // There is also a constructor to build the ID from the various sub-fields
  
  inline IDType( const unsigned long int TheHousehold, 
								 const unsigned long int TheDevice, 
							   std::optional< unsigned long int > TheMode = 
								    std::optional< unsigned long int >() )
  : IDType() 
  {
    std::ostringstream IDString;
    
    Household = TheHousehold;
    Device    = TheDevice;
    
    IDString << "[" << TheHousehold << "]:[" << TheDevice << "]";
    
    if ( TheMode )
    {
      Mode = TheMode;
      IDString << ":[" << TheMode.value() << "]";
    }
    
    assign( IDString.str() );
  }
  
  // The copy constructor simply assigns all fields based on the values of 
  // the other ID
  
  inline IDType( const IDType & Other )
  : std::string( Other )
  {
    Household = Other.Household;
    Device    = Other.Device;
    Mode      = Other.Mode;
  }
  
  // The move constructor is similar for the scalar fields, but should use the 
  // string's move constructor and save on memory allocations.
  
  inline IDType( const IDType && Other )
  : std::string( Other )
  {
    Household = Other.Household;
    Device    = Other.Device;
    Mode      = Other.Mode;    
  }
    
  // ---------------------------------------------------------------------------
  // Input and output
  // ---------------------------------------------------------------------------
  //  
  // The input operator uses the standard input operator for streams, and 
  // then creates a temporary ID object using the stream constructor to 
  // verify that the ID is valid.

  friend std::istream & operator >> ( std::istream & InputStream, IDType & ID )
  {
    std::string IDString;
    
    InputStream >> IDString;
    ID = IDType( IDString );
    
    return InputStream;
  }

  // The output operator simply uses the stream operator for the string.

  friend std::ostream & operator << ( std::ostream & OutputStream, 
																      const IDType & ID )
  {
    OutputStream << static_cast< std::string >( ID );
    
    return OutputStream;
  }

};


// ---------------------------------------------------------------------------
// Utility methods
// ---------------------------------------------------------------------------
//  
// There is a an overload for testing the validity of an ID using the implicit 
// cast to a boolean

inline bool ValidID( const IDType & TheID )
{
  return TheID;
}

}	// End name space CoSSMic

// There is also a hash function to allow unordered maps and other structures 
// using the hash value of an ID. This is based on the string representation 
// of the ID, and uses the standard string hash function. It is defined in the 
// standard name space to allow unqualified used with the STL containers

namespace std {
  
  template<>
  class hash< CoSSMic::IDType >
  {
  public:
    
    size_t operator() (const CoSSMic::IDType & TheID ) const
    {
      return std::hash< string >()( static_cast< string >( TheID ) );
    }
  };
  
}	// End name space std

#endif 	// ID_TYPE_DEFINITIONS
