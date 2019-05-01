/*==============================================================================
Solver

The solver class defines the boundary constraints and the objective function
for the optimizer. The objective function is the integral between the local
energy production and the total consumption. It is positive if the building
needs to take energy from the grid, and negative if it exports energy to the
grid. The objective function is therefore a standard minimisation function.

The actual optimization is done by the Bound Optimization by Quadratic
Approximations (BOBYQA) algorithm [1].

References:
[1] M. J. D. Powell, "The BOBYQA algorithm for bound constrained optimization
    without derivatives," Department of Applied Mathematics and Theoretical
    Physics, Cambridge England, technical report NA2009/06, 2009.

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#ifndef DOMINOES_SOLVER
#define DOMINOES_SOLVER

// Standard headers
#include <list>                              // Storing consumers
#include <filesystem>                        // File names

// Actor framework
#include "Actor.hpp"                         // The Theron++ actor framework

// The optimization algorithm
#include "NonLinear/Algorithms.hpp"          // The algorithms
#include "NonLinear/Optimizer.hpp"           // The solver
#include "NonLinear/LocalApproximation.hpp"  // The BOBYAQA interface

// The CoSSMic Time concept
#include "TimeInterval.hpp"                  // Time

// The Dominoes consumer class
#include "Typedefs.hpp"                      // Dominoes types
#include "Consumer.hpp"                      // Definition of consumers

namespace NL = Optimization::NonLinear;

namespace Dominoes {

class Solver
: public NL::Optimizer< NL::Algorithm::Local::Approximation::Rescaling >
{
private:

  // The solver has a list of consumers. These are owned by the solver
  // because it is necessary to know the number of consumers and to be able
  // to directly deal with them. As the objects are contained as elements a
  // vector cannot be used as it requires the elements to be copyable.

  std::list< Consumer > Consumers;

  // The solver also needs to keep the vector of sample times. The actual energy
  // production will be stored in the energy objective object and is not needed
  // outside of that object.

  SampleTime ProductionSamples;

  // ---------------------------------------------------------------------------
  // Consumption Receiver
  // ---------------------------------------------------------------------------
  //
  // The solver uses a Receiver object to ensure that the objective function
  // waits for the energy consumption from all the consumers. When a profile
  // comes back from a consumer, it will add it to the global consumption
  // profile.
  //
  // The class stores a shared pointer to the production sample times as
  // that represents the abscissa for the net energy of the building.  Since
  // there is no storage of produced energy, only the energy produced over a
  // sample period is compared with the total consumption in that interval.
  // The former is subtracted from the latter and if the total consumption is
  // larger than the production the difference will be considered as the
  // energy the building needs to buy in that time step.

private:

  class EnergyObjective : public Theron::Receiver
  {
  private:

    std::vector< double > IntervalProduction, TotalConsumption;
    const SampleTime      ProductionSamples;

		// The index type is defined for the above vectors

		using Index = std::vector< double >::size_type;

    // There is a handler to collect the received consumptions received from
    // the consumers.

    void SingleConsumption( const std::vector< double > & ConsumptionValues,
                            const Theron::Address TheConsumer );

		// There is a handler for the time coverage messages returned from the
		// consumers. If the coverage of a consumer falls outside of the time
		// interval defined by the production samples, zero production samples
		// will be added until the time coverage of all consumers will be covered.

		void ExtendTimeAxis( const Consumer::TimeCoverage & ConsumerCoverage,
												 const Theron::Address TheConsumer );

  public:

		// The time coverage is requested from the consumers by a small helper
		// function invoked on each consumer once they have been created in the
		// solver's constructor.

		void RequestConsumptionCoverage( const Consumer & TheConsumer );

    // The production values will typically be read after this class has been
    // initialised, and therefore they should be explicitly given from the
    // Solver constructor. They will then be stored with negated values to
    // avoid recomputing every time the objective function should be calculated

    void SetProductionValues( const std::vector< double > & Values );

    // The value of the objective function can then be directly provided by
    // interpolating and integrating the grid energy vector.

    double Value( void );

    // The reset function is used to set all elements in the total consumption
    // vector to zero.

    void Reset( void );

    // The constructor simply initialises the message handler and the net
    // energy vector requiring that the initialise function should be used
    // prior to each objective value to compute.

    EnergyObjective( const SampleTime & ProductionTimes );
    EnergyObjective( void ) = delete;

  } EnergyCost;

  // ---------------------------------------------------------------------------
  // Optimisation problem
  // ---------------------------------------------------------------------------
  //
  // The objective function will reset the energy cost and then send the
  // assigned start times to the consumers and wait for all of them to report
  // back their consumption at the production sample times, and then return
  // the final energy cost when all consumers are done.

  virtual Optimization::VariableType
	ObjectiveFunction( const Optimization::Variables & VariableValues ) override;

  // There is a function to return a vector of bound constraints based on the
  // earliest and latest allowed start time.

  virtual std::vector< Interval > BoundConstraints( void ) override;

  // ---------------------------------------------------------------------------
  // Optimizing
  // ---------------------------------------------------------------------------
  //
  // The main goal of the optimisation problem is to assign start times for
  // each consumer within their individual start times to minimise the use of
  // grid energy. The optimisation problem is solved and the output written
  // to the file whose name is given as argument. The file is created and if
  // it already exist, it will be overwritten. The file will contain two
  // columns: The first is the consumer ID and the second is the assigned start
  // time in POSIX seconds. It is separated out as a dedicated file although
  // the operations could have been directly performed from the constructor.
	//
	// It takes an optional time interval as argument representing the duration
	// of the solar day from sunrise to sunset. The argument is that if the
	// allowed start time interval for a consumer is wide, then parts of that
	// interval may fall outside of the solar day. Then if the initial starting
	// time is drawn in the part of the start time interval outside of the solar
	// day it may take too many iterations for the solver to try a start time
	// within the solar day. The the initial value of the start time for a
	// consumer is therefore confined to the convex hull of the solar day and
	// the allowed start time interval. If no solar day is specified, a random
	// value over the start time interval will be used as initial guess for the
	// consumer's start time

public:

  void AssignStartTimes( const std::filesystem::path & ASTFile,
			 const CoSSMic::TimeInterval & SolarDay = CoSSMic::TimeInterval() );

private:

  // ---------------------------------------------------------------------------
  // Constructor & destructor
  // ---------------------------------------------------------------------------
  //
  // The constructor takes the name of the CVS file giving the produced energy
  // over the full day, and the name of the CSV file containing all the consumer
  // events. It first initialises the production time series vectors, and then
  // creates all the consumers.

public:

  Solver( const std::filesystem::path ProducerFile,
          const std::filesystem::path ConsumerEvents );
  Solver( void ) = delete;
  Solver( const Solver & Other ) = delete;

  // The destructor closes all the consumers. It is virtual to ensure that
  // all base classes properly destruct.

  virtual ~Solver( void );

};

}      // Name space Dominoes
#endif // DOMINOES_SOLVER
