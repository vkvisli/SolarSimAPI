/*=============================================================================
  CoSSMic Network Interface

  The general mechanism when a message is sent across the network is that it 
  will be serialised by the presentation layer, the external addresses of the 
  sender and the remote receiver will be provided by the session layer, and 
  finally the message will be encoded according to the right transmission 
  protocol by the network layer. After serialisation, the other layers will 
  see the message as a payload string of characters.
  
  CoSSMic uses XMPP as the network layer, and XMPP datagrams supports a field 
  called "subject" in addition to the "payload". In CoSSMic the subject is 
  taken to be the command or the packet type that may trigger any action at 
  the receiver side. The standard way of serialising a binary message of a 
  certain command type is to append the command type as the first part of the 
  payload string. This works fine if the remote receiver is another actor whose
  binary message is reconstructed from the payload string. However, other 
  CoSSMic actors may fail to recognise this message as it misses the subject
  field.
  
  The solution is to extend the XMPP link's sender function. This extension will
  read out the command string from the payload message, and then set this field
  in the message to be encoded by the XMPP link layer.
  
  That done, it is necessary to change the network end point as a consequence 
  so that it uses this extension link rather than the standard XMPP link to 
  serve the network layer.
  
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef CoSSMic_NETWORK_INTERFACE
#define CoSSMic_NETWORK_INTERFACE

#include "XMPP.hpp"

// The extensions belongs to the CoSSMic name space not to be confused with 
// the generalised mechanisms

namespace CoSSMic
{
  
// -----------------------------------------------------------------------------
// LINK EXTENSION
// -----------------------------------------------------------------------------
// The Link extension simply redefines the send function, and the constructor 
// and destructor are needed for the class to be properly constructed.

class LinkExtension : public Theron::XMPP::Link
{
public:
  
	 virtual void OutboundMessage( 
							  const Theron::XMPP::OutsideMessage & TheMessage, 
							  const Address From ) override;
	
  // The constructor simply forwards all parameters to the XMPP::Link
  
  LinkExtension( const std::string & EndpointName, 
								 const std::string & EnpointDomain, 
								 const std::string & ServerPassword,
								 const Theron::XMPP::JabberID & InitialRemoteEndpoint, 
								 std::string ServerName = "XMPPLink")
  : Theron::Actor( ServerName ),
    Theron::StandardFallbackHandler( ServerName ),
    Theron::XMPP::Link( EndpointName, EnpointDomain, 
												ServerPassword, InitialRemoteEndpoint, ServerName)
  { }
  
  // And the virtual destructor is just provided to ensure correct behaviour 
  // if other classes are derived from this, but it does nothing by itself.
  
  virtual ~LinkExtension( void )
  { }
};

// -----------------------------------------------------------------------------
// NETWORK INTERFACE
// -----------------------------------------------------------------------------
// The network interface simply provides and initialiser to ensure that the 
// extension link is used instead of the XMPP link. 

class NetworkInterface : public Theron::XMPP::Network
{
protected:
	
  // Only the network server creator function must be redefined
	
	virtual void CreateNetworkLayer( void ) override;
	
  // Then the constructor for the interface simply forwards the initialiser
  // as the type to use to the XMPP manger. This is exactly the same definition
  // as used for the XMPP manger's constructor.

  NetworkInterface( const std::string & EndpointName, 
										const std::string & Location, const std::string & Password,
									  const Theron::XMPP::JabberID & AnotherPeer 
																									  = Theron::XMPP::JabberID() )
  : Theron::XMPP::Network(  EndpointName, Location, Password, AnotherPeer )
  {  }

  // The virtual destructor is merely a place holder
  
public:
	
  virtual ~NetworkInterface( void )
  { }
};

  
}	// End name space CoSSMic
#endif  // CoSSMic_NETWORK_INTERFACE
