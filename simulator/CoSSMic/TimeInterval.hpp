/*=============================================================================
  Time Interval
 
  The concept of interval is central in the computation of the schedule. There
  are two obvious uses: The allowed starting time interval for a load, i.e. the 
  interval from the earliest start time (EST) of a load and its latest start 
  time (LST), which is the completion time minus the load duration. The second
  use is the consumption interval formed by multiple possibly overlapping load
  consumptions, i.e. the period of time where the producer will deliver energy 
  to at least one load. 
  
  Interval arithmetic is a non-trivial subject. Fortunately, Boost has a good 
  solution also for this, and therefore the interval defined here is simply 
  a specialisation of the standard Boost interval. Note that since the time 
  resolution is limited to seconds, and absolute time is measured in seconds 
  since first January 1970, the limits of the time intervals considered here 
  will be long, unsigned integers.
  
  REFERENCES:
  
  [1] Geir Horn: Scheduling Time Variant Jobs on a Time Variant Resource, 
      in the Proceedings of The 7th Multidisciplinary International Conference 
      on Scheduling : Theory and Applications (MISTA 2015), Zdenek Hanzálek et
      al. (eds.), pp. 914-917, Prague, Czech Republic, 25-28 August 2015.
      
  Author: Geir Horn, University of Oslo, 2015-2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef TIME_INTERVAL
#define TIME_INTERVAL

#include <string>
#include <chrono>
#include <iostream>
#include <boost/numeric/interval.hpp>

// Since the time resolution in CoSSMic is seconds, and absolute time is 
// measured in seconds since first January 1970 a long, unsigned integer is 
// normally used for the time fields. However, time is normally defined as 
// time_t which is not a unique and portable representation as C is not 
// standardised. The definition is therefore derived from the representation 
// of the standard chrono library to make sure it matches the representation 
// of seconds on the current platform.
//
// The definition is made in the CoSSMic name space as it is specific to that 
// scope.

namespace CoSSMic
{
  using Time = std::chrono::seconds::rep;

  // Then the time interval can be defined as a simple application of the Boost
  // interval.

  using TimeInterval = boost::numeric::interval< Time >;  
};

// For some not understood reason a time interval's lower() and upper() values
// cannot be streamed. The output is always zero. However, by creating an 
// explicit output function for time intervals, and forcing the boundaries to 
// be converted to strings before being streamed it works. 

std::ostream & operator << ( std::ostream & OutStream, 
												     const CoSSMic::TimeInterval & T )
{	  
  OutStream << "[" << std::to_string( T.lower() ) << "," 
	    << std::to_string( T.upper() ) << "]";
  
  return OutStream;
}

#endif // TIME_INTERVAL
