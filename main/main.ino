#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Max72xxPanel.h>

// Network credentials
const char* ssid = "Bezos Bucks2";
const char* password = "memestocks";

// NTP server
const char* ntpServer = "pool.ntp.org";

// Time zone information for Pacific Time (PST/PDT)
const int utcOffsetInSeconds = -8 * 3600;  // UTC-8 for standard time
const int daylightOffsetInSeconds = 3600;  // +1 hour for daylight saving time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, 0, 60000); // Update interval: 60 seconds

// 511 Transit API host and endpoint
const char* host = "api.511.org";
const int port = 80;
const char* endpoint = "/transit/StopMonitoring"
  "?api_key=55026653-b50f-4ffc-aa52-53c4ee0b8bd4&agency=CT&stopCode=70261&format=json";

// Display vars
int pinCS = 2; 
int numberOfHorizontalDisplays = 24;
int numberOfVerticalDisplays   = 1;
int numDepartures = numberOfHorizontalDisplays / 12;
Max72xxPanel matrix = Max72xxPanel(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);
String content = "Transit Display";
int wait = 50; // In milliseconds
int spacer = 1;
int width  = 5 + spacer; // The font width is 5 pixels
int departureCounter = 0;
int displayBufSize = 33;

void setup() {
  configureMatrix();
  initializeSerial();
  initializeWiFi();
  initializeNTP();
}

void loop() {
  // Make the HTTP GET request
  if (WiFi.status() == WL_CONNECTED) {
    sendHTTPRequest();
  } else {
    Serial.println("Error in Wi-Fi connection");
  }

  // Display the static string
  char stringBuf[displayBufSize];
  content.toCharArray(stringBuf, displayBufSize);
  displayStaticString(stringBuf);

  Serial.println("\nValue of stringBuf:");
  Serial.println(stringBuf);

  // Run once per minute
  delay(30000);
  Serial.println("\n30 seconds until next update...\n");
  delay(30000);
}

void sendHTTPRequest(){
  WiFiClient wifiClient;
  HttpClient http(wifiClient, host, port);

  // Send the request
  http.beginRequest();
  http.get(endpoint);
  http.sendHeader("Accept-Encoding", "identity");
  http.endRequest();

  // Get the response
  int statusCode = http.responseStatusCode();
  String response = http.responseBody();

  // Check the HTTP status code
  Serial.printf("HTTP GET request sent, code: %d\n", statusCode);
  
  if (statusCode == 200) {      
    Serial.println("Raw Response:");
    Serial.println(response);

    // Check for BOM and remove it if present
    if (response.startsWith("\xEF\xBB\xBF")) {
      Serial.println("BOM detected, removing it");
      response.remove(0, 3);
    }
    decodeJSON(response);
  } else {
    Serial.printf("HTTP GET request failed with code: %d\n", statusCode);
    // Display HTTP code if error occurred
    char buffer[displayBufSize];  // Buffer to hold the formatted string
    sprintf(buffer, "HTTP code: %d", statusCode);
    displayStaticString(buffer);
  }
}

void decodeJSON(String response){
  // Create a JSON document
  JsonDocument doc;

  // Deserialize JSON
  DeserializationError error = deserializeJson(doc, response);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  // Extract and print required values from 'MonitoredStopVisit' array
  departureCounter = 0; // Keep track of number of departures
  String departureList = "";
  Serial.println("\nDiridon Northbound Departure Board:");
  JsonArray monitoredStopVisit = doc["ServiceDelivery"]["StopMonitoringDelivery"]["MonitoredStopVisit"].as<JsonArray>();
  if (monitoredStopVisit.size() == 0) {
    Serial.println("MonitoredStopVisit array is empty.");
    displayStaticString("No Departures");
  } else {
    for (JsonObject element : monitoredStopVisit) {
      const char* lineRef = element["MonitoredVehicleJourney"]["LineRef"];
      const char* directionRef = element["MonitoredVehicleJourney"]["DirectionRef"];
      const char* datedVehicleJourneyRef = element["MonitoredVehicleJourney"]["FramedVehicleJourneyRef"]["DatedVehicleJourneyRef"];
      const char* aimedDepartureTime = element["MonitoredVehicleJourney"]["MonitoredCall"]["AimedDepartureTime"];
      time_t aimedDepartureAdjTime = convertTime(aimedDepartureTime);
      const char* expectedDepartureTime = element["MonitoredVehicleJourney"]["MonitoredCall"]["ExpectedDepartureTime"];
      time_t expectedDepartureAdjTime = convertTime(expectedDepartureTime);

      if (strstr(lineRef, "B7") != NULL) {
        //Serial.println("Skipping B7 train");
        continue;
      }

      // Run until all departures are filled
      if (departureCounter >= numDepartures) {
        break;
      }
      departureList += printDeparture(datedVehicleJourneyRef, aimedDepartureAdjTime, expectedDepartureAdjTime);
      departureCounter++;
    }
    content = departureList;
  }
}

// Configure display
void configureMatrix(){
  matrix.setIntensity(2); // Use a value between 0 and 15 for brightness
  for (int i = 0; i < numberOfHorizontalDisplays * numberOfVerticalDisplays; i++) {
    matrix.setRotation(i, 1); // Set the rotation of each display
  }
  matrix.fillScreen(LOW);
  matrix.setTextSize(1); // Set the text size
  matrix.setTextWrap(false); // Don't wrap text at end of line
  matrix.write();
}

// Initialize NTPClient
void initializeNTP() {
  timeClient.begin();
  while (!timeClient.update()) {
    delay(500);
  }
  Serial.println("Time synchronized with NTP server");
  displayStaticString("Synched to NTP");

  // Set the time based on the NTP server
  setTime(timeClient.getEpochTime());
}

// Connect to Wi-Fi
void initializeWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("\nConnecting to Wi-Fi...");
  displayStaticString("Loading WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to Wi-Fi");
  displayStaticString("WiFi Connected");
}

void initializeSerial(){
  Serial.begin(115200);
  delay(500); // Wait for Serial terminal
  Serial.println("\nLoading Transit Display...");
}

// Display Text on LED Matrix
void displayStaticString(const char* text) {
  matrix.fillScreen(LOW);
  int text_size = strlen(text);

  for (int i = 0; i < text_size; i+=17) {
    matrix.setCursor(0, i);  // Start at top-left corner
    matrix.print(text);      // Print the text
  }
  
  matrix.write();          // Write the text to the display
}

String printDeparture(const char* trainNum, time_t aimedDepartureTime, time_t expectedDepartureTime) {
  // Step 1: Calculate the train delay in minutes
  int delayMinutes = (expectedDepartureTime - aimedDepartureTime) / 60;
  
  // Step 2: Create the delay string
  char delayStr[10];
  if (delayMinutes == 0) {
    strcpy(delayStr, "ONTIME");
  } else {
    sprintf(delayStr, "%dM LT", delayMinutes);
  }
  
  // Step 3: Create the expected departure time string in 12-hour format with AM/PM
  char departureTimeStr[10];
  int hr = hour(expectedDepartureTime);
  int min = minute(expectedDepartureTime);
  bool isPM = hr >= 12;
  
  hr = hr % 12;
  if (hr == 0) hr = 12;

  sprintf(departureTimeStr, "%02d:%02d", hr, min);
  
  // Step 4: Concatenate the variables into the final string
  char finalStr[20];
  sprintf(finalStr, "%s %s %s", trainNum, departureTimeStr, delayStr);

  // Print the final string
  Serial.println(finalStr);

  return finalStr;
}

// Check if daylight saving time is currently active
bool isDST(int month, int day, int hour, int weekday) {
  // DST starts on the second Sunday in March
  // DST ends on the first Sunday in November
  if (month > 3 && month < 11) return true;
  if (month < 3 || month > 11) return false;

  int previousSunday = day - weekday;
  if (month == 3) return previousSunday >= 8;
  if (month == 11) return previousSunday < 1;
  return false;
}

time_t convertTime(const char* inputTime) {
  // Set the time based on the NTP server
  setTime(timeClient.getEpochTime());

  // Parse the UTC datetime string
  struct tm tm;
  strptime(inputTime, "%Y-%m-%dT%H:%M:%SZ", &tm);
  time_t utcTime = mktime(&tm);

  // Determine if DST is active
  bool dstActive = isDST(month(utcTime), day(utcTime), hour(utcTime), weekday(utcTime));

  // Convert to local time (Pacific Time with manual DST adjustment)
  time_t localTime = utcTime + utcOffsetInSeconds;
  if (dstActive) {
    localTime += daylightOffsetInSeconds;
  }
  return localTime;
}

void print12HourTime(time_t time) {
  int hr = hour(time);
  int min = minute(time);
  int sec = second(time);
  bool isPM = hr >= 12;
  
  hr = hr % 12;
  if (hr == 0) hr = 12;

  Serial.printf("%02d:%02d:%02d %s\n", hr, min, sec, isPM ? "PM" : "AM");
}
