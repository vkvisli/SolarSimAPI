/***********************************************
 Copyright 2019 Vebjørn Kvisli
 License: GNU Lesser General Public License v3.0
***********************************************/

const fs = require('fs');

// Randomly selects consumption profiles (cumulative) for each appliance run of the given apartments
// and adds the non-cumulative versions as new properties in the given apartment object
addConsumerProfiles = function(apartments) {
  for (let apartment of apartments) {
    for (let applianceRun of apartment.applianceRuns) {

      // Select an appropriate cumulative consumption profile for the current applianceRun
      let cConsumptionProfile = [];
      switch (applianceRun.code) {
        case "wm":
          cConsumptionProfile = selectWashingMachineProfile(applianceRun);
          break;
        case "dw":
          cConsumptionProfile = selectDishwasherProfile(applianceRun);
          break;
      }
      applianceRun.profile = nonCumulative(cConsumptionProfile);
    }
  }
};

// Creates consumption files required for the simulator
createConsumerFiles = function(consumerEventFileName, baseFileLocation, requestBody, userID) {
  let consumerEventContent = "";
  let consumerFileNames = [];

  // Iterate through all applianceRuns
  for (let apartment of requestBody.apartments) {
    for (let applianceRun of apartment.applianceRuns) {

      // Get necessary information about the current applianceRun
      let id = applianceRun.id;
      let unixEarliestStart = stringTimeToUnix(applianceRun.earliestStart, requestBody.scenario.unixTimeStart);
      let unixDoneBy = stringTimeToUnix(applianceRun.doneBy, requestBody.scenario.unixTimeStart);
      let unixLatestStart = unixDoneBy - (applianceRun.profile.length * 60);
      let consumerFileName = "consumer_" + id + "_" + userID + ".csv";
      consumerFileNames.push(consumerFileName);

      // Add a line for this applianceRun in the "consumerEvent" file
      consumerEventContent += id + ";" + unixEarliestStart + ";" + unixLatestStart + ";" + consumerFileName + "\n";

      // Create a consumer file for the current applianceRun
      let consumerContent = getConsumerContent(cumulative(applianceRun.profile));
      fs.writeFileSync(baseFileLocation + consumerFileName, consumerContent);
    }
  }

  // Create the "consumerEvent" file
  fs.writeFileSync(baseFileLocation + consumerEventFileName, consumerEventContent);

  // Return an array of the consumer's file names
  return consumerFileNames;
};

// Assign start times to the appliance run objects in the given apartments
assignStartTimes = function(apartments, startTimesArray) {
  for (let i=0; i<startTimesArray.length; i++) {
    let split = startTimesArray[i].split(" ");
    let id = split[0];
    let startTime = Number(split[1]);

    // Find the applianceRun with the matching ID and assign the start time
    for (let apartment of apartments) {
      for (let applianceRun of apartment.applianceRuns) {
        if (applianceRun.id === id) {
          applianceRun.assignedStartTime = startTime;
        }
      }
    }
  }
};

// Returns an object with all required consumption info based on the request body
toConsumptionObject = function(requestBody) {
  // Create consumer object with info about individual consumers
  let consumers = [];
  for (let apartment of requestBody.apartments) {
    for (let applianceRun of apartment.applianceRuns) {
      consumers.push({
        id: applianceRun.id,
        assignedStartTime: applianceRun.assignedStartTime,
        profile: applianceRun.profile
      });
    }
  }

  // Add the profiles of each individual consumer to a profile for total consumption
  const simDurationMin = 1440; //TODO: Should be placed more centrally
  let consumptionProfile = new Array(simDurationMin).fill(0);
  for (let consumer of consumers) {
    let startIndex = Math.floor((consumer.assignedStartTime - requestBody.scenario.unixTimeStart) / 60);
    for (let i=0; i<consumer.profile.length; i++) {
      consumptionProfile[startIndex + i] += consumer.profile[i];
    }
  }

  // Create a cumulative version of the profile for total consumption
  let cConsumptionProfile = cumulative(consumptionProfile);

  // Convert the non-cumulative consumption profile from energy (kWh) to power (kW)
  // by dividing by hours (min/60)
  for (let i=0; i<consumptionProfile.length; i++) {
    consumptionProfile[i] = consumptionProfile[i] / (1 / 60);
  }

  // Finally, return an object with both total and individual consumption profiles
  return {
    consumptionProfile: consumptionProfile,
    cConsumptionProfile: cConsumptionProfile,
    consumptionInterval: 1,
    consumers: consumers
  };
};

// Convert a time string (with format "08:00") to a unix time stamp,
// by adding the time to "unixStartOfDay"
function stringTimeToUnix(timeString, unixStartOfDay) {
  let hours = parseInt(timeString.substring(0, 2));
  let minutes = parseInt(timeString.substring(3, 5));
  let totalSeconds = (hours * 3600) + (minutes * 60);
  return unixStartOfDay + totalSeconds;
}

// Returns an appropriate, randomly selected washing machine profile
// for the selected program of the given washing machine run
function selectWashingMachineProfile(washingMachineRun) {
  // Get the file names of washing machine clusters which is mapped to the selected program
  const programMapping = require("./data/wm_clusters/programMapping.json");
  let fileNames = [];
  switch (washingMachineRun.program) {
    case "30°": fileNames = programMapping["30"]; break;
    case "30° short": fileNames = programMapping["30short"]; break;
    case "40°": fileNames = programMapping["40"]; break;
    case "40° short": fileNames = programMapping["40short"]; break;
    case "60°": fileNames = programMapping["60"]; break;
    case "60° short": fileNames = programMapping["60short"]; break;
    case "90°": fileNames = programMapping["90"]; break;
    case "90° short": fileNames = programMapping["90short"]; break;
  }

  // Select one of the files randomly
  let randomFileIndex = Math.floor(Math.random() * fileNames.length);
  let selectedFileName = fileNames[randomFileIndex];
  let selectedFile = require("./data/wm_clusters/" + selectedFileName);

  // Finally, select and return one of the profiles in the selected file randomly
  let randomProfileIndex = Math.floor(Math.random() * selectedFile.data.length);
  return selectedFile.data[randomProfileIndex];
}

// Returns an appropriate, randomly selected dishwasher profile
// for the selected program of the given dishwasher run
function selectDishwasherProfile(dishwasherRun) {
  // Get the file names of washing machine clusters which is mapped to the selected program
  const programMapping = require("./data/dw_clusters/programMapping.json");
  let fileNames = [];
  switch (dishwasherRun.program) {
    case "economic": fileNames = programMapping["economic"]; break;
    case "fast": fileNames = programMapping["fast"]; break;
    case "delicate": fileNames = programMapping["delicate"]; break;
    case "normal": fileNames = programMapping["normal"]; break;
    case "intensive": fileNames = programMapping["intensive"]; break;
  }

  // Select one of the files randomly
  let randomFileIndex = Math.floor(Math.random() * fileNames.length);
  let selectedFileName = fileNames[randomFileIndex];
  let selectedFile = require("./data/dw_clusters/" + selectedFileName);

  // Finally, select and return one of the profiles in the selected file randomly
  let randomProfileIndex = Math.floor(Math.random() * selectedFile.data.length);
  return selectedFile.data[randomProfileIndex];
}

// Returns a string with the correct format for consumer files (relative time stamps and consumption values)
function getConsumerContent(consumptionProfile) {
  let consumerContent = "";
  let time = 0;
  for (let consumptionValue of consumptionProfile) {
    consumerContent += time + " " + consumptionValue + "\n";
    time += 60;
  }
  return consumerContent;
}

// Returns the normal, non-cumulative version of a cumulative profile
function nonCumulative(cProfile) {
  let profile = [cProfile[0]];
  for (let i=1; i<cProfile.length; i++) {
    profile.push(cProfile[i] - cProfile[i-1]);
  }
  return profile;
}

// Returns a cumulative (accumulated) version of a profile
function cumulative(profile) {
  let cumulativeProfile = [profile[0]];
  for (let i=1; i<profile.length; i++) {
    cumulativeProfile.push(cumulativeProfile[cumulativeProfile.length-1] + profile[i]);
  }
  return cumulativeProfile;
}