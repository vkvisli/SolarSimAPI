/***********************************************
Copyright 2019 Vebjørn Kvisli
License: GNU Lesser General Public License v3.0
***********************************************/

"use strict";

// Modules
const http = require("http");
const url = require("url");
const exec = require("child_process").exec;
const execSync = require("child_process").execSync;
require("./productionTools.js");
require("./consumptionTools.js");
const fs = require('fs');

const baseFileLocation = "./simulator/";

// Creating and running a server
http.createServer(onRequest).listen(8080);
console.log("The SolarSim API is now running and listening on port 8080.");

// Method that runs when a user makes a request to the server
function onRequest(request, response) {
  console.log(request.method + " request: " + request.url);
  const urlPathname = url.parse(request.url, true).pathname;

  // Accept access to all origins
  response.setHeader('Access-Control-Allow-Origin', '*');

  // Check URL endpoint to decide which action to do
  switch (urlPathname) {

    // Endpoint for starting a simulation
    case "/start":

      if (request.method === "POST") {

        // Read request body
        let body = [];
        request.on('data', (chunk) => {
          body.push(chunk);
        }).on('end', () => {
          body = Buffer.concat(body).toString();

          // Check that the request is valid json and has required content
          let jsonBody;
          let validJSON = false;
          let validRequestContent = false;
          try {
            jsonBody = JSON.parse(body);
            validJSON = true;
          } catch (err) { }

          if (validJSON)
            validRequestContent = isValidRequestContent(jsonBody);

          // If the request is ok, run the simulation and return the results
          if (validJSON && validRequestContent) {
            let simulationResultObject = runSimulation(jsonBody);
            if (simulationResultObject !== null) {
              response.statusCode = 200; // OK
              response.setHeader('Content-Type', 'application/json');
              response.write(JSON.stringify(simulationResultObject));
              response.end();
            } else {
              // Internal server error (failed to execute scheduler)
              response.statusCode = 500;
              response.end();
            }

          } else {
            // Bad request
            response.statusCode = 400;
            response.end();
          }
        });
      } else {
        // Method Not Allowed
        response.statusCode = 405;
        response.write("405 - " + request.method + " method not allowed here");
        response.end();
      }
      break;

    case "/":
      // Welcome message at empty endpoint
      response.statusCode = 200;
      response.write("Welcome to the SolarSim API!\n" +
        "Please use one of the supported endpoints and HTTP methods to get started");
      response.end();
      break;

    // Requests to any other endpoint will return 404 page not found
    default:
      response.statusCode = 404;
      response.write("404 - This resource cannot be found");
      response.end();
  }
}

// Performs the necessary processes to run a simulation
function runSimulation(jsonBody) {
  // Create a unique ID for the current user request
  let userID = uniqueUserID();

  // Create file names
  let producerFileName = "producer_" + userID + ".csv";
  let consumerEventFileName = "consumerEvent_" + userID + ".csv";
  let resultFileName = "AST_" + userID + ".csv";

  // Simulate production data and create production files for the simulator
  let productionObject = generateSimulatedProduction(jsonBody);
  createProducerFiles(producerFileName, baseFileLocation, productionObject.cProductionProfile,
                      jsonBody.scenario.unixTimeStart, productionObject.productionInterval);

  // Get the start and end time for the production
  let psu = prodStartUnix(productionObject.productionProfile, jsonBody.scenario.unixTimeStart,
                                    productionObject.productionInterval);

  let peu = prodEndUnix(productionObject.productionProfile, jsonBody.scenario.unixTimeStart,
                                productionObject.productionInterval);

  // Select consumer profiles and create consumption files for the simulator
  addConsumerProfiles(jsonBody.apartments);
  let consumerFileNames = createConsumerFiles(consumerEventFileName, baseFileLocation, jsonBody, userID);

  // Execute the C++ appliance load scheduler/simulator and retrieve results
  let result = executeSimulator(producerFileName, consumerEventFileName, resultFileName,
                                psu, peu);

  if (result !== null) {
    // Split the result string into an array with elements for each line
    let resultArray = result.split("\n");

    // Get the amount of energy bought from the grid (the minimization objective).
    // This is included as the first line in the result string.
    let gridImportString = resultArray.shift();
    let gridImportArray = gridImportString.split(" ");
    let gridImport = Number(gridImportArray[gridImportArray.length - 1]);
    // TODO: grid import from load scheduler is not currently used

    // Assign the resulting start times to the appliance run objects
    assignStartTimes(jsonBody.apartments, resultArray);

    // Get a new object based on jsonBody that contains all required consumption info
    var consumptionObject = toConsumptionObject(jsonBody);
  }

  // When simulation is done, delete all the created simulation files
  deleteFile(baseFileLocation + producerFileName);
  deleteFile(baseFileLocation + consumerEventFileName);
  deleteFile(baseFileLocation + resultFileName);
  for (let filename of consumerFileNames)
    deleteFile(baseFileLocation + filename);

  if (result !== null) {
    return {
      production: productionObject,
      consumption: consumptionObject
    };
  } else {
    return null;
  }

}

// Checks that all required properties for the simulation are present,
// have the correct datatype, and have values within the allowed range.
// If valid, returns true, else returns false.
function isValidRequestContent(r) {

  // Scenario
  if (typeof r.scenario !== "object") return false;

  // Tilt
  if (typeof r.scenario.pvTilt !== "number") return false;
  if (r.scenario.pvTilt < 0 || r.scenario.pvTilt > 90) return false;

  // Orientation
  if (typeof r.scenario.pvOrientation !== "number") return false;
  if (r.scenario.pvOrientation < -180 || r.scenario.pvOrientation > 180) return false;

  // PV capacity (rated output)
  if (typeof r.scenario.pvCapacity !== "number") return false;
  if (r.scenario.pvCapacity <= 0 || r.scenario.pvCapacity > 10000) return false;

  // Unix time start (must be midnight)
  if (typeof r.scenario.unixTimeStart !== "number") return false;
  if (r.scenario.unixTimeStart < 0 || r.scenario.unixTimeStart > 10000000000) return false;
  let date = new Date(r.scenario.unixTimeStart * 1000);
  if (date.getUTCHours() !== 0 || date.getUTCMinutes() !== 0 || date.getUTCSeconds() !== 0) return false;

  // Latitude
  if (typeof r.scenario.lat !== "number") return false;
  if (r.scenario.lat < -90 || r.scenario.lat > 90) return false;

  // Longitude
  if (typeof r.scenario.long !== "number") return false;
  if (r.scenario.long < -180 || r.scenario.long > 180) return false;

  // Time zone offset
  if (typeof r.scenario.timeZoneOffset !== "number") return false;
  if (r.scenario.timeZoneOffset < -12 || r.scenario.timeZoneOffset > 12) return false;

  // Weather type
  if (typeof r.scenario.weather !== "string") return false;
  if (!(r.scenario.weather === "sunny" || r.scenario.weather === "partially cloudy" ||
      r.scenario.weather === "cloudy" || r.scenario.weather === "rainy")) return false;

  // Apartments
  if (typeof r.apartments !== "object") return false;
  if (r.apartments.length === undefined) return false;
  if (r.apartments.length > 0) {
    for (let apt of r.apartments) {

      // ID
      if (typeof apt.id !== "string") return false;

      // Square meters
      if (typeof apt.m2 !== "number") return false;
      if (apt.m2 < 1 || apt.m2 > 1000) return false;

      // Number of people
      if (typeof apt.nPeople !== "number") return false;
      if (apt.nPeople < 1 || apt.nPeople > 10) return false;

      // Appliance runs
      if (typeof apt.applianceRuns !== "object") return false;
      if (apt.applianceRuns.length === undefined) return false;
      if (apt.applianceRuns.length > 0) {
        for (let apl of apt.applianceRuns) {

          // ID
          if (typeof apl.id !== "string") return false;

          // Code
          if (typeof apl.code !== "string") return false;
          if (!(apl.code === "wm" || apl.code === "dw")) return false;

          // Program
          if (typeof apl.program !== "string") return false;
          const wmPrograms = ["30°", "40°", "60°", "90°", "30° short", "40° short", "60° short", "90° short"];
          const dwPrograms = ["economic", "fast", "delicate", "normal", "intensive"];
          if (apl.code === "wm" && !wmPrograms.includes(apl.program)) return false;
          if (apl.code === "dw" && !dwPrograms.includes(apl.program)) return false;

          // Earliest start
          if (typeof apl.earliestStart !== "string") return false;
          if (apl.earliestStart.length !== 5) return false;
          let hours = Number(apl.earliestStart.charAt([0]) + apl.earliestStart.charAt(1));
          let minutes = Number(apl.earliestStart.charAt([3]) + apl.earliestStart.charAt(4));
          if (isNaN(hours) || isNaN(minutes)) return false;
          if (apl.earliestStart.charAt([2]) !== ":") return false;
          if (hours < 0 || hours > 23) return false;
          if (minutes < 0 || minutes > 59) return false;

          // Done by
          if (typeof apl.doneBy !== "string") return false;
          if (apl.doneBy.length !== 5) return false;
          hours = Number(apl.doneBy.charAt([0]) + apl.doneBy.charAt(1));
          minutes = Number(apl.doneBy.charAt([3]) + apl.doneBy.charAt(4));
          if (isNaN(hours) || isNaN(minutes)) return false;
          if (apl.doneBy.charAt([2]) !== ":") return false;
          if (hours < 0 || hours > 23) return false;
          if (minutes < 0 || minutes > 59) return false;
        }
      }
    }
  }

  return true;
}

// Executes the start command to run the simulator
// Returns a string with the content of the result file
function executeSimulator(producerFileName, consumerEventFileName, resultFileName, prodStartTime, prodEndTime) {
  let dir = " -d /app/simulator";
  let prod = " -p " + producerFileName;
  let cons = " -c " + consumerEventFileName;
  let ast = " -a " + resultFileName;
  let sunDay = "";
  if (prodStartTime !== null && prodEndTime !== null)
    sunDay = " -s " + prodStartTime + " " + prodEndTime;
  let command = baseFileLocation + "DOMINOES/Simulator" + dir + prod + cons + ast + sunDay;

  // Execute the start command
  console.log("Executing command: " + command);
  try {
    execSync(command);
  } catch (error) {
    console.error(error);
    return null;
  }

  // Read the result file and return a string with its content
  try {
    return fs.readFileSync(baseFileLocation + resultFileName).toString();
  } catch (error) {
    console.error(error);
    return null;
  }
}

// Returns a random unique ID for a user of the simulator
function uniqueUserID() {
  const chars = "abcdefghijklmnopqrstuvwxyz0123456789";
  let ID;

  // Generate ID and check if the ID is already used
  while (true) {
    ID = "";
    for (let i=0; i<10; i++)
      ID += chars.charAt(Math.floor(Math.random() * chars.length));

    let existingFiles = fs.readdirSync(baseFileLocation);
    if (!existingFiles.toString().includes(ID))
      break;
  }
  return ID;
}

// Deletes the specified file
function deleteFile(filename) {
  try {
    fs.unlinkSync(filename);
  }
  catch(err) {
    console.error("An error occurred when trying to delete "  + filename + ":");
    console.error(err)
  }
}