#include <M5Core2.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "WiFi.h"
#include "FS.h"                 // SD Card
#include <EEPROM.h>             // Read/write flash memory
#include <NTPClient.h>          // Time protocol libraries
#include <WiFiUdp.h>            // Time protocol libraries
#include <Adafruit_VCNL4040.h>  // Sensor library
#include "Adafruit_SHT4x.h"     // Sensor library
#include <time.h>               // For localtime

//////////////////////////////////////////////
// URL Definitions
//////////////////////////////////////////////
// URL for uploading sensor data (Challenge #1)
const String URL_GCF_UPLOAD = "https://lab-function-service-971602190698.us-central1.run.app";
// URL for retrieving the latest sensor data (Challenge #2)
const char* URL_GCF_LATEST = "https://lab-function-return-971602190698.us-central1.run.app";

//////////////////////////////////////////////
// WiFi Credentials & User Info
//////////////////////////////////////////////
const bool SD_CARD_AND_GCS_UPLOAD_ENABLED = false;
String wifiNetworkName = "SHaven";
String wifiPassword = "27431sushi";
String userId = "CBU Student";

//////////////////////////////////////////////
// Sensor and Time Objects
//////////////////////////////////////////////
Adafruit_VCNL4040 vcnl4040 = Adafruit_VCNL4040();
Adafruit_SHT4x sht4 = Adafruit_SHT4x();
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

unsigned long timerDelayMs = 2000; 

//////////////////////////////////////////////
// Device Details Structure
//////////////////////////////////////////////
struct deviceDetails {
    int prox;
    int ambientLight;
    int whiteLight;
    double rHum;
    double temp;
    double accX;
    double accY;
    double accZ;
};

//////////////////////////////////////////////
// Function Declarations
//////////////////////////////////////////////
int httpGetWithHeaders(String serverURL, String *headerKeys, String *headerVals, int numHeaders);
bool gcfGetWithHeader(String serverUrl, String userId, time_t time, deviceDetails *details);
String generateM5DetailsHeader(String userId, time_t time, deviceDetails *details);
int httpPostFile(String serverURL, String *headerKeys, String *headerVals, int numHeaders, String filePath);
bool gcfPostFile(String serverUrl, String filePathOnSD, String userId, time_t time, deviceDetails *details);
String writeDataToFile(byte * fileData, size_t fileSizeInBytes);
int getNextFileNumFromEEPROM();
double convertFintoC(double f);
double convertCintoF(double c);

// New functions for fetching and displaying the latest sensor data:
void fetchAndDisplayLatestData();
String formatTimestamp(unsigned long ts);

//////////////////////////////////////////////
// Setup Function
//////////////////////////////////////////////
void setup() {
  M5.begin();
  M5.IMU.Init();

  // Initialize sensors
  if (!vcnl4040.begin()) {
    Serial.println("Couldn't find VCNL4040 chip");
    while (1) delay(1);
  }
  Serial.println("Found VCNL4040 chip");

  if (!sht4.begin()) {
    Serial.println("Couldn't find SHT4x");
    while (1) delay(1);
  }
  Serial.println("Found SHT4x sensor");
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);

  // Connect to WiFi
  WiFi.begin(wifiNetworkName.c_str(), wifiPassword.c_str());
  Serial.printf("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\n\nConnected to WiFi with IP: ");
  Serial.println(WiFi.localIP());

  // Initialize NTP time
  timeClient.begin();
  timeClient.setTimeOffset(3600 * -7);

  // Initialize display with a welcome message
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("M5Core2 Sensor Uploader");
  M5.Lcd.println("Press A for Latest Data");
}

//////////////////////////////////////////////
// Main Loop
//////////////////////////////////////////////
void loop() {
  M5.update();

  // If Button A is pressed, navigate to the Latest Data screen.
  if (M5.BtnA.wasPressed()) {
    fetchAndDisplayLatestData();
    // Show a prompt to return.
    M5.Lcd.setTextSize(2);
    M5.Lcd.println("\nPress B to return");
    // Wait until Button B is pressed.
    while (true) {
      M5.update();
      if (M5.BtnB.wasPressed()) {
        // Clear the screen and show the main screen again.
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.setTextSize(2);
        M5.Lcd.println("M5Core2 Sensor Uploader");
        M5.Lcd.println("Press A for Latest Data");
        break;
      }
      delay(100);
    }
  }

  // ----- Normal Sensor Reading & Uploading Process -----
  // Read sensor values
  uint16_t prox = vcnl4040.getProximity();
  uint16_t ambientLight = vcnl4040.getLux();
  uint16_t whiteLight = vcnl4040.getWhiteLight();
  sensors_event_t rHum, temp;
  sht4.getEvent(&rHum, &temp);
  float temperatureF = convertCintoF(temp.temperature);
  float humidity = rHum.relative_humidity;
  float accX, accY, accZ;
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  accX *= 9.8;
  accY *= 9.8;
  accZ *= 9.8;

  // Update time
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  // Use milliseconds for the capture time (as expected by your Firestore document)
  unsigned long epochMillis = epochTime * 1000;

  // Prepare the device details structure
  deviceDetails details;
  details.prox = prox;
  details.ambientLight = ambientLight;
  details.whiteLight = whiteLight;
  details.temp = temp.temperature;
  details.rHum = humidity;
  details.accX = accX;
  details.accY = accY;
  details.accZ = accZ;

  // Upload sensor data using a GET with custom headers (your original method)
  gcfGetWithHeader(URL_GCF_UPLOAD, userId, epochTime, &details);

  delay(timerDelayMs);
}

//////////////////////////////////////////////
// Function: Fetch & Display Latest Sensor Data
//////////////////////////////////////////////
void fetchAndDisplayLatestData() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Fetching Latest Data...");

  HTTPClient http;
  http.begin(URL_GCF_LATEST);
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    String payload = http.getString();
    Serial.println("Latest Data Payload:");
    Serial.println(payload);

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      // Extract values from the JSON response
      float temperature = doc["shtDetails"]["temp"];
      float humidity = doc["shtDetails"]["rHum"];
      unsigned long captureTime = doc["otherDetails"]["captureTime"];
      unsigned long uploadTime = doc["otherDetails"]["cloudUploadTime"];

      String formattedCaptureTime = formatTimestamp(captureTime);
      String formattedUploadTime = formatTimestamp(uploadTime);

      // Clear and update the display with fetched data
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setCursor(0, 0);
      M5.Lcd.println("Latest Sensor Data:");
      M5.Lcd.println();
      M5.Lcd.printf("Temp: %.2f C\n", temperature);
      M5.Lcd.printf("Hum: %.2f %%\n", humidity);
      M5.Lcd.printf("Captured:\n%s\n", formattedCaptureTime.c_str());
      M5.Lcd.printf("Uploaded:\n%s\n", formattedUploadTime.c_str());
    } else {
      M5.Lcd.println("JSON parse error!");
      Serial.println("Error parsing JSON");
    }
  } else {
    M5.Lcd.printf("HTTP error: %d\n", httpResponseCode);
    Serial.printf("HTTP error: %d\n", httpResponseCode);
  }
  http.end();
}

//////////////////////////////////////////////
// Helper: Format Timestamp to Human-Readable Time
//////////////////////////////////////////////
String formatTimestamp(unsigned long ts) {
  time_t seconds = ts / 1000;
  struct tm timeinfo;
  localtime_r(&seconds, &timeinfo);
  char buffer[16];
  strftime(buffer, sizeof(buffer), "%I:%M:%S%p", &timeinfo);
  return String(buffer);
}

//////////////////////////////////////////////
// --- Original Functions from challenge #1 ---
//////////////////////////////////////////////
int httpGetWithHeaders(String serverURL, String *headerKeys, String *headerVals, int numHeaders) {
  HTTPClient http;
  http.begin(serverURL);
  for (int i = 0; i < numHeaders; i++)
    http.addHeader(headerKeys[i].c_str(), headerVals[i].c_str());
  int httpResCode = http.GET();
  Serial.printf("HTTP code: %d\n", httpResCode);
  http.end();
  return httpResCode;
}

bool gcfGetWithHeader(String serverUrl, String userId, time_t time, deviceDetails *details) {
  const int numHeaders = 1;
  String headerKeys[numHeaders] = {"M5-Details"};
  String headerVals[numHeaders];
  headerVals[0] = generateM5DetailsHeader(userId, time, details);
  int resCode = httpGetWithHeaders(serverUrl, headerKeys, headerVals, numHeaders);
  return (resCode == 200);
}

String generateM5DetailsHeader(String userId, time_t time, deviceDetails *details) {
  StaticJsonDocument<650> doc;
  JsonObject objVcnlDetails = doc.createNestedObject("vcnlDetails");
  objVcnlDetails["prox"] = details->prox;
  objVcnlDetails["al"] = details->ambientLight;
  objVcnlDetails["rwl"] = details->whiteLight;
  
  JsonObject objShtDetails = doc.createNestedObject("shtDetails");
  objShtDetails["temp"] = details->temp;
  objShtDetails["rHum"] = details->rHum;
  
  JsonObject objM5Details = doc.createNestedObject("m5Details");
  objM5Details["ax"] = details->accX;
  objM5Details["ay"] = details->accY;
  objM5Details["az"] = details->accZ;
  
  JsonObject objOtherDetails = doc.createNestedObject("otherDetails");
  objOtherDetails["captureTime"] = time * 1000;
  objOtherDetails["userId"] = userId;
  
  String output;
  serializeJson(doc, output);
  return output;
}

int httpPostFile(String serverURL, String *headerKeys, String *headerVals, int numHeaders, String filePath) {
  HTTPClient http;
  http.begin(serverURL);
  for (int i = 0; i < numHeaders; i++)
    http.addHeader(headerKeys[i].c_str(), headerVals[i].c_str());
  fs::FS &sd = SD;
  File file = sd.open(filePath.c_str(), FILE_READ);
  int httpResCode = http.sendRequest("POST", &file, file.size());
  file.close();
  Serial.printf("HTTP POST code: %d\n", httpResCode);
  http.end();
  return httpResCode;
}

bool gcfPostFile(String serverUrl, String filePathOnSD, String userId, time_t time, deviceDetails *details) {
  const int numHeaders = 3;
  String headerKeys[numHeaders] = { "Content-Type", "Content-Disposition", "M5-Details" };
  String headerVals[numHeaders];
  headerVals[0] = "text/plain";
  String filename = filePathOnSD.substring(filePathOnSD.lastIndexOf("/") + 1);
  headerVals[1] = "attachment; filename=" + filename;
  headerVals[2] = generateM5DetailsHeader(userId, time, details);
  int numAttempts = 1;
  int resCode = httpPostFile(serverUrl, headerKeys, headerVals, numHeaders, filePathOnSD);
  while (resCode != 200) {
    if (++numAttempts >= 10)
      break;
    resCode = httpPostFile(serverUrl, headerKeys, headerVals, numHeaders, filePathOnSD);
  }
  return (resCode == 200);
}

String writeDataToFile(byte * fileData, size_t fileSizeInBytes) {
  Serial.println("Writing file to SD card...");
  fs::FS &sd = SD;
  int fileNumber = getNextFileNumFromEEPROM();
  String path = "/file_" + String(fileNumber) + ".txt";
  File file = sd.open(path.c_str(), FILE_WRITE);
  if (file) {
    file.write(fileData, fileSizeInBytes);
    EEPROM.write(0, fileNumber);
    EEPROM.commit();
    file.close();
    return path;
  } else {
    Serial.println("Failed to open file for writing");
    return "";
  }
}

int getNextFileNumFromEEPROM() {
  #define EEPROM_SIZE 1
  EEPROM.begin(EEPROM_SIZE);
  int fileNumber = EEPROM.read(0) + 1;
  return fileNumber;
}

double convertFintoC(double f) { return (f - 32) * 5.0 / 9.0; }
double convertCintoF(double c) { return (c * 9.0 / 5.0) + 32; }
