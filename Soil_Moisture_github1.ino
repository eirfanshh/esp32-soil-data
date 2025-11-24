*Soil_Moisture_github1.ino
  

/****************************************************
 * ESP32 Soil Moisture + Blynk + GitHub (Malaysia Time)
 * Patched for reliability:
 *  - Strips CRLF before Base64 decode from GitHub GET
 *  - Uses correct SHA only when file exists
 *  - Logs error body on failed PUT
 *  - Performs ONE immediate upload on boot after NTP
 *  - Then uploads every 10 minutes
 ****************************************************/

// ------------------ Blynk Template Info ------------------
#define BLYNK_TEMPLATE_ID   "TMPL6zJYicvIo"
#define BLYNK_TEMPLATE_NAME "Sensor 1"
#define BLYNK_AUTH_TOKEN    "3I_dK2Abce1QBE94pp-Dm0zqFBYklE-K"

// ------------------ Libraries ------------------
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <time.h>
#include <ctype.h>

// ------------------ Wi-Fi credentials ------------------
char ssid[] = "shahir6979_2.4G";
char pass[] = "892581181";

// ------------------ GitHub settings ------------------
const char* githubToken    = "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";               // PAT with repo scope
const char* githubRepo     = "eirfanshh/esp32-soil-data";       // e.g., "eirfanshh/esp32-soil-data"
const char* githubFilePath = "data/sensor_data.txt";          // e.g., "data/sensor_data.txt"

// ------------------ Sensor pins (ESP32 ADC1) ------------------
const int sensor1Pin = 39;  // ADC1
const int sensor2Pin = 34;  // ADC1
const int sensor3Pin = 36;  // ADC1

// ------------------ Calibration values (for Blynk %) ------------------
int dryValue = 4095;  // DRY raw ADC
int wetValue = 1500;  // WET raw ADC

BlynkTimer timer;
String lastSha = "";
unsigned long lastUploadTime = 0;
const unsigned long uploadInterval = 10UL * 60UL * 1000UL; // 10 minutes

unsigned long lastResetTime = 0;
const unsigned long resetInterval = 36UL * 60UL * 60UL;    // 36 hours (seconds)

/****************************************************
 * Helpers: Base64 decode (for GitHub GET content)
 ****************************************************/
bool isBase64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

String base64Decode(const String& input) {
  static const char* base64Chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int inLen = input.length();
  int i = 0, in_ = 0;
  unsigned char charArray4[4], charArray3[3];
  String output;

  while (inLen-- && (input[in_] != '=') && strchr(base64Chars, input[in_])) {
    charArray4[i++] = input[in_++];
    if (i == 4) {
      for (i = 0; i < 4; i++)
        charArray4[i] = strchr(base64Chars, charArray4[i]) - base64Chars;

      charArray3[0] = (charArray4[0] << 2) + ((charArray4[1] & 0x30) >> 4);
      charArray3[1] = ((charArray4[1] & 0x0F) << 4) + ((charArray4[2] & 0x3C) >> 2);
      charArray3[2] = ((charArray4[2] & 0x03) << 6) +  charArray4[3];

      for (i = 0; i < 3; i++) output += (char)charArray3[i];
      i = 0;
    }
  }

  if (i) {
    for (int j = i; j < 4; j++) charArray4[j] = 0;
    for (int j = 0; j < 4; j++) {
      char* p = strchr(base64Chars, charArray4[j]);
      charArray4[j] = p ? (p - base64Chars) : 0;
    }

    charArray3[0] = (charArray4[0] << 2) + ((charArray4[1] & 0x30) >> 4);
    charArray3[1] = ((charArray4[1] & 0x0F) << 4) + ((charArray4[2] & 0x3C) >> 2);
    charArray3[2] = ((charArray4[2] & 0x03) << 6) +  charArray4[3];

    for (int j = 0; j < (i - 1); j++) output += (char)charArray3[j];
  }
  return output;
}

/****************************************************
 * Utility: averaged moisture % (reduces noise)
 ****************************************************/
int readMoisturePercent(int pin, int samples = 10) {
  long acc = 0;
  for (int i = 0; i < samples; i++) {
    acc += analogRead(pin);
    delay(2);
  }
  int raw = acc / samples;
  return constrain(map(raw, dryValue, wetValue, 0, 100), 0, 100);
}

/****************************************************
 * Optional: wait until NTP time is available
 ****************************************************/
bool waitForTime(uint32_t timeoutMs = 10000) {
  uint32_t start = millis();
  struct tm timeinfo;
  while (millis() - start < timeoutMs) {
    if (getLocalTime(&timeinfo)) return true;
    delay(100);
  }
  return false;
}

/****************************************************
 * Blynk: send moisture % to V1/V2/V3 every 5s
 ****************************************************/
void sendSensorData() {
  int moisture1 = readMoisturePercent(sensor1Pin);
  int moisture2 = readMoisturePercent(sensor2Pin);
  int moisture3 = readMoisturePercent(sensor3Pin);

  Blynk.virtualWrite(V1, moisture1);
  Blynk.virtualWrite(V2, moisture2);
  Blynk.virtualWrite(V3, moisture3);

  Serial.printf("Moisture%% S1=%d S2=%d S3=%d\n", moisture1, moisture2, moisture3);
}

/****************************************************
 * GitHub: append one line with MYT timestamp + raw ADCs
 ****************************************************/
void uploadToGitHub() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://api.github.com/repos/" + String(githubRepo) + "/contents/" + githubFilePath;

  // 1) GET current content to obtain SHA and decode old file
  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + String(githubToken));
  https.addHeader("User-Agent", "ESP32");
  int httpCode = https.GET();
  String oldContent = "";
  bool fileExists = false;

  if (httpCode == 200) {
    fileExists = true;
    String response = https.getString();
    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      Serial.printf("JSON parse error (GET): %s\n", err.c_str());
      https.end();
      return;
    }
    if (!doc.containsKey("sha")) {
      Serial.println("No 'sha' in GET response; aborting.");
      https.end();
      return;
    }
    lastSha = doc["sha"].as<String>();
    String encodedOldContent = doc["content"].as<String>();
    encodedOldContent.replace("\n", "");
    encodedOldContent.replace("\r", ""); // strip CRLF fully
    oldContent = base64Decode(encodedOldContent);
  } else if (httpCode == 404) {
    // File does not exist yet—start with header
    fileExists = false;
    oldContent = "timestamp,sensor1,sensor2,sensor3\n";
  } else {
    Serial.printf("GET failed: %d\n", httpCode);
    Serial.println(https.getString());
    https.end();
    return;
  }
  https.end();

  // 2) Build new line with Malaysia Time (UTC+8)
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);       // uses offset set in configTime
  char ts[25] = "1970-01-01 00:00:00";
  if (ti) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", ti);

  // Raw ADC values
  int raw1 = analogRead(sensor1Pin);
  int raw2 = analogRead(sensor2Pin);
  int raw3 = analogRead(sensor3Pin);

  String newLine = String(ts) + "," + raw1 + "," + raw2 + "," + raw3 + "\n";
  String updatedContent = oldContent + newLine;
  String encodedContent = base64::encode(updatedContent);

  // 3) PUT updated file (include SHA only if file exists)
  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + String(githubToken));
  https.addHeader("Content-Type", "application/json");
  https.addHeader("User-Agent", "ESP32");

  String payload;
  if (fileExists) {
    payload = "{\"message\":\"Append sensor data\",\"content\":\"" + encodedContent + "\",\"sha\":\"" + lastSha + "\"}";
  } else {
    payload = "{\"message\":\"Create sensor data\",\"content\":\"" + encodedContent + "\"}";
  }

  httpCode = https.PUT(payload);
  String body = https.getString();

  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("GitHub upload success: %d\n", httpCode);
  } else {
    Serial.printf("GitHub upload failed: %d\n", httpCode);
    Serial.println(body); // print API error details
  }
  https.end();
}

/****************************************************
 * Optional: clear file (reset header) every 36 hours
 ****************************************************/
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
    StaticJsonDocument<1024> doc;
    if (!deserializeJson(doc, response)) {
      lastSha = doc["sha"].as<String>();
    }
  }
  https.end();

  // Reset with header
  String header = "timestamp,sensor1,sensor2,sensor3\n";
  String encodedContent = base64::encode(header);

  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + String(githubToken));
  https.addHeader("Content-Type", "application/json");
  https.addHeader("User-Agent", "ESP32");
  String payload = "{\"message\":\"Reset file\",\"content\":\"" + encodedContent + "\",\"sha\":\"" + lastSha + "\"}";
  httpCode = https.PUT(payload);

  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("File cleared: %d\n", httpCode);
  } else {
    Serial.printf("Failed to clear file: %d\n", httpCode);
    Serial.println(https.getString());
  }
  https.end();
}

/****************************************************
 * Scheduler: check each second; upload every 10 minutes
 ****************************************************/
void checkTimeAndUpload() {
  unsigned long nowMillis = millis();

  if (nowMillis - lastUploadTime >= uploadInterval) {
    uploadToGitHub();
    lastUploadTime = nowMillis;
  }

  if ((time(nullptr) - lastResetTime) >= resetInterval) {
    clearGitHubFile();
    lastResetTime = time(nullptr);
  }
}

/****************************************************
 * Setup (includes ONE immediate upload after NTP)
 ****************************************************/
void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Malaysia Time (UTC+8)
  configTime(8 * 3600, 0, "pool.ntp.org");
  if (!waitForTime()) {
    Serial.println("NTP not ready within 10s; proceeding with fallback time.");
  }

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  lastResetTime = time(nullptr);

  // ONE immediate upload on boot to ensure file is fresh
  uploadToGitHub();
  lastUploadTime = millis();

  // Live moisture % to Blynk every 5s
  timer.setInterval(5000L, sendSensorData);

  // Check upload/reset every 1s
  timer.setInterval(1000L, checkTimeAndUpload);
}

/****************************************************
 * Loop
 ****************************************************/
void loop() {
  Blynk.run();
  timer.run();
}
