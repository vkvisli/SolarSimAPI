/*=============================================================================
  CoSSMic Trial
  
  This is the main file for the CoSSMic trial connecting the distributed 
  scheduler. It starts the network interface and then the Actor Manager that 
  waits for messages and the shut down command that terminates the execution.
  The main purpose is to create producers that are able to provide energy and 
  that will most likely run indefinitely, unless the producer is the battery of
  and electrical vehicle. Consumers come and go representing the demand side 
  loads scheduled by the users. The main objective of a consumer is to select 
  a producer as its provider of energy, and for the producers to schedule the 
  loads that wants energy from it according to its predicted production.
  
  Since the system is completely peer-to-peer it is necessary to provide the 
  Jabber ID of another remote endpoint which will be the initial peer for this
  end point and can provide the Jabber IDs of all the agents known to that peer. 
  This peer endpoint parameter should only be omitted in the case this is the 
  very first endpoint of the system. In this case the endpoint will start, but 
  it will not be connected with anyone else.
  
  One producer is special: the electricity grid. The default is to view the 
  grid as a producer of indefinite capacity and thereby accepting all loads. 
  In this case the grid can most efficiently exist as a unique actor on each 
  network endpoint to avoid that requests for energy from the grid being 
  transferred over the Internet. The IDs of devices in CoSSMic generally 
  assumes the form [<houshold_ID>]:[<device_ID] where both the household ID and
  the device ID are integral numbers. If a local grid actor is to be created, 
  its ID should be provided. If no ID is given, a unique actor name is assigned
  at the network endpoint. If the ID is given the name of the grid actor will 
  be "producer[h]:[d]" where h and d are the integral numbers of the ID [h]:[d].
  
  Alternatively, the whole neighbourhood can have only one grid agent. This is 
  useful to evaluate scenarios where the grid is assumed to have capacity 
  constraints, or if it is desirable to log which load is scheduled against 
  the grid. In this case, one and only one of the network endpoints should be 
  asked to start the global grid agent. The name of the global grid producer is 
  hard coded and defined in the Grid header file. This is done for all network 
  endpoints with no grid actor to attach to the single grid agent running in 
  the system. Allowing a command line switch for the global grid name would be 
  to require the very same name to be passed to all network endpoints, 
  increasing the likelihood of a configuration error. 
  
  The default behaviour is to create a local grid with a unique name assigned 
  by the node. The global grid command line switch will override any previously
  given local grid switches, and the application will terminate with an error 
  message if the local grid switch is used after the global grid switch.
  
  The application supports the following command line options and switches:
  
  --name <string>    	     // Default "actormanager" for the Manager
  --domain <string> 	     // Default "127.0.0.1" for localhost
											     // Jabber ID will be <name>@<domain>
  --PeerEndpoint <string>  // Default empty
  --location <string>	     // Default "konstanz" (i.e. the MUC discovery room)
												   // The agent discovery MUC is <location>@<MUCserver>
  --localgrid [<ID>]       // Start the grid as a local actor, i.e. one Grid 
												   // actor per network endpoint
  --globalgrid		         // Start the global grid actor on this node
  --password <string>      // Default "secret" used to log onto the XMPP servers
  --simulation <URL>       // Set URL for simulator's time counter
  --help		               // Prints this information and exits
   
  Author: Geir Horn, University of Oslo, 2016-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <string>		      							// To handle text easily 
#include <map>			      							// To map command line switches
#include <stdexcept>		      					// To indicate errors in a standard way
#include <iostream>		      						// For printing help texts
#include <iomanip>		      						// For formatting the help text

#include <boost/algorithm/string.hpp> 	// To convert to upper case

#include "Actor.hpp"									  // The Theron++ actor framework

#ifdef CoSSMic_DEBUG
  #include "ConsolePrint.hpp"	      		// To avoid threads to mess up output
#endif

#include "IDType.hpp"		      					// The CoSSMic ID
#include "NetworkInterface.hpp"	      	// CoSSMic XMPP communication
#include "ActorManager.hpp"	      			// The CoSSMic master actor 
#include "Grid.hpp"		      						// The CoSSMic Grid provider
#include "Clock.hpp"		      					// CoSSMic Simulated or system clock
#include "ShapleyReward.hpp"	      		// The reward calculator

// -----------------------------------------------------------------------------
// Command line option parser
// -----------------------------------------------------------------------------

class CommandLineParser
{
private:
  
  // Each command corresponds to an enum so that it can be used in the switches
  
  enum class Options
  {
    Name, 		    // The actor name used for the network endpoint
    Domain,		    // The domain of the network endpoint
    PeerEndpoint,	// The endpoint to provide known actors
    LocalGrid,		// Start a local grid actor
    GlobalGrid,		// Start a global grid agent
    Simulation,		// Use simulator's clock not the system clock
    Password,   	// The password to the XMPP server(s)
    Help        	// Prints the help text
  };
  
  // Most of these values are stored as simple strings
  
  std::string EndpointHouse,
			        EndpointDomain,
							EndpointName,
			        XMPPPassword;
	      
  // The grid has two possible instantiations. Either as a local actor or 
  // as a global agent running on this node. The type is globally accessible,
  // but the variable holding the parsed value is read-only. If there is a 
  // global grid agent running on a remote node, the other nodes should not 
  // have any grid actors, and therefore "none" is the default initialisation.

public:
	      
  enum class GridType
  {
    Local,
    Global,
    None
  };
  
private:
  
  GridType GridLocation;
  
  // The ID of the local grid is stored if the local grid option was chosen

  CoSSMic::IDType LocalGridID;
	      
  // The initial remote endpoint is stored by its Jabber ID.
	      
  Theron::XMPP::JabberID InitialRemoteEndpoint;
	      
  // There is a simple function to print the help text as above
	      
  void PrintHelp( void )
  {
    std::cout << std::setw(25) << std::left;
    std::cout << "--name <string>"
				      << "// Default \"taskscheduler\" for the Manager" << std::endl;
    std::cout << std::setw(25) << std::left;
    std::cout << "--domain <string>"
				      << "// Default \"127.0.0.1\" for localhost" << std::endl;
    std::cout << std::setw(25) << std::left;
    std::cout << " "
				      << "// Manager Jabber ID will be <name>@<domain>/actormanager" 
							<< std::endl;     
    std::cout << std::setw(25) << std::left;
    std::cout << "--PeerEndpoint <JabberID>"
				      << "// Default empty" << std::endl; 
    std::cout << std::setw(25) << std::left;
    std::cout << "--localgrid [<ID>]" << "// Start the grid as a local actor"
				      << std::endl;
    std::cout << std::setw(25) << std::left;
    std::cout << "--globalgrid" << "// Start the global grid on this node"
				      << std::endl;
    std::cout << std::setw(25) << std::left;
    std::cout << "--simulator <URL>" << "// Set URL for simulator's time counter"
				      << std::endl;
    std::cout << std::setw(25) << std::left;
    std::cout << "--password <string>"
				      << "// Default \"secret\" login for the XMPP servers" 
				      << std::endl;
    std::cout << std::setw(25) << std::left;
    std::cout << "--help"
				      << "// Prints this text" << std::endl;
    std::cout << "Since the system is completely peer-to-peer it is necessary "
				      << "to provide the Jabber ID of another remote endpoint which "
				      << "will be the initial peer for this end point and can provide "
				      << "the Jabber IDs of all the agents known to that peer." 
				      << std::endl;
  }
  
public:

  // The parsing of the command line options and parameters is done in the 
  // constructor. It is assumed that the compiler understands that it should 
  // call the default constructor on each of the value strings. 
  
  CommandLineParser( int argc, char **argv )
  : GridLocation( GridType::None ), LocalGridID()
  {
    // The command line option strings are stored in a upper case keywords for 
    // unique reference
    
    const std::map< std::string, Options > CommandLineTags =
    {	{ "--NAME", 					Options::Name         		},
			{ "--DOMAIN",					Options::Domain       		},
			{ "--PEERENDPOINT", 	Options::PeerEndpoint 		},
			{ "--LOCALGRID",			Options::LocalGrid    		},
			{ "--GLOBALGRID",			Options::GlobalGrid   		},
			{ "--SIMULATOR",			Options::Simulation   		},
			{ "--PASSWORD",				Options::Password     		},
			{ "--HELP",						Options::Help	      			}
    };
    
    // There is a small helper function to ensure that the argument provided
    // is not a switch. In other words if the user forgets the argument and 
    // provides something like "--name --location Oslo" then there is no 
    // name string given and the program should terminate. However since the 
    // argument values are strings it is not possible to check if they are 
    // really valid.
    
    auto ArgumentCheck = [&]( const std::string & Option, 
			      const int ValueIndex )->std::string
    {
      if ( ValueIndex >= argc )
		  {
		    std::ostringstream ErrorMessage;
		    
		    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								     << Option << " must have a value argument" ;
				 
		    throw std::invalid_argument( ErrorMessage.str() );
		  }
      
      if ( CommandLineTags.find( argv[ ValueIndex ] ) == CommandLineTags.end() )
				return argv[ ValueIndex ];
      else
		  {
		    std::ostringstream ErrorMessage;
		    
		    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
								     << argv[ ValueIndex ] << " is not a valid value for " 
										 << Option;
				 
		    throw std::invalid_argument( ErrorMessage.str() );
		  }
    };
    
    // Then the arguments and potential values are processed one by one 
    
    for ( int i = 1; i < argc; i++ )
    {
      // Read the next option and convert it to upper case letters
      
      std::string TheOption( argv[i] );
      boost::to_upper( TheOption );
      
      // The option is checked and potential arguments stored in the 
      // corresponding string
      
      switch ( CommandLineTags.at( TheOption ) )
      {
				case Options::Name:
				  EndpointName = ArgumentCheck( TheOption, ++i );
				  break;
				case Options::Domain:
				  EndpointDomain = ArgumentCheck( TheOption, ++i );
				  break;
				case Options::LocalGrid:
				{
				  // The string representing the ID is recorded first. Note that since 
				  // this is an optional argument, it may not exist and the argument 
				  // check will throw an exception in this case. We simply ignore the 
				  // exception and reset the argument index counter.
				  
				  std::string IDString;
				  
				  try
				  {
				    IDString = ArgumentCheck( TheOption, ++i);
				  }
				  catch ( std::invalid_argument & Error )
				  {
				    i--;
				  }
				  
				  // Then the location of the Grid actor is verified. If the user has 
				  // already asked for the node to host the global grid, in which case 
				  // there is an error.
				  
				  if ( GridLocation == GridType::Global )
				  {
				    std::cout << "This node is set to host the global grid agent and "
								      << "cannot host a local grid actor!" << std::endl;
				    std::exit( EXIT_FAILURE );
				  }
				  else
				    GridLocation = GridType::Local;
				  
				  // Finally, the ID string is converted to a CoSSMic ID. 
				  
				  LocalGridID = CoSSMic::IDType( IDString );
				}
				case Options::GlobalGrid:
				  // The global grid is simpler since it just sets the grid location, 
				  // potentially overriding a previous local grid command line switch.
				  
				  GridLocation = GridType::Global;
				  
				  break;
				case Options::PeerEndpoint:
				  {
				    Theron::XMPP::JabberID PeerAddress(ArgumentCheck( TheOption, ++i ));
				    
				    if ( PeerAddress.isValid() )
				      InitialRemoteEndpoint = PeerAddress;
				    else
				      InitialRemoteEndpoint = Theron::XMPP::JabberID();
				  }
				  break;
				case Options::Simulation:
				  CoSSMic::Now.SetURL( ArgumentCheck( TheOption, ++i ) );
				  break;
				case Options::Password:
				  XMPPPassword = ArgumentCheck( TheOption, ++i );
				  break;
				default:
				  PrintHelp();
				  exit(0);
      }
    }
  }
  
  // Having parsed the command line there are interface functions to return 
  // the given argument value, or the default value.
  
  inline std::string GetName( void )
  {
    if ( EndpointName.empty() )
      return std::string("taskscheduler");
    else
      return EndpointName;
  }
  
  inline std::string GetDomain( void ) 
  {
    if ( EndpointDomain.empty() )
      return std::string("127.0.0.1");
    else
      return EndpointDomain;
  }
  
  inline Theron::XMPP::JabberID GetPeerEndpoint( void )
  {
    return InitialRemoteEndpoint;
  }
    
  inline std::string GetPassword( void ) 
  {
    if ( XMPPPassword.empty() )
      return std::string( "secret" );
    else
      return XMPPPassword;
  }
  
  inline GridType GetGridType( void )
  {
    return GridLocation;
  }
  
  inline CoSSMic::IDType GetGridID( void )
  {
    return LocalGridID;
  }
};

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int main(int argc, char **argv) 
{ 
  // The options are taken from the command line
  
  CommandLineParser Options( argc, argv );
  
  // Then using these definitions to start the network interface, including 
  // the Theron execution framework.
  
  Theron::NetworkEndPoint< CoSSMic::NetworkInterface > 
  Household( Options.GetName(), Options.GetDomain(),  Options.GetPassword(), 
			       Options.GetPeerEndpoint() );
  
  // Before starting any of the CoSSMic actors, the console print actor is 
  // started if debug messages is produced.
  
  #ifdef CoSSMic_DEBUG
    Theron::ConsolePrintServer PrintServer( &std::cout, "ConsolePrintServer" );
  #endif
  
  // The reward calculator is started so that we can pass its address to the 
  // actor manager.
    
  CoSSMic::ShapleyValueReward TheRewardCalculator( Options.GetDomain() );
    
  // Then add the actor manager - note that the name actor manager is 
  // hard coded for this component, and that actual values are given for 
  // the solution tolerance and the number of iteration allowed to find a 
  // solution. Given that the unit of time is in seconds, the tolerance 
	// should not be less than one second. The schedule for a moderately large 
	// system (measured in loads per producer) will not need too many iterations 
	// to converge. Furthermore, there might not be any difference by shifting 
	// the start time of a load by a few seconds. In other words, the solver 
	// could keep on iterating until the limit without much improvement of the 
	// obtained solution. The number of iterations should be increased if the 
	// solver fails to obtain a solution, or reports other errors.
  
  constexpr double       SolutionTolerance = 1;
  constexpr unsigned int MaxIterations     = 150;
  
  CoSSMic::ActorManager TheActorManager( TheRewardCalculator.GetAddress(), 
																				 SolutionTolerance, MaxIterations );
  
  // Then the Grid actor must be started in the case this node should have a
  // local actor, or if the node should host the global grid agent. The 
  // reference to the grid is kept in a shared pointer to correctly de-allocate 
  // the actor depending on whether it has been initialised or not. Note that 
  // in the case of a global grid, it will execute in the local Theron actor 
  // framework only, and with the ID given on the command line (if any). 
  // Otherwise it executes under the network endpoint as an agent with the 
  // global ID given in the Grid header file.
  
  std::shared_ptr< CoSSMic::Grid > GridActor;
  
  switch ( Options.GetGridType() )
  {
    case CommandLineParser::GridType::Local :
      GridActor = std::make_shared< CoSSMic::Grid >( Options.GetGridID()  );
      break;
    case CommandLineParser::GridType::Global :
      GridActor = std::make_shared< CoSSMic::Grid >();
      break;
    case CommandLineParser::GridType::None : 
      // Nothing to do - a global grid agent on another node will be used
      break;
  }

  // Then it is just to wait for messages until we receive the final shut down
  // message.
  
	Household.TerminationWatch( *GridActor, TheActorManager, TheRewardCalculator, 
															PrintServer )->Wait();
  
  return EXIT_SUCCESS;
}
