###############################################################################
#
# The Dominoes solver
#
# This file makes compiles the files for the Dominoes solver. The dependencies 
# of the project that must be cloned from the following respositories and 
# placed in directories at the same level as the Dominoes:
#
# Theron++     : git clone https://github.com/GeirHo/TheronPlusPlus.git
# CoSSMic      : hg clone https://bitbucket.org/cossmic/taskscheduler/src/C++17%20Release/
# Optimization : git clone https://github.uio.no/geirho/Optimization.git
# CSV          : git clone https://github.com/ben-strasser/fast-cpp-csv-parser.git
# LA-Framework : hg clone https://bitbucket.org/GeirHo/la-framework/src/default/
#
# The Optimization is a safe and modern C++ extension to the NLopt package 
# which can be installed from the standard repositories of most Linux  
# distributions or downloaded from https://nlopt.readthedocs.io/en/latest/
#
# The Armadillo matrix library is needed for representing constraints. It 
# should be available with the Linux distribution, or downloaded from 
# http://arma.sourceforge.net/
#
# A recent version of Boost is also needed, and this should also be available 
# for an install in most Linux versions. Remember to install the boost-devel 
# package for Fedora. 
#
# The interpolation algorithms uses the GNU Scientific Library (GSL) that is 
# available for installation for all Linux versions or can be 
# downloaded from https://www.gnu.org/software/gsl/
# 
# Author: Geir Horn, University of Oslo, 2019
# License: LGPL 3.0
#
###############################################################################

#
# Setting the standard definitions
#

CC = g++
# CC = clang
AR = ar
RM = rm -f

# These locations have been made relative to work independent of the different 
# machines used for this development. 

THERON  ?= ../Theron++
CoSSMic ?= ../CoSSMic
Optimization ?= ../Optimization
LAFramework ?= ../LA-Framework

# These frameworks contain certain objective functions that must be built 
# as part of the built process for the solvers.

THERON_OBJECTS  = $(THERON)/Actor.o
CoSSMic_OBJECTS = $(CoSSMic)/CSVtoTimeSeries.o $(CoSSMic)/Interpolation.o
LA_OBJECTS = $(LAFramework)/RandomGenerator.o

# Optimisation -O3 is the highest level of optimisation and should be used 
# with production code. -Og is the code optimising and offering debugging 
# transparency and should be use while the code is under development
# Since the code is templated, it is recommended to not use any optimisation 
# while testing the code

OPTIMISATION_FLAG = 

# It is useful to let the compiler generate the dependencies for the various 
# files, and the following will produce .d files that can be included at the 
# end. The -MMD flag is equivalent with -MD, but the latter will include system 
# headers in the output (which we do not need here). The -MP includes an 
# empty rule to create the dependencies so that make would not create any errors 
# if the file name changes.

DEPENDENCY_FLAGS = -MMD -MP

# GSL requires compiler options in order to allow standard C++ exceptions in 
# user defined functions called from the library.

GSL_OPTIONS = -fexceptions

# General Options 

GENERAL_OPTIONS = -c -Wall -std=c++1z -ggdb -D_DEBUG -Wformat-truncation=0 -Wno-sign-compare -Wno-deprecated-declarations
INCLUDE_DIRECTORIES = -I. -I/usr/include -I$(THERON) -I$(Optimization) -I$(CoSSMic) -I$(LAFramework) -I../CSV 

# Then the flags for the compiler can be defined

CFLAGS = $(DEPENDENCY_FLAGS) $(OPTIMISATION_FLAG) $(GSL_OPTIONS) $(GENERAL_OPTIONS) 

#
# LINKER LIBRARIES
#
# Libraries used: The code uses the GNU Scientific Library (GSL)
# and the math library (m), and the nlopt library, anb boost

MACHINE := $(shell uname --nodename)
ifeq ($(MACHINE),Glamdring)
  SCIENCE_LIB ?= -lgsl -lgslcblas -lnlopt_cxx -lboost_system -lm
else
  SCIENCE_LIB ?= -lgsl -lgslcblas -lnlopt_cxx -lboost_system -lm
endif

# The standard file system requires certain libraries to link properly and 
# the name of these libraries depends on the compiler used.

ifeq ($(CC),clang)
  FILESYSTEM_LIB = -lc++fs
else
  FILESYSTEM_LIB = -lstdc++fs
endif

# The Boost command line options parser is also used, and this requires a 
# separate library to be linked.

BOOST_OPTIONS_LIB = -lboost_program_options

# The linker needs a separate set of flags and libraries. Note that the 
# Google protocol buffers library is given directly here.

LDFLAGS = -Wl,--allow-multiple-definition -pthread -ggdb -D_DEBUG
LD_LIBS = ${COM_LIB} ${FILESYSTEM_LIB} ${BOOST_OPTIONS_LIB} ${SCIENCE_LIB}

#
# SOLVER MODULES
#
# The compiled files that are needed by the linker in order to build the 
# solver executable.

SOLVER_OBJECTS = Consumer.o Solver.o CommandOptions.o main.o

# And these are needed to build the various targets

ALL_MODULES = $(THERON_OBJECTS) $(LA_OBJECTS) $(CoSSMic_OBJECTS) $(SOLVER_OBJECTS)

#
# TARGETS
#

clean:
	${RM} *.o
	${RM} *.d
	${RM} *.a
	${RM} *.pdb
	$(RM) ${THERON}/*.o
	$(RM) ${THERON}/*.d
	$(RM) ${CoSSMic}/*.o
	$(RM) ${CoSSMic}/*.d
	$(RM) ${LAFramework}/.o
	$(RM) ${LAFramework}/.d
	$(RM) Simulator

# Generic compile targets

%.o : %.cpp
	$(CC) $(CFLAGS) $< -o $@ $(INCLUDE_DIRECTORIES)

#
# Main target
#

Simulator: ${ALL_MODULES}
	$(CC) ${ALL_MODULES} $(LDFLAGS) $(LD_LIBS) -o Simulator

#
# DEPENDENCIES
#

-include $(ALL_MODULES:.o=.d)
