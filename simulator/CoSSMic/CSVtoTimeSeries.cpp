/*==============================================================================
CSV to Time Series

This is the implementation of the utility function to read a time series 
consisting of rows with two columns: One for the time stamp and one for 
the cumulative energy value. The CSV parser is Ben Strasser's fast C++ CSV 
Reader class [1].

References:
[1] https://github.com/ben-strasser/fast-cpp-csv-parser

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#include <string>                  // Standard strings
#include <map>                     // The time series map
#include <sstream>                 // For error messages
#include <stdexcept>               // For standard exceptions

#include "CSVtoTimeSeries.hpp"     // Function signature
#include "csv.h"                   // The CSV parser


std::map< CoSSMic::Time, double > 
CoSSMic::CSVtoTimeSeries( std::string FileName )
{
  std::map< CoSSMic::Time, double> TimeSeries;	  // The time series to return
  CoSSMic::Time   	  		  		   TimeStamp;     // To store read time stamp
  double 		  		  			         Value;	  	    // To store the read value
  
  // Parse two columns from the file, using space as separator and ignore only
  // tabs.
  
  io::CSVReader<2, io::trim_chars<'\t'>, io::no_quote_escape<' '> > 
      CSVParser( FileName ); 
  
  // We define the column headers we are looking for provided that the file 
  // has column headers. Since there are no headers in the file, we simply
  // define them. However, it is not clear if this is strictly necessary or 
  // not.
  
  CSVParser.set_header("Time", "Energy");
  
  while ( CSVParser.read_row( TimeStamp, Value ) )
    TimeSeries.emplace( TimeStamp, Value );
    
  //  It could be that the CSV file did not contain any valid data and in 
  //  that case the time series will not be valid. 
  
  if ( TimeSeries.empty() )
  {
	  std::ostringstream ErrorMessage;
	  
	  ErrorMessage <<  __FILE__ << " at line " << __LINE__ << ": "
							   << "CSV Read error: File \"" <<  FileName 
							   << "\" does not contain any data";
				   
	  throw std::invalid_argument( ErrorMessage.str() );
  }
  
  // If the time series contained data, it is just to return the response
  
  return TimeSeries;
}
