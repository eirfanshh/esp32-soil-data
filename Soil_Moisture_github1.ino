*Soil_Moisture_github1.ino
  
***************************************************
 * ESP32 Soil Moisture Monitoring with Blynk + GitHub
 ***************************************************/

// Blynk Template Info
#define BLYNK_TEMPLATE_ID "TMPL6zJYicvIo"
#define BLYNK_TEMPLATE_NAME "Sensor 1"
#define BLYNK_AUTH_TOKEN "3I_dK2Abce1QBE94pp-Dm0zqFBYklE-K"

// Libraries
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>
#include <base64.h>
#include <time.h>

// Wi-Fi credentials
char ssid[] = "shahir6979_2.4G";
char pass[] = "892581181";

// GitHub settings
const char* githubToken = "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"; 
const char* githubRepo = "eirfanshh/esp32-soil-data";
const char* githubFilePath = "data/sensor_data.txt";

// Sensor pins
const int sensor1Pin = 39;
const int sensor2Pin = 34;
const int sensor3Pin = 36;

// Calibration values
int dryValue = 4095;
int wetValue = 1500;

BlynkTimer timer;
String lastSha = "";
bool uploadedThisMinute = false;

// Reset logic
unsigned long lastResetTime = 0;
const unsigned long resetInterval = 36UL * 60UL * 60UL; // 36 hours in seconds

/***************************************************
 * Base64 Decode Function
 ***************************************************/
bool isBase64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

String base64Decode(String input) {
  const char* base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int inLen = input.length();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char charArray4[4], charArray3[3];
  String output = "";

  while (inLen-- && (input[in_] != '=') && isBase64(input[in_])) {
    charArray4[i++] = input[in_]; in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++)
        charArray4[i] = strchr(base64Chars, charArray4[i]) - base64Chars;

      charArray3[0] = (charArray4[0] << 2) + ((charArray4[1] & 0x30) >> 4);
      charArray3[1] = ((charArray4[1] & 0xf) << 4) + ((charArray4[2] & 0x3c) >> 2);
      charArray3[2] = ((charArray4[2] & 0x3) << 6) + charArray4[3];

      for (i = 0; i < 3; i++)
        output += (char)charArray3[i];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++)
      charArray4[j] = 0;

    for (j = 0; j < 4; j++)
      charArray4[j] = strchr(base64Chars, charArray4[j]) - base64Chars;

    charArray3[0] = (charArray4[0] << 2) + ((charArray4[1] & 0x30) >> 4);
    charArray3[1] = ((charArray4[1] & 0xf) << 4) + ((charArray4[2] & 0x3c) >> 2);
    charArray3[2] = ((charArray4[2] & 0x3) << 6) + charArray4[3];

    for (j = 0; j < (i - 1); j++) output += (char)charArray3[j];
  }

  return output;
}

/***************************************************
 * Send Sensor Data to Blynk
 ***************************************************/
void sendSensorData() {
  int raw1 = analogRead(sensor1Pin);
  int raw2 = analogRead(sensor2Pin);
  int raw3 = analogRead(sensor3Pin);

  int moisture1 = constrain(map(raw1, dryValue, wetValue, 0, 100), 0, 100);
  int moisture2 = constrain(map(raw2, dryValue, wetValue, 0, 100), 0, 100);
  int moisture3 = constrain(map(raw3, dryValue, wetValue, 0, 100), 0, 100);

  Blynk.virtualWrite(V1, moisture1);
  Blynk.virtualWrite(V2, moisture2);
  Blynk.virtualWrite(V3, moisture3);

  Serial.printf("Sensor1: %d%% | Sensor2: %d%% | Sensor3: %d%%\n", moisture1, moisture2, moisture3);
}

/***************************************************
 * Upload Data to GitHub (Append Mode)
 ***************************************************/
void uploadToGitHub() {
  int raw1 = analogRead(sensor1Pin);
  int raw2 = analogRead(sensor2Pin);
  int raw3 = analogRead(sensor3Pin);

  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

  String newLine = String(timestamp) + "," + raw1 + "," + raw2 + "," + raw3 + "\n";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://api.github.com/repos/" + String(githubRepo) + "/contents/" + githubFilePath;

  // Step 1: Get current file content and SHA
  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + String(githubToken));
  https.addHeader("User-Agent", "ESP32");
  int httpCode = https.GET();
  String oldContent = "";
  if (httpCode == 200) {
    String response = https.getString();
    int shaIndex = response.indexOf("\"sha\":\"") + 7;
    lastSha = response.substring(shaIndex, response.indexOf("\"", shaIndex));

    int contentIndex = response.indexOf("\"content\":\"") + 11;
    int contentEnd = response.indexOf("\"", contentIndex);
    String encodedOldContent = response.substring(contentIndex, contentEnd);
    encodedOldContent.replace("\\n", "\n");
    oldContent = base64Decode(encodedOldContent);
  }
  https.end();

  // Step 2: Append new data
  String updatedContent = oldContent + newLine;
  String encodedContent = base64::encode(updatedContent);

  // Step 3: Upload updated file
  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + String(githubToken));
  https.addHeader("Content-Type", "application/json");
  https.addHeader("User-Agent", "ESP32");

  String payload = "{\"message\":\"Append sensor data\",\"content\":\"" + encodedContent + "\",\"sha\":\"" + lastSha + "\"}";
  httpCode = https.PUT(payload);

  if (httpCode > 0) {
    Serial.printf("GitHub Response: %d\n", httpCode);
    Serial.println(https.getString());
  } else {
    Serial.printf("GitHub upload failed: %d\n", httpCode);
  }
  https.end();
}

/***************************************************
 * Clear GitHub File After 36 Hours
 ***************************************************/
void clearGitHubFile() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://api.github.com/repos/" + String(githubRepo) + "/contents/" + githubFilePath;

  // Get current SHA
  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + String(githubToken));
  https.addHeader("User-Agent", "ESP32");
  int httpCode = https.GET();
  if (httpCode == 200) {
    String response = https.getString();
    int shaIndex = response.indexOf("\"sha\":\"") + 7;
    lastSha = response.substring(shaIndex, response.indexOf("\"", shaIndex));
  }
  https.end();

  // New content with header only
  String header = "timestamp,sensor1,sensor2,sensor3\n";
  String encodedContent = base64::encode(header);

  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + String(githubToken));
  https.addHeader("Content-Type", "application/json");
  https.addHeader("User-Agent", "ESP32");

  String payload = "{\"message\":\"Reset file\",\"content\":\"" + encodedContent + "\",\"sha\":\"" + lastSha + "\"}";
  httpCode = https.PUT(payload);

  if (httpCode > 0) {
    Serial.printf("File cleared. Response: %d\n", httpCode);
    Serial.println(https.getString());
  } else {
    Serial.printf("Failed to clear file: %d\n", httpCode);
  }
  https.end();
}

/***************************************************
 * Check Time and Upload Every 10 Minutes
 ***************************************************/
void checkTimeAndUpload() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  int minute = timeinfo->tm_min;
  int second = timeinfo->tm_sec;

  // Upload every 10 minutes
  if (minute % 10 == 0 && second == 0 && !uploadedThisMinute) {
    uploadToGitHub();
    uploadedThisMinute = true;
  }
  if (second != 0) {
    uploadedThisMinute = false;
  }

  // Clear file after 36 hours
  if ((time(nullptr) - lastResetTime) >= resetInterval) {
    clearGitHubFile();
    lastResetTime = time(nullptr);
  }
}

/***************************************************
 * Setup
 ***************************************************/
void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Malaysia Time (GMT+8)
  configTime(28800, 0, "pool.ntp.org");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  lastResetTime = time(nullptr); // Initialize reset timer

  timer.setInterval(5000L, sendSensorData); // Update Blynk every 5 sec
  timer.setInterval(1000L, checkTimeAndUpload); // Check time every second
}

/***************************************************
 * Loop
 ***************************************************/
void loop() {
  Blynk.run();
  timer.run();
}

*
