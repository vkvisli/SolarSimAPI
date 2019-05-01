/*==============================================================================
CSV to Time Series

This defines a small utility function to read a time series consisting of 
rows with two columns: One for the time stamp and one for the cumulative 
energy value. The CSV parser is Ben Strasser's fast C++ CSV Reader class [1].

References:
[1] https://github.com/ben-strasser/fast-cpp-csv-parser

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#ifndef CSV_TIME_SERIES_PARSER
#define CSV_TIME_SERIES_PARSER

#include <map>                    // Time to energy map
#include "TimeInterval.hpp"       // To have CoSSMic time

namespace CoSSMic
{
  extern std::map< Time, double > CSVtoTimeSeries( std::string FileName );
}      // name space CoSSMic
#endif // CSV_TIME_SERIES_PARSER

