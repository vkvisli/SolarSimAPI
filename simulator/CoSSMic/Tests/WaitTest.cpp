// Minimal test to debug the wait protocol

#include "Actor.hpp"

class Responder : public Theron::Actor
{
private:
	
	unsigned int Count;
	
public:
	
	Theron::Address TerminationReceiver;
	
	void PingResponse( const std::string & TheMessage, 
										 const Theron::Address From )
	{
		std::cout << GetAddress().AsString() << " Got " << TheMessage << " from " 
		          << From.AsString() << std::endl;
		
		if ( ++Count < 3 )
			Send( std::string( "PING from Responder for message ") + TheMessage, From );
		else
			Send( true, TerminationReceiver );
	}
		
	void SendMessage( const std::string & TheMessage, const Theron::Address TheReceiver )
	{
		Send( TheMessage, TheReceiver );
	}
	
	Responder( void ) 
	: Theron::Actor(), TerminationReceiver()
	{
		Count = 0;
		RegisterHandler(this, &Responder::PingResponse );
	}
	
};

class Terminator : public Theron::Receiver
{
private:
	
	std::atomic< unsigned int > ClosingMessages;
	
	void Closing( const bool & TheMessage, const Theron::Address From )
	{
		std::cout << GetAddress().AsString() << " got termination signal " 
							<< TheMessage << " from " << From.AsString() << std::endl;
		ClosingMessages++;
	}

public:
	
	Terminator( void )
	: ClosingMessages()
	{
		RegisterHandler( this, &Terminator::Closing );
	}
	
};

// The main creates three responder instances, and two will send messages, while
// the third will wait for a message before terminating.

int main(int argc, char **argv) 
{
	Responder Actor1, Actor2;
	Terminator Actor3;
  
  Actor1.TerminationReceiver = Actor3.GetAddress();
  Actor2.TerminationReceiver = Actor3.GetAddress();

	std::cout << "Starting processing..." << std::endl;
	
	Actor1.SendMessage( "Ping!", Actor2.GetAddress() );
	Actor2.SendMessage( "Pong!", Actor1.GetAddress() );
	
	std::cout << "Before the wait..." << std::endl;
	
	// At this point one would expect to wait for two messages, but it could be 
	// that a message has already arrived before the wait is executed as the two
	// Actor threads may be faster than this thread.
	
	unsigned int ShutdownMessages = 2;
	
	while ( ShutdownMessages > 0 )
		ShutdownMessages -= Actor3.Wait();
	
	std::cout << "...Normal termination" << std::endl;
	return 0;
}
