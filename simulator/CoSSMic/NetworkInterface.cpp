/*=============================================================================
  CoSSMic Network Interface

  This file simply provides the overloaded methods of the CoSSMic XMPP link 
  extension and the accompanying network interface. See the header file for 
  details.
    
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <sstream>

#include "NetworkInterface.hpp"

// -----------------------------------------------------------------------------
// LINK EXTENSION
// -----------------------------------------------------------------------------
//
// The send message method uses the first word of the payload string as the 
// message's subject before sending it using the standard XMPP link's send 
// function. One thing to remark is that the whole stack supports only constant
// arguments in order to allow temporary (in-place or r-value) constructed 
// messages. It is therefore necessary to make a copy when this is forwarded.
 
void CoSSMic::LinkExtension::OutboundMessage( 
													   const Theron::XMPP::OutsideMessage & TheMessage, 
													   const Address From )
{
  std::istringstream Payload( TheMessage.GetPayload() );
  std::string        Subject;
  Theron::XMPP::OutsideMessage ExtendedMessage( TheMessage );
  
  Payload >> Subject;
  ExtendedMessage.SetSubject( Subject );
  
  Theron::XMPP::Link::OutboundMessage( ExtendedMessage, From );
}

// -----------------------------------------------------------------------------
// NETWORK INTERFACE
// -----------------------------------------------------------------------------
//
// The method creating the various communication stack servers is basically 
// the same as the XMPP manager, with the difference that it creates a 
// link extension instead of the just the XMPP link.

void CoSSMic::NetworkInterface::CreateNetworkLayer( void )
{
	Network::CreateServer< Network::Layer::Network, CoSSMic::LinkExtension >
	 ( GetAddress().AsString(), Domain, ServerPassword, InitialRemoteEndpoint  );
}
  
