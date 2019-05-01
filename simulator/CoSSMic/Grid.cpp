/*=============================================================================
  Grid

  This is the implementation of the Grid class. Please see the header file for
  details and use.
  
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <string>
#include <sstream>
#include <stdexcept>
#include "Producer.hpp"
#include "ConsumerProxy.hpp"
#include "Grid.hpp"

namespace CoSSMic
{
// -----------------------------------------------------------------------------
// Grid name and address handling
// -----------------------------------------------------------------------------
//
// The static name of the grid should be initialised by the compile before 
// main is invoked, and it represents by default the global grid actor. Hence
// if there is no grid actor running on a node, the global name is assumed.
  
std::string Grid::GridActorName( Grid::GlobalGridName );
IDType      Grid::GridID;

// -----------------------------------------------------------------------------
// Assigning new loads
// -----------------------------------------------------------------------------
//
// The default handler for the consumer requests first lets the producer's 
// new load function construct the proxy for the consumer, and then a reference
// to this consumer proxy is obtained and used to assign the initial start 
// time equal to the earliest start time.

void Grid::NewLoad( const Producer::ScheduleCommand & TheCommand, 
                    const Theron::Address TheConsumer )
{
  Producer::NewLoad( TheCommand, TheConsumer );
  
  // It is necessary to search for the reference to the consumer proxy since 
  // it has been constructed by the New Load handler and there is no better way
  // to get it than to look it up. Something is seriously wrong if it does not 
  // exist.
  
  ConsumerReference Consumer = FindConsumer( TheConsumer );
  
  if ( Consumer == EndConsumer() )
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						     << GetAddress().AsString() << " could not create a proxy for "
                 << TheConsumer.AsString();
		 
    throw std::runtime_error( ErrorMessage.str() );
  }
  
  // The assigned start time is returned via the consumer's proxy since 
  // the proxy could potentially use this information, or add extra 
  // information before returning the result to the consumer (possibly remote)
  
  Send( Producer::AssignedStartTime( (*Consumer)->AllowedInterval().lower() ), 
																		 (*Consumer)->GetAddress()  );
}

  
} // Name space CoSSMic
