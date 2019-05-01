/*=============================================================================
  Photo Voltaic Producer

  The PV producer is a producer agent that receives new production predictions,
  and then schedules the assigned loads to consume the predicted production. 
  It uses the algorithm in [1] to obtain the schedule.
 
  REFERENCES:
  
  [1] Geir Horn: Scheduling Time Variant Jobs on a Time Variant Resource, 
      in the Proceedings of The 7th Multidisciplinary International Conference 
      on Scheduling : Theory and Applications (MISTA 2015), Zdenek Hanzálek et
      al. (eds.), pp. 914-917, Prague, Czech Republic, 25-28 August 2015.

  Author: Geir Horn, University of Oslo, 2016-2017
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef COSSMIC_PV_PRODUCER
#define COSSMIC_PV_PRODUCER

#include <string>										// Standard text strings
#include <memory>										// Shared pointers
#include <limits>										// Numeric limits
#include <vector>										// For not started loads, and future loads
#include <chrono>										// For system clock and time offset

#include "Actor.hpp"								// The Theron++ actor framework
#include "SerialMessage.hpp" 				// Support for network messages
#include "DeserializingActor.hpp" 	// Support for receiving a serial message
#include "StandardFallbackHandler.hpp"

#include "Producer.hpp"							// The generic producer 
#include "Predictor.hpp"						// Management of the energy prediction

namespace CoSSMic
{

class CollectContribution;
	
// Strictly speaking, only the producer needs to be inherited. However, given 
// that the producer inherits the actor and the de-serialising actor as virtual 
// base classes to avoid potential diamond inheritance problems, the constructor
// of the PV Producer must explicitly call the the constructors of the virtual 
// base classes. It looks strange to call base classes not visible from the 
// code, and so they are explicitly included as base classes although this is
// not a requirement.
  
class PVProducer : virtual public Theron::Actor,
									 virtual public Theron::StandardFallbackHandler,
								   virtual public Theron::DeserializingActor,
								   virtual public Producer
{
  // ---------------------------------------------------------------------------
  // Type checking 
  // ---------------------------------------------------------------------------
  //
  // The general mapping from a producer address to a producer type is done 
  // by checking an actor's address using the static method of the producer
  // base class which will call the relevant static verification function on 
  // the relevant subclass. It is based on the concept of first defining the 
  // fixed name root used by all producers of a given type, and then look for 
  // this sub-string in the textual representation of the actor's address.
  
public:
	
  constexpr static auto PVProducerNameBase = "pv_producer";

private:
	
 	static bool TypeName( const std::string & ActorName )
	{
		if ( ActorName.find( PVProducerNameBase ) == std::string::npos )
			return false;
		else
			return true;
	}

	// Since this function is called from the base class interface,  the base 
	// class needs to be given explicit access. 
	
	friend class Producer;

  // ---------------------------------------------------------------------------
  // Updating the prediction
  // ---------------------------------------------------------------------------
  //
  // The prediction is basically a time series with two values, the time stamp
  // and the cumulative energy produced until that time. The prediction is 
  // provided as a CSV file, and the file name (full path) is the argument of 
  // the update command

public:
  
  class NewPrediction : public Theron::SerialMessage
  {
  public:
    
    std::string     NewPredictionFile;
    
    // This supports two constructors depending on how the string is provided,
    // and one copy constructor to be used to ensure that the file name is 
    // correctly copied (useful to have compile time guarantees with derived
    // classes)
    
    NewPrediction( const std::string & FileName )
    : NewPredictionFile( FileName )
    { }
    
    NewPrediction( const std::string && FileName )
    : NewPredictionFile( FileName )
    { }
    
    NewPrediction( const NewPrediction & PredictionMessage )
    : NewPredictionFile( PredictionMessage.NewPredictionFile )
    { }
    
    // The default constructor is basically used for de-serialising the message
    
    NewPrediction( void ) : NewPredictionFile()
    { }
    
    // The functions to serialise and de-serialise the message across the 
    // network
    
    virtual Theron::SerialMessage::Payload 
	    Serialize( void ) const override;
    virtual bool
	    Deserialize( const Theron::SerialMessage::Payload & Payload) override;
    
    // The destructor is just a place holder
    
    virtual ~NewPrediction( void )
    { }
  };

  // The result of the prediction update is an interpolated function, and the
  // integrated prediction. However, these computations can happen in parallel 
  // with other processing, and for that reason this information is encapsulated
  // in a separate actor. In line with best practice this is only referenced
  // to avoid that memory for this actor is shifted in and out of threads when 
  // the PV producer executes.

private:
  
  std::shared_ptr< Predictor > Prediction;
  std::shared_ptr< CollectContribution > Collector;
	
  // The messages indicating the availability of a new prediction are received 
  // by the Update Prediction handler, which then forwards the request to the 
  // prediction actor that will compute the new prediction functions in the 
  // background and then send a zero energy load back as a schedule command 
  // to trigger an update of the schedule for this newly updated prediction.
  //
  // One could think that this would lead to a race condition in two cases: 
  //
  // 1) There is a schedule computation ongoing when the request for updating 
  //    the prediction arrives, and the update would then cause this schedule 
  // 	to be partly computed using the old prediction and partially with the 
  // 	new prediction.
  // 2) A new load request starts the scheduling in parallel with the update 
  // 	of the prediction function, and hence can start using the old prediction
  // 	and in the middle of the computation will switch to the new one.
  // 
  // The actor model prevents both of these. For the first, since the update 
  // prediction request arrives via the PVProducer, it has to finish the 
  // previous new load request before the prediction update will be processed. 
  // this means that the ongoing scheduling will use the old prediction values.
  // The second case is covered by the same principle, in order for the 
  // scheduler to access the prediction it has to send messages to the predictor
  // and these messages will only be processed once the prediction has been 
  // updated. Hence, the schedule will be produced using the new prediction 
  // values.
  
  void UpdatePrediction( const NewPrediction & TheCommand,
												 const Theron::Address TheForecaster )
  {
		#ifdef CoSSMic_DEBUG
		  Theron::ConsolePrint DebugMessage;

			DebugMessage << "Update prediction request from " 
									 << TheForecaster.AsString() 
								   << " with file {" << TheCommand.NewPredictionFile 
								   << "}" << std::endl;
		#endif

    Send( TheCommand.NewPredictionFile, Prediction->GetAddress() );
  }
  
  // When the predictor has loaded the new prediction, the schedule of the 
  // allocated loads must be re-computed based on the current prediction. Thus,
  // the predictor will send a schedule command to the producer to trigger a 
  // new computation. This command is recognised by the new load handler by 
  // requesting zero energy, and the allowed start interval contains the domain
  // for which there is a production prediction. Valid start times can only be
  // given by the producer so that the consumer finish its consumption within 
  // this domain.
  //
  // Note that if the load cannot be executed within the prediction domain,
  // it will be rejected.
    
  TimeInterval PredictionDomain;
  
  // ---------------------------------------------------------------------------
  // New load and scheduling
  // ---------------------------------------------------------------------------
  //
  // The set of assigned consumers can be split into three parts: Those that 
  // can be scheduled or re-scheduled; and those that have started executing;
  // and the loads with earliest start time beyond the domain of the prediction.
  // The different types of loads will be kept in dedicated lists that are
  // established in the new load handler, and used every time the objective 
  // function is evaluated. There is an obvious invariant that the size of the 
  // set of active loads must equal the length of the proposed start times. 
  //
  // Implementation note: A set would be a natural container, however a set 
  // will keep the elements in "sorted order" and there is no natural way to 
  // define the ordering of two consumer references. They can be tested for 
  // equality, but not for ordering. The main reason to use a set would be to 
  // ensure that the contained elements would be unique. However, this is anyway
  // taken care of by the partitioning function to be defined next.
  //
  // The standard list seems to have a major issue if functions are called on 
  // an empty list, like for instance erasing an empty list causes a 
  // segmentation fault as there is no implicit test to see if the list is 
  // empty. Hence vectors will be used, although it is probably less efficient
  // than a list, it is still more efficient than many other STL containers.
  
  std::vector< Producer::ConsumerReference > ActiveLoads, StartedLoads, 
																					   FutureLoads;
  
  // There is also a utility function that partitions the loads based on the 
  // current time now of the system clock. Clock synchronisation in distributed
  // systems is a huge topic, and this may lead to minor inaccuracies. This 
  // function will make a scan of the list of associated consumers, and based
  // on the set start times, it will allocate each consumer to one of the three
  // lists and move on to the next element in the list. Since the element 
  // references will be different, the elements of the three sets will be 
  // disjoint.
  
  void PartitionLoads( void );
  
  // A time offset will be used to compensate for these inaccuracies. Thus,
  // a job is considered as stared if its start time is less than Now + Time
  // Offset. The latter is to compensate for the time it takes to compute the 
  // schedule and it will be dynamically updated for each computation as an 
  // exponentially moving average of the time the scheduling operation has 
  // taken.
  
  std::chrono::milliseconds TimeOffset;

  // The actual scheduling is done in response to receiving a new load from 
  // a consumer. It will then first check if any of the assigned loads have 
  // started and exclude them from further scheduling, and then compute the 
  // schedule based on the remaining loads. 
  
protected:  

  virtual void NewLoad( const Producer::ScheduleCommand & TheCommand, 
												const Theron::Address TheConsumer  );

  // A core issue with the scheduling is started jobs since their remaining 
  // energy consumption cannot be rescheduled (moved), and the job has to 
  // run to completion. This implies that its consumption from 'now' until 
  // the end of the job should be subtracted from the available predicted 
  // energy. Besides being complicated theoretically, this also requires the 
  // full load profile L(t) to be known by the predictor allowing it to compute
  // the net prediction. Since consumers in general are remote, it requires the 
  // load profile to be transferred from a remote endpoint, and then there are 
  // issues related to serialisation or a compact representation of the load 
  // profile.
  //
  // The elegant solution to this problem is to compute the schedule from a 
  // time T < 'now', where T is the earliest assigned start time for loads 
  // that have started. Then the complete duration of all loads are considered,
  // the framework in [1] can be applied to both running and not started jobs.
  // The running jobs are used in the computation, and it is only that their 
  // start times will not be changed from the already assigned time in the past
  // when the job started.
  //
  // A consequence of this is that the domain of the prediction has to be from 
  // the time T until the full prediction horizon. When a load finishes, it 
  // could have been the earliest assigned start time in the set of loads, 
  // and as such it will be another load with a start time t>T that will define
  // the earliest assigned start time for the next scheduling operation. The 
  // predictor must therefore be informed about the earliest assigned start 
  // time when a consumer cancel a load. This implies that the kill proxy 
  // management handler must send the earliest assigned start time to the 
  // predictor so that it can update the profile domain.
  
  virtual void KillProxy( const Producer::KillProxyCommand & TheCommand, 
												  const Theron::Address TheConsumer    );
  
  // In order to avoid searching the list of assigned consumers to see if 
  // a kill proxy request corresponds to the earliest start time assigned, 
  // a reference to the consumer with the earliest start time is updated by 
  // the new load message handler when the schedule has been computed. The 
  // start time of this load may be in the past. Note the invariant that this 
  // reference must always be valid since a consumer must first register its 
  // load before it can kill its proxy (in the case of a single consumer 
  // assigned).
  
private:
  
  Producer::ConsumerReference EarliestStartingConsumer;
  
  // The schedule is produced by solving a non-linear mathematical programme, 
  // and its objective function takes a vector of proposed start times. By 
  // definition this vector should have the same length as the consumers waiting 
  // to be scheduled. The Gradient is not used in this version. This will be 
  // called from an external solver and hence it needs to be publicly accessible

public:
  
  double ObjectiveFunction( const std::vector< double > & ProposedStartTimes );

  // The search is governed by one accuracy parameter, and a limit on the 
  // number of iterations to do in order to find a good solution. These are 
  // set by the constructor.
  
private:
  
  double ObjectiveFunctionTolerance;
  int    EvaluationLimit;

  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------

public:
  
  PVProducer( const IDType & ProducerID, 
				      const std::string & PredictionFile, 
				      double SolutionTolerance = 1e-8,
				      int MaxEvaluations = std::numeric_limits< int >::max()  );
  
  // The destructor does nothing since the automatic destruction will handle
  // the destruction of all objects owned by this PV producer. 
  
  virtual ~PVProducer( void )
  { }
  
};	// End class PVProducer
  
} 	// end namespace CoSSMic
#endif  // COSSMIC_PV_PRODUCER
