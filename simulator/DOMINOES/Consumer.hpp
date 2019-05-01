/*==============================================================================
Consumer

The consumer class represents a device to be scheduled. It loads a CSV load
profiles with relative time for the consumption, meaning that the time starts
at zero. It also has the earliest start time and the latest start time as
parameters. It is then assigned a start time by the solver in this start time
window.

This behaviour is similar to the CoSSMic solver. However, as there is only
one producer to consider, the consumer does not have to select a producer.
However, it has to participate to the computation of the energy consumption
profile as before. Energy production is only known at discrete time points
over a day. All consumers will therefore provide their energy needs at exactly
these time points using an interpolation of their consumption profile.

There is a curse of large numbers: The production profile is in real POSIX time,
i.e. seconds since 1 January 1970. If the start interval for a consumer is
short, this means that only the last digits. The double is usually implemented
as a 64 bit IEEE-754 number with about 15 significant digits in the significand
(mantissa). A typical real time epoch is 10 digits, meaning that most of the
significant will be equal between two epochs for a standard problem to solve.
This makes scaling difficult in many solvers. The consumer will therefore
scale the consumption period to be relative to the earliest start time given.
This means that the start interval will always start at zero, and the time
origin must be added to the assigned start time to get the real time.

The consumer is a Theron++ actor [1] which responds to two messages. The first
message is to 'self' to load the file. This to allowed the file to be executed
by the actor's thread and thereby all consumers may load the files in parallel.
The second message is the assigned start time where the message handler
returns the consumption at the given production sample times.

References:
[1] https://github.com/GeirHo/TheronPlusPlus

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#ifndef DOMINOES_CONSUMER
#define DOMINOES_CONSUMER

// Standard headers
#include <string>                            // Standard strings
#include <vector>                            // Standard vectors
#include <memory>                            // Smart pointers
#include <filesystem>                        // Filenames

// Other headers
#include <boost/numeric/conversion/cast.hpp> // Casting numeric types
#include "Actor.hpp"                         // Theron++ Actors

// CoSSMic headers
#include "TimeInterval.hpp"                  // Consistent time representation
#include "Interpolation.hpp"                 // To interpolate the time series

// Dominoes
#include "Typedefs.hpp"

namespace Dominoes {

class Consumer : virtual public Theron::Actor
{
private:

  // The consumer is defined by the start interval, the energy profile and
  // the duration of the consumption. The duration is stored as it is only
  // available after the profile data has been read from the file, and it is
  // used to calculate the consumed energy. It is not necessary to store the
  // name of the consumer as this is given by the actor name.

  CoSSMic::TimeInterval            StartInterval;
	std::unique_ptr< Interpolation > Energy;
  CoSSMic::Time                    ConsumptionDuration, TimeOrigin;
	const SampleTime                 ProductionSamples;

	// The message handler to parse the sample profile file is private as it
	// should never be called directly. The message is a simple string containing
	// the file name to be parsed.

	void ReadLoad( const std::filesystem::path & FileName, const Address Sender );

	// The message handler for the assigned start time simply takes a time and
	// returns the consumption profile for the time points of the sampled
	// production

	void Consumption( const CoSSMic::Time & RelativeStartTime,
                    const Address Solver );

  // ---------------------------------------------------------------------------
  // Access functions
  // ---------------------------------------------------------------------------
  //
  // The start interval can be read to set the bounds for the solver.

public:

  inline CoSSMic::TimeInterval GetStartInterval( void ) const
  { return StartInterval; }

  // The ID of the consumer can be obtained by a similar access function

  inline std::string GetName( void ) const
  { return GetAddress().AsString(); }

  // The real start time is given as the relative start time plus the time
  // origin (the earliest start time)

  inline CoSSMic::Time RealStartTime( CoSSMic::Time RelativeStartTime )
  { return TimeOrigin + RelativeStartTime; }

  inline CoSSMic::Time RealStartTime( double RelativeTimeStamp )
  { return TimeOrigin
           + boost::numeric_cast< CoSSMic::Time >( RelativeTimeStamp ); }

  // ---------------------------------------------------------------------------
  // Time axis
  // ---------------------------------------------------------------------------
  //
	// All consumers shares the time axis defined by the production samples and
	// this is defined based on the available samples of energy produced. Consider
	// the case when this covers the time from T1 to T2. If a consumer has its
	// Earliest Start Time (EST) before T1, the time axis should be extended with
	// zero production from this EST to T1. If a consumer load has a duration D
	// the load may need power from its Latest Start Time (LST) until LST + D,
	// which can again be larger than T2 and the time axis should be extended from
	// T2 until LST+D. EST and LST are stored in the consumer class, and the
	// duration of the load is only known after the load profile is read by the
	// consumer. Thus, each Consumer needs to communicate back to the solver
	// their time coverage, i.e. the interval from EST to LST+D. This is done in
	// a message that is just a CoSSMic time interval, and this message will be
	// received by the consumption receiver class of the solver.

	using TimeCoverage = CoSSMic::TimeInterval;

  // There is a simple message type to request this time coverage whose only
  // purpose is to identify the request

  class TimeCoverageRequest
  {};

  // There is a private handler for this request type that simply creates the
  // time coverage interval and returns this message to the sender.

private:

  void ComputeCoverage( const TimeCoverageRequest & TheRequest,
                        const Address Solver );

  // ---------------------------------------------------------------------------
  // Constructor and destructor
  // ---------------------------------------------------------------------------
  //
	// The constructor takes the earliest and latest start times, the file name
	// of the consumption and a pointer to the production time samples.

public:

	Consumer( const std::string & ID, CoSSMic::Time EarliestStart,
            CoSSMic::Time LatestStart, const std::filesystem::path & FileName,
					  const SampleTime SampleProductionTimes );

	// The default constructor is not allowed, and it makes no sense to copy
	// a consumer.

	Consumer( void ) = delete;
	Consumer( const Consumer & Other ) = delete;

	// The destructor is virtual to ensure that the actor class is properly
	// destructed.

	virtual ~Consumer( void )
  {}

};     // End Consumer actor
}      // End name space Dominoes
#endif // DOMINOES_CONSUMER
