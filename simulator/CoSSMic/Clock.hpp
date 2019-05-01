/*=============================================================================
  Clock

  This class is a result of the need to maintain two separate versions of the 
  CoSSMic distributed scheduler: One for the production and the trials, and 
  one for the simulation of larger neighbourhoods to test the effect of 
  different parameters on the efficiency and scalability of the CoSSMic 
  approach.
  
  A time point (epoch) "now" is needed by the scheduler to decide whether a 
  consumer has started a load or not. In the latter case, it can be re-scheduled
  for a future time, while in the former case it should be allowed to run 
  uninterrupted. 
  
  In the case of a real trial, "now" corresponds to the real system clock 
  rounded to the nearest second with epoch origin 1 January 1970, i.e. the 
  standard POSIX time. For the simulated case, the simulated "now" will be 
  known by the simulator's event dispatcher, and it is necessary to read the 
  clock from the dispatcher through a REST call using the cURL library.
  
  Since the cURL library needs some state information, the clock is defined as
  a class holding the potential state. This class is a standard function that 
  is defined at run-time to either read the system clock or to read the 
  dispatcher's clock. The system clock is used by default since it is always 
  available.
  
  ACKNOWLEDGEMENT:
  
  Salvatore Venticinque of Second University of Naples for writing the 
  first version of the cURL library code to obtain the now time stamp from the
  event dispatcher.
      
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef COSSMIC_CLOCK
#define COSSMIC_CLOCK

#include <functional>   	// to have a generic function
#include <string>		// string manipulation
#include <curlpp/cURLpp.hpp> 	// to read from the dispatcher

#include "TimeInterval.hpp"	// To use consistent time


namespace CoSSMic
{
class Clock 
: protected std::function< Time(void) >
{
private:
  
  CURL *  cURL_data;		// To hold the state of the cURL calls
  Time    CurrentTime;	// To store the current time
  
public:
  
  // The cURL library requires a callback function to set the value of the 
  // current time field. It is defined as static since cURL is, well, a 
  // C-library that does not support the this pointer. A pointer to this 
  // class is passed in the user data.
  
  static size_t ConvertTime( void * DataReceived, std::size_t DataSize,
                             std::size_t ChunckSize, void * UserData   );
  
  // There is a public function to set the URL of the simulator's event 
  // dispatcher - it should contain the full URL including the port 
  // number and potentially the data structure to read. An example is 
  // "http://localhost:8808/time.json". This will then initialise the 
  // clock to read from this URL for the future. It will also initialise the 
  // cURL library and the underlying function.
  
  void SetURL( const std::string & URL );
  
  // In some cases it could be useful to fix the clock to a specific value,  
  // particularly when debugging a problem. The time stamp given to the fix 
  // function will be returned until a new value is given 
  
  void Fix( Time TimeStamp );
  
  // It is also possible to provide a dedicated time function that will replace
	// the time returning function by the given function to provide any kind of 
	// clock behaviour. One must however make ensure the causality of time: for 
	// two subsequent readings, the latter must be larger or equal to the former.
	
	void SetClockFunction( const std::function< Time(void) > & TheNewClock );
  
  // In order to read the time, the functor operator of the standard function
  // base class is simply reused.
  
  using std::function< Time(void) >::operator();
  
  // The standard constructor initialises the base class function to read the 
  // system clock and return the time value. Initialisation for the simulation
  // case is done by the SetURL function above.
  
  Clock( void );
  
  // The destructor will clean up the cURL data if it was a simulation,
  // otherwise it will do nothing.
  
  ~Clock( void );
  
};

extern Clock Now;
  
} 	  		// name space CoSSMic
#endif    // COSSMIC_CLOCK
