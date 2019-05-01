/*=============================================================================
  Test of the communication
  
  This version of the test function creates an explicit XMPP link, and then 
  a few actors that will all register to the XMPP link, and simply write out
  messages sent to them.
    
  Author: Geir Horn, University of Oslo, 2015
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <iostream>
#include "Actor.hpp"

#include "NetworkEndPoint.hpp"
#include "ActorRegistry.hpp"
#include "ConsolePrint.hpp"
#include "XMPP.hpp"

// ----------------------------------------------------------------------------
// Dummy actor printing out all messages
//-----------------------------------------------------------------------------

class ProtocolTester : public Theron::Actor
{
private:
  
  Theron::XMPP::JabberID ProtocolID;
  bool Active;
  
public:
  
  // MESSAGE HANDLERS
  // 
  // When a remote client indicates that it is available then this message is
  // forwarded by the Link Server
  
  void AvailabilityNotification( 
    const Theron::XMPP::Link::AvailabilityStatus & TheStatus,
    const Theron::Address From )
  {
    ConsolePrint TheConsole( GetFramework() );
    
    TheConsole << "Agent " << TheStatus.GetJID().toString() 
	       << " is now ";
	       
    switch ( TheStatus.GetStatus() )
    {
      case Theron::XMPP::Link::AvailabilityStatus::StatusType::Available :
	TheConsole << "available" << std::endl;
	break;
      case Theron::XMPP::Link::AvailabilityStatus::StatusType::Unavailable :
	TheConsole << "unavailable" << std::endl;
	break;
    }
  }
  
  // Normal messages received from the link server are just printed
  
  void ReceiveMessages( const Theron::XMPP::OutsideMessage & Message,
			const Theron::Address From )
  {
    ConsolePrint TheConsole( GetFramework() );
    
    TheConsole << "MESSAGE from " << Message.GetSender().toString() 
	       << " SUBJECT: " << Message.GetCommand()
	       << " BODY: " << Message.GetPayload() << std::endl;
  }
  
  // The constructor registers this actor with the actor registry and then
  // asks for the corresponding XMPP client to be created in the link server
  
  ProtocolTester ( Theron::Framework & TheFramework )
  : Theron::Actor( TheFramework, "XMPPProtocolEngine" ),
    ProtocolID("RandomName","127.0.0.1","XMPPProtocolEngine")
  {
    RegisterHandler( this, &ProtocolTester::AvailabilityNotification );
    RegisterHandler( this, &ProtocolTester::ReceiveMessages );
    
    ActorRegistry::Register( this );
    Active = true;
std::cout << "Registering the new client" << std::endl;
    Send( Theron::XMPP::Link::NewClient( ProtocolID, "secret" ),
	  Theron::Address( "XMPPLink") );
  };
  
  // There is a function to send a messsage text to remote actor. Currently
  // there is no mapping of internal and external actor IDs, so the Jabber 
  // ID of the receiver must be given explicitly.
  
  void ExternalMessage( const std::string & Text, 
			Theron::XMPP::JabberID Recipient )
  {
    Theron::XMPP::OutsideMessage Message;
    
    Message.SetSender( ProtocolID );
    Message.SetRecipient( Recipient );
    Message.SetPayload( Text );
    
    Send( Message, Theron::Address( "XMPPLink") );
  }
  
  // Then there is a function to basically shut down this actor - it will 
  // ask for the link client to be closed, and then de-register from the 
  // actor registry.
  
  void ShutDown( void )
  {
    Send( Theron::XMPP::Link::DeleteClient( ProtocolID ), 
	  Theron::Address( "XMPPLink") );
    
    ActorRegistry::Deregister( this );
    Active = false;
  }
  
  // The destructor shuts down if this protocol tester is still active, 
  // otherwise nothing to do
  
  virtual ~ProtocolTester( void )
  {
    if ( Active ) 
      ShutDown();
  }
  
};

// ----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(int argc, char **argv) 
{
  // Setting up the generic excution framework 
  
  // Theron::EndPoint   ThisComputer("Localhost","127.0.0.1");
  // Theron::Framework  ExecutionFramework( ThisComputer );
  
  Theron::NetworkEndPoint ThisComputer( "ActorManager","127.0.1");
  ConsolePrintServer 	  TheConsole( ThisComputer.GetFramework() );
  ActorRegistry      	  TheRegistry( ThisComputer.GetFramework() );
  Terminator	     	  ReadyToClose( TheRegistry );

  // Testing the XMPP framework -the link server must be started first,
  // then the protocol engine and finally the serialiser (not yet present)

  std::cout << "Ready to start the XMPP link" << std::endl ;
  
  Theron::XMPP::Link TheLinkServer( &ThisComputer, 
		     Theron::XMPP::JabberID("MUCServer","ResolverDomain") );
  
  std::cout << "Starting the protocol tester" << std::endl;
  
  ProtocolTester Tester( ThisComputer.GetFramework() );
  
  // Then we send some dummy messages to the manual test client
  
  sleep( 60 );
  std::cout << "Sending a message" << std::endl;
  Tester.ExternalMessage("The first test message", 
			 Theron::XMPP::JabberID("actormanager","127.0.0.1","test"));
  std::cout << "Waiting..." << std::endl;
  // and wait 10 minutes for incoming messages 
  
  sleep( 10 * 60 );
  
  // Then we ask the protocol engine to shut down
  std::cout << "Closing..." << std::endl;
  Tester.ShutDown();
  
  // In order to ensure that all theron actors have signed out from the 
  // registry and no further processing is expected, we have to wait for the 
  // terminator in the main thread.
  
  ReadyToClose.Wait();
  return 0;
}
