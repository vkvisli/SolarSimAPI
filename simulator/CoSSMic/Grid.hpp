/*=============================================================================
  Grid
  
  The Grid is a special type of producer that receives a schedule command. The
  default behaviour is to accept any request, although this behaviour can be 
  overloaded. The potential of having a grid with restrained capacity is one 
  reason for defining this as a separate actor. 
  
  With the default behaviour, there is essentially no reason to send the 
  request from consumers on remote endpoints (households) to a central grid 
  agent, since it is known à priori that the request will be accepted. Thus 
  a grid actor can run locally and transparently respond to the local actors'
  requests.
  
  However, if one has a constraint grid or if one just want to obtain statistics
  about when and how much the grid is being used by the consumers in the 
  neighbourhood, there is a need to have the grid running as one single agent
  in the system. 
  
  The default identifier of a producer is a string 
  
	producer[HouseholdID]:[DeviceID]
	
  Where the household identifier is an unsigned integer and the device 
  identifier is an unsigned integer. The special device ID zero is reserved for
  the grid actor, so if the grid is running locally it will have the actor 
  address 
  
        producer[HouseholdID]:[0]
        
  In the same way, the household ID zero means the grid, so the global grid 
  agent will have the address 
  
	producer[0]:[0]
        
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef COSSMIC_GRID
#define COSSMIC_GRID

#include <string>
#include <sstream>

#include "Actor.hpp"
#include "StandardFallbackHandler.hpp"
#include "PresentationLayer.hpp"
#include "DeserializingActor.hpp" // Support for receiving a serial message

#include "Producer.hpp"

namespace CoSSMic
{
  
// The Grid is a producer, and since the producer inherits the actor and the 
// de-serialising actor as virtual classes to avoid the diamond problem, their
// constructors must be explicitly called by the Grid class constructor. It 
// seems strange to call a constructor for a class that is not inherited, and 
// the unnecessary inheritance is made explicit here for readability of the 
// code.
  
class Grid : virtual public Theron::Actor,
						 virtual public Theron::StandardFallbackHandler,
				     virtual public Theron::DeserializingActor,
				     public Producer
{
private:
 
  // The convention defined for the global grid ID
  
  constexpr static auto GlobalGridID   = "[0]:[0]";
  constexpr static auto GlobalGridName = "grid[0]:[0]";
  
  // Local consumers will see the grid address as a global address, and ideally
  // it could be implemented in terms of a global actor address for the 
  // consumers on this network endpoint. However, Theron does not support 
  // global variables. Hence, the address is stored as a static string, which 
  // is by default initialised to the grid globally unique address when there
  // is only one grid agent running in the system. If there is an instantiation 
  // of a local grid actor it will have the value corresponding to the local 
  // grid actor's address as a string. 
  
  static std::string GridActorName;
   
  // The lookup function is also static so that it can be invoked even without 
  // any local grid actor object. It simply constructs the Theron address 
  // object based on the stored string. It is static so that it can be called
  // with or without an instantiated grid object.

public:
  
  inline static Theron::Address Address( void )
  {
    return Theron::Address( GridActorName.data() );
  }

  // In the same way the ID assigned to the Grid actor is stored in a static
  // variable initialised to the global grid ID, but possibly changed in the 
  // constructor.
  
private:
  
  static IDType GridID;
  
  // The interface function is used to obtain this ID even without knowing the 
  // Grid agent object.
  
public:
  
  inline static IDType ID( void )
  { return GridID; }
  
protected:
  
  // When a schedule command is received from a consumer, the new load handler 
  // is invoked. This simply accepts the load in the default implementation,
  // and returns the earliest start time back to the consumer.
  
  virtual void NewLoad( const Producer::ScheduleCommand & TheCommand,
			const Theron::Address TheConsumer );

  // Note that it is not necessary to implement the proxy removal handler, i.e.
  // the kill proxy since the basic functionality of the producer's handler is
  // sufficient with nothing to be added for the default grid.
  
public:
  
  // The constructor can either be given a framework as execution context if 
  // the grid is a local agent that executes without any global presence, or 
  // it can be given a network endpoint to execute as an externally addressable
  // agent. This dualism is supported by defining the constructor as a template
  // as the specific additional agent registration is anyway managed by the 
  // base class producer. 
  //
  // If an invalid ID is given, then the Theron's automatic naming is applied.
  // If no ID is provided, it is assumed by default to be the global Grid ID,
  // as the static variable has been initialised before this object is 
  // constructed. It should be noted that as the actor is inherited as a virtual
  // base class, the call on the actor's constructor made by the producer base 
  // class will be suppressed by the compiler, and only the explicit call below 
  // will be valid.
  //
  // However, the same applies for derived classes, and it is therefore 
  // necessary to cache the actor name, since it may be set differently by a
  // derived class, or if a different ID is explicitly given, or if the actor 
  // was assigned a default name by Theron.
  //
  // Even though the first parameter can be any type in this template version,
  // the Producer constructor will only accept either a Theron Framework or 
  // a Theron Network Endpoint, and the compiler should issue an error if the 
  // provided type is not one of the two.

  Grid( const IDType & TheID = IDType( GlobalGridID ) )
  : Actor( ( ValidID( TheID ) ? 
			       "grid"  + std::string( TheID ) : std::string() ) ),
    StandardFallbackHandler( GetAddress().AsString() ),
    DeserializingActor( GetAddress().AsString() ),
    Producer( TheID )
  {
    GridActorName = GetAddress().AsString();
    
    if ( ValidID( TheID ) )
      GridID = TheID;
  }
  
  // The destructor does nothing, but is a place holder for derived classes
  
  virtual ~Grid()
  { }
};

}	// Name space CoSSMic
#endif 	// COSSMIC_GRID
