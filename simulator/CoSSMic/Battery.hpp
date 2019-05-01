/*=============================================================================
  Battery

  The battery model is fundamentally a producer that aims at maintaining its 
  state of charge (SOC) at 100%. Once the SOC drops below 100% it will ask the
  Actor Manager to create an associated Battery Consumer to source energy to 
  recharge up to 100%. The Battery Consumer is special in that it will not 
  charge energy from other batteries or the grid, and the battery will only 
  have one active consumer at any time.
  
  When a battery receives the first request for energy, it will compute the 
  estimated state of charge when this request has been serviced, and if the 
  battery has sufficient energy the request it accepted. The battery then 
  immediately sets up its consumer to source this energy over an interval 
  starting at the current time and ending 24 hours into the future. This latest
  start time is set in this way since batteries will typically be discharged 
  during night where there is little PV production, and hence it can well be 
  that the charging will only happen the next morning.
  
  If more requests are coming, they are accepted to the extent the battery has 
  enough energy to serve the combined demand of the requests. Whenever a request
  is accepted, the new anticipated SOC at the end of the last request will be 
  sent to the Battery Consumer. This will be used by the Battery Consumer to 
  extend the charging period, if the charging has not already started. As soon
  as the Battery Consumer receives a start time for the charging, it returns 
  the starting time and the duration of the charging to the battery.
  
  It is assumed that the charging will happen at maximum charging current for 
  a given amount of time. Physically, the battery cannot charge or discharge 
  simultaneously, however the battery could in principle just send out the 
  charging current to the load it serves. Consequently, only the net flow of 
  energy is considered when computing the state of charge.
  
  The state of charge given a net current flow is computed according to 
  Tremblay's and Dessaint's model [1]. Since the model is slightly different 
  for the type of battery: lead-acid, Li-Ion; and NiMH and NiCd. The standard
  battery model will therefore defer the actual formulas to relevant subclasses
  for the different types of batteries.
    
  There are fundamentally a few events that must be considered:
  
  A. The battery starts delivering energy to a new consumer. This is the 
     assigned start time of that consumer.
  B. The battery stops delivering energy to a consumer. This is the time when 
     the consumer is killed by the Task Manager. 
  C. The battery starts to receive power from a remote source. This is the 
     assigned start time for the battery's consumer agent.
  D. The battery stops receiving power from the remote source. This is when 
     its consumer agent is killed by the task manager. 
     
  At each of these events the battery voltage can be computed. Let I(t) be the 
  net current flow out of the battery. This implies that the current is negative
  when the battery charges. The battery voltage at an event time T is then 
  given as 
  
  V(T) = V0 - K * Q * Integral[I(t),0,T]/(Q - Integral[I(t),0,T]) - R * I(T) 
         + A * exp( -B * Integral[I(t),0,T] ) 
         - K * Q * I'(T) / (Q - Integral[I(t),0,T])
        
  It is worth noting that since the battery starts full, the integral of the 
  current should be zero when the battery returns to full. The objective of 
  the battery control is to bring this voltage to the nominal voltage of the 
  battery.
  
  The factor I'(t) is a first order low pass filtered version of the input 
  current. Such a filter is defined by the differential equation. 
  
  I(t) - I'(t) = C * dI'(t)/dt
  
  where C is the time constant of the filter. Tremblay and Dessaint argue
  that this must be measured for each battery, "however, experimental data has
  shown a time constant of about 30s"
  
    
  REFERENCES:
  
  [1] Olivier Tremblay and Louis-A. Dessaint (2009): "Experimental Validation 
      of a Battery Dynamic Model for EV Applications", Proceedings of the 
      International Battery Hybrid and Fuel Cell Electric Vehicle Symposium 
      (EVS24), 13-16 May in Stavanger, Norway, in the World Electric Vehicle 
      Journal, Vol. 3, pp. 289-298, ISSN 2032-6653
  
  Author: Geir Horn, University of Oslo, 2016
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/

#ifndef COSSMIC_BATTERY
#define COSSMIC_BATTERY

#include <list>			 							// The standard list

#include "Actor.hpp"	 						// The Theron++ actor framework
#include "StandardFallbackHandler.hpp"
#include "Interpolation.hpp"     	// The interpolated net current
#include "PresentationLayer.hpp" 	// De-serializing Actor

#include "Interpolation.hpp"	 		// The interpolated function
#include "TimeInterval.hpp"	 			// CoSSMic time and intervals
#include "Producer.hpp"		 				// Generic CoSSMic producer


// All the code specific to the CoSSMic project belongs to the CoSSMic name 
// space

namespace CoSSMic
{

/*****************************************************************************
 Generic Battery Model
******************************************************************************/
// The base class battery model is a producer and for clarity the virtual 
// inheritance of the actor and the de-serialising actor is explicit.

class Battery : virtual public Theron::Actor,
								virtual public Theron::StandardFallbackHandler,
				        virtual public Theron::DeserializingActor,
								virtual public Producer
{

	// ---------------------------------------------------------------------------
  // Type checking
  // ---------------------------------------------------------------------------
  //
	// The battery supports the producer type naming conventions allowing the 
	// class type to be encoded in the names of the instantiated actors. For this 
	// a base name is defined, and a function to test if this base name is 
	// part of a name string.
	
private:
	
  constexpr static auto BatteryNameBase = "battery";
  
 	static bool TypeName( const std::string & ActorName )
	{
		if ( ActorName.find( BatteryNameBase ) == std::string::npos )
			return false;
		else
			return true;
	}

	// The producer interface to this actor name checking must be allowed to 
	// access the static function and it is therefore declared as a friend 
	// class.
	
	friend class Producer;

	// ---------------------------------------------------------------------------
  // Model parameters
  // ---------------------------------------------------------------------------
  //
  // Tremblay's and Dessaint's model [1] requires a set of parameters describing
  // the characteristics of the battery modelled. A range of parameters are 
  // used, and the paper shows how these can be extracted from the data sheet
  // of the battery. Note that since they define the battery, they are read-only
  // and initialised when the class is constructed.

protected:
  
  const double  BatteryConstantVoltage,			// E_0 in Volts
								PolarisationConstant,				// K 
								BatteryCapacity,						// Q in Ampere hours
								ExponentialZoneAmplitude, 	// A in Volts
								ExpZoneTimeConstantInverse,	// B in 1/Ah
								InternalResistance,					// R in Ohm
								MaxChargeCurrent;						// in Ampere
		
  // ---------------------------------------------------------------------------
  // Net current
  // ---------------------------------------------------------------------------
  //
  // The main value to compute is the net currency flow. It consists of two 
  // terms: The charging current and the discharge current. The charging 
  // current is by default assumed to be constant over the interval when the 
  // battery consumer is sourcing power. The current it then simply the provided
  // power divided by the battery constant voltage.
  
  TimeInterval ChargingInterval;
  
  virtual double ChargingCurrent( Time t )
  {
    if ( boost::numeric::in( t, ChargingInterval ) )
      return MaxChargeCurrent;
    else
      return 0.0;
  }
  
  // The discharge current is more complicated. Every load is fundamentally an
  // energy profile L(t), which is valid from the assigned start time to the 
  // end of the duration of the load. Outside this interval the energy profile
  // is zero. The battery therefore needs all the assigned consumption profiles
  // where the time stamps have been made absolute to the assigned start time 
  // of the load so a requested time stamp can be easily checked against the 
  // domain.
  
  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------
  //
  // Currently this constructor is only aimed at initialising the model 
  // constants.
  
  Battery( const IDType & ProducerID,
				   double Volts, double Polarisation, double Capacity, 
					 double Amplitude, double ExpZoneInverse, double Resistance, 
					 double MaxCharge )
	: Actor( ( ValidID( ProducerID ) ? 
	           BatteryNameBase + ProducerID : std::string() ) ),
	  StandardFallbackHandler( Actor::GetAddress().AsString() ),
	  DeserializingActor( Actor::GetAddress().AsString() ),
	  Producer( ProducerID ),
	  BatteryConstantVoltage( Volts ), PolarisationConstant( Polarisation ),
	  BatteryCapacity( Capacity ), ExponentialZoneAmplitude( Amplitude ),
	  ExpZoneTimeConstantInverse( ExpZoneInverse ),
	  InternalResistance( Resistance ), MaxChargeCurrent( MaxCharge )
	{ }
};     // End class battery

/*****************************************************************************
 Li-Ion specialisation
******************************************************************************/


}      // End name space CoSSmic
#endif // COSSMIC_BATTERY
