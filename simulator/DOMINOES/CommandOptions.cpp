/*==============================================================================
Options

This implements the options class, i.e. the constructor implementing the
parsing of the command line options.

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#include <string>                    // Standard strings
#include <iostream>                  // Printing errors
#include <vector>                    // Standard vectors
#include <algorithm>                 // Standard max and min
#include <boost/program_options.hpp> // Option parser

#include "CommandOptions.hpp"

namespace cmd = boost::program_options;

Dominoes::CommandLineOptions::CommandLineOptions( int argc, char **argv )
: WorkingDirectory( std::filesystem::current_path() ),
  ProducerProfile(),  ConsumerProfiles(),  Results("AST.csv"),
  Day()
{
	// The options class must have an object describing the options and the
	// help messages generated

	cmd::options_description Description("Allowed options");

	// The values are stored in a map from option names to values

	cmd::variables_map Values;

  // Defining and describing the options in case help is requested

	Description.add_options()
		( "help,h",  "Produce this help message" )
		( "ProductionFile,p", cmd::value< std::string >()->required(),
								 "File name of the production file"   )
		( "Consumers,c", cmd::value< std::string >()->required(),
								 "File describing the consuming devices"	)
		( "Directory,d", cmd::value< std::string >(),
								 "Working directory" )
    ( "AssignedTimes,a", cmd::value< std::string>(),
                 "Result file name" )
    ( "SunDay,s",  cmd::value< std::vector< CoSSMic::Time > >()->multitoken(),
                 "Sun day" );

	// Parsing the command line and throwing an exception if the required
	// options are not given

	cmd::store( cmd::parse_command_line( argc, argv, Description), Values );

	// Printing the description of the options if the help option is present

  if ( Values.count("help") > 0 )
  {
		std::cout << Description << std::endl;
		exit( EXIT_SUCCESS );
	}

	// If the day duration was given, the sunrise time and the sunset time
	// is read out and stored. Although it is unlikely that the sunset time will
	// be given before the sunrise time, it is still allowed as we make sure to
	// initialise the interval in the right order.

	if ( Values.count("SunDay") > 0 )
  {
		std::vector< CoSSMic::Time >
		SunTime( Values["SunDay"].as< std::vector< CoSSMic::Time > >() );

		if ( SunTime.size() == 2 )
			Day.assign( std::min( SunTime[0],  SunTime[1] ),
		              std::max( SunTime[0],  SunTime[1] ) );
		else
	  {
			std::cout << "The Sun Day duration option requires the time of sunrise "
							  << "and the time of sunset (two parameters) but "
				        << SunTime.size() << " where given" << std::endl;

      exit( EXIT_FAILURE );
		}
  }

  // The files for the producer and the consumer profiles must be given so
  // they are readily stored

  ProducerProfile  = Values["ProductionFile"].as< std::string >();
  ConsumerProfiles = Values["Consumers"].as< std::string >();

  // The result file is optional and stored if given

  if ( Values.count("AssignedTimes") > 0 )
	  Results = Values["AssignedTimes"].as< std::string >();

  // Setting the working directory if it was given, and changing the active
  // working director to the given path.

  if ( Values.count("Directory") > 0 )
  {
    WorkingDirectory = Values["Directory"].as< std::string >();

    if ( std::filesystem::is_directory( WorkingDirectory ) )
      std::filesystem::current_path( WorkingDirectory );
    else
    {
      std::cout << "The given working directory " << WorkingDirectory
                << " is not a directory!";
      exit( EXIT_FAILURE );
    }
  }

  // Verifying that the files exist or terminate with errors if not

  std::filesystem::path FileToCheck = ProductionFile();

  if ( !std::filesystem::exists( FileToCheck ) )
  {
    std::cout << "The production file " << FileToCheck
              << " does not exist!" << std::endl;

    exit( EXIT_FAILURE );
  }

  FileToCheck = ConsumersFile();

  if ( !std::filesystem::exists( FileToCheck ) )
  {
    std::cout << "The file " << FileToCheck << " with consumer information "
              << "does not exist";

    exit( EXIT_FAILURE );
  }

  // Is this right at the end or should it be before the directory reading?
	cmd::notify( Values );
}
