/*=============================================================================
  Test of interpolation

  This file tests the interpolation function class by first defining two 
  functions, interpolate them and compare their values under translation and 
  combination of the functions. For each main step a data file is produced 
  containing the relevant values and the error between the interpolated 
  function and the real functional value. Thus this file also tests the 
  difference between cubic spline interpolation and Akima interpolation.
  
  Author: Geir Horn, University of Oslo, 2014
  Contact: Geir.Horn [at] mn.uio.no
  License: LGPL3.0
=============================================================================*/
  
#include <cmath>
#include <iostream>
#include <fstream>
#include <utility>

#include "Interpolation.hpp"

using namespace std;

// ----------------------------------------------------------------------------
// Test functions
//-----------------------------------------------------------------------------

double F( double x )
{
  return log(x);
}

double G( double x )
{
  return sin(x)/(0.1*x);
}

double FandG( double x )
{
  return F(x)+G(x);
}

// ----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(int argc, char **argv) 
{
  // Setting up the three interpolation objects - default Akima spline is used
  
  cout << "Starting up" << endl;
  cout << "Cubic spline requires " 
       << gsl_interp_type_min_size( gsl_interp_cspline )
       << " samples for the intrepolation. " << endl;
  cout << "Akima spline requires "
       << gsl_interp_type_min_size( gsl_interp_akima )
       << " samples for the interpolation." << endl;
  cout << "Creating the interpolation of F..." << endl;
  
  map< double, double > DataPoints;
  
  for ( double x = 1; x<=101; x += 0.2 )
    DataPoints.insert( make_pair(x , F(x)) );
  
  Interpolation Fi( DataPoints.begin(), DataPoints.end() );
  
  DataPoints.clear();
  
  cout << "Interpolating G... "<< endl;
  
  for (double x = 1; x <= 101; x+= 0.13 )
    DataPoints.insert( make_pair(x, G(x)) );
  
  Interpolation Gi( DataPoints );
  
  DataPoints.clear();
  
  cout << "Interpolating G + F..." << endl;
  
  vector< double > Abscissa, Ordinate;
  
  for ( double x = 1; x<= 101; x += 0.7 )
  {
    Abscissa.push_back( x );
    Ordinate.push_back( FandG(x) );
  }
  
  Interpolation FandG_i( Abscissa.begin(), Abscissa.end(), Ordinate.begin(),
			 Ordinate.end() );
 
  // Make the combined interpolated object from the two interpolation functions
 
  cout << "Adding interpolated F + interpolated G..." << endl;
  
  Interpolation Fi_and_Gi( Fi + Gi );
  
  // Then we generate values on a coerce grid and print out the values of the
  // three functions together with their interpolated variants together with 
  // the interpolation errors.
  
  cout << "Generating the data file..." << endl;
  
  ofstream SimpleTest ("Akima.dta");
  
  if (SimpleTest)
    for ( double x = 1; x<=100; x +=0.1 )
    {
      double f  = F(x),
             g  = G(x),
             fi = Fi(x),
             gi = Gi(x),
             fg = FandG(x),
             fgi = FandG_i(x),
             figi = Fi_and_Gi(x);
	     
      SimpleTest << x << " " << f << " " << fi << " " << f-fi << " "
		 << g << " " << gi << " " << g-gi << " "
		 << fg << " " << fgi << " " << figi << " " 
		 << fg - fgi << " " << fg - figi << endl;
    }
  
  SimpleTest.close();
  
  cout << "Work done, terminating! Bye!" << endl;
  
  return 0;
}
