/*==============================================================================
Options

The command line options are processed using Boost::Program Options. The parsing
is done in a class that can be instantiated on the command line argument vector
and the argument count.

The following options are currently supported:

-h [ --help   ] 	              = help message
-p [ --ProductionFile <CSV> ]   = CSV time series for the production
-c [ --Consumers <CSV> ]        = CSV file defining the consumers
Optional parameters:
-d [ --Directory ]              = Working directory. Default: current directory
-a [ --AssignedTimes <name> ]   = Result file name. Default: AST.csv
-s [ --SunDay <sunrise> <sunset> ] = to set the duration of the day

Each line in the consumer CSV file has the following formate
<Consumer ID>, <Earliest Start time>, <Latest start time>, <Energy CSV>
where the ID is a string, the start times are in Unix (UTC) seconds, and the
cumulative energy consumption is given as a relative CSV time series (starting
with the first sample at time = 0). All times are supposed to be in POSIX
seconds (or at least integers).

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#ifndef DOMINOES_OPTIONS
#define DOMINOES_OPTIONS

#include <filesystem>               // Portable filesystem
#include "TimeInterval.hpp"         // CoSSMic Time

namespace Dominoes {

class CommandLineOptions
{
private:

  // The working directory,  the producer and consumer files and the results

  std::filesystem::path
  WorkingDirectory, ProducerProfile,  ConsumerProfiles,  Results;

  //  An the sunrise and sunset are stored as CoSSMic Time stamps

  CoSSMic::TimeInterval Day;

public:

  // The production file and the consumers file can be obtained by
  // interface functions. Note that these are returned as absolute files.

  inline std::filesystem::path ProductionFile( void )
  { return WorkingDirectory / ProducerProfile; }

  inline std::filesystem::path ConsumersFile( void )
  { return WorkingDirectory / ConsumerProfiles; }

  // The file for the assigned start times is slightly more complicated as it
  // is an optional file name, and if it is not given the default value should
  // be used.

  inline std::filesystem::path ResultFile( void )
  { return WorkingDirectory / Results;  }

  // The day can be obtained from the day duration function

  inline CoSSMic::TimeInterval DayDuration( void )
  { return Day; }

  // The constructor must have the argument count and the argument vector
  // and it will do all the command line parsing.

  CommandLineOptions( int argc, char **argv );
  CommandLineOptions( void ) = delete;
  CommandLineOptions( const CommandLineOptions & Other ) = delete;
};

}      // end name space Dominoes
#endif // DOMINOES_OPTIONS
