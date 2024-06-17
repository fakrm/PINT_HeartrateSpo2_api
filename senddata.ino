#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <WiFi.h>
#include "time.h"
#include <Adafruit_GFX.h> // OLED libraries
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <driver/i2s.h>
#include "soundalrm.h"
#include "pic.h"
#include "MAX30105.h" // MAX3010x library
#include "heartRate.h" // Heart rate calculating algorithm

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET -1 // Reset pin
#define SEALEVELPRESSURE_HPA (1013.25)//for the sensors that they use air pressre 

Adafruit_BME280 bme;

unsigned long targetTime = 0;
unsigned long lastSensorDataSendTime = 0; // Time of the last sensor data send

float temp = 0.0;
float hum = 0.0;

int wakeUpHour = 00; // Wake-up hour
int wakeUpMinute = 00; // Wake-up minute

const i2s_port_t i2s_num = I2S_NUM_0; // I2S port number
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Declaring the display name (display)

const char* ssid       = "*****"; // WiFi network SSID
const char* password   = "*****"; // WiFi password

const char* ntpServer = "pool.ntp.org"; // NTP server to synchronize time
const long  gmtOffset_sec = 7200; // GMT offset in seconds (GMT+2 for the Netherlands)
const int   daylightOffset_sec = 0; // Daylight saving time offset in seconds (not used)

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
#define TFT_BLACK 0x0000  // Color code for black

bool alarmTriggered = false; // Flag to track if alarm has been triggered
unsigned long alarmStartTime; // Time when alarm was triggered

String username = "";
static int userID = -1;

void setup(void) {
  
  targetTime = millis() + 1000;

  if (!bme.begin(0x76)) {//I2C address
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  tft.init(); // Initialize TFT display
  tft.setRotation(0); // Set display rotation
  Serial.begin(115200);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Start the OLED display
  display.display();
  delay(3000);
  Serial.print(WiFi.macAddress());
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password); // Connect to WiFi network
  while (WiFi.status() != WL_CONNECTED) { // Wait for WiFi connection
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); // Configure time synchronization
  printLocalTime();

  // Read username from serial input
  /*Serial.println("Enter username:");
  username = readStringFromSerial();

  // Get user ID from API
  userID = getUserID(username.c_str());
  Serial.println(userID);*/
Serial.println("Enter username:");
  username = readStringFromSerial();
  // Call function wakeup 
  //getWakeUpTimeFromAPI(); // Fetch wake-up time from API

  WiFi.disconnect(true); // Disconnect from WiFi network
  WiFi.mode(WIFI_OFF); // Turn off WiFi to save power

  // Initialize I2S for audio
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),//esp32 is master and generate clock syc|transfer audio data from esp32
    .sample_rate = 16000, // Adjust sample rate if needed
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // default interrupt priority
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = -1
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = 27,
    .ws_io_num = 26,
    .data_out_num = 25,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin(i2s_num, &pin_config);
}

void loop() {
  printLocalTime();
  delay(10000);
  printLocalTime();

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  Serial.print("Current time: ");
  Serial.print(timeinfo.tm_hour);
  Serial.print(":");
  Serial.println(timeinfo.tm_min);

  if (timeinfo.tm_hour == wakeUpHour && timeinfo.tm_min == wakeUpMinute && !alarmTriggered) {
    Serial.println("Wake-up time reached!");
    alarmTriggered = true;
    playWakeUpAlarm();
  }

  display.clearDisplay();
  display.drawBitmap(0, 0, logo3_bmp, 32, 32, WHITE);//to draw a pic on screen
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(50,0);
  display.println("Tigan");
  display.setCursor(50,18);
  display.display();

  if (targetTime < millis()) {
    targetTime = millis() + 1000;
    readTemperatureAndHumidity();
  }
/*Serial.println("Enter username:");
  username = readStringFromSerial();*/

  // Get user ID from API
  userID = getUserID(username.c_str());
  Serial.println(userID);

   getWakeUpTimeFromAPI(); // Fetch wake-up time from API

  if (millis() - lastSensorDataSendTime >= 60000) {
    lastSensorDataSendTime = millis();
    sendSensorData(temp, hum);
  }
}

// Wake-up alarm function
void playWakeUpAlarm() {//playing wakeup alarm sample 22 is in other tab
  size_t bytes_written;
  i2s_write(i2s_num, sample22, sizeof(sample22), &bytes_written, portMAX_DELAY);//writes audio data to I2S|port number used|have delay to make sure it is compeletly done
  delay(80);
  alarmTriggered = false; // Reset alarm flag
}

// Getting time
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  tft.fillScreen(TFT_BLACK); // Clear TFT display with black color
  tft.setCursor(120, 80, 2);//set the position
  tft.setTextColor(TFT_WHITE, TFT_RED);  
  tft.setTextSize(1); // Set text size
  tft.println(&timeinfo, "%A"); // Print day of the week on OLED 
  Serial.println(&timeinfo, "%A"); // Print day of the week to serial monitor

  tft.setCursor(120, 120, 2);
  tft.setTextColor(TFT_WHITE, TFT_RED);  // Set text color
  tft.setTextSize(1); // Set text size
  tft.println(&timeinfo, "%B %d %Y");
  Serial.println(&timeinfo, "%B %d %Y");

  tft.setCursor(120, 160, 2);
  tft.setTextColor(TFT_WHITE, TFT_RED);  
  tft.setTextSize(1); // Set text size
  tft.println(&timeinfo,"%H : %M : %S");
  Serial.println(&timeinfo,"%H: %M: %S");
}

void readTemperatureAndHumidity() {
  temp = bme.readTemperature();
  hum = bme.readHumidity();

  Serial.print("Temp: ");
  Serial.print(temp);
  Serial.println(" *C");

  Serial.print("Hum: ");
  Serial.print(hum);
  Serial.println(" %");

  Serial.println(); 
}

void sendSensorData(float temperature, float humidity) {//sending hum and temp and datetime
  WiFi.mode(WIFI_STA); // Enable WiFi
  WiFi.begin(ssid, password); // Connect to WiFi

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("********************************"); //url of the end point    
    http.addHeader("Content-Type", "application/json");

    // Create JSON array
    StaticJsonDocument<400> jsonDoc;//400 byte is the capacity

    JsonObject dataItem = jsonDoc.to<JsonObject>();
    
    
    dataItem["user_id"] = userID; // Use retrieved User ID
    dataItem["hardware_id"] = WiFi.macAddress(); // Your User ID

    // Format temperature and humidity to 2 decimal places
    char tempStr[10];
    char humStr[10];
    sprintf(tempStr, "%.2f", temperature);
    sprintf(humStr, "%.2f", humidity);

    dataItem["temperature"] = tempStr;
    dataItem["humidity"] = humStr;
    
    // Get the current date and time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      return;
    }

    // Format date and time as "YYYY-MM-DD HH:mm:ss"
    char dateTimeString[20];
    strftime(dateTimeString, sizeof(dateTimeString), "%Y-%m-%d %H:%M:%S", &timeinfo);

    dataItem["datetime"] = dateTimeString; // Current date and time

    // Serialize JSON data directly to the HTTPClient
    String jsonString;
    serializeJson(jsonDoc, jsonString);
    Serial.println("[" + jsonString + "]");
    // Send JSON data as the body of the POST request
    int httpResponseCode = http.POST("[" + jsonString + "]");

    if (httpResponseCode > 0) {
      String response = http.getString(); // Get the response to the request
      Serial.println(httpResponseCode); // Print return code
      Serial.println(response); // Print response
    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }

    http.end(); // Free resources
  }

  WiFi.disconnect(true); // Disconnect from WiFi
  WiFi.mode(WIFI_OFF); // Turn off WiFi to save power
}

void getWakeUpTimeFromAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println(" CONNECTED");
  }

  HTTPClient http;
  String url = "*********************************************************";//get req to api for wakeup time
  url += "?HardwareID=";
  url += WiFi.macAddress();
  http.begin(url); // Replace with your API endpoint
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Response: " + response);

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, response);
    if(error){ Serial.println("error deserialising"); return;}

    const char* wakeuptimeStr = doc["data"]["wakeuptime"];
    int flex = doc["data"]["flex"];
    Serial.println(wakeuptimeStr);
    Serial.println(flex);
    extractHourAndMinute(wakeuptimeStr);
  }
}

void extractHourAndMinute(const char* dateTimeStr) {//for wakeup time i will get an timestampe in datetime but i just need hour and min
  int year, month, day, hour, minute, second;
  sscanf(dateTimeStr, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);

  wakeUpHour = hour;
  wakeUpMinute = minute;
  
  Serial.println("Wake-up hour: " + String(wakeUpHour));
  Serial.println("Wake-up minute: " + String(wakeUpMinute));
}

String readStringFromSerial() {//we will put the username on the serial monitor 
  String input = "";
  while (true) {
    if (Serial.available() > 0) {//if there is sth in serial monitor so so read it
      char received = Serial.read();
      if (received == '\n') {
        break;
      }
      input += received;
    }
  }
  return input;
}

int getUserID(const char* username) {
  WiFi.mode(WIFI_STA); // Enable WiFi
  WiFi.begin(ssid, password); // Connect to WiFi

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");

  HTTPClient http;
  String url = "******************************************************"; 
  url += username; // Append the username parameter
  http.begin(url);

  int retrievedUserID = -1;

  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Response: " + response);

    DynamicJsonDocument doc(200);//initial capacity of document in byte
    DeserializationError error = deserializeJson(doc, response);//parse the json and store the result in error 
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
  WiFi.disconnect(true); // Disconnect from WiFi
  WiFi.mode(WIFI_OFF); // Turn off WiFi to save power

  return retrievedUserID;
}
