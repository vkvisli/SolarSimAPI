/*=============================================================================
  Clock

  This implements the clock functionality and the global object Now that should
  be used to read out the current POSIX time in seconds since 1 January 1970.
  
  ACKNOWLEDGEMENT:
  
  Salvatore Venticinque of Second University of Naples for writing the 
  first version of the cURL library code to obtain the now time stamp from the
  event dispatcher.
      
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#include <chrono>		// For clock functionality
#include <sstream>		// To convert the simulator's clock to time_t
#include <curlpp/Options.hpp>	// To set the right cURL parameters

#ifdef CoSSMic_DEBUG
  #include "ConsolePrint.hpp"   // Debug messages
#endif

#include "Clock.hpp"

namespace CoSSMic 
{

// -----------------------------------------------------------------------------
// Global clock
// -----------------------------------------------------------------------------
//
// In order to ensure that the right time is used for the current time stamp
// a global now is defined and must be used whenever an absolute time is needed

Clock Now;

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
//
// The constructor initialises the cURL part to a safe, but void state and 
// register the normal chrono library functions to obtain the time stamp from 
// the system clock.

Clock::Clock(void)
: std::function< Time(void) >()
{
  cURL_data = nullptr;
  
  std::function< Time(void) >::operator= (
    [this](void)->Time{
      CurrentTime = std::chrono::system_clock::to_time_t( 
					          std::chrono::system_clock::now()    );
      return CurrentTime;
    }
  );
  
  CurrentTime = std::function< Time(void) >::operator()();
}

// -----------------------------------------------------------------------------
// Set URL - constructor for simulation clock
// -----------------------------------------------------------------------------
//
// The change of clock state to use simulated time is made by calling the 
// method to set the URL of the dispatcher's clock function. This function 
// also initialises the cURL data object and sets the callback function when 
// the URL data is returned, i.e. the time from the dispatcher's clock. It 
// then changes the function invoked by the () operator to call the cURL 
// library to obtain the current time.

void Clock::SetURL( const std::string & URL )
{
  // Initialisation of the cURL state cache
  
  curl_global_init( CURL_GLOBAL_ALL );
  cURL_data = curl_easy_init();
  
  // Registration of the dispatcher address. A standard C-style string has 
  // to be passed as the function is unable to work with strings that are 
  // constant references. Removing the const reference is not an option since
  // then it will be difficult to pass temporary objects.
  
  curl_easy_setopt( cURL_data, CURLOPT_URL, URL.data() );
  curl_easy_setopt( cURL_data, CURLOPT_WRITEFUNCTION, URL.data() );
  
  // Registration of the callback function and the 'this' pointer
  
  curl_easy_setopt( cURL_data, CURLOPT_WRITEFUNCTION, &Clock::ConvertTime );
  curl_easy_setopt( cURL_data, CURLOPT_WRITEDATA, this );
  
  // The register the cURL function to be used to read the current time. This
  // will contact the dispatcher's clock on the given URL, and when the current
  // time string is returned it will be sent to the convert time that will 
  // set the current time of the clock. When the callback handler convert time
  // terminates, the cURL call terminates and the current time can be returned.
  
  std::function< Time(void) >::operator= (
    [this](void)->Time{
      curl_easy_perform( cURL_data );
      #ifdef CoSSMic_DEBUG
        Theron::ConsolePrint DebugMessage;
        DebugMessage << "Simulator time: " << CurrentTime << std::endl;
      #endif
      return CurrentTime;
    }
  );
  
}

// -----------------------------------------------------------------------------
// Fix time
// -----------------------------------------------------------------------------
// The fixed time given is set in the clock variable current time,  and the 
// time function is set just to return this value.

void Clock::Fix( Time TimeStamp )
{
  CurrentTime = TimeStamp;
  
  std::function< Time(void) >::operator= (
    [this](void)->Time{ return CurrentTime; }
    );
}

// -----------------------------------------------------------------------------
// Convert time
// -----------------------------------------------------------------------------
//
// This is the callback function used when the dispatcher returns the current 
// time as a string. Its parameters are given by the cURL library, and then 
// bound to the correct types here.

size_t Clock::ConvertTime( void * DataReceived, std::size_t DataSize, 
                           std::size_t ChunckSize, void * UserData     )
{
  Clock *            This = static_cast< Clock * >( UserData );
  std::size_t        BufferSize = DataSize * ChunckSize;
  std::istringstream TimeStamp( 
      std::string( static_cast< char * >( DataReceived ), BufferSize ) );
  
  TimeStamp >> This->CurrentTime;
  
  return BufferSize;
}

// -----------------------------------------------------------------------------
// Set clock function
// -----------------------------------------------------------------------------
//
// This method simply replaces the current clock function with the given
// function, and it is up to the user to ensure that the given clock respect
// the causality rule that time is always increasing. 

void Clock::SetClockFunction( const std::function< Time(void) > & TheNewClock )
{
	std::function< Time(void) >::operator=( TheNewClock );
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
//
// The destructor cleans up the cURL data cached if simulated time was used

Clock::~Clock(void)
{
  if ( cURL_data != nullptr )
  {
    curl_easy_cleanup( cURL_data );
    curl_global_cleanup();
  }
  
}


} // End name space CoSSMic
