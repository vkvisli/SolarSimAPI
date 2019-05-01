/*=============================================================================
Simulator

The simulator provides the main() entry point for a simulated scheduling, and
provides a dedicated task manager. The task manager will 

	a. Read and create all producer events: Creation and prediction updates
	b. Read and create all load creation events
	c. Receive assigned start times for each load as they are scheduled, and 
     set a "delete load" event for the time the load has finished execution 
     and can be deleted.
     
The events in (a) and (b) will be read from two event CSV files, and the 
necessary load consumption profile and prediction time series CSV files will 
be read from the same directory where the simulator executes. 

The events received by the Task Manager will be logged to a TaskManager.log 
file for subsequent verification.

The CSV Parser used here is the Fast C++ CSV Parser by Ben Strasser available
at [1] and licensed under the BSD license.

REFERENCES

[1] https://github.com/ben-strasser/fast-cpp-csv-parser

Author and Copyright: Geir Horn, 2017
License: LGPL 3.0
=============================================================================*/

// General headers:
#include <memory>												// Smart, shared pointers
#include <fstream>											// The Task Manager's log file
#include <map>													// To remember load duration and energy
#include <sstream>						      		// For error reporting
#include <stdexcept>										// For standard exceptions
#include <chrono>									  		// For a real time wait management
#include <thread>												// To pause only the current thread
// Utilities
#include "csv.h"												// The CSV file parser
#include "RunningStatistics.hpp"	  		// To compute running statistics
// Actor framework headers
#include "Actor.hpp"									  // The Theron++ actor framework
#include "EventHandler.hpp"							// The event manager
#include "ConsolePrint.hpp"				  		// Printing messages from actors
#include "StandardFallbackHandler.hpp"	// Catch unhandled messages
// CoSSMic headers
#include "TimeInterval.hpp"							// The concept of time
#include "Clock.hpp"										// The concept of moving time
#include "IDType.hpp"										// The CoSSMic agent ID format
#include "ActorManager.hpp"							// The Actor Manager class
#include "PVProducer.hpp"								// The PV producer actor
#include "ConsumerAgent.hpp"						// The consumer agent
#include "NetworkInterface.hpp"		  		// The communication endpoint
#include "ShapleyReward.hpp"						// The reward calculator
#include "Grid.hpp"											// The global grid producer

namespace CoSSMic
{
// There is already an implementation in the CoSSMic of a function parsing a 
// time series CSV file and converting it to a time-value map.
	
extern std::map< Time, double > CSVtoTimeSeries( std::string FileName );

/*=============================================================================

 Delayed Event Acknowledgement

=============================================================================*/
//
// Even though the creation of a single load is over when it gets a start time
// assigned, it is not possible to acknowledge the event at that time because 
// adding a new load to a producer make that producer reschedule all loads 
// currently allocated to the load. The producer may even decide to reject 
// one or more of the previously allocated loads, and these will then have to 
// find other producers. It is not possible to figure out in advance how many
// loads that will be affected by this re-shuffling. The only thing that is 
// assured is that the re-shuffling will come to a new stable state because of
// the infinite capacity of the grid. During the simulator clock must not be 
// moved to the next event time during this re-shuffling. 
//
// It is therefore not possible to implement this as a wait for a number of 
// start time assignments since it is not known a priori how many loads will 
// be re-shuffled. It is also not possible to implement this as a simple time 
// out since the speed of the re-scheduling depends on the speed of the 
// computer executing the re-scheduling and the number of producers that will 
// be involved with the re-scheduling. The only thing that can be assumed 
// however is that the assigned start time messages will come as a stream of 
// messages, as fast as they can be computed. Hence, when there are no more 
// messages left in the inbound queue for the Task Manager, a certain wait 
// can be implemented and if no message has arrived during this wait, it 
// is probably safe to move to the next action event in the event queue.
//
// This strategy is implemented by a dedicated class to deal with all time
// computations. Its operation is as follows (exemplified for the load 
// scheduling):
//
//		1. The Task Manager gets the Assigned Start Time message from a
//       consumer.
//		2. The Task Managers message handler immediately sends a message 
// 	     to the Delayed Event Acknowledgement actor.
//	  3. This message triggers that the Delayed Event Acknowledgement actor 
//			 computes the time to wait with the acknowledgement, and
//	  4. Sets up a thread to wait for the acknowledgement time to be due.
//    5. When the time-out expires, it will send an acknowledgement back to 
//       the Task Mangaer, which will then acknowledge the event to the 
//			 event queue, which will dispatch the next event that will move the 
//			 simulator's clock to the time of the next event.
//
// If the message in (2) arrives when there is an active thread waiting for 
// the time out, this thread will be cancelled and a new time out thread 
// started. 
//
// NOT YET IMPLEMENTED:
// The time to wait computed in (3) is done via a sample Chebychev bounds, see
// the implementation in the running statistics class. 

class DelayedEventAcknowledgement : public Theron::Actor
{
public:
	
	// Delay times are given as durations as measured by the system clock, which
	// means that general durations should be converted to system clock durations
	// with the right period
	
	template< typename Representation, class Period >
	static std::chrono::system_clock::duration DelayTime( 
						const std::chrono::duration< Representation, Period > & GivenDelay )
	{
		return std::chrono::duration_cast< std::chrono::system_clock::duration >
					 ( GivenDelay );
	}
	
	// There is a standard function setting the default delay if no delay time 
	// is given.
	
	static std::chrono::system_clock::duration DelayTime( void )
	{
		return std::chrono::system_clock::duration();
	}
	
	// There is a default delay defined to avoid using changing the delay in 
	// several places. Since the standard seconds are defined as a class, this 
	// must be defined as a function since a class cannot be initiated as a 
	// constant expression or in-line
	
	constexpr static std::chrono::seconds DefaultDelay( void )
	{
		return std::chrono::seconds( 10 );
	}
	
	// The real time delays are use the system clock and is bound by its
	// resolution. It may neither be high resolution nor stable, but it is 
	// consistent with the operating system's clock and therefore suitable when 
	// the OS is responsible for waking up the thread at the time out.
	
	using TimePoint = std::chrono::system_clock::time_point;
	
  // The time to wake up is also recorded to decide if it is longer than the 
	// current wake up time.

private:
	
  TimePoint WakeUpTime;
	
	// There is also a mutex and a condition variable to ensure that the waiting 
	// thread is woken up by one of two events: Either the wait times out and the 
	// acknowledgement should be sent, or another start time message is received
	// and the wait is cancelled and replaced with another time out.
	
	std::mutex 		    		  TimeOutGuard;
  std::condition_variable TimeOutEvent;
	
	// The actual wait happens in a separate thread so that the actor system 
	// can continue to execute messages as before.
	
	std::thread TimeOutThread;
	
	// This thread executes a waiting function that will set up a lock on the 
	// mutex and wait for the a time out event on the condition variable. If 
	// the time out happens it will acknowledge the pending event to the the 
	// event queue. 
	// 
	// A small complication is the fact that the event queue will use the sender's
	// address to ensure that the right event is removed - this is important if 
	// there are several actors sending events to the event queue, and events 
	// from several actors have the same event time. Then only one of the events
	// from the actor sending the acknowledgement will be removed. This implies
	// that the delayed acknowledgement must personalise as the actor owning 
	// the event and setting up the delayed acknowledgement.
	
	void Wait( const Theron::Address TheTaskManager )
	{
		std::unique_lock< std::mutex > WaitLock( TimeOutGuard );
		
	  if ( TimeOutEvent.wait_until( WaitLock, WakeUpTime ) 
																								    == std::cv_status::timeout )
		 Send( Theron::EventData::EventCompleted(), TheTaskManager );
	}
	
	// There is a simple function to terminate the time out thread by signalling 
	// the condition variable if the thread is running.
	
  void TerminateWait( void )
  {
    if ( TimeOutThread.joinable() )
    { 
			// The notification requires a lock (undocumented) and a standard 
			// guard variable is used. It is encapsulated in a separate code block
			// to ensure that it is released before we start waiting for the 
			// thread to terminate. This because the condition variable will lock
			// the global mutex before terminating the wait so that the thread 
			// function knows it has locked access to data when it wakes up.
			
			{
			  std::lock_guard< std::mutex > Guard( TimeOutGuard );
			  TimeOutEvent.notify_one();
			}
			TimeOutThread.join();
    }
  }
    
	// The handler for the assigned start time message fundamentally records how 
	// long it has been since the last start time provided that this new message 
	// arrives as a burst of start times. Based on all inter-arrival times seen, 
	// it computes an expected wait time for the next assignment event. If no new 
	// start time  message has been received within this time window, the event 
	// creating the start time message will be acknowledged. Otherwise, the wait 
	// will be cancelled and a new epoch defined for as an upper limit for the 
	// wait on the next assignment.
		
  void AssignmentDelay( const std::chrono::system_clock::duration & GivenDelay, 
											  const Theron::Address TheTaskManager )
	{		
		// The fundamental operation is to compute a time to wait for the burst of
		// assigned start time events to cease, and this is a duration variable of
		// the system clock. 
		
		std::chrono::system_clock::duration TimeToWait( GivenDelay );

		// The time of invocation is taken as the absolute time of this assignment
		
		TimePoint AssignmentTime = std::chrono::system_clock::now();
		
		// Time intervals must be recorded only if there is a burst of assignments
		// coming. The time between two assignments should not be counted because 
		// it is only interesting to know how long it is necessary to wait for the 
		// next assignment before sending the event acknowledgement. As the whole 
		// point is to wait with the acknowledgement long enough to allow for the 
		// next assignment, the Time Out thread should be active if this assignment
		// takes place within a burst, and terminated (or not started) if this is 
		// the first assignment of a new burst. 
		
		if ( TimeOutThread.joinable() )
		{			
			// If this leads to a wake up time which is larger than the current 
			// wake up time, the the waiting thread should be cancelled and 
			// a thread started for this new wake up time.
				
			if ( AssignmentTime + TimeToWait > WakeUpTime )
			{
				TerminateWait();
				WakeUpTime = AssignmentTime + TimeToWait;
				
			  TimeOutThread = std::thread( &DelayedEventAcknowledgement::Wait,
																	   this, TheTaskManager );			
			}
		}
		else
		{
			// In this case there is no running thread so the wake up time can be 
			// set and the thread to wait for this time started.
			
			WakeUpTime = AssignmentTime + TimeToWait;
			TimeOutThread = std::thread( &DelayedEventAcknowledgement::Wait,
		  													   this, TheTaskManager );			
		}
		
	  #ifdef CoSSMic_DEBUG
		  Theron::ConsolePrint DebugMessage;
		  
			DebugMessage << Now() << " Assignment time " 
									 << std::chrono::system_clock::to_time_t( AssignmentTime )
									 << " Time to wait " 
									 << std::chrono::duration_cast< std::chrono::seconds >( TimeToWait ).count()
									 << " current wake up time " 
									 << std::chrono::system_clock::to_time_t( WakeUpTime ) 
									 << std::endl;
		#endif
	}
	
	// The constructor takes framework Then it takes the address of the event 
	// handler actor receiving the acknowledgements and the name assigned to this 
	// actor. By default it assumes an automatically assigned actor name.
	
public:
	
	DelayedEventAcknowledgement( const std::string & name = std::string()	)
	: Theron::Actor( name.empty() ? nullptr : name.data() ),
	  WakeUpTime(), TimeOutGuard(), TimeOutEvent(), TimeOutThread()
	{
		RegisterHandler( this, &DelayedEventAcknowledgement::AssignmentDelay );
	}
	
	// There should not be a need to wait for the thread to terminate since 
	// that would imply that the associated acknowledgement might not have 
	// been sent. However it will make a good point to place a  break point...
	
	~DelayedEventAcknowledgement( void )
	{
		TerminateWait();
	}
};

/*=============================================================================

 Task manager

=============================================================================*/
//
// The task manager is managing the tasks that comes from the users through 
// other parts of the CoSSMic system, and interacts with the Actor Manager of 
// the task scheduler to update predictions and start loads for a particular 
// household. Hence the two actors form a unique pair for the household.

class TaskManager : public virtual Theron::Actor,
										public virtual Theron::StandardFallbackHandler
{
private:
	
	// It is necessary to know the address of the event queue to submit and 
	// acknowledge events. However, delayed acknowledgements is handled by the 
	// Delayed Event Acknowledgement above, and its address is also needed.
	
	const Theron::Address EventQueue, DelayedAcknowledgement;
			
public:
	
	// The Task Manager needs to know the address of the actor manager it sends
	// its commands to. It is a constant that will be initialised by the task 
	// manager constructor.
	
	const Theron::Address ActorManager;
			
	// The events will be recorded in a log file to investigate the actual actions
	// taken by the task manager. The file will be opened in the constructor and 
	// closed in the destructor.
	
private:
	
	std::ofstream LogFile;

  // ---------------------------------------------------------------------------
  // Acknowledgement
  // ---------------------------------------------------------------------------
  //
	// In Discrete Event Simulation (DES) an event's time stamp will be the global
	// clock, and when the event has been handled, the clock will move to the time 
	// stamp of the next event. An acknowledgement is the signal from the event 
	// processor, here the Task Manager, that the event has been processed and 
	// the next event can be dispatched. It is even more important if the times of 
	// the events are far apart.
	//
	// For the CoSSMic project this is more complicated since the event may imply 
	// a scheduling of one or more tasks, and the start time for these will be 
	// from the current simulated time epoch Now to the end of the legal scheduling
	// interval for the loads. It is therefore important that the simulator clock
	// remains correct and fixed throughout the whole scheduling operation. 
	// 
	// An example illustrates this: Assume a new load is submitted to the system 
	// with allowed start time window [t1, t2], it is submitted at an event time 
	// T < t1. The next event in the event queue has event time T2 > t2. Hence, 
	// if this event is released before the scheduling of the new load is 
	// completed, it will be impossible to schedule the load. It is therefore
	// necessary to look at the feedback indicating that the scheduling has 
	// taken place before the event is acknowledged. 
	//
	// In some cases it is easy to know when the event has been handled. Creating 
	// a load will at some point be followed by a start time assigned to that 
	// load. Note, however that it can be misleading to think that this will be 
	// the first assigned start time produced by the solver. Since the new load 
	// will try to find a producer, it will try the producers in turn, and this 
	// will lead to a re-scheduling of the loads allocated to that producer and 
	// these will be notified in turn about their new start time, which means 
	// that the new load will be notified last of all the loads assigned to 
	// the same producer as the new load.
	//
	// When a load is deleted, there will be a direct message from the solver that
	// the load has been deleted, but this may come after new start times 
	// assigned to other loads. 
	// 
	// In general, each type of event will need a different acknowledgement 
	// policy. This is implemented via an acknowledgement class that will be 
	// the base class for the various types of acknowledgement management 
	// described under each event type.
	
	class AcknowledgeEvent
	{
	public:
		
		// The class provides a method taking an ID as argument to be able 
		// to wait for the right closing action for the current event. This may or 
		// may not be used. The method will return a boolean indicating if the 
		// event has been properly handled and acknowledged. The Acknowledgement
		// Event object can be deleted if this function returns true.
		
		virtual bool CheckID( const IDType & ID ) = 0;
		
		// As discussed above, one may in many cases not have any choice but to 
		// wait for some time to pass before the event can be acknowledged, and 
		// it should therefore provide a method to be invoked when the time out 
		// occurs. Again, if it returns true the event has been properly 
		// acknowledged and this object can be deleted, otherwise, it will wait 
		// for another ID to be checked or for another time out. 
		
		virtual bool TimeOut( void ) = 0;
		
		// The constructor and destructor are currently empty
		
		AcknowledgeEvent( void )
		{ }
		
		virtual ~AcknowledgeEvent( void )
		{ }
	};
	
	// A pointer to this class may be created by the event after executing, and 
	// this will be tested by the Task Manager's message handlers when messages 
	// arrive from the scheduler. Once the acknowledge function returns true, 
	// then the acknowledge event object is also removed.
	
	std::shared_ptr< AcknowledgeEvent > PendingAcknowledgement;

  // ---------------------------------------------------------------------------
  // Events 
  // ---------------------------------------------------------------------------
  //
	// There are three different event types corresponding to the different 
	// operational modes of the task manger. Each type of event is defined as 
	// a sub-class of the generic event class.
	
public:
	
	class Event
	{
	protected:
	
		// Since all events are created by the task manager class, the events will 
		// contain a pointer to the task manager and use the task manager to send 
		// messages to the producers and consumers. If memory is a concern, then 
		// this pointer can be made static.

		TaskManager * const TheTaskManager;
		
		// There are some functions available for debugging
		
	  #ifdef CoSSMic_DEBUG
	    Theron::Framework & GetFramework( void ) const
			{
				return TheTaskManager->GetFramework();
			}
			
			// There is a useful function to return a string showing the type of 
			// event.
			
	public:
		
			virtual std::string GetEventType( void ) const
			{
				return std::string("Event base class");
			}
	  #endif
		
	public:
		
		// The actual event execution is managed by an event specific method that 
		// must be defined by the various event types. It is defined as constant 
		// since it is not supposed to change the actual event class, and the 
		// constant definition allows it to be called on a constant object
		
		virtual void ExecuteEvent( void ) const = 0;
		
		// There is a virtual function called after the execution of the event 
		// in order to define the acknowledgement object to be invoked later. 
		// It should be noted that it is perfectly legal for this to return 
		// a null pointer if the event can be acknowledged immediately. This is 
		// the default action.
		
		virtual std::shared_ptr< AcknowledgeEvent > Acknowledgement( void ) const
		{
			TheTaskManager->Send( Theron::EventData::EventCompleted(), 
														TheTaskManager->EventQueue );
			
			return std::shared_ptr< TaskManager::AcknowledgeEvent >();
		}
		
		// The constructor stores the task manager pointer
		
		Event( TaskManager * TheOwner )
		: TheTaskManager( TheOwner )
		{ }
		
		// And the copy constructor simply makes a copy of the pointer
		
		Event( const Event & OtherEvent )
		: TheTaskManager( OtherEvent.TheTaskManager )
		{ }
		
		// There is a virtual destructor to ensure that derived objects can be 
		// properly deleted. It does nothing for the generic event.
		
		virtual ~Event( void )
		{ }
		
	};
	
	// These events are referred to by references that are shared pointers 
	// ensuring that the event is properly deleted when it is no longer referenced
	
	using EventReference = std::shared_ptr< Event >;
	
	// This reference is what the event handler will store and return when the 
	// event time is due. Hence there is also a shorthand for the event message
	
	using EventMessage = Theron::DiscreteEventManager< Time, 
																										 EventReference >::Event;
		
	// =========================== UPDATE PREDICTION  ===========================
	
	// There is an event that sends updates of the predictions to the various
	// producers. It should be noted that this message goes directly to the 
	// PV producer. This is because it is assumed to be generated from a 
	// predictor agent that sits somewhere in the CoSSMic neighbourhood and 
	// updates the producers as new weather forecasts arrives. Typically, there
	// would be one such prediction agent per household knowing the geographical 
	// location of the household's PV panels, and their physical properties to 
	// predict their production. The prediction agent generates a local CSV 
	// time series prediction file, and then asks the PV producer to load this 
	// prediction file. The message thus contains of only a file name. However,
	// since this message is sent to the producer, the right producer address must 
	// be remembered in the event. This is however a problem since the producers
	// are maintained by the Actor Manager, and it is not possible to read 
	// its address directly. It is therefore assumed to be constructed from the 
	// standard base name and the id. Note that this may be risky if the 
	// consumer agent implementation changes policy.
	
private:
	
	class UpdatePrediction : public Event
	{
	private:
		
		std::string 					    ThePVProducer;
		PVProducer::NewPrediction TheMessage;
		
		// The acknowledgement object for the update prediction will set an 
		// initial delay when constructed, and remember the time for the time 
		// out. Since updating the prediction could result in a burst of assigned
		// start times, it will add an offset to the end of the block time. When 
		// the time out happens, it will check against the system clock and if 
		// this is later than the release time for the next event, it will return
		// true, otherwise, it will wait for the lag time between the current 
		// clock time and the release time.
		
		class AcknowledgePredictionUpdate : public AcknowledgeEvent
		{
		private:
			
			DelayedEventAcknowledgement::TimePoint ReleaseTime;
			TaskManager * const TheTaskManager;
			
		public:
			
			// When an ID is checked it will be because one of the loads has been 
			// assigned a new start time, and the wait should just be extended to 
			// ensure that there is time to receive potentially other assignments 
			// that might arrive.
			
			virtual bool CheckID( const IDType & ID )
			{
				ReleaseTime = std::chrono::system_clock::now() + 
											DelayedEventAcknowledgement::DefaultDelay();
											
				return false;
			}
			
			// The time out function will just acknowledge the event if the time is 
			// after the release time, otherwise set up a new delay for the time 
			// remaining to the release time.
			
			virtual bool TimeOut( void )
			{
				if ( std::chrono::system_clock::now() >= ReleaseTime  )
				{
				  TheTaskManager->Send( Theron::EventData::EventCompleted(), 
																TheTaskManager->EventQueue );
					return true;
				}
				else
				{
					auto AdditionalWait = DelayedEventAcknowledgement::DelayTime( 
														    ReleaseTime - std::chrono::system_clock::now());
					
					TheTaskManager->Send( AdditionalWait, 
																TheTaskManager->DelayedAcknowledgement );
					return false;
				}
			}
			
			// The constructor stores the task manager and initialises the first 
			// release time. 
			
			AcknowledgePredictionUpdate( TaskManager * TheManager )
			: AcknowledgeEvent(), TheTaskManager( TheManager )
			{
				ReleaseTime = std::chrono::system_clock::now() + 
											DelayedEventAcknowledgement::DefaultDelay();
											
				TheTaskManager->Send( DelayedEventAcknowledgement::DelayTime( 
															DelayedEventAcknowledgement::DefaultDelay() ), 
															TheTaskManager->DelayedAcknowledgement );
			}
			
			// The destructor does nothing in this case.
			
			virtual ~AcknowledgePredictionUpdate( void )
			{ }
		};
		
	public:
		
	  #ifdef CoSSMic_DEBUG
			virtual std::string GetEventType( void ) const
			{ 
				std::ostringstream EventDescription;
				
				EventDescription << "Update prediction " << ThePVProducer;
				
				return std::string( EventDescription.str() );
			}		
		#endif
		
		virtual void ExecuteEvent( void ) const
		{
		  TheTaskManager->Send( TheMessage, Theron::Address( ThePVProducer ) );
		}
		
		// Updating the prediction may cause a re-scheduling of the loads assigned
		// to the given producer. However, this may cause some loads to be rejected
		// and they will then seek new producers. It is therefore possible that 
		// many or all loads in the system will get a new start time as a result of
		// this operation. Consequently, this event cannot be directly acknowledged,
		// and has to wait for the burst of assigned start time messages to cease.
		// 
		// It can not be implemented as a simple delay since the acknowledgement of
		// this event should come at the end of the assignments. At the same time,
		// the possibility that there will be no new start times assigned may 
		// also be catered for in a forced delayed acknowledgement. This must be 
		// handled by the delayed event acknowledgement actor since it should not 
		// block this actor (hence a simple sleep thread is insufficient), and 
		// the acknowledgement may be further postponed by re-assignments of start
		// times being made.
		
		virtual std::shared_ptr< AcknowledgeEvent > Acknowledgement( void ) const
		{	
			return std::make_shared< AcknowledgePredictionUpdate >( TheTaskManager );
		}
		
		// The constructor takes the task manager pointer, the address of the 
		// PV producer and the file name string and initialises the fields.
		
		UpdatePrediction( TaskManager * TheOwner, 
											const IDType & ProducerID, 
											const std::string & PredictionFileName 	)
		: Event( TheOwner ), ThePVProducer( 
										     PVProducer::PVProducerNameBase + ProducerID ),
		  TheMessage( PredictionFileName )
		{ }
		
		// The copy constructor is necessary to safely transfer the message to a 
		// new thread
		
		UpdatePrediction( const UpdatePrediction & OtherEvent )
		: Event( OtherEvent ),
		  ThePVProducer( OtherEvent.ThePVProducer ),
		  TheMessage   ( OtherEvent.TheMessage )
		{ }
		
		// The destructor
		
		virtual ~UpdatePrediction( void )
		{ }
	};
	
	// =========================== CREATE PRODUCER  ===========================
	
	// The first event is however to create the producer.Even though one could
	// imagine that this can be done immediately when the Task Manager is 
	// constructed if the Actor Manager is running, it could be that other actors
	// would need to do other events before the producers should come live. The
	// cleanest approach is therefore to treat these actions as normal events;
	// potentially the first events to be done.
	
	class CreateProducer : public Event
	{
	private:
		
		ActorManager::AddProducer TheMessage;
		
		// Since there should not be any adverse side effects of creating a producer
		// then it is just a matter of waiting a short time before releasing the 
		// acknowledgement when the time out occurs. It is a logical error if the 
		// Check ID is called since no new schedule should be produced as a c
		// consequence of this event.
		
		class AcknowledgeCreateProducer : public AcknowledgeEvent
		{ 
		private:
			
			TaskManager * const TheTaskManager;
			
		public:
			
			// If an ID is received it corresponds to either an assigned start time 
			// or a deleted consumer, and in either case it is an indication that the 
			// event causing this message to be sent was acknowledged too early.
			
			virtual bool CheckID( const IDType & ID )
			{
				std::ostringstream ErrorMessage;
				
				ErrorMessage << "A message for ID " << ID << " arrived while waiting "
										 << " to acknowledge a Create Producer event";
										 
			  throw std::logic_error( ErrorMessage.str() );
			}
		
			// When the time out is returned it is an indication that the small wait 
			// is completed and the event can be acknowledged.
			
  		virtual bool TimeOut( void )
			{
				TheTaskManager->Send( Theron::EventData::EventCompleted(), 
															TheTaskManager->EventQueue );
				return true;
			}
	
			// The constructor stores the task manager pointer and sets up the delay
			// for the proper actions to be taken by the Actor Manager.
			
			AcknowledgeCreateProducer( TaskManager * TheManager )
			: AcknowledgeEvent(), TheTaskManager( TheManager )
			{
				TheTaskManager->Send( DelayedEventAcknowledgement::DelayTime( 
															DelayedEventAcknowledgement::DefaultDelay() ), 
															TheTaskManager->DelayedAcknowledgement );
			}
	
			virtual ~AcknowledgeCreateProducer( void )
			{ }
		};
		
	public:

	  #ifdef CoSSMic_DEBUG
			virtual std::string GetEventType( void ) const
			{
				std::ostringstream EventDescription;
				
				EventDescription << "Create producer " << TheMessage.GetID();
				
				return std::string( EventDescription.str() );
			}		
		#endif
		
		virtual void ExecuteEvent( void ) const
		{
		 TheTaskManager->Send( TheMessage, TheTaskManager->ActorManager );
		  #ifdef CoSSMic_DEBUG
			  Theron::ConsolePrint DebugMessage;
		    DebugMessage << Now() << " Creating producer " << TheMessage.GetID() 
										 << std::endl;
		  #endif
		}
		
		// The creation of a new producer should not have any negative side 
		// effects since the consumers will just note the arrival of the new 
		// producer, but stick to its currently assigned producer until this 
		// producer rejects the load. However the producer needs to register as 
		// an external agent, and then this new producer's address will be 
		// distributed to all actors. A short delay is therefore imposed on this
		// thread to ensure that the others may have ample time to finish this 
		// process.
		
		virtual std::shared_ptr< AcknowledgeEvent > Acknowledgement( void ) const
		{
			return std::make_shared< AcknowledgeCreateProducer >( TheTaskManager );
		}
		
		// The constructor creates the add producer message based on its direct 
		// constructor
		
		CreateProducer( TaskManager * TheOwner, 
										ActorManager::AddProducer::Type ProducerType, 
										const IDType & ProducerID, 
									  const std::string InitialPredictionFile 	)
		: Event( TheOwner ), 
		  TheMessage( ProducerType, ProducerID, InitialPredictionFile )
		{ }
		
		// The copy constructor simply copies the message to ensure that this event
		// can be transferred between threads
		
		CreateProducer( const CreateProducer & OtherEvent )
		: Event( OtherEvent ),
		  TheMessage( OtherEvent.TheMessage )
		{ }
		
		// Finally the destructor is a place holder to ensure proper destruction. 
		
		virtual ~CreateProducer( void )
		{ }
	};
	
	// =========================== SUBMIT LOAD  ===========================
		
	// The event submitting a load sends a message to the actor manager and asks 
	// it to create the consumer actor for this load. It should be noted that 
	// this event is not acknowledged before the scheduler assigns a start time,
	// an a start time will always be defined because the grid will always be 
	// able to provide electricity.
	
	class SubmitLoad : public Event
	{
	private:
		
		ActorManager::CreateLoad TheMessage;

		// The event cannot be acknowledged because the load creation is not over 
		// before the load has been assigned a start time. It is even possible that
		// this will lead to many loads needing to change their start times, and 
		// the delayed acknowledgement is used. This delayed acknowledgement will 
		// be set up only when the start time is assigned for this load. Other start 
		// times may be assigned before this one, and others may follow. the ones 
		// following the assignment of a start time for this load will just extend
		// the delay of the acknowledgement. This object is created and returned 
		// by the event's acknowledgement function.

		class WaitForAssignment : public AcknowledgeEvent
		{
		private:
			
			// The ID of the created load will be remembered so that it will be 
			// possible to know when this ID has been assigned a start time.
			
			const IDType LoadID;
			
			// It needs a time for the earliest release of the acknowledgement, and 
			// this will increase with the number of loads that are assigned start
			// times so that the release time is always the default delay after the 
			// the last assignment.
			
			DelayedEventAcknowledgement::TimePoint ReleaseTime;
			
			// The task manager pointer must also be remembered since this class is 
			// not derived from the Event class;
			
			TaskManager * const TheTaskManager;
			
		public:
			
			// The Check ID function is called when a start time is assigned and 
			// it will ignore assignments before the ID just created, and after 
			// that it will only extend the release time. It will always return 
			// false because the acknowledgement will happen only when the time out 
			// occurs.
			
			virtual bool CheckID( const IDType & ID )
			{
				if ( ID == LoadID )
					TheTaskManager->Send( DelayedEventAcknowledgement::DelayTime( 
														    DelayedEventAcknowledgement::DefaultDelay() ), 
														    TheTaskManager->DelayedAcknowledgement);

				ReleaseTime = std::chrono::system_clock::now() + 
											DelayedEventAcknowledgement::DefaultDelay();

				return false;
			}
			
			// When the time out is signalled, then a new time out is set up if time
			// is not the release time, otherwise the event is acknowledged. 
			
			virtual bool TimeOut( void )
			{
				if ( std::chrono::system_clock::now() >= ReleaseTime )
				{
					TheTaskManager->Send( Theron::EventData::EventCompleted(), 
																TheTaskManager->EventQueue );
					return true;
				}
				else
				{
					TheTaskManager->Send( DelayedEventAcknowledgement::DelayTime( 
																ReleaseTime - std::chrono::system_clock::now()), 
																TheTaskManager->DelayedAcknowledgement );
					return false;
				}
			}
			
			// The constructor takes the ID and the task manager pointer and stores
			// these for reference by the Acknowledge function.
			
			WaitForAssignment( const IDType & TheLoadID, TaskManager * TheManager  )
			: LoadID( TheLoadID ), ReleaseTime(), TheTaskManager( TheManager )
			{	}
			
			// Currently the virtual destructor does nothing
			
			virtual ~WaitForAssignment( void )
			{ }
		};
		
	public:
		
	  #ifdef CoSSMic_DEBUG
			virtual std::string GetEventType( void ) const
			{
				std::ostringstream EventDescription;
				
				EventDescription << "Submit load " << TheMessage.GetID();
				
				return std::string( EventDescription.str() );
			}		
		#endif
		
		// Executing the event is just to send the creation message to the actor 
		// manager.
		
		virtual void ExecuteEvent( void ) const
		{
			TheTaskManager->Send( TheMessage, TheTaskManager->ActorManager );
			#ifdef CoSSMic_DEBUG
			  Theron::ConsolePrint DebugMessage;
		    DebugMessage << Now() << " Creating load " << TheMessage.GetID() 
										 << std::endl;
		  #endif
		}
		
		
		virtual std::shared_ptr< AcknowledgeEvent > Acknowledgement( void ) const
		{ 
			return std::make_shared< WaitForAssignment >( TheMessage.GetID(), 
																										TheTaskManager );
		}
		
		// The constructor basically creates the message from the information found
		// in the load event CSV file. Note that the sequence number is given as 
		// zero since it is not provided by the event file.
		
		SubmitLoad( TaskManager * TheOwner, 
								const Time EarliestStartTime, const Time LatestStartTime, 
							  const IDType & ConsumerID, 
							  const std::string & LoadFileName )
		: Event( TheOwner ),
		  TheMessage( ConsumerID, EarliestStartTime, LatestStartTime, 
									LoadFileName, 0 )
		{	}
		
		// The copy constructor is needed to send the even between threads in a 
		// safe way.
		
		SubmitLoad( const SubmitLoad & OtherEvent )
		: Event( OtherEvent ), TheMessage( OtherEvent.TheMessage )
		{}
		
		virtual ~SubmitLoad( void )
		{ }
	};

	// =========================== DELETE LOAD  ===========================
			
	// Deleting a load is more complex because a load can be re-scheduled and 
	// get a new start time. This new start time will invalidate any previous 
	// start time for that load, and hence any previous delete load events set.
	// It is difficult to cancel an event, but it is relatively easy to ignore 
	// events that have been disabled. Hence the event state can be either active 
	// or ignore, and it starts at active and it can be disabled but never 
	// re-activated. 
	
	class DeleteLoad : public Event
	{
	private:
		
		// There is an enumeration to remember the current execution state
		
		enum class State
		{
			Active,
			Ignore
		} ExecutionState;
		
		// The message to send to the actor manager to delete the load is also 
		// remembered.
		
		ActorManager::DeleteLoad TheMessage;
		
		// Sine the delete load command is acknowledged, the acknowledgement object
		// just needs to wait for the right ID to be removed and then acknowledge 
		// the event as completed, and all other IDs can just be ignored. This 
		// also applies to any wrongly sent time outs.

		class WaitForDeletion : public AcknowledgeEvent
		{
		private:
			
			// The ID to look for must be remembered
			
			const IDType LoadID;
			
			// Since this acknowledgement object is not an event, it must remember 
			// the pointer to the task manager.
			
			TaskManager * const TheTaskManager;
			
		public:
			
			// The acknowledgement of the event will in this case be directly done 
			// when the confirmation is received from the actor manager.
			
			virtual bool CheckID( const IDType & ID )
			{
				if ( ID == LoadID )
				{
					TheTaskManager->Send( Theron::EventData::EventCompleted(), 
																TheTaskManager->EventQueue );
					return true;
				}
				else
					return false;
			}
			
			// The time out handler will neither allow the acknowledgement nor the 
			// removal of the acknowledgement object.
			
			virtual bool TimeOut( void )
			{ return false; }
			
			// The constructor simply stores the ID to look for and the task manager
			// pointer.
			
			WaitForDeletion( const IDType & TheLoadID, TaskManager * TheManager )
			: AcknowledgeEvent(), LoadID( TheLoadID ), TheTaskManager( TheManager )
			{ }
			
			// The destructor is again an empty place holder
			
			virtual ~WaitForDeletion( void )
			{ }
		};
		
	public:
		
		inline void Disable( void )
		{
			ExecutionState = State::Ignore;
		}
		
	  #ifdef CoSSMic_DEBUG
			virtual std::string GetEventType( void ) const
			{
				std::ostringstream EventDescription;
				
				EventDescription << "Delete load " << TheMessage.GetID();
				
				if ( ExecutionState == State::Active )
					EventDescription << " ACTIVE ";
				else
					EventDescription << " INACTIVE ";
				
				return std::string( EventDescription.str() );
			}		
		#endif
		
		// If the event is active it will send a message to the actor manager 
		// requesting the removal of the consumer actor associated with this load.
		
		virtual void ExecuteEvent( void ) const
		{
			if ( ExecutionState == State::Active )
				TheTaskManager->Send( TheMessage, TheTaskManager->ActorManager );
		}

		// Deleting a load has by itself no side effects and is should be possible
		// to advance the event clock to the next event. However, if that next 
		// event happens to be a prediction update, then it is necessary to 
		// ensure that the load has been properly deleted before submitting the 
		// prediction update. The reason is that the prediction update is sent 
		// directly to the producer, and the update will trigger a re-scheduling. 
		// If the deletion of the load is not completed, the load is then a part 
		// of this re-scheduling, which may lead to causality problems; i.e. 
		// the latest start time of the load will be before the simulator's clock
		// Now.
		//
		// The acknowledgement of the deletion event is therefore done by when 
		// the actor manager confirms that the associated Consumer Agent has been 
		// removed. This is done by the Task Manager, and so there is nothing to 
		// acknowledge at this point. However, if the event has been deactivated,
		// no message will be coming from the Actor Manager that this load has been 
		// removed, and it should be acknowledged immediately as the event has done 
		// nothing and it has no side effects.
		
		virtual std::shared_ptr< AcknowledgeEvent > Acknowledgement( void ) const
		{	
			if ( ExecutionState == State::Ignore )
		  {
				TheTaskManager->Send( Theron::EventData::EventCompleted(), 
															TheTaskManager->EventQueue );
				
				return std::shared_ptr< AcknowledgeEvent >();
			}
			else
				return std::make_shared< WaitForDeletion >( TheMessage.GetID(), 
																										TheTaskManager );
		}

		// The constructor takes the information needed to construct the message
		// that allows correct deletion of the consumer agent.
		
		DeleteLoad( TaskManager * TheOwner,
								const IDType & LoadID, 	const double ConsumedEnergy, 
							  const IDType & ProvidingProducer )
		: Event( TheOwner ),
		  TheMessage( LoadID, ConsumedEnergy, ProvidingProducer )
		{
			ExecutionState = State::Active;
		}
		
		// The copy constructor ensures that this event can be sent between two 
		// threads.
		
		DeleteLoad( const DeleteLoad & OtherEvent )
		: Event( OtherEvent ), TheMessage( OtherEvent.TheMessage )
		{}
		
		// The destructor does nothing by itself
		
		virtual ~DeleteLoad( void )
		{ }
	};
	
  // ---------------------------------------------------------------------------
  // Active loads
  // ---------------------------------------------------------------------------
  //	
	// Observe that there will be only one active delete load event in the 
	// system: The last one. Thus whenever the task manager receives a new start
	// time or an updated start time it can safely disable the event currently 
	// active for this load and then create a new event. When the start time is
	// assigned, the consumer also provides the producer agreeing to provide the 
	// energy. However, in order to set up the correct delete event, the task 
	// manager should know the duration of the load, and the energy it has 
	// consumed. In the real life, the consumed energy is measured, but in the 
	// simulated world it is just taken to be equal to the energy amount suggested
	// by the load profile. The information is captured in a small class
	
	class LoadInformation
	{
	public:
		
		Time   				 								Duration;
		double 				 							  ConsumedEnergy;
		std::shared_ptr< DeleteLoad > DeleteEvent;
		
		LoadInformation( Time Delta, double TheEnergy )
		: DeleteEvent()
		{
			Duration 			 = Delta;
			ConsumedEnergy = TheEnergy;
		}
		
		// In order to store this in a standard container it must provide copy and 
		// move constructors.
		
		LoadInformation( const LoadInformation & AnotherInformation )
		: Duration(       AnotherInformation.Duration ),
		  ConsumedEnergy( AnotherInformation.ConsumedEnergy ),
		  DeleteEvent(    AnotherInformation.DeleteEvent )
		{	}
		

  	LoadInformation( const LoadInformation && AnotherInformation )
		: Duration(       AnotherInformation.Duration ),
		  ConsumedEnergy( AnotherInformation.ConsumedEnergy ),
		  DeleteEvent(    AnotherInformation.DeleteEvent )
		{	}

	};
	
	// This information is then stored in an unordered map because this will in 
	// general have better lookup performance. It is also defined as a type 
	// to more easily refer or change the actual structure. An unordered map 
	// is used since it should normally be faster in the lookup, and we do not
	// need to traverse the active loads sequentially. 
	
	using LoadMap = std::map< IDType, LoadInformation >;
	
	// The actual map of active loads
	
	LoadMap ActiveLoads;
	
  // ---------------------------------------------------------------------------
  // Message handlers 
  // ---------------------------------------------------------------------------
  //
	// The task manager must deal with the messages sent from the consumer agent
	// when a start time is assigned or when the start time is cancelled. It must
	// also deal with the event messages, and the message that a consumer agent 
	// has been deleted, and should be deleted from the map of active loads.

private:
		
	void AssignedStartTime( const ConsumerAgent::StartTimeMessage & AST, 
													const Theron::Address TheActorManager )
	{
		// The following search must result in a match since the load was added 
		// to the active loads when the event to create the load was set up.
		
		auto TheLoad = ActiveLoads.find( AST.GetLoadID() );
		
		if (TheLoad == ActiveLoads.end() )
	  {
		  #ifdef CoSSMic_DEBUG
			  Theron::ConsolePrint DebugMessage;

				DebugMessage << "ERROR: Could not find load " << AST.GetLoadID() 
					           << " supposed to get energy from " << AST.GetProducerID() 
										 << std::endl;
			#endif
			return;
		}

		// The currently active delete load must be disabled, and a new event 
		// created relative to the provided start time. 
		
		if ( TheLoad->second.DeleteEvent )
			TheLoad->second.DeleteEvent->Disable();
			
		TheLoad->second.DeleteEvent = std::make_shared< DeleteLoad >(
			this, AST.GetLoadID(), TheLoad->second.ConsumedEnergy, AST.GetProducerID()
		);
		
		// Writing the event to the log file
		
		LogFile << Now() << " AST = " << AST.GetStartTime()
						<< " Load " << AST.GetLoadID() 
						<< " Producer " << AST.GetProducerID() 
						<< " Completion time " 
						<< AST.GetStartTime() + TheLoad->second.Duration
						<< std::endl;
		
		// The event message will contain the time the event will be due, i.e. 
		// AST + duration, and the reference to the event.
		
		Send( EventMessage( AST.GetStartTime() + TheLoad->second.Duration,
												EventReference( TheLoad->second.DeleteEvent ) ),
					EventQueue );
		
	  #ifdef CoSSMic_DEBUG
		  Theron::ConsolePrint DebugMessage;
	     
			DebugMessage << "End time event for " << AST.GetLoadID() 
									 << " set to time " 
								   << AST.GetStartTime() + TheLoad->second.Duration 
								   << std::endl;
		#endif
									 
		// An assignment can be the response to a create load event, or to an 
		// updated prediction for one of the producers. In either case, several 
		// loads may end up with reassigned start times, and the simulator clock 
		// must not move before all the events are received. Hence, the 
		// acknowledgement of event causing the assignment will be delayed, and 
		// if a new assigned start time arrives for another load the acknowledgement
		// will be delayed again and again until all re-assignments have been 
		// completed. It should be noted that there should be an active pending 
	  // acknowledgement, additional delays will must be handled by this, 
	  // and if the ID allows the acknowledgement to be returned to the event 
	  // manager, then the acknowledgement object can be deleted by resetting 
	  // the pending acknowledgement pointer.
		
	  if ( PendingAcknowledgement && 
			   PendingAcknowledgement->CheckID( AST.GetLoadID() ) )
			PendingAcknowledgement.reset();
	}
	
	// When a start time is cancelled, it is just to disable the currently set 
	// event and then wait for another start time. The search for the load must
	// succeed since the load is recorded as active when the create events were 
	// set up. Normally, the start time is cancelled if the currently allocated 
	// producer is unable to provide energy because other consumers make a 
	// better match with the predicted production. In this case the consumer 
	// agent will continue search for a new start time. 
	//
	// However, a cancel start time message can also result from a newly created
	// load having its latest start time too close into the future so the load 
	// is impossible to schedule. In this case it represents the end of a creation
	// event, and that event should be acknowledged if there is no delete event 
	// set for this load.
	
	void ClearStartTime( const ConsumerAgent::CancelStartTime & Cancellation, 
										   const Theron::Address TheActorManager	)
	{
		auto TheLoad = ActiveLoads.find( Cancellation.GetLoadID() );
		
		if ( TheLoad->second.DeleteEvent )
			TheLoad->second.DeleteEvent->Disable();		
		else
			Send( Theron::EventData::EventCompleted(), EventQueue );
		
		LogFile << Now() <<  " Cancel start time for load " 
						<< Cancellation.GetLoadID() << std::endl;
	}
	
	// Standard events dispatched from the event manager will come as an event 
	// reference message, and this event will simply be executed and then the 
	// acknowledgement set up according to the behaviour for this event type.
	
	void HandleEvent( const EventReference & TheEvent, 
										const Theron::Address TheEventDispatcher )
	{
		#ifdef CoSSMic_DEBUG
		  Theron::ConsolePrint DebugMessage;
			
			DebugMessage << "EVENT at " << Now() << " Type: " 
								   << TheEvent->GetEventType() << std::endl;
		#endif

		TheEvent->ExecuteEvent();
		PendingAcknowledgement = TheEvent->Acknowledgement();
	}

	// When a delayed acknowledgement is set up, the Delayed Acknowledgement actor 
	// will return an event acknowledgement message back to the task manager when 
	// this time-out is over. This is be passed to the the pending acknowledgement
	// which may then decide to extend the wait, or to acknowledge the the 
	// pending event. 
	
	void DelayTimeout( const Theron::EventData::EventCompleted & TheAck, 
										 const Theron::Address TimeOutHanlder )
	{
		if ( PendingAcknowledgement && PendingAcknowledgement->TimeOut() )
			PendingAcknowledgement.reset();
	}
	
	// When the delete load event sends the command to the actor manager to remove
	// the load, it also sends the same message to the task manager so that the 
	// load can be removed from the set of active loads. It will throw a 
	// standard run-time error if the load cannot be found in the set of active 
	// loads since this should be an invariant. 
	
	void RemoveLoad( const ActorManager::DeleteLoad & TheMessage, 
									 const Theron::Address Sender 			  )
	{
		auto TheLoad = ActiveLoads.find( TheMessage.GetID() );
		
		if ( TheLoad != ActiveLoads.end() )
	  {
		  #ifdef CoSSMic_DEBUG
			  Theron::ConsolePrint DebugMessage;
				
			  DebugMessage << "Removing " << TheLoad->first << " by request from "
								     << Sender.AsString() << std::endl;
		  #endif
										 
			ActiveLoads.erase( TheLoad );
	  }
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << "Task manager: Delete load confirmation for unknown "
									 << "load ID " << TheMessage.GetID() << " from " 
									 << Sender.AsString();
									 
		  throw std::runtime_error( ErrorMessage.str() );
		}
		
		// The load deletion event can then be acknowledged so that the event 
		// manager can dispatch the next event. Note that the following test should
		// never fail, and if it does, there is a logical error.
		
		if ( PendingAcknowledgement && 
			   PendingAcknowledgement->CheckID( TheMessage.GetID() ) )
			PendingAcknowledgement.reset();
		else
		{
			std::ostringstream ErrorMessage;
			
			ErrorMessage << "Task manager: Delete load confirmation for load  "
									 << TheMessage.GetID() << " with no pending acknowledgement "
									 << "set for this ID";
									 
		  throw std::logic_error( ErrorMessage.str() );
		}
		
		// If the last load was deleted, the simulation should terminate and 
		// the actor manager is asked to close. After this no more start times 
		// will be assigned and the log file can be closed.
		
		if ( ActiveLoads.empty() )
		{
			Send( CoSSMic::ActorManager::ShutdownMessage(), ActorManager );
			LogFile.close();
			
		  #ifdef CoSSMic_DEBUG
			  Theron::ConsolePrint DebugMessage;
			  DebugMessage << "Last load removed proceeding to shut down!" 
										 << std::endl;
		  #endif
		}
	}
	
	// When the Actor Manager has shut down the producers it will return the 
	// shut down message to the task manager. There is nothing to be done by 
	// this handler - it is just here to respect the protocol. One could
	// imagine that in reality it would close the log file, but in the simulated
	// version it will run in the same process as the Actor Manager that also 
	// notifies its own termination object. Typically, main will wait for this 
	// notification and then terminate - which will mean that all the actors 
	// will also be closed, including the task manager and there could be a race 
	// condition between this shut down message and the destruction potentially 
	// leaving the log file open. For that reason it is safer to close it when
	// removing the last active load.
	
	void Termination( const CoSSMic::ActorManager::ShutdownMessage & TheMessage, 
										const Theron::Address Sender )
	{
		#ifdef CoSSMic_DEBUG
			std::cout << "Correct shut down process completed!" << std::endl ;
		#endif
	}
		
  // ---------------------------------------------------------------------------
  // Utilities 
  // ---------------------------------------------------------------------------
  
  // The producer events will either lead to direct dispatch of messages to 
  // the actor manager create the producers, or subsequent events to update 
  // their prediction files. In addition, the predictions implicitly defines 
  // the duration of the simulation: In each prediction file there is a 
  // latest prediction time stamp indicating when this prediction is no longer
  // valid. Hence, there is no reason to try to submit loads for scheduling when
  // no produces have any predicted production left. Consequently, the last 
  // prediction update file for each producer will be opened and checked for the 
  // time of the last time stamp, and then the maximum value of these last times
  // will be used as the simulation stop time.
  
private:
	
	void CreateProducerEvents( const std::string & EventFileName  )
	{
		// Given that the initial event for a producer is to create it, while 
		// subsequent events will be to update the prediction file, it is necessary
		// to remember the producers created. The associated prediction file name 
		// is also stored since it will be used to find the latest time of any 
		// prediction (to be used for setting the simulation stop time).
		
	  std::map< IDType, std::string >	TheProducers;
		
		// The producer event file has three columns, and they are separated by 
		// a semicolon
  
	  io::CSVReader<3, io::trim_chars<'\t'>, io::no_quote_escape<';'> > 
	      CSVParser( EventFileName );
				
		CSVParser.set_header("Time", "ID", "PredictionFileName");
		
		// Variables to be filled by the parser
		
		Time 					TimeStamp;
		std::string 	IDText, PredictionFileName;
		
		// Then the event file can be parsed. The ID has to be parsed as a string
		// and then converted to a real ID since the parser does not support 
		// direct parsing of user defined types
		
		while ( CSVParser.read_row( TimeStamp, IDText, PredictionFileName ) )
		{
		  IDType ProducerID( IDText );
			auto KnownProducer = TheProducers.find( ProducerID );
			
			if ( KnownProducer == TheProducers.end() )
		  {
				// The producer is not known and it should be stored and the create 
				// producer event must be submitted.
				
				TheProducers.emplace( ProducerID, PredictionFileName );
				
				EventReference TheEvent( 
					new CreateProducer( this, 
															ActorManager::AddProducer::Type::PhotoVoltaic, 
														  ProducerID, PredictionFileName ) );
				
				Send( EventMessage( TimeStamp, TheEvent), EventQueue );
			} 
			else
			{
				// The producer has already been created and the file name of the last
				// submitted prediction must be updated, and an update event must be 
				// created and submitted.
				
				KnownProducer->second = PredictionFileName;
				
				EventReference TheEvent( 
					new UpdatePrediction( this, ProducerID, PredictionFileName )  );
				
				Send( EventMessage( TimeStamp, TheEvent ), EventQueue );
			}
		}		
	}
	
	// The consumer events will simply submit all the load creation events, and 
	// then register the consumers in the active loads map. However, in order to
	// extract the total energy consumption of the load and its duration, the 
	// associated load profile file must be opened and the last values stored. 
	
	void CreateLoadEvents( const std::string & EventFileName )
	{
		io::CSVReader<5, io::trim_chars<'\t'>, io::no_quote_escape<';'> > 
	      CSVParser( EventFileName );
				
		// The columns are the time of the event; the earliest start time for the 
		// load; the latest start time for the load; the ID of the device; and 
		// the load profile. 
		
		CSVParser.set_header( "Time", "EST", "LST", "ID", "LoadProfile" );
		
		// The variables to store the fields per line
		
		Time 			 	 TimeStamp, EarliestStartTime, LatestStartTime;
		std::string  IDText, LoadProfileFileName;
		
		while ( CSVParser.read_row( TimeStamp, EarliestStartTime, LatestStartTime,
																IDText, LoadProfileFileName	) )
		{
			IDType ConsumerID( IDText );
		
			EventReference TheEvent( new SubmitLoad( this, 
				 EarliestStartTime, LatestStartTime, ConsumerID, LoadProfileFileName ) );
			
			Send( EventMessage( TimeStamp, TheEvent ), EventQueue );
			
			// In order to register this as an active load it is necessary to parse 
			// the load profile in order to find the duration of the load 
			// corresponding to the last time stamp in the profile file since all 
			// load profiles should be time relative and start at time zero.
			
			auto LoadProfile = CSVtoTimeSeries( LoadProfileFileName );
			
			ActiveLoads.emplace( ConsumerID, LoadInformation( 
									LoadProfile.rbegin()->first, LoadProfile.rbegin()->second )	); 
			
		}		
	}
	
	// ---------------------------------------------------------------------------
  // Constructor and destructor 
  // ---------------------------------------------------------------------------
  //
	// The constructor registers all the message handlers and use the two utility 
	// functions to establish the event queues. 
	
public:
	
	TaskManager( const Theron::Address & ActorManagerAddress,
							 const Theron::Address & EventQueueAddress,
							 const Theron::Address & DelayedAckAddress,
						   const std::string & ProducerEventsFileName,
						   const std::string & ConsumerEventsFileName 						)
	: Actor( "taskmanager"),
	  StandardFallbackHandler( "taskmanager"),
	  EventQueue( EventQueueAddress ), 
	  DelayedAcknowledgement( DelayedAckAddress ),
	  ActorManager( ActorManagerAddress ), 
	  LogFile()
	{
		RegisterHandler( this, &TaskManager::AssignedStartTime );
		RegisterHandler( this, &TaskManager::ClearStartTime 	 );
		RegisterHandler( this, &TaskManager::DelayTimeout      );
		RegisterHandler( this, &TaskManager::RemoveLoad 		   );
		RegisterHandler( this, &TaskManager::HandleEvent 			 );
		RegisterHandler( this, &TaskManager::Termination			 );
		
		if ( !ProducerEventsFileName.empty() )
			CreateProducerEvents( ProducerEventsFileName );
		else
	  {
	    std::ostringstream ErrorMessage;
	    
	    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
							     << "Task Manager: No producer event file name" ;
			 
	    throw std::invalid_argument( ErrorMessage.str() );
	  }
		
		if ( !ConsumerEventsFileName.empty() )
			CreateLoadEvents( ConsumerEventsFileName );
		else
	  {
	    std::ostringstream ErrorMessage;
	    
	    ErrorMessage << __FILE__ << " at line " << __LINE__ << ": "
							     << "Task Manager: No consumer event file name";
			 
	    throw std::invalid_argument( ErrorMessage.str() );
	  }
		
		LogFile.open( "TaskManager.log" );
	}
	
	//  The destructor does nothing in this version
	
	virtual ~TaskManager( void )
	{	}
		
}; // End class Task Manager

/*=============================================================================

 Command line options

=============================================================================*/
//
// This class parses the command line options, and is a copy of the one that
// is used in the real code. It is just to ensure the same set of default 
// values since none of the command line options are used (yet)

class CommandLineParser
{
private:
  
  // Each command corresponds to an enum so that it can be used in the switches
  
  enum class Options
  {
		ConsumerEvents, // Name of the consumer event file
    Name, 		    	// The actor name used for the network endpoint
    Domain,		    	// The domain of the network endpoint
    PeerEndpoint,		// The endpoint to provide known actors
    LocalGrid,			// Start a local grid actor
    GlobalGrid,			// Start a global grid agent
    Simulation,			// Use simulator's clock not the system clock
    Password,   		// The password to the XMPP server(s)
    ProducerEvents, // Name of the producer event file
    Help        		// Prints the help text
  };
  
  // Most of these values are stored as simple strings
  
  std::string ConsumerEventsFileName,
						  EndpointName, 
				      EndpointDomain,
							ProducerEventsFileName,
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
				      << "// Default \"actormanager\" for the Manager" << std::endl;
    std::cout << std::setw(25) << std::left;
    std::cout << "--domain <string>"
				      << "// Default \"127.0.0.1\" for localhost" << std::endl;
    std::cout << std::setw(25) << std::left;
    std::cout << " "
				      << "// Manager Jabber ID will be <name>@<domain>" << std::endl;     
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
    {	{ "--CONSUMEREVENTS", Options::ConsumerEvents		},
			{ "--NAME", 					Options::Name         		},
			{ "--DOMAIN",					Options::Domain       		},
			{ "--PEERENDPOINT", 	Options::PeerEndpoint 		},
			{ "--LOCALGRID",			Options::LocalGrid    		},
			{ "--GLOBALGRID",			Options::GlobalGrid   		},
			{ "--SIMULATOR",			Options::Simulation   		},
			{ "--PASSWORD",				Options::Password     		},
			{ "--PRODUCEREVENTS", Options::ProducerEvents   },
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
				case Options::ConsumerEvents:
					ConsumerEventsFileName = ArgumentCheck( TheOption, ++i );
					break;
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
				case Options::ProducerEvents:
					ProducerEventsFileName = ArgumentCheck( TheOption, ++i );
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
      return std::string("actormanager");
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
  
  inline std::string GetConsumerEvents( void )
	{
		return ConsumerEventsFileName;
	}
	
  inline std::string GetProducerEvents( void )
	{
		return ProducerEventsFileName;
	}
};

}  // End name space CoSSMic

/*=============================================================================

 Main

=============================================================================*/
//
// The Main entry point sets up the various actors of the scheduler plus the 
// task manager before the simulation starts.

int main(int argc, char **argv) 
{
	// In general there could be command line options given
	
	CoSSMic::CommandLineParser Options( argc, argv );
	
  // Starting the network endpoint class based on the CoSSMic network interface
	// Note: The household should not have the same name as an actor! The first 
	// argument was Options.GetName() which defaults to "actormanager"
	// 
  
	Theron::NetworkEndPoint< CoSSMic::NetworkInterface > 
		Household( "Household" , Options.GetDomain(), 
												     Options.GetPassword(), 
												     Options.GetPeerEndpoint()  );
  
  // Before starting any of the CoSSMic actors, the console print actor is 
  // started if debug messages is produced.
  
  #ifdef CoSSMic_DEBUG
    Theron::ConsolePrintServer PrintServer( &std::cout, "ConsolePrintServer" );
  #endif
  
	// The event manager is set up to handle the event requests from the Task 
	// manager, and to send back the events so that the task manager can take 
	// proper actions.
		
	Theron::DiscreteEventManager< CoSSMic::Time, 
																CoSSMic::TaskManager::EventReference > 
																TheEventManager( "EventManager");
					
	// The last part of the event management framework is to ensure that the 
	// CoSSMic clock reflects the event clock
					
	CoSSMic::Now.SetClockFunction( 
				   [&](void)->CoSSMic::Time{ 
										  return TheEventManager.Now< CoSSMic::Time >(); } );
	
  // There is a class to provide delayed acknowledgements to the event manager

  CoSSMic::DelayedEventAcknowledgement 
	  AcknowledgementActor( "DelayedAcknowledgementActor" );
	
  // The reward calculator is started so that we can pass its address to the 
  // actor manager.
    
  CoSSMic::ShapleyValueReward TheRewardCalculator( Options.GetDomain() );

		// The Grid actor provides the last resort for the consumers to find energy,
  // and is defined by its default producer ID

	CoSSMic::Grid GridActor;
	
  // Then add the actor manager - note that the name actor manager is 
  // hard coded for this component, and that actual values are given for 
  // the solution tolerance and the number of iteration allowed to find a 
  // solution.  
  
  constexpr double       SolutionTolerance = 1;
  constexpr unsigned int MaxIterations     = 100;
  
  CoSSMic::ActorManager TheActorManager( TheRewardCalculator.GetAddress(), 
																				 SolutionTolerance, MaxIterations );

	// The task manager is given the address of the actor manager and the 
	// event manager, and the names of the two event files for the producer 
	// events and the consumer events.
	
	CoSSMic::TaskManager TheTaskManager( TheActorManager.GetAddress(),
																			 TheEventManager.GetAddress(),
																			 AcknowledgementActor.GetAddress(),
																			 Options.GetProducerEvents(),
																	     Options.GetConsumerEvents() 	);
	
	// The event stream is started by sending an empty acknowledgement to the 
	// event queue

	Household.Send( Theron::EventData::EventCompleted(), 
									Theron::Address::Null(), TheEventManager.GetAddress() );
	
	// Then it is just to wait for the termination event which will ensure that
	// none of the given actors has messages left when the wait returns.
	
	Household.TerminationWatch( 
			TheTaskManager, GridActor, TheActorManager, TheRewardCalculator, 
			AcknowledgementActor, TheEventManager, PrintServer )->Wait();

	std::cout << "Normal termination" << std::endl;
			
	return EXIT_SUCCESS;

} // End of Main
