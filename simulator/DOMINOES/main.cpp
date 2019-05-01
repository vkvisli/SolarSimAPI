/*==============================================================================
DOMINOES simulator

The DOMINIOES simulator implements an optimiser for the a set of shiftable
electricity consumers getting energy from a single renewable producer. Each
consumer provides a time window for which it can start. The task is then to
assign a start time for each of the consumers in order to minimise the energy
that has to be bought from the electricity grid.

The optimiser uses a direct approach: Each consumer comes with a CSV file
stating the cumulative consumption as a relative time series from time zero.
The consumption profile is kept interpolated, and once the start times has
been assigned, the consumer produces a time series of real time consumptions
sampled at the times for which there are production data available. These real
time productions are then added together as the positive total consumption, and
the production at the same time stamps is subtracted to calculate the net grid
energy. The objective function to be minimised is then the integral of the
interpolated net energy profile for a particular start time assingment.

Since the start times of the consumers are bound to their respective start time
windows, and there are no other constraints on the problem, the optimization is
done by using the NLOpt [1] implementation of the Bound Optimization by
Quadratic Approximations (BOBYQA) algorithm [2].

The makefile provides the information about dependencies of the project and
how to install these prior to running the code. In particular, it should be
noted that the code uses the Theron++ actor framework [3] allowing each consumer
and the solver's computation of the objective function to run in separate safe
threads.

The Command Options header documents the supported command line options, or a
summary can be obtained from using 'Simulator --help'. An example could be

Simulator --ProductionFile PV.csv --Consumers ConsumerEvents.csv
          --Directory ./Data --AssignedTimes Results.csv

where the production file gives the cumulative production in absolute time:
<POSIX seconds>, <cummulative energy produced>
and the consumer event file has lines of the following format:
<Consumer ID string>, <Earliest start in POSIX seconds>,
<Latest start in POSIX seconds>, <CSV Consumption profile file name>
All the data files should be contained in the working directory (here ./Data),
and the assigned start times will be written to a results file in the working
directory, here Results.csv in the following format:
<Consumer ID string>, <Assigned start time in POSIX seconds>

References:
[1] Steven G. Johnson: The NLopt nonlinear-optimization package,
    http://ab-initio.mit.edu/nlopt
[2] M. J. D. Powell, "The BOBYQA algorithm for bound constrained optimization
    without derivatives," Department of Applied Mathematics and Theoretical
    Physics, Cambridge England, technical report NA2009/06, 2009.
[3] Geir Horn, Theron++, https://github.com/GeirHo/TheronPlusPlus

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#include "CommandOptions.hpp"    // The command line options
#include "Solver.hpp"            // The Solver (keeps the Consumers)

#include <iostream>

int main( int argc, char **argv )
{
  // Parsing the command line options

  Dominoes::CommandLineOptions Options( argc, argv );

  // Starting the solver that starts the consumers

  Dominoes::Solver Solver( Options.ProductionFile(), Options.ConsumersFile() );

  // Finding a solution

  Solver.AssignStartTimes( Options.ResultFile(), Options.DayDuration() );

  // There is always a happy ending

  return EXIT_SUCCESS;
}
