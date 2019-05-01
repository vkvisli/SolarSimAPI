/***********************************************
 Copyright 2019 Vebjørn Kvisli
 License: GNU Lesser General Public License v3.0
 ***********************************************/

// Math expressions
const sin = Math.sin;
const cos = Math.cos;
const tan = Math.tan;
const asin = Math.asin;
const acos = Math.acos;
const pow = Math.pow;

const fs = require('fs');

// Generates and returns simulated PV production by first calculating a theoretically optimal
// production using a mathematical model, and then applying weather variations extracted from
// measurement data in Konstanz, Germany.
generateSimulatedProduction = function(parameters) {

  // Scenario parameters
  const tilt = parameters.scenario.pvTilt;
  const orientation = parameters.scenario.pvOrientation;
  const capacity = parameters.scenario.pvCapacity;
  const unixTimeStart = parameters.scenario.unixTimeStart;
  const lat = parameters.scenario.lat;
  const long = parameters.scenario.long;
  const timeZoneOffset = parameters.scenario.timeZoneOffset;
  const weather = parameters.scenario.weather;

  // GPS coordinates and time zone offset for Konstanz, Germany, where the CoSSMic data was measured
  const kLat = 47.695;
  const kLong = 9.175;
  const kTimeZoneOffset = 1;

  // Initial sampling interval in minutes for calculating optimal productions
  const interval = 1;

  // Select a random production profile (SP) and its 'meta data'
  // from CoSSMic data with the given season and weather type
  const selectedProfileObject = randomProductionProfile(unixTimeStart, weather);
  const sp = selectedProfileObject.profile;
  const sp_tilt = selectedProfileObject.pvTilt;
  const sp_orientation = selectedProfileObject.pvOrientation;
  const sp_minuteRes = selectedProfileObject.minuteRes;
  const sp_startTime = selectedProfileObject.unixStartUTC;
  const sp_startTimeMidnight = new Date(sp_startTime * 1000).setUTCHours(24,0,0,0) / 1000;

  // Get the times of sunrise and sunset in Konstanz at the day when SP was measured,
  // by using the method for calculating an optimal production at that location and time
  const konstanzOptimal = optimalProduction(sp_tilt, sp_orientation, capacity, sp_startTimeMidnight, kLat, kLong, kTimeZoneOffset, interval);
  const konstanzSunrise = konstanzOptimal.sunRise;
  const konstanzSunSet = konstanzOptimal.sunSet;
  const konstanzSunEntersPanel = konstanzOptimal.sunEntersPanel;
  const konstanzSunLeavesPanel = konstanzOptimal.sunLeavesPanel;

  // The indices for start and end of production in SP is calculated using 3 different methods:

  // Method 1: Indices for the theoretically calculated times of sunset and sunrise in Konstanz
  const sp_sunRiseIndex = Math.round((konstanzSunrise - sp_startTime) / (60 * sp_minuteRes));
  const sp_sunSetIndex = Math.round((konstanzSunSet - sp_startTime) / (60 * sp_minuteRes));

  // Method 2: Indices for when the sun enters and leaves the panels in Konstanz (AOI = 90)
  const sp_sunEntersindex = Math.round((konstanzSunEntersPanel - sp_startTime) / (60 * sp_minuteRes));
  const sp_sunLeavesindex = Math.round((konstanzSunLeavesPanel - sp_startTime) / (60 * sp_minuteRes));

  // Method 3: Indices for when SP actually starts and ends production
  const sp_inclineStartIndex = prodStartIndex(sp);
  const sp_inclineEndIndex = prodEndIndex(sp);

  // Which indices to use is chosen by selecting the highest (latest) start index
  // and the lowest (earliest) end index
  const sp_solarDayStartIndex = Math.max(sp_sunRiseIndex, sp_sunEntersindex, sp_inclineStartIndex);
  const sp_solarDayEndIndex = Math.min(sp_sunSetIndex, sp_sunLeavesindex, sp_inclineEndIndex);
  const sp_solarDaySamples = sp_solarDayEndIndex - sp_solarDayStartIndex;

  // Generate an estimated optimal production (OPT), based on the parameters of the PV system to be simulated
  const opt = optimalProduction(tilt, orientation, capacity, unixTimeStart, lat, long, timeZoneOffset, interval);

  // Find the relative times of the start and end of the solar day (index * interval) in OPT,
  // and the number of minutes within that period
  const opt_solarDayStart = prodStartIndex(opt.productionProfile) * interval;
  const opt_solarDayEnd = prodEndIndex(opt.productionProfile) * interval;
  const opt_solarDayMinutes = opt_solarDayEnd - opt_solarDayStart;

  // Calculate delta t - the new interval to match OPT's solar day length
  // and SP's number of solar day samples
  const delta_t = opt_solarDayMinutes / sp_solarDaySamples;

  // Generate a new optimal production (D_OPT) with delta t as the interval
  const d_opt = optimalProduction(tilt, orientation, capacity, unixTimeStart, lat, long, timeZoneOffset, delta_t);

  // Find the index of where the solar day starts and ends in D_OPT,
  // and the number of samples within that period
  const d_opt_solarDayStartIndex = prodStartIndex(d_opt.productionProfile);
  const d_opt_solarDayEndIndex = prodEndIndex(d_opt.productionProfile);
  const d_opt_solarDaySamples = d_opt_solarDayEndIndex - d_opt_solarDayStartIndex;

  // Calculate energy values for the profile to be simulated (SIM_E),
  // by multiplying the values in SP by the values of D_OPT,
  // and also create a time axis for that profile (SIM_T)
  let sp_index = sp_solarDayStartIndex;
  let d_opt_index = d_opt_solarDayStartIndex;
  let time = d_opt_solarDayStartIndex * delta_t;
  let sim_e = [];
  let sim_t = [];
  for (let i=0; i<d_opt_solarDaySamples; i++) {
    sim_e.push(sp[sp_index] * d_opt.productionProfile[d_opt_index]);
    sim_t.push(time);
    sp_index++;
    d_opt_index++;
    time += delta_t;
  }

  // For SIM_T times before the start of solar day, insert zeros to the front of SIM_E,
  // and for SIM_T times after the end of solar day, append the total energy value to the end of SIM_E,
  while (true) {
    let time = sim_t[0] - delta_t;
    if (time < 0 || isNaN(time))
      break;
    sim_t.unshift(time);
    sim_e.unshift(0);
  }

  while (true) {
    let time = sim_t[sim_t.length-1] + delta_t;
    if (time > 1440 || isNaN(time)) // TODO: This parameter (1440) should be placed more centrally?
      break;
    sim_t.push(time);
    sim_e.push(0);
  }

  // Compare the length of SIM_E and SIM_T against the profiles in D_OPT. Due to rounding of time interval,
  // they may not have the same length. If not, change the D_OPT profiles to match SIM_E
  const lengthDiff = d_opt.productionProfile.length - sim_e.length;
  if (lengthDiff === 1) {
    d_opt.productionProfile.pop();
    d_opt.cProductionProfile.pop();
    d_opt.aoiProfile.pop();
  } else if (lengthDiff === -1) {
    sim_e.pop();
    sim_t.pop();
  }

  // Create a cumulative version of SIM_E
  let c_sim_e = cumulative(sim_e);

  // Convert the non-cumulative profiles from energy (kWh) to power (kW) by dividing by hours (min/60)
  for (let i=0; i<sim_e.length; i++) {
    sim_e[i] = sim_e[i] / (delta_t / 60);
  }
  for (let i=0; i<d_opt.productionProfile.length; i++) {
    d_opt.productionProfile[i] = d_opt.productionProfile[i] / (delta_t / 60);
  }

  // Finally, return an object with the various simulated profiles
  return {
    productionInterval: delta_t,
    productionProfile: sim_e,
    cProductionProfile: c_sim_e,
    productionIntervalOpt: delta_t,
    productionProfileOpt: d_opt.productionProfile,
    cProductionProfileOpt: d_opt.cProductionProfile,
    aoiInterval: interval,
    aoiProfile: opt.aoiProfile,
    sunRise: opt.sunRise,
    sunSet: opt.sunSet
  };
};

// Creates production files required for the simulator (Only one producer).
createProducerFiles = function(prodFileName, baseFileLoc, cProdProfile, unixTimeStart, minInterval) {
  let producerContent = "";
  for (let i=0; i<cProdProfile.length; i++) {
    let timestamp = Math.round(unixTimeStart + (i * minInterval * 60));
    producerContent += timestamp + " " + cProdProfile[i] + "\n";
  }
  fs.writeFileSync(baseFileLoc + prodFileName, producerContent);
};

// Generates an estimated optimal production profile with a specific interval in minutes,
// based on the given parameters and the sun's angle of incidence on the PV panels.
// Returns and object containing the production profile, cumulative production profile,
// aoi profile, and the times of sunrise and sunset
function optimalProduction(tilt, orientation, capacity, unixTimeStart, lat, long, timeZoneOffset, interval) {
  let prodProfile = [];
  let aoiProfile = [];
  let sunRise = null;
  let sunSet = null;
  let sunEntersPanel = null;
  let sunLeavesPanel = null;
  let unixTime = unixTimeStart;
  let prevSunIsUp;
  let prevSunIsOnPanel;

  // Set the number of samples to generate (number of minutes in a day divided by the interval)
  const samples = Math.round(1440 / interval);

  // Loop through every minute of the simulation period to generate production profile
  for (let i=0; i<samples; i++) {

    // Angle of incidence (aoi) and incidence angle modifier (iam) for the PV module
    const aoi = angleOfIncidence(tilt, orientation, unixTime, lat, long, timeZoneOffset);
    aoiProfile.push(aoi);
    const iam = physicalIamModifier(aoi, 1.526, 4, 0.002);

    // Calculate AOI for tilt = 0, to check if the sun is up.
    // If the angle is less than 90 degrees, the sun is up. Else, it is not
    const aoiTilt0 = angleOfIncidence(0, orientation, unixTime, lat, long, timeZoneOffset);

    const sunIsOnPanel = (aoi < 90);
    const sunIsUp = (aoiTilt0 < 90);

    // If it is the first iteration, set previous sun states to the same as the current
    if (i === 0) {
      prevSunIsOnPanel = sunIsOnPanel;
      prevSunIsUp = sunIsUp;
    }

    // Check if the sun enters or leaves the panel at this time
    if (prevSunIsOnPanel && !sunIsOnPanel)
      sunLeavesPanel = Math.floor(unixTime);
    if (!prevSunIsOnPanel && sunIsOnPanel)
      sunEntersPanel = Math.floor(unixTime);

    // Check if there is a sunrise or sunset at this time
    if (prevSunIsUp && !sunIsUp)
      sunSet = Math.floor(unixTime);
    if (!prevSunIsUp && sunIsUp)
      sunRise = Math.floor(unixTime);

    // If the sun is up, calculate and add the production. Else, add 0 to production
    if (sunIsUp)
      prodProfile.push(capacity * (interval / 60) * iam);
    else
      prodProfile.push(0);

    // Update previous sun states to the current one
    prevSunIsUp = sunIsUp;
    prevSunIsOnPanel = sunIsOnPanel;

    // Finally, add the interval (converted to seconds) to the unix time
    unixTime = unixTime + (interval * 60);
  }

  // Generate a cumulative (accumulated) version of the production profile
  const cProdProfile = cumulative(prodProfile);

  // Return production profile, cumulative production profile, aoi profile,
  // and the times of sunrise and sunset
  return {
    productionProfile: prodProfile,
    cProductionProfile: cProdProfile,
    aoiProfile: aoiProfile,
    sunRise: sunRise,
    sunSet: sunSet,
    sunEntersPanel: sunEntersPanel,
    sunLeavesPanel: sunLeavesPanel
  };
}

// Calculates the sun's angle of incidence on a PV module,
// based on tilt, orientation, time, and location. Equation: Twidell and Weir (2015)
function angleOfIncidence(tilt, orientation, unixTime, lat, long, timeZoneOffset) {
  let n = dayOfYear(unixTime);
  let delta = declination(n);
  let omega = solarHourAngle(unixTime, long, timeZoneOffset);
  let a = sin(rad(lat)) * cos(rad(tilt));
  let b = cos(rad(lat)) * sin(rad(tilt)) * cos(rad(orientation));
  let c = sin(rad(tilt)) * sin(rad(orientation));
  let d = cos(rad(lat)) * cos(rad(tilt));
  let e = sin(rad(lat)) * sin(rad(tilt)) * cos(rad(orientation));
  let cosAOI = (a - b) * sin(rad(delta)) + (c * sin(rad(omega)) + (d + e) * cos(rad(omega))) * cos(rad(delta));
  return deg(acos(cosAOI));
}

// Returns the day of the year (1-366) from a unix timestamp
function dayOfYear(unixTime) {
  let date = new Date(unixTime*1000);
  return Math.ceil((date - new Date(date.getFullYear(),0,1)) / 86400000);
}

// Returns the earth's declination at the given day of year
function declination(dayOfYear) {
  return 23.45 * sin(rad(360 * (284 + dayOfYear) / 365));
}

// Returns the solar hour angle (equation of time ignored)
function solarHourAngle(unixTime, longitude, timeZoneOffset) {
  let date = new Date(unixTime*1000);
  let zoneTimeHours = date.getUTCHours() + (date.getUTCMinutes() / 60);
  let zoneLongitude = 15 * timeZoneOffset;
  return 15 * (zoneTimeHours - 12) + (longitude - zoneLongitude);
}

// Converts degrees to radians
function rad(degrees) {
  return degrees * (Math.PI / 180);
}

// Converts radians to degrees
function deg(radians) {
  return radians * (180 / Math.PI);
}

// Returns the incidence angle modifier using the physical IAM model (De Soto et al.)
// aoi: the angle of incidence
// n:   the index of refraction
// k:   the glazing extinction coefficient (1/meters)
// l:   the glazing thickness (meters)
physicalIamModifier = function(aoi, n, k, l) {
  if (aoi >= 0 && aoi <= 90) {
    let t_aoi = transmittance(aoi, n, k, l);
    let t_0 = transmittanceOfZero(n, k, l);
    return t_aoi / t_0;
  } else {
    return 0;
  }
};

// Returns the transmittance of angle x (equation by De Soto et al. 2006)
// The equation is divided into smaller sections named by letters for readability
// x: the angle to calculate transmittance of
// n: the index of refraction for x
// k: the glazing extinction coefficient (1/meters)
// l: the glazing thickness (meters)
function transmittance(x, n, k, l) {
  let xr = refractionAngle(x, n);
  let a = pow(Math.E, -((k*l) / cos(xr)));
  let b = pow(sin(xr - rad(x)), 2);
  let c = pow(sin(xr + rad(x)), 2);
  let d = pow(tan(xr - rad(x)), 2);
  let e = pow(tan(xr + rad(x)), 2);
  return a * (1 - (0.5 * ((b/c) + (d/e))));
}

// Calculates the transmittance when the angle is 0, normal to the sun
function transmittanceOfZero(n, k, l) {
  return Math.exp(-(k * l)) * (1 - pow((1-n) / (1+n), 2));
}

// Returns the refraction angle of x with refraction index n
function refractionAngle(x, n) {
  return asin((1/n) * sin(rad(x)));
}

// Returns the index where the production starts in the given profile.
// Works for both cumulative and non-cumulative profiles.
function prodStartIndex(profile) {
  if (profile[0] !== 0)
    return 0;

  for (let i=1; i<profile.length; i++)
    if (profile[i] !== profile[i-1])
      return i;
}

// Returns the index where the production ends in the given profile.
// NB: only works for non-cumulative profiles.
function prodEndIndex(profile) {
  if (profile[profile.length-1] !== 0)
    return profile.length-1;

  for (let i=profile.length-2; i>=0; i--)
    if (profile[i] !== profile[i+1])
      return i+1;
}

// Returns the unix time stamp for the start of a profile's production
// Works for both cumulative and non-cumulative profiles.
prodStartUnix = function(profile, unixStartTime, interval) {
  let startIndex = prodStartIndex(profile);
  let startUnix = unixStartTime + (startIndex * interval * 60);
  return Math.floor(startUnix);
};

// Returns the unix time stamp for the end of a profile's production
// NB: only works for non-cumulative profiles.
prodEndUnix = function(profile, unixStartTime, interval) {
  let endIndex = prodEndIndex(profile);
  let endUnix = unixStartTime + (endIndex * interval * 60);
  return Math.floor(endUnix);
};

// Returns a cumulative (accumulated) version of a profile
function cumulative(profile) {
  let cumulativeProfile = [profile[0]];
  for (let i=1; i<profile.length; i++) {
    cumulativeProfile.push(cumulativeProfile[cumulativeProfile.length-1] + profile[i]);
  }
  return cumulativeProfile;
}

// Returns the normal, non-cumulative version of a cumulative profile
function nonCumulative(cProfile) {
  let profile = [cProfile[0]];
  for (let i=1; i<cProfile.length; i++) {
    profile.push(cProfile[i] - cProfile[i-1]);
  }
  return profile;
}

// Returns an object with a randomly selected production profile
// for a specific season and weather type
function randomProductionProfile(unixTime, weatherType) {
  let monthNumber = new Date(unixTime * 1000).getUTCMonth()+1;
  let fileContent = null;

  switch (monthNumber) {

    // Spring months
    case 3 : case 4: case 5:
      switch (weatherType) {
        case "sunny": fileContent = require("./data/pv_categories/spring_sunny.json"); break;
        case "partially cloudy": fileContent = require("./data/pv_categories/spring_partially_cloudy.json"); break;
        case "cloudy": fileContent = require("./data/pv_categories/spring_cloudy.json"); break;
        case "rainy": fileContent = require("./data/pv_categories/spring_rainy.json"); break;
        default: console.error("Invalid weather type: " + weatherType);
      }
      break;

    // Summer months
    case 6 : case 7: case 8:
      switch (weatherType) {
        case "sunny": fileContent = require("./data/pv_categories/summer_sunny.json"); break;
        case "partially cloudy": fileContent = require("./data/pv_categories/summer_partially_cloudy.json"); break;
        case "cloudy": fileContent = require("./data/pv_categories/summer_cloudy.json"); break;
        case "rainy": fileContent = require("./data/pv_categories/summer_rainy.json"); break;
        default: console.error("Invalid weather type: " + weatherType);
      }
      break;

    // Autumn months
    case 9 : case 10: case 11:
      switch (weatherType) {
        case "sunny": fileContent = require("./data/pv_categories/autumn_sunny.json"); break;
        case "partially cloudy": fileContent = require("./data/pv_categories/autumn_partially_cloudy.json"); break;
        case "cloudy": fileContent = require("./data/pv_categories/autumn_cloudy.json"); break;
        case "rainy": fileContent = require("./data/pv_categories/autumn_rainy.json"); break;
        default: console.error("Invalid weather type: " + weatherType);
      }
      break;

    // Winter months
    case 12 : case 1: case 2:
      switch (weatherType) {
        case "sunny": fileContent = require("./data/pv_categories/winter_sunny.json"); break;
        case "partially cloudy": fileContent = require("./data/pv_categories/winter_partially_cloudy.json"); break;
        case "cloudy": fileContent = require("./data/pv_categories/winter_cloudy.json"); break;
        case "rainy": fileContent = require("./data/pv_categories/winter_rainy.json"); break;
        default: console.error("Invalid weather type: " + weatherType);
      }
      break;

    default:
      console.error("Error: Invalid month number");
      return null;
  }

  // Randomly select and return one production profile from the selected json file
  let randomIndex = Math.floor(Math.random() * fileContent.data.length);
  return fileContent.data[randomIndex];
}