/*=============================================================================
  Schedule test
  
  This test the distributed scheduler without any external communication. 
  In other words, all the actors will be running on the same network end point.
  Two producers are created and attached to two dummy prediction files, and 
  then there are three consumers created that will try to be scheduled on the
  producers.
  
  There is an implementation of the Task Manager agent that normally can only 
  be reached through external communication, and is the entity responsible for 
  creating the producers and consumers by commands passed to the Actor Manager.
  This is now implemented as an emulation, i.e. the time is real time to wait 
  before a load ends its execution.
  
  Most of the XMPP stack is included, but the Network Layer has been replaced
  by a console printer that simply reports messages that normally would have 
  been sent. This again should not happen because all the actors have addresses
  that are known at the local actor network, and the only functionality that 
  is used in the test is the Session Layer's directory of agents.
   
  Author: Geir Horn, University of Oslo, 2015, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <map>
#include <iostream>
#include <algorithm>

#include <ctime>			  										// Seconds since 1.1.1970 in time_t
#include <chrono>			  										// For setting realistic EST and LST
#include <unistd.h>													// For a simple sleep

#include <boost/optional/optional_io.hpp> 	// For printing optionals
#include "Actor.hpp"		     					      // Actor framework

#ifdef CoSSMic_DEBUG
  #include "ConsolePrint.hpp"	     					// To avoid threads to do output
#endif

#include "WallClockEvent.hpp"		   					// Real time events
#include "Clock.hpp"               					// The CoSSMic clock

#include "NetworkLayer.hpp"
#include "XMPP.hpp"
#include "ActorManager.hpp"
#include "ConsumerAgent.hpp"
#include "PVProducer.hpp"

#include "ShapleyReward.hpp"

/******************************************************************************
  Communication stack extensions
*******************************************************************************/

// The link must always implement the method to send messages on the physical 
// link. Here it will just report what it has received (if anything is received).

class MinimalLink : public Theron::NetworkLayer< Theron::XMPP::OutsideMessage >
{
public:
  
  virtual void SendMessage( const Theron::XMPP::OutsideMessage & TheMessage )
  {
    std::cout << TheMessage.GetSender() << " ==> " << TheMessage.GetRecipient()
              << " Command: " << TheMessage.GetSubject()
				      << " Message: " << TheMessage.GetPayload() << std::endl;
  }

  // 
  // MESSAGE HANDLERS
  // 
  // Unhanded messages are just reported and then the application is aborted
  
  void UnhandledMessage( const Theron::Address Sender )
  {
    std::cout << "Minimal Link: Unhanded message received from "
				      << Sender.AsString() << " Aborting!" << std::endl;
	      
    abort();
  }

  // We report when we get a request for a new Agent, i.e. an externally 
  // visible actor.
  
  void NewAgent( const Theron::XMPP::Link::NewClient & TheActor, 
		 const Theron::Address Sender )
  {
    std::cout << Sender.AsString() << " has requested a new client to be "
              << "created with Jabber ID " << TheActor.GetJID() 
              << " with priority " << TheActor.GetPriority() << " and password "
              << TheActor.GetPassword() << std::endl;
  }
  
  // There is also a message to remove agents when the actors cease to exist
  
  void DeleteAgent( const Theron::XMPP::Link::DeleteClient & TheActor,
		    const Theron::Address Sender )
  {
    std::cout << Sender.AsString() << " has requested to remove the client "
				      << "with Jabber ID " << TheActor.GetJID() << std::endl;
  }
  
  MinimalLink( Theron::NetworkEndPoint * Host, 
		std::string ServerName = "NetworkLayer")
  : NetworkLayer< Theron::XMPP::OutsideMessage >( Host, ServerName )
  {
    SetDefaultHandler(this, &MinimalLink::UnhandledMessage );
    RegisterHandler  (this, &MinimalLink::NewAgent         );
    RegisterHandler  (this, &MinimalLink::DeleteAgent      );
  }
 
  
};

// We need to change the Network End Point to use this instead of the full 
// blown XMPP link. It is sufficient to overload only the create method to 
// make it work correctly. However it should be noted that this should be done
// in the initialiser class. The binding of the objects are as provided by 
// the XMPP manager class.

class MinimalEndPoint : public Theron::XMPP::Manager
{
public:
  
  class Initialiser : public Theron::XMPP::Manager::Initialiser
  {
  public:
    
    virtual void CreateServerActors( void )
    {
      // Starting first the minimal link as the network layer for this endpoint 
      
      GetNodePointer()->Create< MinimalLink >( 
				      Theron::NetworkEndPoint::Layer::Network );

      // Then the Session layer and presentation layer can be constructed as
      // they are in the XMPP endpoint

      GetNodePointer()->Create< Theron::XMPP::XMPPProtocolEngine >( 
				      Theron::NetworkEndPoint::Layer::Session,
				      ServerPassword	);

      // The presentation layer can stay as the basic implementation since there
      // is nothing XMPP specific translations to be done to the messages
      
      GetNodePointer()->Create< Theron::PresentationLayer >( 
				 Theron::NetworkEndPoint::Layer::Presentation );

    }
    
    void BindServerActors( void )
    {
      // Binding Network Layer and the Session Layer by telling the Network 
      // Layer actor the Theron address of the Session Layer actor.

      GetNodePointer()->Pointer<MinimalLink>( NetworkEndPoint::Layer::Network )
	      ->SetSessionLayerAddress( 
        GetNodePointer()->GetAddress(NetworkEndPoint::Layer::Session) );
      
      // The Session Layer needs the address of both the Network Layer and the 
      // Presentation Layer, so we better obtain the pointer only once and set the 
      // two needed addresses using this pointer.
	
      auto ProtocolEnginePointer = 
           GetNodePointer()->Pointer< Theron::XMPP::XMPPProtocolEngine >( 
           NetworkEndPoint::Layer::Session );
	  
      ProtocolEnginePointer->SetNetworkLayerAddress( 
           GetNodePointer()->GetAddress( NetworkEndPoint::Layer::Network ));
      ProtocolEnginePointer->SetPresentationLayerAddress( 
           GetNodePointer()->GetAddress( NetworkEndPoint::Layer::Presentation ));
      
      // Finally the Presentation Layer needs the address of the Session Layer.
      
      GetNodePointer()->Pointer< Theron::PresentationLayer >( 
          NetworkEndPoint::Layer::Presentation )->SetSessionLayerAddress( 
          GetNodePointer()->GetAddress( NetworkEndPoint::Layer::Session ));
    }
    
    // The constructor basically forwards the parameters to the base class 
    // initialiser.
    
    Initialiser( const std::string & Password,
		 const Theron::XMPP::JabberID 
		       AnotherPeer = Theron::XMPP::JabberID() ) 
    : Theron::XMPP::Manager::Initialiser( Password, AnotherPeer )
    { }
  };
  
  friend class MinimalEndPoint::Initialiser;

  // The constructor is needed in order to pass the parameters on to the 
  // constructor of the XMPP manager, and the correct initialiser. Note that 
  // the XMPP::Manager::Initialiser requires the following two parameters to
  // be given as part of the variadic arguments:
  //  Theron::XMPP::JabberID DiscoveryMUC
  //  std::string Password  - for the XMPP server.
  
  MinimalEndPoint( const std::string & Name, const std::string & Location,
		   Theron::NetworkEndPoint::InitialiserType TheInitialiser )
  : Theron::XMPP::Manager( Name, Location, TheInitialiser )
  { }
};

/******************************************************************************
  Task manager
*******************************************************************************/

// First the producers are created, and then the consumers. When the first 
// consumer reports back the start time, a new prediction will be pushed for
// the producers, basically interchanging the two original predictions. When 
// all consumers have received start times, the task manager will wait some 
// time before it deletes the loads, and terminates.
// 
// An interesting observation is that normally it should be the task manger 
// getting the information from the physical device that the consumption has 
// ended, and then it will terminate the load object. In this simulated case
// this must be done by setting manually a duration for the load, and then 
// terminate the load when this time is reached. 

class TaskManager : public Theron::Actor
{
private:
  
  const Theron::Address ActorManagerAddress;
  
  // We will count how many times each of the different loads are assigned 
  // start times, and we count them in a map. We cannot a priori know how 
  // many elements there will be in a map, and the [ ] operator will create 
  // a new element if the key given does not already exist. How about the 
  // counter when a new element is created? The default constructor will be 
  // called on the value of the element, but an int has no default constructor
  // and prior to C++11 this would be left uninitialised. However, based on 
  // the description of value initialisation in the reference at
  // http://en.cppreference.com/w/cpp/language/value_initialization
  // the value should indeed be initialised to zero.
  
  std::map< CoSSMic::IDType, unsigned long > AssignmentCounter;

  // ---------------------------------------------------------------------------
  // Job finish time events
  // ---------------------------------------------------------------------------
  
  // Start times can be assigned, cancelled, and re-assigned and we need to 
  // keep a sorted list of assigned times. The Wall Clock Event handler will 
  // be used and asked to fire an event for the first job finish time we have.
  // However, as the start times for the jobs change this event time may become
  // obsolete. When the event message arrives, we need to see if it corresponds
  // to the current first event time, and if it does not it can safely be 
  // ignored. Similarly, if a job is given a new start time moving it to the 
  // head of the job finish queue, a new event will be called for at this 
  // earlier time, making the previous job completion time obsolete to be 
  // ignored when the event arrives at that later time. In other words, we 
  // will keep an updated time sorted directory of assigned job finish times
  // and create a new event whenever the finish time of the job at the head of 
  // the queue changes; and ignore events whose times does not correspond to 
  // events in the queue.
  
  std::multimap< Theron::WallClockEvent::EventTime, 
		 CoSSMic::IDType > JobFinishTime;

  // Since we are only getting the start times, we need to know also the 
  // duration of the jobs for this simulated setting. These times should be
  // taken from the load profiles. The job finish time is the assigned start 
  // time + this duration.
  
  std::map< CoSSMic::IDType, CoSSMic::Time > DurationDatabase;

  // A core problem is to distinguish the situation where the last job 
  // terminates from the situation where the no jobs have assigned start 
  // times. In both cases the directory of job termination times will be 
  // empty. In order to distinguish the two cases, a simple counter is used
  // to note the number of unassigned jobs. If this is zero and there are 
  // no more jobs in the the job finish time directory, all jobs have run
  // to completion. 
  
  unsigned int UnassignedCount;
    
  // The jobs have done times, and these will be based on the fixed duration 
  // times, in other words, the sum of Now + duration. Simulated time is a 
  // little difficult since we cannot block the thread hosting this actor 
  // until the first load finishes since the load can be re-scheduled at 
  // any time, and if the thread is waiting it will not respond to messages
  // about re-scheduling and consequently continue to wait for the wrong 
  // job termination time.
  // 
  // To circumvent this problem we will use the wall clock event queue that 
  // accepts future event times, and will send a message back to the task 
  // manager actor when the event time occurs. It is important to note that 
  // the event queue expects the event to be acknowledged once the processing 
  // is done. The event queue can be shared by other actors, so it is 
  // sufficient here to store its address.
  
  Theron::Address TimeEventHandler;
  
  // Then we have to deal with the finish time computation. There are several
  // situations to consider: 
  // A) The load is not the one with the earliest finishing time, then if the 
  //	newly assigned start time will make it the load to finish first we 
  //    will need to register a new job completion time.
  // B) The load is not the one with the earliest finishing time, and it will 
  //    not be the one with the earliest finishing time, so we just insert it
  //    at the appropriate place in the list of finishing times.
  // C) The load is the one with the earliest finish time, and it will remain 
  //    at the head of the queue even after the update of the finish time. Then
  //    we have to make sure that this new finish time will be registered as 
  //    the next job completion time.
  // D) The load was at the head of the queue, and the newly assigned start 
  //    time will move it backwards in the queue. Then the new head of the queue
  //    should be registered with the new start time. 
  // All of these situations can be generically handled as follows:
  // 1) Remove the current finish time for this load from the queue
  // 2) Insert the newly assigned finish time for this load in the queue
  // 3) If the finish time for the first load in the queue has changed, then 
  //    register it as the next job completion time. 
  // The following function handles this, and if the next stop time is given as 
  // zero, it indicates that the job has no new stop time and should just be 
  // deleted from the queue of job finish times.
 
  void ChangeFinishTime( const CoSSMic::IDType & ID, 
                         CoSSMic::Time NewStopTime = 0 )
  {
    // First we record the current earliest stop time, but only if we have any
    // stop times.

    Theron::WallClockEvent::EventTime CurrentFirstStopTime = 0;
						
    if ( !JobFinishTime.empty() )
      CurrentFirstStopTime = JobFinishTime.begin()->first;
    
    // Then find the current position in the finish time queue of the job that 
    // has now a new start time, remove this finish time, and insert the job 
    // based on its new stop time only if it has one assigned. 
    
    auto ThisJob = std::find_if( JobFinishTime.begin(), JobFinishTime.end(),
    [&](const std::pair< Theron::WallClockEvent::EventTime, CoSSMic::IDType> 
		  & FinishTimeRecord )->bool{ 
	    return FinishTimeRecord.second == ID; 
    });
    
    if ( ThisJob != JobFinishTime.end() )
      JobFinishTime.erase( ThisJob );
    
    if ( NewStopTime > 0 )
      JobFinishTime.emplace( 
        static_cast< Theron::WallClockEvent::EventTime >( NewStopTime ), ID );
    
    // Finally we may need to set the wall clock event if there are events to
    // wait for and the new first stop time is different from the first stop
    // time we had already

    if ( (!JobFinishTime.empty()) && 
         ( CurrentFirstStopTime != JobFinishTime.begin()->first ) )
      Send( JobFinishTime.begin()->first, TimeEventHandler );
  }

  // ---------------------------------------------------------------------------
  // Ending the simulation
  // ---------------------------------------------------------------------------
  // When the event for the end time point of the last executing job is 
  // received the simulation can terminate. A special receiver is provided 
  // for this purpose that will get a message from the event handler once 
  // all loads have been properly served and executed.
  
public:
  
  class AllWorkDone : public Theron::Receiver
  {
    // There is a handler for the time events, that will only print out the 
    // current time point, but it allows the receiver to wait for this message.
    
    void TimeEventPrinter( const Theron::WallClockEvent::EventTime & Now,
			   const Theron::Address Sender )
    {
      std::time_t CurrentTime( Now );
      std::cout << "Time is now: " << std::ctime( &CurrentTime ); 
    }
    
  public:
    
    // The constructor registers the time point printer as a message handler
    
    AllWorkDone( void )
    {
      RegisterHandler( this, &AllWorkDone::TimeEventPrinter );
    }
    
  } SimulationEnd;
  
  
  // ---------------------------------------------------------------------------
  // Message handlers
  // ---------------------------------------------------------------------------
  
  // The wall clock event queue will send the time of the event back to the 
  // actor owning the event when the event time is due. In practice this means
  // that the wall clock time is the time received. If this time corresponds 
  // to the first job finish time in an non-empty queue, we will remove all 
  // jobs from the queue having this time as their end time (note that this is 
  // a nice feature of the erase function on a multimap). If there are more 
  // events we will set the next event for the new head of the job finish time 
  // queue. However, if the queue is empty and there are no unassigned jobs 
  // waiting to have start times set, we indicate an end of the simulation. 
  // Finally, the event will be acknowledged.
  
private:
  
  void FinishTimeEvent( const Theron::WallClockEvent::EventTime & Now, 
			const Theron::Address WallClockActor     )
  {
    if ( (!JobFinishTime.empty()) && (JobFinishTime.begin()->first == Now) )
    {
      JobFinishTime.erase( Now );
      if ( !JobFinishTime.empty() )
        Send( JobFinishTime.begin()->first, WallClockActor );
      else if ( UnassignedCount == 0 )
        Send( Now, SimulationEnd.GetAddress() );
    }
    
    Send( true, WallClockActor );
  }
  
  // We must handle two types of messages from the consumer agents. One type
  // is when the consumer indicates that it has been given a start time. We
  // simply print out the message and take not of how many times this load 
  // has been assigned a start time.

  void RecordStartTime( const CoSSMic::ConsumerAgent::StartTimeMessage &
                        StartTime, const Theron::Address Consumer  )
  {
    // Printing out the start time in a human readable form
    std::time_t TheTime( StartTime.GetStartTime() );
    std::string HumanReadableStartTime( std::ctime( &TheTime ) );
    
    // remove the new line automatically inserted by ctime
    
    HumanReadableStartTime.resize( HumanReadableStartTime.size()-1 );
    
    std::cout << "Load " << StartTime.GetLoadID() << " has been assigned "
              << "start time " << HumanReadableStartTime
              << " by producer" << StartTime.GetProducerID() << std::endl;
    
    AssignmentCounter[ StartTime.GetLoadID() ]++;
    
    // This assignment reduces the number of unassigned loads by one, and 
    // we record this fact.
    
    UnassignedCount--;
    
    // Then we have to change the finish time based on this newly assigned 
    // start time.

    #ifdef BINARY_MESSAGE_TEST
      ChangeFinishTime( StartTime.GetLoadID(), StartTime.GetStartTime() + 
          DurationDatabase[ StartTime.GetLoadID() ]  );
    #endif
  } 
  
  // If an assigned start time is cancelled by a provider, the consumer agent
  // will inform the task manager about this event with a message cancelling
  // the start time. This will be captured by the following handler that just
  // prints the message.
  
  void CancelLoad( const CoSSMic::ConsumerAgent::CancelStartTime & Message,
		   const Theron::Address Consumer )
  {
    std::cout << "Start time for load " << Message.GetLoadID() 
              << " has been cancelled"  << std::endl;
	      
    // The status of this job now went from assigned to unassigned, and we 
    // record this as one more job awaiting to be scheduled.
	    
    UnassignedCount++;
    
    // The job also needs to be removed from the queue of jobs waiting to 
    // to finish execution
    #ifdef BINARY_MESSAGE_TEST
      ChangeFinishTime( Message.GetLoadID() );
    #endif
  }
  
  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
  
  // The constructor interacts with the Activity Manager as an outside 
  // task manager would do it.

public:
  
  TaskManager(MinimalEndPoint * TheHost, const Theron::Address & TheWallClock)
  : Theron::Actor( TheHost->GetFramework(), "taskmanager" ),
    ActorManagerAddress( "actormanager" ),
    AssignmentCounter(), JobFinishTime(), 
    DurationDatabase({{"[1]:[1]", 1000}, {"[1]:[2]", 1000}, {"[1]:[3]", 1000}}),
    TimeEventHandler( TheWallClock ), SimulationEnd()
  {
    // Register the message handlers
    
    RegisterHandler( this, &TaskManager::FinishTimeEvent );
    RegisterHandler( this, &TaskManager::RecordStartTime );
    RegisterHandler( this, &TaskManager::CancelLoad      );
    
    // The actor manager is told to create the producers based on their IDs
    // and the first prediction files

#ifdef BINARY_MESSAGE_TEST
    Send( CoSSMic::ActorManager::AddProducer( 
          CoSSMic::ActorManager::AddProducer::Type::PhotoVoltaic, 
          "[1]:[10]", "Tests/solarPanel1_220.csv" ),  ActorManagerAddress );
    Send( CoSSMic::ActorManager::AddProducer( 
          CoSSMic::ActorManager::AddProducer::Type::PhotoVoltaic, 
          "[1]:[20]", "Tests/solarPanel2_221.csv" ),  ActorManagerAddress );
	  
    // The consumers also requires the earliest start time and the latest start
    // time for this load. This scheduling interval is set from 
    // 5 minutes into the future until 
    
    std::time_t Now = std::chrono::system_clock::to_time_t( 
		      std::chrono::system_clock::now() );

    Send( CoSSMic::ActorManager::CreateLoad( "[1]:[1]", 
          Now+3000, Now+30000, "Tests/consumer811.csv",811), 
          ActorManagerAddress );
    Send( CoSSMic::ActorManager::CreateLoad( "[1]:[2]", 
          Now+3000, Now+30000, "Tests/consumer814.csv",814), 
          ActorManagerAddress );
    Send( CoSSMic::ActorManager::CreateLoad( "[1]:[3]", 
          Now+3000, Now+30000, "Tests/consumer818.csv",818), 
          ActorManagerAddress );
    
#else

    // Time is fixed to be within the prediction interval of the prediction 
    // CSV files, and it is taken as time of the third sample in the example 
    // files
    
    CoSSMic::Now.Fix( 1462413600 );
    
    // Testing serialised messages as they will be received from the XMPP 
    // interface.

    Send( Theron::SerialMessage::Payload(
        "CREATE_PRODUCER PV [27]:[95] 27_95.csv" ),  
        ActorManagerAddress );
        
    Send( Theron::SerialMessage::Payload(
        "CREATE_PRODUCER PV [28]:[98] 28_98.csv" ),  
        ActorManagerAddress );
        
    // Then consumers are created
    
    Send( Theron::SerialMessage::Payload(
        "LOAD ID [28]:[96]:[3] EST 1462431600 LST 1462438800 SEQUENCE 1 PROFILE 91.csv" ),  
        ActorManagerAddress );
    
#endif

    // Then we record how many loads that should run until completion before 
    // we terminate the simulation
    
    UnassignedCount = 1;
  }
};

/******************************************************************************
  Wait object
*******************************************************************************/

// The following object is a standard receiver with a public function to set up
// wall clock events for time points accounted from the current time.

class WaitForTimeout : public Theron::Receiver
{
private:
  
  Theron::Address     TheEventHandler;
  Theron::Framework * TheFramework;
  
  // The callback handler receives the events from the event handler and 
  // discards them. It is only to allow the receiver to wait for the event
  // to happen, and it acknowledges the event immediately so that the event 
  // handler can start waiting for the next event.
  
  void EventDue( const Theron::WallClockEvent::EventTime & NowTime, 
		 const Theron::Address EventDispatcher )
  {
    TheFramework->Send( true, GetAddress(), EventDispatcher );
  }
  
public:
  
  void SomeSeconds( Theron::WallClockEvent::EventTime SecondsToWait )
  {
    TheFramework->Send( SecondsToWait + std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now() ), GetAddress(), TheEventHandler );
  }
  
  // The constructor takes the framework and the address of the event queue
  // and register the message handler for the due events.
  
  WaitForTimeout( Theron::Framework & ExecutionFramework, 
		  Theron::Address TheWallClock )
  : Theron::Receiver(), TheEventHandler( TheWallClock )
  {
    TheFramework = &ExecutionFramework;
    RegisterHandler(this, &WaitForTimeout::EventDue );
  }
};

/******************************************************************************
  Main
*******************************************************************************/

// For these testing purposes, we will first start all components and then 
// wait before we change the prediction for the producers one by one and then 
// wait again until the emulation ends. 

int main(int argc, char **argv) 
{
  typedef MinimalEndPoint::Initialiser	StandardInitialiser;
  
  MinimalEndPoint TheHousehold( "actormanager", "cloud.cossmic.eu",
    Theron::NetworkEndPoint::SetInitialiser< StandardInitialiser >(
      "secret", Theron::XMPP::JabberID("caserta","muc.cloud.cossmic.eu") ) );
  
  // Before starting any of the CoSSMic actors, the console print actor is 
  // started if debug messages is produced.
  
  #ifdef CoSSMic_DEBUG
    Theron::ConsolePrintServer PrintServer( 
      TheHousehold, &std::cout, "ConsolePrintServer" );
  #endif
    
  Theron::WallClockEvent      EventManager( TheHousehold.GetFramework() );
  CoSSMic::ShapleyValueReward TheRewardCalculator( TheHousehold );
  CoSSMic::ActorManager       TheActorManager( TheHousehold, 
                              TheRewardCalculator.GetAddress(), 
                              1e-4, 10000 );
  CoSSMic::Grid               TheGrid( TheHousehold.GetFramework() );
  TaskManager     	          HousholdManager( &TheHousehold, 
                                               EventManager.GetAddress() );
  
  WaitForTimeout TheWaiter( EventManager.GetFramework(), 
                            EventManager.GetAddress()    );

  std::cout << "All actors constructed... waiting for action!" << std::endl;
 
 /* 
  // Wait for 90 seconds before changing the prediction for the first producer
  // We pretend that the message is being sent from the Task Manager, and the 
  // code could have been included in the task manager, but since we would like
  // to keep the task manger actor as close as possible in logic to the real 
  // task manager, it is hard coded here. 
  
  TheWaiter.SomeSeconds(90);
  TheWaiter.SomeSeconds(30); // should cancel the first wait
  TheWaiter.Wait();
  TheWaiter.Wait();
  
  std::cout << "Switching prediction for producer[1]:[10]" << std::endl;
  
  TheHousehold.Send( 
    CoSSMic::PVProducer::NewPrediction( "Tests/solarPanel2_221.csv" ),
    TheActorManager.GetAddress(), Theron::Address( "producer[1]:[10]" ) 
  );
  
  // Wait another 90 seconds before changing the prediction for the second 
  // producer

  TheWaiter.SomeSeconds(90);
  TheWaiter.Wait();
  
  std::cout << "Switching prediction for producer[1]:[20]" << std::endl;
  
  TheHousehold.Send( 
    CoSSMic::PVProducer::NewPrediction( "Tests/solarPanel1_220.csv" ),
    TheActorManager.GetAddress(), Theron::Address( "producer[1]:[20]" ) 
  );
*/  
  // Wait another 90 seconds before deleting the first consumer
  
  TheWaiter.SomeSeconds(30);
  TheWaiter.Wait();
  
  std::cout << "Killing consumer [28]:[96]:[3]" << std::endl;
  
  TheHousehold.Send( Theron::SerialMessage::Payload(
    "DELETE_LOAD [28]:[96]:[3] 615 [0]:[0]"), 
    HousholdManager.GetAddress(),
    TheActorManager.GetAddress()    
  );  

  // After a small wait the consumer will be re-created
  
  TheWaiter.SomeSeconds(30);
  TheWaiter.Wait();
  
  std::cout << "Re-creating consumer [28]:[96]:[3]" << std::endl;  
    
  TheHousehold.Send( Theron::SerialMessage::Payload(
        "LOAD ID [28]:[96]:[3] EST 1462431600 LST 1462438800 SEQUENCE 1 PROFILE 91.csv" ),  
        HousholdManager.GetAddress(), TheActorManager.GetAddress() );

  // We have to delete the consumer before terminating, otherwise the 
  // Theron will fail with a segmentation fault because some actors may 
  // still remain in the system (typically the proxy)

  TheWaiter.SomeSeconds(30);
  TheWaiter.Wait();
  
  std::cout << "Sending SHUT DOWN to the Actor Manager" << std::endl;
  
  TheHousehold.Send( Theron::SerialMessage::Payload( "SHUTDOWN" ), 
    HousholdManager.GetAddress(),
    TheActorManager.GetAddress()    
  );  
  
  std::cout << "Waiting for simulation to end" << std::endl;
  
  #ifdef BINARY_MESSAGE_TEST  
    // Wait for completion and all jobs done.
    HousholdManager.SimulationEnd.Wait();
  #else
    TheActorManager.GetTerminationObject()->Wait();
  #endif

  std::cout << "Outstanding messages: " << std::endl;
  std::cout << "Event manager    : " << EventManager.GetNumQueuedMessages() << std::endl;
  std::cout << "Reward Calculator: " << TheRewardCalculator.GetNumQueuedMessages() << std::endl;
  std::cout << "Actor manager    : " << TheActorManager.GetNumQueuedMessages() << std::endl;
  std::cout << "Task manager     : " << HousholdManager.GetNumQueuedMessages() << std::endl;
  #ifdef CoSSMic_DEBUG
	  std::cout << "Console server   : " << PrintServer.GetNumQueuedMessages() << std::endl;
	#endif
  std::cout << " " << std::endl;
  
  std::cout << "Correct termination" << std::endl;
   
  return 0;
}
