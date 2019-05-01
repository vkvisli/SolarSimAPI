/*=============================================================================
  Reward Calculator
  
  The reward calculator implements the fundamental functionality that all 
  reward calculators must support, and the registry for local consumers and 
  producers.
  
  
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <string>		  // Standard strings
#include <sstream>		  // Error messages and serialisation
#include <fstream>		  // File output
#include <stdexcept>		  // standard exceptions

#include "Clock.hpp"		  // Transparent simulation and system clock
#include "PresentationLayer.hpp"  // For serialised messages
#include "ActorManager.hpp"	  // The orchestrator
#include "Grid.hpp"		  // The standard Grid producer

#include "RewardCalculator.hpp"

namespace CoSSMic
{
/*****************************************************************************
  Messages and message handlers
******************************************************************************/
//
// -----------------------------------------------------------------------------
// New producer created on the local node
// -----------------------------------------------------------------------------
//
// When producers are created on the local node they are added to the set of 
// local producers. It should be noted that this producer is not added to the 
// energy exchange matrix before it provides energy to one of the local 
// consumers. It is, at least theoretically, possible that a producer will only
// provide energy to remote consumers and it should therefore not have a column
// in the (inbound) energy exchange matrix.

void RewardCalculator::RegisterProducer(
  const RewardCalculator::NewProducer & TheProducer, 
  const Theron::Address TheActorManager )
{
  if ( LocalProducers.find( TheProducer ) == LocalProducers.end() )
    LocalProducers.emplace( TheProducer );
}

// -----------------------------------------------------------------------------
// Distributing the new PV Energy
// -----------------------------------------------------------------------------
//
// When a load terminates and has taken energy from a PV related source, either 
// a solar panel or a battery previously filled with PV energy, the recorded 
// amount of energy is disseminated in the neighbourhood as a new PV energy 
// message from the the reward calculator on the network endpoint hosting the 
// consumer of the load. Hence the the message must support serialisation.

Theron::SerialMessage::Payload 
RewardCalculator::NewPVEnergy::Serialize( void ) const
{
  std::ostringstream Message;
  
  Message << "NEW_PV_ENERGY " << EnergyValue << " " << Producer;
  
  return Message.str();
}

// The reverse handler is simply checking if the command string is correct
// and then initialise the value field.

bool RewardCalculator::NewPVEnergy::Deserialize( 
  const Theron::SerialMessage::Payload & Payload )
{
  std::istringstream Message( Payload );
  std::string Command;
  
  EnergyValue = 0;
  Message >> Command;
  
  if ( Command == "NEW_PV_ENERGY" )
  {
    Message >> EnergyValue;
    Message >> Producer;
    return true;
  }
  else
    return false;
}

// The constructor of the message class simply invokes the above 
// de-serialisation function, and if it fails it will throw an invalid argument 
// with a descriptive message.

RewardCalculator::NewPVEnergy::NewPVEnergy(
  const Theron::SerialMessage::Payload & Payload )
{
  if ( ! Deserialize( Payload ) )
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						     << "NewPVEnergy != " << Payload ;
		 
    throw std::invalid_argument( ErrorMessage.str() );
  }
}

// The default message handler simply maintains the counters for the PV 
// energy, the total energy produced in the whole system and the energy
// contributed by producers on this node (network endpoint)

void RewardCalculator::NewPVEnergyValue( 
  const RewardCalculator::NewPVEnergy & EnergyMessage, 
  const Theron::Address Sender )
{
  // The energy is first used to increase the total amount of PV energy consumed
  // in the neighbourhood.
  
  NeighbourhoodPVEnergy += EnergyMessage.Energy();
  
  // If the producer is on this node, the energy should be recorded as energy 
  // shared with the neighbourhood from a producer on this node. 
  
  if ( LocalProducers.find(EnergyMessage.ProducerID()) != LocalProducers.end() )
    TotalPVShared += EnergyMessage.Energy();  
}

// A derived class must call the function to save the node reward file once 
// the node reward has been computed.

void RewardCalculator::SaveRewardFile( double NodeRewardValue )
{
  // The agreed file format has three columns, one for the time stamp now,
  // one for the reward and one for the total PV shared from this household. 
  // The columns are space separated.
  
  std::ofstream RewardFile( "Reward.csv", 
												    std::ofstream::out | std::ofstream::app );
  
  RewardFile << Now() << " " 
				     << NodeRewardValue 
				     << " " << TotalPVShared << std::endl;
  
  RewardFile.close();

}

// -----------------------------------------------------------------------------
// Consumer management
// -----------------------------------------------------------------------------
//
// Every time a consumer is created to serve a load, the actor manager will
// send a request to its local reward calculator. 

void RewardCalculator::NewConsumer( 
  const RewardCalculator::AddConsumer & ConsumerRequest, 
  const Theron::Address Sender )
{
  ActiveConsumers.insert( ConsumerRequest.GetAddress() );
}

// -----------------------------------------------------------------------------
// Load finished - compute the reward
// -----------------------------------------------------------------------------
//
// By default, it is not possible to compute the reward, so the message handler
// does the necessary housekeeping.

void RewardCalculator::NewEnergy( 
  const RewardCalculator::AddEnergy & EnergyMessage, 
  const Theron::Address Sender )
{
  // Dispatch to the other reward calculators only if this is PV energy
  
  if ( EnergyMessage.Producer() != Grid::ID() )
    for ( const Theron::Address & Calculator : RewardCalculators )
      Send( NewPVEnergy( EnergyMessage.Energy(), EnergyMessage.Producer() ), 
	    Calculator );

  // The consumer will be removed by the Actor Manager once it has received 
  // its reward, and it should therefore no longer be considered an active 
  // consumer.

  ActiveConsumers.erase( EnergyMessage.Consumer() );
  
  // Finally, the message can be acknowledged so that the consumer may be 
  // deleted by the actor manager.
  
  Send( ActorManager::AcknowledgeEnergy( EnergyMessage.Consumer() ), Sender );
}

// -----------------------------------------------------------------------------
// Detecting peer reward calculators
// -----------------------------------------------------------------------------
//
// Adding calculators to the list of remote calculators means checking if the 
// symbolic address name contains the name root sub-string, and if it does it 
// will be added to the list. Note that the message is actually a set of known
// peer agents if the session layer has discovered two or more peers. 

void RewardCalculator::AddCalculator( 
  const Theron::SessionLayerMessages::NewPeerAdded & NewAgent, 
  const Theron::Address SessionLayerServer )
{

  for ( const Theron::Address & TheAgent : NewAgent )
    if ( (std::string( TheAgent.AsString() ).find( NameRoot ) != 
          std::string::npos) && ( TheAgent != GetAddress() ) )
      RewardCalculators.insert( TheAgent );
}

// If a remote node closes or a remote reward calculator is shut down, a 
// peer removed message is sent from the session layer, which simply removes
// the remote reward calculator from the list of active calculators.

void RewardCalculator::RemoveCalculator(
  const Theron::SessionLayerMessages::PeerRemoved & LeavingAgent, 
  const Theron::Address SessionLayerServer )
{
  RewardCalculators.erase( LeavingAgent.GetAddress() );
}

// Serialising the shut down message is just printing the command string

Theron::SerialMessage::Payload 
RewardCalculator::Shutdown::Serialize( void ) const
{
  return std::string( "REWARD_CALCULATOR_SHUTDOWN" );
}

// Taking the inverse is just to verify that the message contains this string

bool RewardCalculator::Shutdown::Deserialize(
  const Theron::SerialMessage::Payload & Payload)
{
  std::string Command( Payload );
  
  if ( Command == "REWARD_CALCULATOR_SHUTDOWN")
    return true;
  else
    return false;
}

// The payload constructor attempt to de-serialise the payload and if it works
// then the message is of the right type; otherwise the standard exception is 
// thrown

RewardCalculator::Shutdown::Shutdown(
  const Theron::SerialMessage::Payload & Payload )
: Shutdown()
{
  if ( ! Deserialize( Payload ) )
  {
    std::ostringstream ErrorMessage;
    
    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
						     << "Reward calculator shut down != " << Payload ;
		 
    throw std::invalid_argument( ErrorMessage.str() );
  }
}

// The message handler is identical to the handler removing calculators based 
// on their session layer un-subscription. 

void RewardCalculator::ForgetCalculator(
  const RewardCalculator::Shutdown & ShutdownMessage, 
  const Theron::Address ClosingCalculator )
{
  RewardCalculators.erase( ClosingCalculator );
}

/*****************************************************************************
  Constructor and destructor
******************************************************************************/

RewardCalculator::RewardCalculator( const std::string & Location )
: Actor( std::string( NameRoot ) + Location ), 
  StandardFallbackHandler( GetAddress().AsString() ),
  DeserializingActor( GetAddress().AsString() ),
  ActiveConsumers(), RewardCalculators(),
  SessionServer( Theron::Network::GetAddress(Theron::Network::Layer::Session) ),
  LocalProducers()
{
  NeighbourhoodPVEnergy = 0.0;
  TotalPVShared         = 0.0;
  
  // Register all the message handlers
  
  RegisterHandler( this, &RewardCalculator::RegisterProducer );
  RegisterHandler( this, &RewardCalculator::NewPVEnergyValue );
  RegisterHandler( this, &RewardCalculator::NewConsumer      );
  RegisterHandler( this, &RewardCalculator::NewEnergy        );
  RegisterHandler( this, &RewardCalculator::AddCalculator    );
  RegisterHandler( this, &RewardCalculator::RemoveCalculator );
  RegisterHandler( this, &RewardCalculator::ForgetCalculator );
  
  // Finally, the subscription is made to the session layer to be informed 
  // about new peer agents known to the system.
  
  Send( Theron::SessionLayerMessages::NewPeerSubscription(), SessionServer );
}

// The destructor will first stop the subscription for peer reward calculators,
// then tell the other peer reward calculators that this calculator is stopping
// before it de-registers with the session server.

RewardCalculator::~RewardCalculator()
{
  Send( Theron::SessionLayerMessages::NewPeerUnsubscription(), SessionServer );
  
  for ( const Theron::Address & RemoteCalculator : RewardCalculators )
    Send( Shutdown(), RemoteCalculator );
}


} 	// end name space CoSSMic
