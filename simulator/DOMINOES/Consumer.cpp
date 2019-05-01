/*==============================================================================
Consumer

This is the implementation of the Consumer class.

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#include <map>                                // Standard map
#include <filesystem>                         // Paths to files
#include "TimeInterval.hpp"                   // The time concept
#include "Consumer.hpp"                       // Class definition
#include "CSVtoTimeSeries.hpp"                // To read CSV files

/*==============================================================================

 Message handlers

==============================================================================*/
//
// The first message handler takes the file name, parses the CSV file and
// produces the interpolation.

void Dominoes::Consumer::ReadLoad( const std::filesystem::path & FileName,
                                   const Theron::Address Sender )
{
  std::map< CoSSMic::Time, double >
  LoadProfile( CoSSMic::CSVtoTimeSeries( FileName ) );

  ConsumptionDuration = LoadProfile.rbegin()->first;
  Energy              = std::make_unique< Interpolation >( LoadProfile );
}

// The second message handler is more complex as it returns a vector the energy
// the consumes needs between two sample times of the production profile. Hence,
// the power can be estimated by dividing the returned dE on the dt as the
// sample time of the production.
//
// The returned energy consumption vector has the same size as the production
// vector. This vector is initialized to zero energy, and this energy is kept
// for all elements before the assigned start time when the production sample
// times are evaluated one by one. After the assigned start time, the
// cumulative energy of the load profile is computed and the cumulative energy
// of the previous time stamp is subtracted from this. The delta energy is then
// stored in the vector, and the current cumulative energy is remembered for
// the next time step. The scan of the production sample times stops as soon
// as the time stamp is larger than the end of the consumption profile.

void Dominoes::Consumer::Consumption( const CoSSMic::Time & AssignedStartTime,
                                      const Theron::Address Solver )
{
	std::vector< double > dE( ProductionSamples->size(), 0.0 );
  double PastCumulativeEnergy = 0.0;
	auto CurrentConsumption = dE.begin();
	CoSSMic::Time ConsumptionEnd = AssignedStartTime + ConsumptionDuration;

	for ( const CoSSMic::Time TimeStamp : (*ProductionSamples) )
		if ( TimeStamp < AssignedStartTime )
			++CurrentConsumption;
		else if ( TimeStamp <= ConsumptionEnd )
		{
			double CumulativeEnergy  = Energy->operator()(
             boost::numeric_cast<double>( TimeStamp - AssignedStartTime ) );
			*CurrentConsumption  = CumulativeEnergy - PastCumulativeEnergy;
      PastCumulativeEnergy = CumulativeEnergy;
			++CurrentConsumption;
		}
		else break;

	Send( dE, Solver );
}

// The message handler to compute the time coverage of the consumer is trivial

void Dominoes::Consumer::ComputeCoverage(
     const Dominoes::Consumer::TimeCoverageRequest & TheRequest,
     const Theron::Actor::Address Solver)
{
  Send( TimeCoverage( StartInterval.lower(),
                      StartInterval.upper() + ConsumptionDuration ), Solver );
}


/*==============================================================================

 Constructor

==============================================================================*/
//
// The Constructor stores the start interval and the production sample time
// pointer, and sends a message to start the parsing of the consumption file.

Dominoes::Consumer::Consumer( const std::string & ID,
     CoSSMic::Time EarliestStart, CoSSMic::Time LatestStart,
     const std::filesystem::path & FileName,
		 const Dominoes::SampleTime SampleProductionTimes )
: Theron::Actor( ID ), StartInterval( EarliestStart, LatestStart ),
  Energy(), ConsumptionDuration(0),
  TimeOrigin( EarliestStart ), ProductionSamples( SampleProductionTimes )
{
  RegisterHandler( this, &Consumer::ReadLoad        );
  RegisterHandler( this, &Consumer::Consumption     );
  RegisterHandler( this, &Consumer::ComputeCoverage );

  Send( FileName, GetAddress() );
}
