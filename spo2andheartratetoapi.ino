#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "MAX30105.h"
#include "heartRate.h"

const float CALIBRATION_OFFSET = 5.0;
const float CALIBRATION_SCALE = 1.1;
const int BUFFER_SIZE = 100;  //buffer size 
long irBuffer[BUFFER_SIZE];
long redBuffer[BUFFER_SIZE];
int bufferIndex = 0;

MAX30105 particleSensor;
float spo2Value;
const char* ssid = "*****"; 
const char* password = "*******"; 

const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE]; // Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; // Time at which the last beat occurred

float beatsPerMinute;
float beatAvg;

unsigned long lastSendTime = 0; // To track the last time data was sent
const unsigned long sendInterval = 60000; // Send data every 60 seconds (1 minute)

String username = "";
static int userID = -1;

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing...");

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");

  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) { // Use default I2C port, 400kHz speed
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  particleSensor.setup(); // Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A); // Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(0); // Turn off Green LED

  Serial.println("Enter username:");
  username = readStringFromSerial();
  
  // Get userID once during setup
  userID = getUserID(username.c_str());
   Serial.print("userID:  ");
  Serial.println(userID);
}

void loop() {
  long irValue = particleSensor.getIR();
  Serial.print("IR Value: ");
  Serial.println(irValue);

  // Check if a finger is placed on the sensor
  if (irValue < 50000) {
    // No finger detected, set heart rate values to 0
    beatsPerMinute = 0;
    beatAvg = 0;
    spo2Value = 0;
    Serial.println("No finger detected.");
  } else {
    // Finger detected, proceed with heart rate calculation
    if (checkForBeat(irValue) == true) {
      spo2Value = calculateSpO2();

      // We sensed a beat!
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);
      Serial.print("Beats per Minute: ");
      Serial.println(beatsPerMinute);

      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute; // Store this reading in the array
        rateSpot %= RATE_SIZE; // Wrap variable

        // Print the rates array for debugging
        Serial.print("Rates array: ");
        for (byte x = 0; x < RATE_SIZE; x++) {
          Serial.print(rates[x]);
          Serial.print(" ");
        }
        Serial.println();

        // Take average of readings
        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) {
          beatAvg += rates[x];
        }
        beatAvg /= RATE_SIZE;
      }
    }
  }

  Serial.print("Avg BPM: ");
  Serial.println(beatAvg);
  Serial.print("spo2Value: ");
  Serial.println(spo2Value);

  // Check if it's time to send data
  unsigned long currentTime = millis();
  if (currentTime - lastSendTime >= sendInterval) {
    sendSensorData(beatAvg, spo2Value);
    lastSendTime = currentTime;
  }
}

void sendSensorData(int avgHeartRate, float spo2Value) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("*****************************");
    http.addHeader("Content-Type", "application/json");

    // Create JSON array
    StaticJsonDocument<200> jsonDoc;
    JsonArray array = jsonDoc.to<JsonArray>();

    // Create a JSON object and add it to the array
    JsonObject data = array.createNestedObject();
    data["user_id"] = userID; // Your User ID
    data["heartrate"] = avgHeartRate;
    // Format spo2Value to two decimal places
    char spo2String[10];
    dtostrf(spo2Value, 4, 2, spo2String);
    data["oxygen"] = String(spo2String);

    // Serialize JSON data
    String jsonString;
    serializeJson(array, jsonString);
    Serial.println("JSON to send: " + jsonString);

    // Send JSON data as the body of the POST request
    int httpResponseCode = http.POST(jsonString);

    if (httpResponseCode > 0) {
      String response = http.getString(); // Get the response to the request
      Serial.println("HTTP Response code: " + String(httpResponseCode)); // Print return code
      Serial.println("Response: " + response); // Print response
    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }

    http.end(); // Free resources
  } else {
    Serial.println("Error in WiFi connection");
  }
}

/*float calculateSpO2() {
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();

  // Define thresholds for no finger detection
  const long irThreshold = 5000; 
  const long redThreshold = 1000; 

  // Print redValue for debugging purposes
  Serial.print("Red Value: ");
  Serial.println(redValue);

  // Check if no finger is detected
  if (irValue < irThreshold || redValue < redThreshold) {
    return 0;
  }

  // Ensure that redValue is not zero to avoid division by zero
  if (redValue == 0) {
    return 0; // Handle this case appropriately
  }

  // Calculate R ratio
  float ratio = (float)irValue / (float)redValue;
  float spo2 = 95 + 2 * ratio;

  return spo2;
}*/

//red is how much the red cells in our blod absorbed and ir is how much red is absobered by all cells of finger
float calculateSpO2() {
   long irValue = particleSensor.getIR();//get ir value 
  long redValue = particleSensor.getRed();//get red value

  // Define thresholds for no finger detection
  const long irThreshold = 5000; 
  const long redThreshold = 1000; 

  // Print redValue for debugging purposes
  Serial.print("Red Value: ");
  Serial.println(redValue);

  // Check if no finger is detected
  if (irValue < irThreshold || redValue < redThreshold) {
    return 0;
  }

  // Add the new readings to the buffer
  irBuffer[bufferIndex] = irValue;
  redBuffer[bufferIndex] = redValue;
  bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;//next buffer index

  // average value for ir and red value
  long irSum = 0, redSum = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    irSum += irBuffer[i];
    redSum += redBuffer[i];
  }
  long irDC = irSum / BUFFER_SIZE;
  long redDC = redSum / BUFFER_SIZE;

  // Calculate the AC component (peak-to-peak value)|alternative current and direct current 
  //calculating the pick to show if there was a significant change 
  long irMax = irBuffer[0], irMin = irBuffer[0];
  long redMax = redBuffer[0], redMin = redBuffer[0];
  for (int i = 1; i < BUFFER_SIZE; i++) {
    if (irBuffer[i] > irMax) irMax = irBuffer[i];
    if (irBuffer[i] < irMin) irMin = irBuffer[i];
    if (redBuffer[i] > redMax) redMax = redBuffer[i];
    if (redBuffer[i] < redMin) redMin = redBuffer[i];
  }
  long irAC = irMax - irMin;
  long redAC = redMax - redMin;

  // Calculate the ratio
  float ratio = (float)redAC / (float)redDC / ((float)irAC / (float)irDC);

  // Adjust the empirical formula constants
  float spo2 = 110.0 - 25.0 * ratio;

  // Apply calibration factors based on the readings I had
  spo2 = CALIBRATION_OFFSET + CALIBRATION_SCALE * spo2;

  // Ensure SpO2 is within the realistic range
  if (spo2 > 100.0) spo2 = 100.0;
  if (spo2 < 0.0) spo2 = 0.0;

  return spo2;
}


String readStringFromSerial() {//read from serial monitor
  String input = "";
  while (true) {
    if (Serial.available() > 0) {//if serial monitor avalable
      char received = Serial.read();
      if (received == '\n') {
        break;
      }
      input += received;
    }
  }
  return input;
}

int getUserID(const char* username) {//retriving user id 
  HTTPClient http;
  String url = "**********************************************";
  url += username; // Append the username parameter
  Serial.println("URL: " + url); // Print constructed URL
  http.begin(url);

  int retrievedUserID = -1;

  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Response: " + response);

    DynamicJsonDocument doc(200);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.println("Error deserializing JSON");
    } else {
      if (doc["success"]) {
        retrievedUserID = doc["user_id"];
        Serial.println("User ID: " + String(retrievedUserID));
      } else {
        Serial.println("User not found");
      }
    }
  } else {
    Serial.print("Error on sending GET request: ");
    Serial.println(httpResponseCode);
  }

  http.end(); // Free resources

  return retrievedUserID;
}
