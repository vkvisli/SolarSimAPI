###############################################################################
#
# The LA framework
#
# This file makes compiles the files for the LA framework making sure that
# the framework files are compiled as necessary.
# 
# Author: Geir Horn, 2013-2018
#
###############################################################################

#
# Setting the standard definitions
#

CC = g++
AR = ar
RM = rm -f

# These locations have been made relative to work independent of the different 
# machines used for this development. Note that the LA framework will be 
# located differently during a real deployment.

THERON ?= ../Theron++
LA_FRAMEWORK ?= .

# These frameworks contain certain object files that must be built if needed

THERON_OBJECTS = $(THERON)/Actor.o
LA_OBJECTS = ${LA_FRAMEWORK}/RandomGenerator.o

# Optimisation -O3 is the highest level of optimisation and should be used 
# with production code. -Og is the code optimising and offering debugging 
# transparency and should be use while the code is under development

OPTIMISATION_FLAG = -Og

# It is useful to let the compiler generate the dependencies for the various 
# files, and the following will produce .d files that can be included at the 
# end. The -MMD flag is equivalent with -MD, but the latter will include system 
# headers in the output (which we do not need here). The -MP includes an 
# empty rule to create the dependencies so that make would not create any errors 
# if the file name changes.

DEPENDENCY_FLAGS = -MMD -MP

# The Armadillo library should be compiled to use C++11 feature (although this 
# should be the default by now)

ARMADILLO_FLAGS = -DARMA_USE_CXX11

# Then the compiler flags can be set 

GENERAL_OPTIONS = -c -Wall -std=c++1z -ggdb -D_DEBUG -Wformat-truncation=0 -Wno-sign-compare
CFLAGS = $(GENERAL_OPTIONS) $(DEPENDENCY_FLAGS) $(OPTIMISATION_FLAG) $(ARMADILLO_FLAGS)
INCLUDE_FLAGS = -I. -I/usr/include -I${LA_FRAMEWORK} -I${THERON}

# Libraries used: The code uses the GNU Scientific Library (GSL)
# and the math library (m), and the nlopt library

MACHINE := $(shell uname --nodename)
ifeq ($(MACHINE),Glamdring)
  NLOPT_LIB ?= -lgslcblas -lnlopt_cxx
else
  NLOPT_LIB ?= -lgslcblas -lnlopt
endif

# There are some boost linker libraries to be used

BOOST_LIBRARIES = -lboost_program_options -lboost_system

# The linker will need the libraries used

LDFLAGS = -Wl,--allow-multiple-definition ${BOOST_LIBRARIES} -ggdb -lgsl ${NLOPT_LIB} -lm

#
# FRAMEWORK files
#

LA_FRAMEWORK_HEADERS = \
	${LA_FRAMEWORK}/AssignmentAutomata.hpp \
	${LA_FRAMEWORK}/ConvergeAutomata.hpp \
	${LA_FRAMEWORK}/EmpiricalDensityFunction.hpp \
	${LA_FRAMEWORK}/EstimatorAutomata.hpp \
	${LA_FRAMEWORK}/LearningAutomata.hpp \
	${LA_FRAMEWORK}/LearningEnvironment.hpp \
	${LA_FRAMEWORK}/LinearLA.hpp \
	${LA_FRAMEWORK}/ProbabilityMass.hpp \
	${LA_FRAMEWORK}/PursuitAutomata.hpp \
	${LA_FRAMEWORK}/RandomGenerator.hpp \
	${LA_FRAMEWORK}/RewardEstimators.hpp \
	${LA_FRAMEWORK}/SModelNormalisation.hpp \
	${LA_FRAMEWORK}/Star.hpp \
	${LA_FRAMEWORK}/StatisticalAutomata.hpp \
	${LA_FRAMEWORK}/StatisticalEnvironment.hpp \
	${LA_FRAMEWORK}/VariableActionSet.hpp

LA_FRAMEWORK_SOURCE = ${LA_FRAMEWORK}/RandomGenerator.cpp 
	
#
# TARGETS
#

clean:
	${RM} *.o
	${RM} *.d
	${RM} *.a
	${RM} *.pdb
	${RM} ${LA_FRAMEWORK}/*.o
	${RM} ${LA_FRAMEWORK}/Tests/*.o
	${RM} ${LA_FRAMEWORK}/Tests/*.d
	$(RM) ${THERON}/*.o
	${RM} ${LA_FRAMEWORK}/*.d
	$(RM) ${THERON}/*.d

# Generic compile targets

%.o : %.cpp ${LA_FRAMEWORK_HEADERS} ${LA_FRAMEWORK_SOURCE}
	$(CC) $(CFLAGS) $< -o $@ $(INCLUDE_FLAGS)

#
# Test targets
#

Statistical: ${LA_FRAMEWORK_HEADERS} Tests/Statistical.o ${LA_OBJECTS}
	$(CC) Tests/Statistical.o ${LA_OBJECTS} $(LDFLAGS) $(LD_LIBS) -o Tests/Statistical

#
# DEPENDENCIES
#

-include $(ALL_MODULES:.o=.d)


