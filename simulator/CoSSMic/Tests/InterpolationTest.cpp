/*=============================================================================
  Akima interpolation

  This programme reads trace file with real measurements, interpolates it, 
  and writes out the resampled interpolated function for a dense grid. The
  trace data are for a day (measured in seconds) and we generate the data with 
  seconds resolution.
  
  Author: Geir Horn, University of Oslo, 2014
  Contact: Geir.Horn [at] mn.uio.no
  License: GPL (LGPL3.0 without the GNU Scientific Library)
=============================================================================*/
  
#include <cmath>
#include <iostream>
#include <fstream>
#include <utility>
#include <chrono>

#include "Interpolation.hpp"

// ----------------------------------------------------------------------------
// Name spaces used
//-----------------------------------------------------------------------------

using namespace std;
using std::chrono::_V2::system_clock;

// ----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(int argc, char **argv) 
{

  system_clock::time_point StartTime = system_clock::now();
  cout << "Reading and interpolating the trace" << endl;
  
  Interpolation Trace("TracePoints.dta");
  ofstream AkimaValues("Akima.csv");
  
  std::chrono::milliseconds ElapsedTime = 
      std::chrono::duration_cast< std::chrono::milliseconds >(
	    system_clock::now() - StartTime );
  
  cout << "Constructing the interpolation function took " 
       <<  ElapsedTime.count() << " milliseconds" << endl;
  
  cout << "Generating interpolated samples for " 
       << Trace.DomainUpper() - Trace.DomainLower() << " seconds from "
       << Trace.DomainLower() << " to " << Trace.DomainUpper() << endl;
  
  long unsigned int SampleCounter = 0;
       
  if (AkimaValues)
    for ( double x = Trace.DomainLower(); x <= Trace.DomainUpper(); x += 1 )
    {
      AkimaValues << x << "," << Trace(x) << endl;
      if ( ++SampleCounter % 1000 == 0 )
      {
	cout << ".";
	cout.flush();
      }
    }
  
  cout << endl;
  AkimaValues.close();
  
  ElapsedTime = std::chrono::duration_cast< std::chrono::milliseconds >( 
		      system_clock::now() - StartTime );
  
  cout << "The total job took " << ElapsedTime.count()  
       << " milliseconds" << endl;
  
  return 0;
}
