###############################################################################
#
# CoSSMic
#
# This file builds the distributed scheduler used in the CoSSMic project
#
# It has two main targets: 
#
#	Trial - which builds the stand alone version interfaced with XMPP
#       Simulation - which is allows the scheduler to be simulated from local 
# 	             event files. The prediction files normally contains 
#                    absolute time stamps, but this can conditionally be 
#                    changed to relative time stamps by invoking the make 
#                    file with the switch 
#                      make PREDICTION=-DRELATIVE_PREDICTION Simulator
#                    this will override the default prediction flag, which is 
#                    empty by default.
#
# The task scheduler depends on the following own projects
#
#	LA-Framework for the learning automata (LA) selection of producers
#	This framework is available (Mercurial - hg) from
#	https://bitbucket.org/GeirHo/la-framework
#
#	The Theron++ Actor framework available from (git)
#	https://github.com/GeirHo/TheronPlusPlus
#
# In addition the code uses the following libraries that are available as 
# standard from most Linux distributions
#
# Armadillo - http://arma.sourceforge.net/
# NLOpt     - https://nlopt.readthedocs.io/en/latest/
# Boost     - http://www.boost.org/
# GSL	    - https://www.gnu.org/software/gsl/
# GSL CBLAS - normally a part of GSL, but may need separate installation
# cURL      - https://curl.haxx.se/
#
# Since the CoSSMic task manager is separate from the scheduler, it sends 
# XMPP commands to the scheduler. The Swiften library is used for this 
# protocol and is also needed for the stand alone simulation since the 
# scheduler code is not changed for simulation.
#
# Swiften   - https://swift.im/swiften.html
#
# In addition Ben Strasser's CSV file parser is used and this can be found at
# This is a header-only library, which must be pulled as code and placed in a 
# directory at the same level as the CoSSMiC code (included via ../CSV)
#
# CSV       - https://github.com/ben-strasser/fast-cpp-csv-parser
#                   
# Author: Geir Horn, University of Oslo, 2014-2018
#
###############################################################################

#
# Setting the standard definitions
#

CC = g++
#CC = clang++
AR = ar
RM = rm -f

# These locations have been made relative to work independent of the different 
# machines used for this development

THERON = ../Theron
THERON_EXTENSIONS = ../Theron++
LA_FRAMEWORK = ../LA-Framework

# Optimisation -O3 is the highest level of optimisation and should be used 
# with production code. -Og is the code optimising and offering debugging 
# transparency and should be use while the code is under development

OPTIMISATION_FLAG = 

# The prediction flag is used to control whether a prediction file will contain
# absolute time stamp values or time stamp values relative to the current time 
# The default is absolute prediction

PREDICTION ?= -DABSOLUTE_PREDICTION

# It is useful to let the compiler generate the dependencies for the various 
# files, and the following will produce .d files that can be included at the 
# end. The -MMD flag is equivalent with -MD, but the latter will include system 
# headers in the output (which we do not need here). The -MP includes an 
# empty rule to create the dependencies so that make would not create any errors 
# if the file name changes.

DEPENDENCY_FLAGS = -MMD -MP

# Package specific options. The Glib flags are inherited from the Theron 
# makefile, and may not be necessary (seems to affect thread scheduling)

#THERON_FLAGS =  -DTHERON_POSIX=0 -DTHERON_BOOST=0 -DTHERON_CPP11=1 
ARMADILLO_FLAGS = -DARMA_USE_CXX11
GLIB_FLAGS = -D_GLIBCXX_USE_NANOSLEEP -D_GLIBCXX_USE_SCHED_YIELD -DHAVE_INLINE

# The following general option could go before -std=c++11 
# but it is really not needed: -pthread
# The format-truncation is necessary to prevent warnings from the CSV parsing 
# library that uses what g++7 considers too short buffers.
GENERAL_OPTIONS = -c -Wall -std=c++1z -ggdb -D_DEBUG -Wformat-truncation=0
INCLUDE_DIRECTORIES = -I. -I/usr/include -I ${LA_FRAMEWORK}  \
		      -I $(THERON_EXTENSIONS) -I ../CSV

# Libraries used - note that the Theron lib should be added to the link command
# later when the code uses it. The code uses the GNU Scientific Library (GSL)
# and the math library (m).

MACHINE := $(shell uname --nodename)
ifeq ($(MACHINE),Glamdring)
  NLOPT_LIB = -lgslcblas -lnlopt_cxx
else
  NLOPT_LIB = -lgslcblas -lnlopt
endif

#First of the general libraries can be -lpthread but it is better to use native
# C++ threads
#THERON_LIB = ${THERON}/Lib/libtherond.a
LIBRARIES = -lgsl $(NLOPT_LIB) -lcurl -lm 

# Putting it together as the actual options given to the compiler and the 
# linker

CFLAGS = $(DEPENDENCY_FLAGS) $(OPTIMISATION_FLAG) $(GENERAL_OPTIONS) \
	 $(GLIB_FLAGS) $(THERON_FLAGS) $(ARMADILLO_FLAGS) -DCoSSMic_DEBUG \
	 $(PREDICTION)
         
LDFLAGS = -Wl,--allow-multiple-definition -ggdb -D_DEBUG -pthread

# Communication headers are used if the actors should have transparent external
# messaging with actors on other network nodes (Endpoints). 

THERON_EXTENSION_HEADERS = LinkMessage.hpp NetworkLayer.hpp \
			   SessionLayer.hpp PresentationLayer.hpp\
			   ConsolePrint.hpp EventHandler.hpp
THERON_EXTENSION_SOURCE  = $(THERON_EXTENSIONS)/ConsolePrint.cpp \
			   $(THERON_EXTENSIONS)/EventHandler.cpp \
			   $(THERON_EXTENSIONS)/NetworkEndPoint.cpp
THERON_EXTENSION_OBJECTS = ${THERON_EXTENSION_SOURCE:.cpp=.o}
			 
# These generic mechanisms are extended by the XMPP protocol mechanism which 
# is both a header file and an objective file.

XMPP_OBJECTS = $(THERON_EXTENSIONS)/XMPP.o
XMPP_LIBS    = -lSwiften -lboost_system -lboost_signals
			    
# There are several CoSSMic actors that are needed

CoSSMic_ACTOR_HEADERS = ActorManager.hpp ConsumerAgent.hpp ConsumerProxy.hpp \
	Producer.hpp PVProducer.hpp Predictor.hpp NetworkInterface.hpp \
	Grid.hpp Clock.hpp RewardCalculator.hpp ShapleyReward.hpp

# Since each of these corresponds to a source file, the set of source files 
# can easily be constructed, and also the objectives

CoSSMic_ACTOR_SOURCE  = ${CoSSMic_ACTOR_HEADERS:.hpp=.cpp}
CoSSMic_ACTOR_OBJECTS = ${CoSSMic_ACTOR_SOURCE:.cpp=.o}
	
# There is a set of header files which contain both in-line code and templates
# so if any of these changes, it is necessary to recompile most of the modules
# even though the lines compiling the modules often explicitly identifies the
# dependency for that particular module.

CoSSMic_UTILITIES_HEADERS = Interpolation.hpp TimeInterval.hpp

# This project consists of several modules that will be compiled individually 
# and linked together with the application

EXTRA_MODULES = Interpolation.o ${LA_FRAMEWORK}/RandomGenerator.o

# Finally we can form the full set of objective functions for the linker

ALL_MODULES = $(CoSSMic_ACTOR_OBJECTS) $(THERON_EXTENSION_OBJECTS) \
              $(XMPP_OBJECTS) $(EXTRA_MODULES)

#
# TARGETS
#
#
# Build the individual modules
#
# Compile targets

%.o : %.cpp
	$(CC) $(CFLAGS) $< -o $@ $(INCLUDE_DIRECTORIES)

#
# TARGETS Linking the modules
#
# MAIN target to be used if not testing
# 
# CoSSMic: $(CoSSMic_ACTOR_OBJECTS) $(ALL_MODULES) $(XMPP_OBJECTS) CoSSMic.o
# 	$(CC) $(LDFLAGS) $(ALL_MODULES) CoSSMiC.o $(THERON_LIB) $(XMPP_LIBS) $(LIBRARIES) -o CoSSMic
# 	
#
# TESTS to be used for various purposes
#

Trial: $(CoSSMic_ACTOR_OBJECTS) $(ALL_MODULES) $(XMPP_OBJECTS) Tests/Trial.o $(THERON_EXTENSIONS)/Actor.o
	$(CC) $(LDFLAGS) $(ALL_MODULES) Tests/Trial.o $(THERON_EXTENSIONS)/Actor.o $(XMPP_LIBS) $(LIBRARIES) -o CoSSMic

Simulator: $(CoSSMic_ACTOR_OBJECTS) $(ALL_MODULES) $(XMPP_OBJECTS) Tests/Simulator.o $(THERON_EXTENSIONS)/Actor.o
	$(CC) $(LDFLAGS) $(ALL_MODULES) Tests/Simulator.o $(THERON_EXTENSIONS)/Actor.o $(XMPP_LIBS) $(LIBRARIES) -o CoSSMic

WaitTest: Tests/WaitTest.o $(THERON_EXTENSIONS)/Actor.o
	$(CC) $(LDFLAGS) Tests/WaitTest.o $(THERON_EXTENSIONS)/Actor.o $(XMPP_LIBS) $(LIBRARIES) -o WaitTest

ScheduleTest: $(CoSSMic_ACTOR_OBJECTS) $(ALL_MODULES) $(XMPP_OBJECTS) Tests/ScheduleTest.o
	$(CC) $(LDFLAGS) $(ALL_MODULES) Tests/ScheduleTest.o $(THERON_LIB) $(XMPP_LIBS) $(LIBRARIES) -o CoSSMic

XMPPTest:   $(XMPP_COMMUNICATION) $(XMPP_OBJECTS) XMPPTest.o
	$(CC) $(LDFLAGS) $(XMPP_OBJECTS) test.o $(THERON_LIB) $(XMPP_LIBS) $(LIBRARIES) -o Test

clean:
	${RM} *.o
	${RM} Tests/*.o
	${RM} ${LA_FRAMEWORK}/*.o
	$(RM) ${THERON_EXTENSIONS}/*.o
	${RM} *.d
	${RM} Tests/*.d
	${RM} ${LA_FRAMEWORK}/*.d
	$(RM) ${THERON_EXTENSIONS}/*.d
	${RM} *.a
	${RM} *.pdb
	${RM} CoSSMic

#
# DEPENDENCIES
#

-include $(ALL_MODULES:.o=.d)
