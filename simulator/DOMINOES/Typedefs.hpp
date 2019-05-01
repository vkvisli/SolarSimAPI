/*==============================================================================
Type definitions

The purpose of this file is to group all type definitions in one place to
ensure that all classes uses the same definitions and so that they are not
depending on including other class definitions that may not be needed.

Author and Copyright: Geir Horn, 2019
License: LGPL 3.0
==============================================================================*/

#ifndef DOMINOES_TYPES
#define DOMINOES_TYPES

#include <vector>             // Standard vectors
#include <memory>             // For smart pointers

#include "TimeInterval.hpp"   // The CoSSMic Time concept

namespace Dominoes {
// The sample time is defined as a smart pointer to a standard vector of 
// time points
	
using SampleTime =  std::shared_ptr< std::vector< CoSSMic::Time > >;

}


#endif // DOMINOES_TYPES
