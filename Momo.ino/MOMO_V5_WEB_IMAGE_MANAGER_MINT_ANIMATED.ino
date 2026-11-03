// ============================================================
// MOMO ROBOT V5 - OLED ANTI-BURN-IN EDITION
// ESP32-C3 Super Mini
//
// RoboEyes + Touch + WiFi Setup + Editable Weather Location
// Google NTP (IST) + OpenWeatherMap only + Editable Location + OLED Weather Icons + Offline Mode
//
// CONTROLS
// ------------------------------------------------------------
// NORMAL FACE
//   Single tap       -> HAPPY for 5 sec
//   Triple tap       -> ANGRY for 5 sec
//   5 sec long press -> TIME
//
// TIME -> WEATHER -> DETAILS -> SYSTEM -> TIME (single tap)
// Any menu page: 5 sec long press -> NORMAL FACE
//
// NO TOUCH
//   60 sec -> SAD
//   20 sec -> NORMAL
//
// STARTUP
//   MOMO (3 sec) -> NORMAL FACE
//
// SETUP AP
//   SSID     : Momo-Setup
//   Password : 12345678
//   IP       : 192.168.4.1
//
// WIFI PERSISTENCE
//   Credentials are saved in EEPROM. On every boot Momo
//   tries the saved network first. Only if that fails (or
//   nothing is saved) does it open Setup AP.
// ============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <time.h>

#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <FluxGarage_RoboEyes.h>


// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define OLED_SDA 6  // ESP32-C3 GPIO6
#define OLED_SCL 7  // ESP32-C3 GPIO7
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

RoboEyes<Adafruit_SSD1306> roboEyes(display);


// ============================================================
// TOUCH
// ============================================================

#define TOUCH_PIN 10 // ESP32-C3 GPIO10
#define TOUCH_ACTIVE HIGH


// ============================================================
// WIFI SETUP AP
// ============================================================

const char* SETUP_AP_NAME = "Momo-Setup";
const char* SETUP_AP_PASS = "12345678";

WebServer server(80);
static const char *CUSTOM_IMAGE_PATH = "/momo_page4.bin";
static const size_t CUSTOM_IMAGE_BYTES = 1024; // 128x64, 1 bit per pixel
File imageUploadFile;
bool littleFsReady = false;
void handleImageUpload();
void handleImageUploadResult();


// ============================================================
// SAVED CONFIGURATION
// ============================================================

String wifiSSID;
String wifiPassword;
String openWeatherApiKey;
String ownerName = "MOMO";
String cityName = "";
String welcomeText = "WELCOME TO MOMO";
uint8_t welcomeTextStyle = 0; // 0=Classic, 1=Bold, 2=Compact
uint8_t bootAnimation = 0; // 0=Cloud, 1=Rain, 2=Stars, 3=Radar, 4=Matrix

bool hasSavedWeather = false;

float latitude = 0.0f;  // Set from Web Setup; not hardcoded
float longitude = 0.0f; // Set from Web Setup; not hardcoded


// ============================================================
// NETWORK / OFFLINE MODE
// ============================================================

bool offlineMode = false;

unsigned long lastWiFiRetry = 0;

const unsigned long SETUP_TIMEOUT = 20000;
const unsigned long WIFI_RETRY_INTERVAL = 60000;


// ============================================================
// NTP (India = UTC + 5:30)
// ============================================================

const long GMT_OFFSET_SEC = 19800;
const int DAYLIGHT_OFFSET_SEC = 0;


// ============================================================
// WEATHER
// ============================================================

float temperature = 0;
float feelsLike = 0;
float humidity = 0;
float windSpeed = 0;
float pressureHPa = 0;
float cloudiness = 0;
float visibilityMeters = 0;

// Sunrise/sunset are supplied by OpenWeatherMap (Unix seconds).
unsigned long sunriseEpoch = 0;
unsigned long sunsetEpoch = 0;
bool isDaytime = true;

int weatherCode = -1;

String weatherText = "Unknown";

unsigned long lastWeatherUpdate = 0;

const unsigned long WEATHER_INTERVAL = 10UL * 60UL * 1000UL;


// ============================================================
// FACE STATES
// ============================================================

enum FaceState {
  FACE_NORMAL,
  FACE_HAPPY,
  FACE_ANGRY,
  FACE_SAD
};

FaceState currentFace = FACE_NORMAL;


// ============================================================
// DISPLAY / MENU
// ============================================================

enum MenuState {
  MENU_CLOSED,
  SHOW_TIME,
  SHOW_WEATHER,
  SHOW_DETAILS,
  SHOW_SYSTEM
};

MenuState menuState = MENU_CLOSED;

unsigned long menuLastInteraction = 0;

// V5: menu pages stay open until a 5-second touch hold.
// OLED never auto-switches off or dims. A subtle vertical pixel shift
// periodically moves the panel start line to reduce static-image retention.


// ============================================================
// TOUCH STATE
// ============================================================

bool lastTouch = false;

unsigned long touchStartTime = 0;
unsigned long lastTouchTime = 0;

bool longPressHandled = false;

const unsigned long LONG_PRESS_TIME = 5000;


// ============================================================
// TRIPLE TAP
// ============================================================

int tapCount = 0;

unsigned long lastTapTime = 0;

const unsigned long TAP_GAP = 700;
const unsigned long TAP_MAX_TIME = 500;


// V5 startup splash and OLED protection globals.
// Declared before their first use for ArduinoDroid's compiler.
bool startupSplash = true;
bool splashDrawn = false;
unsigned long splashStartTime = 0;
const unsigned long SPLASH_TIME = 4000;
const unsigned long CLOUD_ANIMATION_TIME = 1800;

// OLED protection: no auto-off / no auto-dimming.
// Move the displayed image by 0-2 physical pixel rows periodically.
const unsigned long OLED_PIXEL_SHIFT_INTERVAL = 30000UL;
unsigned long lastOledPixelShift = 0;
uint8_t oledPixelShiftIndex = 0;
const uint8_t OLED_PIXEL_SHIFT_STEPS[] = {0, 1, 2, 1};
const uint8_t OLED_PIXEL_SHIFT_COUNT = sizeof(OLED_PIXEL_SHIFT_STEPS) / sizeof(OLED_PIXEL_SHIFT_STEPS[0]);

void drawBootCloud(int x, int y);

// ============================================================
// FACE TIMERS
// ============================================================

unsigned long happyStartTime = 0;
unsigned long sadStartTime = 0;
unsigned long angryStartTime = 0;

const unsigned long HAPPY_TIME = 5000;
const unsigned long ANGRY_TIME = 5000;
const unsigned long NO_TOUCH_TIME = 60000;
const unsigned long SAD_TIME = 20000;



void drawBootRain(uint32_t elapsed) {
  for (int i = 0; i < 18; i++) {
    int x = (i * 17 + 7) % 128;
    int y = (int)((elapsed / 18 + i * 11) % 54);
    display.drawLine(x, y, x - 2, y + 5, SSD1306_WHITE);
  }
}

void drawBootStars(uint32_t elapsed) {
  for (int i = 0; i < 22; i++) {
    int x = (i * 37 + 13) % 126;
    int y = (i * 19 + 4) % 42;
    if (((elapsed / 250 + i) % 3) != 0) display.drawPixel(x, y, SSD1306_WHITE);
  }
  display.drawCircle(64, 23, 13, SSD1306_WHITE);
  display.drawCircle(64, 23, 8, SSD1306_WHITE);
}

void drawBootRadar(uint32_t elapsed) {
  display.drawCircle(64, 25, 23, SSD1306_WHITE);
  display.drawCircle(64, 25, 15, SSD1306_WHITE);
  display.drawCircle(64, 25, 7, SSD1306_WHITE);
  display.drawLine(64, 25, 64 + (int)(22 * cos(elapsed / 260.0)), 25 + (int)(22 * sin(elapsed / 260.0)), SSD1306_WHITE);
  display.drawLine(41, 25, 87, 25, SSD1306_WHITE);
  display.drawLine(64, 2, 64, 48, SSD1306_WHITE);
}

void drawBootMatrix(uint32_t elapsed) {
  for (int col = 0; col < 16; col++) {
    int x = col * 8;
    int y = (int)((elapsed / 35 + col * 13) % 55);
    display.drawPixel(x + 2, y, SSD1306_WHITE);
    if (y > 8) display.drawPixel(x + 2, y - 8, SSD1306_WHITE);
    if (y > 16) display.drawPixel(x + 2, y - 16, SSD1306_WHITE);
  }
}

void drawSelectedBootAnimation(uint32_t elapsed) {
  switch (bootAnimation) {
    case 1: drawBootRain(elapsed); break;
    case 2: drawBootStars(elapsed); break;
    case 3: drawBootRadar(elapsed); break;
    case 4: drawBootMatrix(elapsed); break;
    default: {
      int x = -38 + (int)((166UL * (elapsed < CLOUD_ANIMATION_TIME ? elapsed : CLOUD_ANIMATION_TIME)) / CLOUD_ANIMATION_TIME);
      drawBootCloud(x, 8);
      break;
    }
  }
}

void updateOledProtection() {
  // SSD1306 Set Display Start Line command (0x40..0x7F).
  // The framebuffer is unchanged; only the visible scan origin shifts.
  // This keeps OLED active at normal brightness and gently moves pixels.
  if (millis() - lastOledPixelShift >= OLED_PIXEL_SHIFT_INTERVAL) {
    lastOledPixelShift = millis();
    oledPixelShiftIndex = (oledPixelShiftIndex + 1) % OLED_PIXEL_SHIFT_COUNT;
    uint8_t rowOffset = OLED_PIXEL_SHIFT_STEPS[oledPixelShiftIndex];
    display.ssd1306_command(0x40 | (rowOffset & 0x3F));
  }
}


// ============================================================
// STARTUP SPLASH
// ============================================================

// ============================================================
// WEATHER CODE
// ============================================================

// Explicit prototypes help some mobile Arduino IDEs preprocess this sketch correctly.
void drawCenteredText(const char* text, uint8_t size, int y);
void drawCenteredBoth(const char* text, uint8_t size);
void showSearchingScreen(int dots);
void handleSetupPage();
void handleSave();
void drawSetupAPScreen();
String weatherCodeToText(int code);
void printCentered(const String &text, int width);
void printBanner(const String &title);
void setFace(FaceState state);
void saveConfig();
bool loadConfig();
void saveWeatherCache();
void loadWeatherCache();
void startSetupAP();
bool connectWiFi();
void setupNTP();
void updateWeather();
void showTimeScreen();
void showWeatherScreen();
void showWeatherDetailsScreen();
void showSystemInfoScreen();
void drawPageTransition();
void drawStatusHeader(const char* title);
void drawWeatherIcon(int code, int x, int y);
void closeMenu();
void handleTouch();
void handleFaceTimers();
void updateMenuDisplay();
void updateStartupSplash();
void updateOledProtection();
void retryWiFi();

// ============================================================
// CENTERED TEXT HELPERS
// ============================================================

// Horizontally centers text at row y.
void drawCenteredText(const char* text, uint8_t size, int y) {

  int16_t x1, y1;
  uint16_t w, h;

  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2 - x1, y);
  display.print(text);
}

// Centers text both horizontally and vertically.
void drawCenteredBoth(const char* text, uint8_t size) {

  int16_t x1, y1;
  uint16_t w, h;

  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(
    (SCREEN_WIDTH - (int)w) / 2 - x1,
    (SCREEN_HEIGHT - (int)h) / 2 - y1
  );
  display.print(text);
}

// "SEARCHING..." screen shown while connecting to saved Wi-Fi.
void showSearchingScreen(int dots) {

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  drawCenteredText("SEARCHING", 2, 10);

  const char* dotStr[] = { "", ".", "..", "..." };
  drawCenteredText(dotStr[dots % 4], 2, 30);

  String ssid = wifiSSID;
  if (ssid.length() > 21)
    ssid = ssid.substring(0, 21);

  drawCenteredText(ssid.c_str(), 1, 54);

  display.display();
}


String weatherCodeToText(int code) {
  // OpenWeatherMap condition IDs
  if (code >= 200 && code <= 232) return "Storm";
  if (code >= 300 && code <= 321) return "Drizzle";
  if (code >= 500 && code <= 531) return "Rain";
  if (code >= 600 && code <= 622) return "Snow";
  if (code >= 701 && code <= 781) return "Fog";
  if (code == 800) return "Clear";
  if (code == 801 || code == 802) return "Cloudy";
  if (code == 803 || code == 804) return "Overcast";
  return "Unknown";
}


// ============================================================
// SERIAL BANNER HELPERS
// ============================================================

void printCentered(const String &text, int width) {

  int padding = (width - (int)text.length()) / 2;

  if (padding < 0)
    padding = 0;

  for (int i = 0; i < padding; i++) {
    Serial.print(" ");
  }

  Serial.println(text);
}


void printBanner(const String &title) {

  const int width = 32;

  String line = "";

  for (int i = 0; i < width; i++) {
    line += "=";
  }

  Serial.println();
  Serial.println(line);
  printCentered(title, width);
  Serial.println(line);
}


// ============================================================
// FACE CONTROL
// ============================================================

void setFace(FaceState state) {

  if (currentFace == state)
    return;

  currentFace = state;

  switch (state) {

    case FACE_NORMAL:
      roboEyes.setMood(DEFAULT);
      Serial.println("FACE: NORMAL");
      break;

    case FACE_HAPPY:
      roboEyes.setMood(HAPPY);
      happyStartTime = millis();
      Serial.println("FACE: HAPPY");
      break;

    case FACE_ANGRY:
      roboEyes.setMood(ANGRY);
      angryStartTime = millis();
      Serial.println("FACE: ANGRY");
      break;

    case FACE_SAD:
      roboEyes.setMood(TIRED);
      sadStartTime = millis();
      Serial.println("FACE: SAD");
      break;
  }
}


// ============================================================
// SAVE CONFIG
// ============================================================

void saveConfig() {

  const uint32_t MAGIC = 0x4D4F4D4FUL; // "MOMO"
  const int EEPROM_SIZE = 392;
  const int SSID_ADDR = 4;
  const int PASS_ADDR = 68;
  const int API_ADDR  = 140;

  EEPROM.begin(EEPROM_SIZE);

  EEPROM.put(0, MAGIC);

  char ssidBuf[64] = {0};
  char passBuf[64] = {0};

  wifiSSID.toCharArray(ssidBuf, sizeof(ssidBuf));
  wifiPassword.toCharArray(passBuf, sizeof(passBuf));

  for (int i = 0; i < 64; i++) {
    EEPROM.write(SSID_ADDR + i, ssidBuf[i]);
    EEPROM.write(PASS_ADDR + i, passBuf[i]);
  }


  char apiBuf[64] = {0};
  openWeatherApiKey.toCharArray(apiBuf, sizeof(apiBuf));
  for (int i = 0; i < 64; i++) {
    EEPROM.write(API_ADDR + i, apiBuf[i]);
  }

  char ownerBuf[32] = {0};
  ownerName.toCharArray(ownerBuf, sizeof(ownerBuf));
  for (int i = 0; i < 32; i++) EEPROM.write(204 + i, ownerBuf[i]);

  char cityBuf[32] = {0};
  cityName.toCharArray(cityBuf, sizeof(cityBuf));
  for (int i = 0; i < 32; i++) EEPROM.write(236 + i, cityBuf[i]);

  // User-editable boot welcome text and text style (unused EEPROM area).
  char welcomeBuf[40] = {0};
  welcomeText.toCharArray(welcomeBuf, sizeof(welcomeBuf));
  for (int i = 0; i < 40; i++) EEPROM.write(340 + i, welcomeBuf[i]);
  EEPROM.write(380, welcomeTextStyle);
  EEPROM.write(381, bootAnimation);

  // Editable weather coordinates from Web Setup.
  EEPROM.put(384, latitude);
  EEPROM.put(388, longitude);

  EEPROM.commit();
  EEPROM.end();

  Serial.println("Configuration saved to EEPROM.");
}


// ============================================================
// LOAD CONFIG
// ============================================================

bool loadConfig() {

  const uint32_t MAGIC = 0x4D4F4D4FUL; // "MOMO"
  const int EEPROM_SIZE = 392;
  const int SSID_ADDR = 4;
  const int PASS_ADDR = 68;
  const int API_ADDR  = 140;

  EEPROM.begin(EEPROM_SIZE);

  uint32_t storedMagic = 0;
  EEPROM.get(0, storedMagic);

  if (storedMagic != MAGIC) {
    EEPROM.end();
    wifiSSID = "";
    wifiPassword = "";
    openWeatherApiKey = "";
    return false;
  }

  char ssidBuf[65] = {0};
  char passBuf[65] = {0};

  for (int i = 0; i < 64; i++) {
    ssidBuf[i] = EEPROM.read(SSID_ADDR + i);
    passBuf[i] = EEPROM.read(PASS_ADDR + i);
  }

  ssidBuf[64] = '\0';
  passBuf[64] = '\0';

  wifiSSID = String(ssidBuf);
  wifiPassword = String(passBuf);


  char apiBuf[65] = {0};
  for (int i = 0; i < 64; i++) {
    apiBuf[i] = EEPROM.read(API_ADDR + i);
  }
  apiBuf[64] = '\0';
  openWeatherApiKey = String(apiBuf);

  // Read saved owner/city safely. Unwritten EEPROM bytes are often 0xFF;
  // reject those bytes so an old configuration cannot produce a blank/garbled name.
  char ownerBuf[33] = {0};
  char cityBuf[33] = {0};
  for (int i = 0; i < 32; i++) {
    uint8_t ownerByte = EEPROM.read(204 + i);
    uint8_t cityByte = EEPROM.read(236 + i);

    ownerBuf[i] = (ownerByte >= 32 && ownerByte <= 126) ? (char)ownerByte : '\0';
    cityBuf[i] = (cityByte >= 32 && cityByte <= 126) ? (char)cityByte : '\0';

    if (ownerBuf[i] == '\0') {
      for (int j = i + 1; j < 32; j++) ownerBuf[j] = '\0';
      break;
    }
    if (cityBuf[i] == '\0') {
      for (int j = i + 1; j < 32; j++) cityBuf[j] = '\0';
      break;
    }
  }
  ownerBuf[32] = '\0';
  cityBuf[32] = '\0';
  ownerName = String(ownerBuf);
  cityName = String(cityBuf);
  ownerName.trim();
  cityName.trim();
  if (ownerName.length() == 0) ownerName = "MOMO";

  char welcomeBuf[41] = {0};
  for (int i = 0; i < 40; i++) {
    uint8_t b = EEPROM.read(340 + i);
    welcomeBuf[i] = (b >= 32 && b <= 126) ? (char)b : '\0';
    if (welcomeBuf[i] == '\0') break;
  }
  welcomeBuf[40] = '\0';
  String loadedWelcome = String(welcomeBuf);
  if (loadedWelcome.length()) welcomeText = loadedWelcome;
  uint8_t storedStyle = EEPROM.read(380);
  welcomeTextStyle = (storedStyle <= 2) ? storedStyle : 0;
  uint8_t storedBoot = EEPROM.read(381);
  bootAnimation = (storedBoot <= 4) ? storedBoot : 0;

  EEPROM.get(384, latitude);
  EEPROM.get(388, longitude);
  if (!isfinite(latitude) || latitude < -90.0f || latitude > 90.0f) latitude = 0.0f;
  if (!isfinite(longitude) || longitude < -180.0f || longitude > 180.0f) longitude = 0.0f;

  EEPROM.end();
  loadWeatherCache();

  return wifiSSID.length() > 0;
}


// ============================================================
// SAVED WEATHER CACHE - keeps last successful reading on reboot
// ============================================================

void saveWeatherCache() {
  if (weatherCode < 0) return;
  EEPROM.begin(392);
  EEPROM.put(268, temperature);
  EEPROM.put(272, humidity);
  EEPROM.put(276, windSpeed);
  EEPROM.put(280, weatherCode);
  char textBuf[24] = {0};
  weatherText.toCharArray(textBuf, sizeof(textBuf));
  for (int i = 0; i < 24; i++) EEPROM.write(284 + i, textBuf[i]);
  char cityBuf[32] = {0};
  cityName.toCharArray(cityBuf, sizeof(cityBuf));
  for (int i = 0; i < 32; i++) EEPROM.write(308 + i, cityBuf[i]);
  EEPROM.commit();
  EEPROM.end();
  hasSavedWeather = true;
}

void loadWeatherCache() {
  EEPROM.begin(392);
  EEPROM.get(268, temperature);
  EEPROM.get(272, humidity);
  EEPROM.get(276, windSpeed);
  EEPROM.get(280, weatherCode);
  char textBuf[25] = {0};
  char cityBuf[33] = {0};
  for (int i = 0; i < 24; i++) textBuf[i] = EEPROM.read(284 + i);
  for (int i = 0; i < 32; i++) cityBuf[i] = EEPROM.read(308 + i);
  textBuf[24] = '\0';
  cityBuf[32] = '\0';
  String savedText = String(textBuf);
  String savedCity = String(cityBuf);
  if (weatherCode >= 200 && weatherCode <= 804 && !isnan(temperature) && temperature > -100 && temperature < 100) {
    weatherText = savedText.length() ? savedText : weatherCodeToText(weatherCode);
    if (savedCity.length()) cityName = savedCity;
    hasSavedWeather = true;
    Serial.println("Loaded last saved weather from EEPROM.");
  } else {
    weatherCode = -1;
    hasSavedWeather = false;
  }
  EEPROM.end();
}


// ============================================================
// WEB SETUP PAGE (no emoji characters)
// ============================================================

// Receive a Base64-encoded 128x64 monochrome bitmap.
// Form-urlencoded transport avoids binary-body truncation in some mobile clients.
int momoB64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

void handleImageUploadResult() {
  if (!littleFsReady) {
    server.send(500, "text/plain", "LittleFS is not available. Check partition settings.");
    return;
  }

  String encoded = server.arg("data");
  const size_t expectedEncoded = ((CUSTOM_IMAGE_BYTES + 2) / 3) * 4;
  if (encoded.length() != expectedEncoded) {
    server.send(400, "text/plain", "Invalid image data. Expected 1024-byte bitmap (Base64 length 1368); received " + String(encoded.length()) + ". Please retry upload.");
    return;
  }

  uint8_t bitmap[CUSTOM_IMAGE_BYTES];
  size_t out = 0;
  for (size_t i = 0; i < encoded.length(); i += 4) {
    int a = momoB64Value(encoded[i]);
    int b = momoB64Value(encoded[i + 1]);
    int c = encoded[i + 2] == '=' ? 0 : momoB64Value(encoded[i + 2]);
    int d = encoded[i + 3] == '=' ? 0 : momoB64Value(encoded[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0 || out >= CUSTOM_IMAGE_BYTES) {
      server.send(400, "text/plain", "Invalid Base64 image data.");
      return;
    }
    uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
    bitmap[out++] = (v >> 16) & 0xFF;
    if (encoded[i + 2] != '=' && out < CUSTOM_IMAGE_BYTES) bitmap[out++] = (v >> 8) & 0xFF;
    if (encoded[i + 3] != '=' && out < CUSTOM_IMAGE_BYTES) bitmap[out++] = v & 0xFF;
  }
  if (out != CUSTOM_IMAGE_BYTES) {
    server.send(400, "text/plain", "Decoded image size is not 1024 bytes.");
    return;
  }

  const char *tempPath = "/momo_page4.tmp";
  File f = LittleFS.open(tempPath, "w");
  if (!f) {
    server.send(500, "text/plain", "Could not create image file in LittleFS.");
    return;
  }
  size_t written = f.write(bitmap, CUSTOM_IMAGE_BYTES);
  f.close();
  if (written != CUSTOM_IMAGE_BYTES) {
    LittleFS.remove(tempPath);
    server.send(500, "text/plain", "Image write failed.");
    return;
  }
  LittleFS.remove(CUSTOM_IMAGE_PATH);
  if (!LittleFS.rename(tempPath, CUSTOM_IMAGE_PATH)) {
    LittleFS.remove(tempPath);
    server.send(500, "text/plain", "Could not save uploaded image.");
    return;
  }
  server.send(200, "text/plain", "Image uploaded successfully. Open PAGE 4 to view it.");
}

void registerMomoWebRoutes() {
  server.on("/", handleSetupPage);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/upload-image", HTTP_POST, handleImageUploadResult);
}

void handleSetupPage() {

  String page = R"rawliteral(

<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PAGE_TITLE</title>

<style>

* { box-sizing: border-box; }

body {
  margin: 0;
  min-height: 100vh;
  font-family: Arial, Helvetica, sans-serif;
  color: white;
  background:
    radial-gradient(circle at 10% 10%, #243b55, transparent 40%),
    radial-gradient(circle at 90% 90%, #6a3093, transparent 40%),
    #090d18;
  padding: 20px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.container { width: 100%; max-width: 500px; }

.header { text-align: center; margin-bottom: 22px; }

.logo { font-size: 42px; font-weight: 800; letter-spacing: 9px; }

.subtitle { margin-top: 7px; font-size: 14px; opacity: 0.6; }

.ap-info {
  text-align: center;
  padding: 15px;
  margin-bottom: 17px;
  border-radius: 17px;
  background: rgba(0,130,255,0.12);
  border: 1px solid rgba(0,150,255,0.22);
}

.ap-label {
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 2px;
  opacity: 0.55;
}

.ap-name { font-size: 20px; font-weight: 700; margin-top: 6px; }

.ap-details { font-size: 12px; opacity: 0.5; margin-top: 5px; }

.card {
  background: rgba(255,255,255,0.08);
  border: 1px solid rgba(255,255,255,0.14);
  border-radius: 22px;
  padding: 23px;
  margin-bottom: 17px;
  box-shadow: 0 18px 45px rgba(0,0,0,0.25);
  backdrop-filter: blur(15px);
}

.card-title { font-size: 19px; font-weight: 700; margin-bottom: 20px; }

label {
  display: block;
  font-size: 13px;
  font-weight: 600;
  margin-bottom: 7px;
  opacity: 0.75;
}

input {
  width: 100%;
  padding: 13px 14px;
  margin-bottom: 16px;
  border-radius: 12px;
  border: 1px solid rgba(255,255,255,0.13);
  outline: none;
  background: rgba(0,0,0,0.22);
  color: white;
  font-size: 15px;
}

input::placeholder { color: rgba(255,255,255,0.35); }

input:focus {
  border-color: #718cff;
  box-shadow: 0 0 0 3px rgba(113,140,255,0.15);
}

.location-button {
  width: 100%;
  padding: 14px;
  border: none;
  border-radius: 13px;
  background: linear-gradient(135deg, #4776e6, #8e54e9);
  color: white;
  font-size: 15px;
  font-weight: 700;
  cursor: pointer;
}

.location-status {
  text-align: center;
  min-height: 18px;
  margin: 12px 0 15px;
  font-size: 12px;
  opacity: 0.65;
}

.location-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}

.info { font-size: 12px; line-height: 1.5; opacity: 0.55; }

.save-button {
  width: 100%;
  padding: 16px;
  border: none;
  border-radius: 14px;
  background: linear-gradient(135deg, #00c6ff, #0072ff);
  color: white;
  font-size: 16px;
  font-weight: 700;
  cursor: pointer;
  box-shadow: 0 12px 30px rgba(0,114,255,0.25);
}

.footer { text-align: center; font-size: 12px; opacity: 0.4; margin-top: 17px; }

@media(max-width:450px) {
  .location-row { grid-template-columns: 1fr; gap: 0; }
  .logo { font-size: 34px; }
}



/* MOMO mint themes: Mint Green and Mint Sky Blue */
:root {
  color-scheme: light;
  --bg:#e8f8ef; --panel:rgba(255,255,255,.91); --text:#174b3b;
  --muted:#527b6b; --field:#f5fff9; --line:rgba(49,155,111,.24);
  --glow:rgba(67,185,135,.15); --accent1:#39b982; --accent2:#8bdcba;
}
body {
  color:var(--text);
  background:radial-gradient(circle at 12% 8%,#c8f1dc 0,transparent 40%),
             radial-gradient(circle at 90% 88%,#d9f7e8 0,transparent 42%),var(--bg);
  transition:background .25s,color .25s;
}
.card { background:var(--panel); border:1px solid var(--line); box-shadow:0 12px 38px var(--glow),0 18px 45px rgba(20,70,50,.08); }
.ap-info { background:rgba(57,185,130,.10); border:1px solid var(--line); }
input, select { background:var(--field) !important; color:var(--text) !important; border:1px solid var(--line) !important; }
input::placeholder { color:#86a99a; }
input:focus, select:focus { border-color:var(--accent1) !important; box-shadow:0 0 0 3px var(--glow); }
.logo { color:#26996b; text-shadow:0 0 14px rgba(57,185,130,.22); }
.subtitle, .info, label, .footer { color:var(--muted); }
.location-button, .save-button { background:linear-gradient(135deg,var(--accent1),var(--accent2)); color:#103d2d; box-shadow:0 8px 24px var(--glow); }
.theme-toggle { display:block; margin:0 auto 16px; padding:10px 18px; border-radius:999px; border:1px solid var(--line); color:var(--text); background:var(--panel); font-weight:700; cursor:pointer; }
body.sky {
  --bg:#eaf7ff; --panel:rgba(255,255,255,.93); --text:#17435b;
  --muted:#587b91; --field:#f5fbff; --line:rgba(66,158,205,.25);
  --glow:rgba(74,174,220,.16); --accent1:#54bce5; --accent2:#a4e3f4;
}
body.sky { background:radial-gradient(circle at 10% 10%,#cceeff,transparent 42%),radial-gradient(circle at 90% 90%,#d9f4ff,transparent 42%),var(--bg); }
body.sky .logo { color:#279ac7; text-shadow:0 0 14px rgba(65,180,225,.25); }

/* Animated mint background: soft floating orbs behind the portal */
body {
  position:relative;
  isolation:isolate;
  overflow-x:hidden;
}
body::before, body::after {
  content:"";
  position:fixed;
  z-index:-1;
  width: min(58vw, 430px);
  height: min(58vw, 430px);
  border-radius:50%;
  pointer-events:none;
  filter:blur(12px);
  opacity:.55;
  will-change:transform;
}
body::before {
  top:-130px;
  left:-120px;
  background:radial-gradient(circle, rgba(83,211,155,.52) 0%, rgba(137,231,190,.22) 42%, transparent 72%);
  animation:momoFloatOne 17s ease-in-out infinite alternate;
}
body::after {
  right:-140px;
  bottom:-150px;
  background:radial-gradient(circle, rgba(91,194,232,.48) 0%, rgba(164,227,244,.22) 44%, transparent 72%);
  animation:momoFloatTwo 21s ease-in-out infinite alternate;
}
body.sky::before {
  background:radial-gradient(circle, rgba(83,190,235,.52) 0%, rgba(160,225,247,.22) 42%, transparent 72%);
}
body.sky::after {
  background:radial-gradient(circle, rgba(105,215,229,.46) 0%, rgba(188,239,249,.22) 44%, transparent 72%);
}
.container { position:relative; z-index:1; animation:momoPortalEnter .75s ease both; }
.card, .ap-info { animation:momoCardEnter .8s ease both; }
.card:nth-of-type(2) { animation-delay:.08s; }
.card:nth-of-type(3) { animation-delay:.16s; }
.theme-toggle, .location-button, .save-button { transition:transform .2s ease, box-shadow .2s ease, filter .2s ease; }
.theme-toggle:hover, .location-button:hover, .save-button:hover { transform:translateY(-2px); filter:saturate(1.08); }
@keyframes momoFloatOne {
  from { transform:translate3d(0,0,0) scale(1); }
  to { transform:translate3d(16vw,18vh,0) scale(1.22); }
}
@keyframes momoFloatTwo {
  from { transform:translate3d(0,0,0) scale(1); }
  to { transform:translate3d(-17vw,-15vh,0) scale(1.18); }
}
@keyframes momoPortalEnter {
  from { opacity:0; transform:translateY(12px); }
  to { opacity:1; transform:translateY(0); }
}
@keyframes momoCardEnter {
  from { opacity:0; transform:translateY(10px); }
  to { opacity:1; transform:translateY(0); }
}
@media (prefers-reduced-motion: reduce) {
  body::before, body::after, .container, .card, .ap-info { animation:none !important; }
  .theme-toggle, .location-button, .save-button { transition:none; }
}
</style>
</head>

<body>

<div class="container">

  <div class="header">
    <div class="logo">MOMO</div>
    <div class="subtitle">Smart Desktop Robot Setup</div>
    <div class="subtitle" id="ownerLabel">Owner: MOMO</div>
    <button type="button" class="theme-toggle" id="themeToggle" onclick="toggleTheme()">Switch to Mint Sky Blue</button>
  </div>

  <div class="ap-info">
    <div class="ap-label">Connect to Momo</div>
    <div class="ap-name">Momo-Setup</div>
    <div class="ap-details">
      Password: 12345678 &nbsp; | &nbsp; 192.168.4.1
    </div>
  </div>

  <div class="card">

    <div class="card-title">Wi-Fi Connection</div>

    <form action="/save" method="POST">

      <label>Wi-Fi Name</label>
      <input name="ssid" required placeholder="Enter your Wi-Fi name" autocomplete="off">

      <label>Wi-Fi Password</label>
      <input name="password" type="password" placeholder="Wi-Fi password" autocomplete="off">

      <label>OpenWeatherMap API Key</label>
      <input name="weatherkey" type="password" placeholder="Enter your OpenWeatherMap API key" autocomplete="off">

      <label>Owner / Web Page Name</label>
      <input name="owner" maxlength="31" placeholder="Enter owner or page name" autocomplete="off">

      <div class="card-title" style="margin-top:8px;">Boot Screen</div>
      <label>Welcome Text (OLED)</label>
      <input name="welcome" maxlength="39" placeholder="WELCOME TO MOMO" autocomplete="off">
      <label>Welcome Text Style</label>
      <select name="welcomestyle" style="width:100%;padding:13px 14px;margin-bottom:16px;border-radius:12px;border:1px solid rgba(255,255,255,0.13);background:#111827;color:white;font-size:15px;">
        <option value="0">Classic</option>
        <option value="1">Bold</option>
        <option value="2">Compact</option>
      </select>
      <div class="info">The welcome message and style appear on the OLED during startup.</div>
      <label>Boot Animation</label>
      <select name="bootanimation" style="width:100%;padding:13px 14px;margin-bottom:16px;border-radius:12px;border:1px solid rgba(255,255,255,0.13);background:#111827;color:white;font-size:15px;">
        <option value="0">Cloud</option>
        <option value="1">Rain</option>
        <option value="2">Stars</option>
        <option value="3">Radar</option>
        <option value="4">Matrix</option>
      </select>
      <div class="info">Choose the OLED startup animation. Your selection is saved after pressing SAVE &amp; CONNECT.</div>

      <div class="card-title" style="margin-top:8px;">Weather Location</div>
      <label>City Name</label>
      <input name="city" maxlength="31" required placeholder="Enter city name (e.g. Kozhikode)">
      <div class="info">Weather is fetched using your city name and OpenWeatherMap API. GPS and coordinate inputs are not used.</div>

      <button type="submit" class="save-button" style="margin-top:20px;">
        SAVE & CONNECT
      </button>

    </form>

  </div>

  <div class="card">
    <div class="card-title">PAGE 4 - Image Manager</div>
    <p class="info">Choose a JPG or PNG. MOMO converts it to a 128x64 monochrome OLED image. The uploaded image replaces the previous image and is shown full-screen on PAGE 4. No default image is included.</p>
    <input id="momoImage" type="file" accept="image/png,image/jpeg,image/webp">
    <canvas id="imagePreview" width="128" height="64" style="width:100%;max-width:256px;image-rendering:pixelated;background:#000;border:1px solid #35baff;border-radius:8px;display:block;margin:10px auto"></canvas>
    <button class="save-button" type="button" onclick="uploadMomoImage()">UPLOAD PAGE 4 IMAGE</button>
    <div class="location-status" id="imageStatus"></div>
  </div>
  <div class="footer">Momo - ESP32-C3 Super Mini</div>

</div>

<script>


const imageInput = document.getElementById('momoImage');
const previewCanvas = document.getElementById('imagePreview');
const previewCtx = previewCanvas.getContext('2d', { willReadFrequently: true });
let imageReady = false;

// Convert any browser-supported JPG/PNG/WebP into a sharp 128x64 image.
// The preview mirrors OLED output: dark source pixels become lit OLED pixels.
imageInput.addEventListener('change', () => {
  imageReady = false;
  const f = imageInput.files && imageInput.files[0];
  if (!f) return;
  if (!/^image\/(jpeg|png|webp)$/.test(f.type)) {
    document.getElementById('imageStatus').textContent = 'Please choose a JPG, PNG or WebP image.';
    return;
  }
  const url = URL.createObjectURL(f);
  const img = new Image();
  img.onload = () => {
    // Fill the complete 128x64 area (center-crop, no letterbox).
    const scale = Math.max(128 / img.width, 64 / img.height);
    const sw = 128 / scale, sh = 64 / scale;
    const sx = (img.width - sw) / 2, sy = (img.height - sh) / 2;
    const work = document.createElement('canvas');
    work.width = 128; work.height = 64;
    const wctx = work.getContext('2d', { willReadFrequently: true });
    wctx.fillStyle = '#fff'; wctx.fillRect(0, 0, 128, 64);
    wctx.drawImage(img, sx, sy, sw, sh, 0, 0, 128, 64);
    const pixels = wctx.getImageData(0, 0, 128, 64);
    const gray = new Uint8Array(128 * 64), hist = new Uint32Array(256);
    for (let i = 0, j = 0; i < pixels.data.length; i += 4, j++) {
      const y = Math.max(0, Math.min(255, Math.round(0.299*pixels.data[i] + 0.587*pixels.data[i+1] + 0.114*pixels.data[i+2])));
      gray[j] = y; hist[y]++;
    }
    // Otsu threshold gives an automatic high-contrast black/white conversion.
    let total = gray.length, sum = 0;
    for (let i=0;i<256;i++) sum += i * hist[i];
    let sumB=0, wB=0, best=-1, threshold=128;
    for (let t=0;t<256;t++) {
      wB += hist[t]; if (!wB) continue;
      const wF = total-wB; if (!wF) break;
      sumB += t*hist[t];
      const mB=sumB/wB, mF=(sum-sumB)/wF;
      const between=wB*wF*(mB-mF)*(mB-mF);
      if (between>best) { best=between; threshold=t; }
    }
    // Avoid extreme thresholds on nearly flat photos.
    threshold = Math.max(70, Math.min(190, threshold));
    const out = previewCtx.createImageData(128,64);
    for (let i=0;i<gray.length;i++) {
      const lit = gray[i] < threshold; // dark detail becomes white on OLED
      const c = lit ? 255 : 0;
      const k=i*4; out.data[k]=out.data[k+1]=out.data[k+2]=c; out.data[k+3]=255;
    }
    previewCtx.putImageData(out,0,0);
    imageReady = true;
    document.getElementById('imageStatus').textContent = 'Preview ready: 128x64, full-screen, high contrast.';
    URL.revokeObjectURL(url);
  };
  img.onerror = () => { document.getElementById('imageStatus').textContent='Could not read this image. Try another JPG or PNG.'; URL.revokeObjectURL(url); };
  img.src = url;
});

function canvasToBitmap(){
  const d=previewCtx.getImageData(0,0,128,64).data, bytes=new Uint8Array(1024);
  for(let y=0;y<64;y++) for(let x=0;x<128;x++){
    const i=(y*128+x)*4;
    if(d[i]>127) bytes[y*16+(x>>3)] |= (0x80>>(x&7));
  }
  return bytes;
}
function bytesToBase64(bytes){
  let binary='';
  const chunk=0x8000;
  for(let i=0;i<bytes.length;i+=chunk){binary+=String.fromCharCode.apply(null,bytes.subarray(i,Math.min(i+chunk,bytes.length)));}
  return btoa(binary);
}
async function uploadMomoImage(){
  if(!imageReady){document.getElementById('imageStatus').textContent='Choose an image and wait for the preview first.';return;}
  document.getElementById('imageStatus').textContent='Uploading...';
  try {
    const bitmap=canvasToBitmap();
    const encoded=bytesToBase64(bitmap);
    const r=await fetch('/upload-image',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'data='+encodeURIComponent(encoded)});
    const msg=await r.text();
    document.getElementById('imageStatus').textContent=msg;
  } catch(e) {
    document.getElementById('imageStatus').textContent='Upload failed. Check MOMO WiFi connection and try again.';
  }
}
function applyTheme() {
  const sky = localStorage.getItem("momoTheme") === "sky";
  document.body.classList.toggle("sky", sky);
  document.getElementById("themeToggle").innerText =
    sky ? "Switch to Mint Green" : "Switch to Mint Sky Blue";
}
function toggleTheme() {
  const next = document.body.classList.contains("sky") ? "green" : "sky";
  localStorage.setItem("momoTheme", next);
  applyTheme();
}
applyTheme();
</script>

</body>
</html>

)rawliteral";

  page.replace("PAGE_TITLE", ownerName + " Setup");
  page.replace("Owner: MOMO", "Owner: " + ownerName);
  page.replace("placeholder=\"Enter your Wi-Fi name\"", "value=\"" + wifiSSID + "\"");
  page.replace("placeholder=\"Enter owner or page name\"", "value=\"" + ownerName + "\"");
  page.replace("placeholder=\"WELCOME TO MOMO\"", "value=\"" + welcomeText + "\"");
  page.replace("<option value=\"" + String(welcomeTextStyle) + "\">", "<option value=\"" + String(welcomeTextStyle) + "\" selected>");
  page.replace("<option value=\"" + String(bootAnimation) + "\">", "<option value=\"" + String(bootAnimation) + "\" selected>");
  page.replace("placeholder=\"Enter city name (e.g. Kozhikode)\"", "value=\"" + cityName + "\" placeholder=\"Enter city name (e.g. Kozhikode)\"");
  page.replace("<div class=\"logo\">MOMO</div>", "<div class=\"logo\">" + ownerName + "</div>");
  server.send(200, "text/html", page);
}


// ============================================================
// SAVE FROM WEB
// ============================================================

void handleSave() {

  if (server.arg("ssid").length()) wifiSSID = server.arg("ssid");
  if (server.arg("password").length()) wifiPassword = server.arg("password");
  if (server.arg("weatherkey").length()) openWeatherApiKey = server.arg("weatherkey");
  String newOwner = server.arg("owner");
  String newCity = server.arg("city");
  if (newOwner.length()) ownerName = newOwner;
  if (newCity.length()) cityName = newCity;
  String newWelcome = server.arg("welcome");
  if (newWelcome.length()) welcomeText = newWelcome;
  if (server.hasArg("welcomestyle")) {
    int styleValue = server.arg("welcomestyle").toInt();
    if (styleValue >= 0 && styleValue <= 2) welcomeTextStyle = (uint8_t)styleValue;
  }
  if (server.hasArg("bootanimation")) {
    int animationValue = server.arg("bootanimation").toInt();
    if (animationValue >= 0 && animationValue <= 4) bootAnimation = (uint8_t)animationValue;
  }


  Serial.println();
  Serial.println("Configuration received.");

  Serial.print("SSID: ");
  Serial.println(wifiSSID);

  Serial.println("Weather city saved: " + cityName);
  Serial.println(openWeatherApiKey.length() ? "OpenWeatherMap API key received." : "OpenWeatherMap API key is empty.");

  saveConfig();

  server.send(

    200,

    "text/html",

    "<html>"
    "<head><meta charset='UTF-8'></head>"
    "<body style='font-family:Arial;text-align:center;"
    "background:#111;color:white;padding:40px'>"
    "<h1>MOMO</h1>"
    "<p>Configuration saved.</p>"
    "<p>Momo is restarting...</p>"
    "</body>"
    "</html>"

  );

  delay(1500);

  ESP.restart();
}


// ============================================================
// AP MODE OLED SCREEN
// ============================================================

void drawSetupAPScreen() {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(38, 0);
  display.println("MOMO SETUP");

  display.setCursor(0, 16);
  display.print("WiFi: ");
  display.println(SETUP_AP_NAME);

  display.setCursor(0, 30);
  display.print("Pass: ");
  display.println(SETUP_AP_PASS);

  display.setCursor(0, 44);
  display.print("IP: ");
  display.println(WiFi.softAPIP());

  display.display();
}


// ============================================================
// START SETUP AP
//
// Stays here until the user presses SAVE on the web page.
// handleSave() saves the config and restarts the board.
// No timeout, no roboEyes.update() (it would overwrite the
// setup info on the OLED).
// ============================================================

void startSetupAP() {

  printBanner("MOMO SETUP MODE");

  offlineMode = false;

  WiFi.mode(WIFI_AP);

  WiFi.softAP(SETUP_AP_NAME, SETUP_AP_PASS);

  registerMomoWebRoutes();
  server.begin();

  Serial.println();
  Serial.print("SSID: ");
  Serial.println(SETUP_AP_NAME);

  Serial.print("PASSWORD: ");
  Serial.println(SETUP_AP_PASS);

  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  Serial.println("Waiting for Wi-Fi credentials...");

  drawSetupAPScreen();

  while (true) {

    server.handleClient();

    delay(2);
  }
}


// ============================================================
// CONNECT WIFI (saved credentials)
// ============================================================

bool connectWiFi() {

  if (wifiSSID.length() == 0) {
    Serial.println("No Wi-Fi credentials.");
    return false;
  }

  Serial.println();
  Serial.println("MOMO CONNECTING TO SAVED WIFI");

  Serial.print("SSID: ");
  Serial.println(wifiSSID);

  WiFi.mode(WIFI_STA);

  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  unsigned long start = millis();

  int dots = 0;

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    showSearchingScreen(dots++);
    Serial.print(".");
    delay(300);
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("WiFi connected.");

    Serial.print("IP ADDRESS: ");
    Serial.println(WiFi.localIP());

    offlineMode = false;

    // Keep the configuration page available on the local Wi-Fi network.
    registerMomoWebRoutes();
    server.begin();
    Serial.print("MOMO config page: http://");
    Serial.println(WiFi.localIP());

    return true;
  }

  Serial.println("WiFi connection failed.");

  return false;
}


// ============================================================
// NTP
// ============================================================

void setupNTP() {
  // India Standard Time: UTC+05:30. Google NTP first, with fallback servers.
  configTzTime("IST-5:30", "time.google.com", "pool.ntp.org", "time.nist.gov");
  Serial.println("NTP configured; waiting for India time sync...");

  struct tm timeinfo;
  unsigned long started = millis();
  while (!getLocalTime(&timeinfo, 500) && millis() - started < 10000UL) {
    delay(250);
  }
  if (getLocalTime(&timeinfo, 100)) {
    Serial.printf("IST synced: %04d-%02d-%02d %02d:%02d:%02d\n",
      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    Serial.println("NTP sync pending; will retry automatically.");
  }
}


// ============================================================
// WEATHER
// ============================================================

void updateWeather() {

  if (offlineMode || WiFi.status() != WL_CONNECTED)
    return;

  if (openWeatherApiKey.length() == 0) {
    Serial.println("Weather skipped: OpenWeatherMap API key is not configured.");
    lastWeatherUpdate = millis(); // Avoid retrying and printing on every loop.
    return;
  }

  if (cityName.length() == 0) {
    Serial.println("Weather skipped: enter a city name in Web Setup.");
    lastWeatherUpdate = millis();
    return;
  }

  String queryCity = cityName;
  queryCity.replace(" ", "%20");
  String url = "https://api.openweathermap.org/data/2.5/weather?q=" +
               queryCity + "&appid=" + openWeatherApiKey + "&units=metric";

  Serial.println();
  Serial.println("Getting weather from OpenWeatherMap...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  if (!http.begin(client, url)) {
    Serial.println("HTTPS connection setup failed.");
    lastWeatherUpdate = millis();
    return;
  }

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    // Compatible with ArduinoJson 6 (common in mobile IDEs) and 7.
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(2048);
#endif
    DeserializationError error = deserializeJson(doc, payload);

    if (!error && doc["main"].is<JsonObject>() && doc["weather"][0].is<JsonObject>()) {
      temperature = doc["main"]["temp"] | 0.0;
      feelsLike = doc["main"]["feels_like"] | temperature;
      humidity = doc["main"]["humidity"] | 0.0;
      windSpeed = doc["wind"]["speed"] | 0.0; // metres per second
      pressureHPa = doc["main"]["pressure"] | 0.0;
      cloudiness = doc["clouds"]["all"] | 0.0;
      visibilityMeters = doc["visibility"] | 0.0;
      weatherCode = doc["weather"][0]["id"] | -1;
      sunriseEpoch = doc["sys"]["sunrise"] | 0UL;
      sunsetEpoch = doc["sys"]["sunset"] | 0UL;
      time_t nowEpoch = time(nullptr);
      if (sunriseEpoch > 0 && sunsetEpoch > 0 && nowEpoch > 100000UL) {
        isDaytime = ((unsigned long)nowEpoch >= sunriseEpoch && (unsigned long)nowEpoch < sunsetEpoch);
      }
      weatherText = weatherCodeToText(weatherCode);
      const char* apiCity = doc["name"] | "";
      if (cityName.length() == 0 && apiCity && strlen(apiCity) > 0) cityName = String(apiCity);
      saveWeatherCache();

      Serial.println("OpenWeatherMap weather updated.");
      Serial.print("Temperature: "); Serial.println(temperature);
      Serial.print("Humidity: "); Serial.println(humidity);
      Serial.print("Wind speed (m/s): "); Serial.println(windSpeed);
      Serial.print("Condition: "); Serial.println(weatherText);
    } else {
      Serial.println("Weather JSON parsing failed or response fields are missing.");
    }
  } else {
    Serial.print("OpenWeatherMap HTTP error: ");
    Serial.println(httpCode);
    if (httpCode == 401) Serial.println("Check API key; a new key may take up to 2 hours to activate.");
  }

  http.end();
  lastWeatherUpdate = millis();
}


// ============================================================
// TRY WIFI AGAIN
// ============================================================

void retryWiFi() {

  if (wifiSSID.length() == 0)
    return;

  Serial.println();
  Serial.println("Trying Wi-Fi again...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("Internet restored.");

    offlineMode = false;

    setupNTP();

    updateWeather();

  } else {

    Serial.println("Still offline.");

  }
}


// Small horizontal reveal used only when changing pages.
void drawPageTransition() {
  for (int x = 0; x < SCREEN_WIDTH; x += 8) {
    display.clearDisplay();
    display.drawFastVLine(x, 0, SCREEN_HEIGHT, SSD1306_WHITE);
    display.display();
    delay(8);
  }
  display.clearDisplay();
}

void drawStatusHeader(const char* title) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(title);
  display.setCursor(91, 0);
  display.print(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "OFFLINE");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
}

bool hasWeatherAlert() {
  bool rainOrStorm = (weatherCode >= 200 && weatherCode <= 531);
  bool strongWind = windSpeed >= 10.0f; // approx 36 km/h
  return rainOrStorm || strongWind;
}

// ============================================================
// TIME SCREEN
// ============================================================

void showTimeScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawStatusHeader("MOMO | TIME");

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    drawCenteredText("No internet time", 1, 30);
    display.display();
    return;
  }
  char timeString[10];
  snprintf(timeString, sizeof(timeString), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  char dateString[20];
  snprintf(dateString, sizeof(dateString), "%02d/%02d/%04d",
           timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  drawCenteredText(timeString, 3, 16);
  drawCenteredText(dateString, 1, 49);
  isDaytime = (timeinfo.tm_hour >= 6 && timeinfo.tm_hour < 18);
  display.setTextSize(1);
  display.setCursor(48, 59);
  display.print(isDaytime ? "DAY" : "NIGHT");
  drawWeatherIcon(800, 98, 38);
  display.display();
}


// ============================================================
// WEATHER ICONS (monochrome SSD1306 drawings)
// ============================================================

void drawWeatherIcon(int code, int x, int y) {
  // OpenWeatherMap condition IDs: 2xx storm, 3xx drizzle,
  // 5xx rain, 6xx snow, 7xx fog, 800 clear, 80x clouds.
  if (code == 800 && !isDaytime) {
    // Moon for clear night
    display.drawCircle(x + 13, y + 12, 9, SSD1306_WHITE);
    display.fillCircle(x + 18, y + 8, 8, SSD1306_BLACK);
    display.drawPixel(x + 27, y + 3, SSD1306_WHITE);
    display.drawPixel(x + 4, y + 1, SSD1306_WHITE);
  } else if (code == 800) {
    // Sun
    display.fillCircle(x + 10, y + 10, 5, SSD1306_WHITE);
    display.drawCircle(x + 10, y + 10, 8, SSD1306_WHITE);
    display.drawLine(x + 10, y, x + 10, y - 3, SSD1306_WHITE);
    display.drawLine(x + 10, y + 20, x + 10, y + 23, SSD1306_WHITE);
    display.drawLine(x, y + 10, x - 3, y + 10, SSD1306_WHITE);
    display.drawLine(x + 20, y + 10, x + 23, y + 10, SSD1306_WHITE);
    display.drawLine(x + 3, y + 3, x + 1, y + 1, SSD1306_WHITE);
    display.drawLine(x + 17, y + 17, x + 19, y + 19, SSD1306_WHITE);
    display.drawLine(x + 17, y + 3, x + 19, y + 1, SSD1306_WHITE);
    display.drawLine(x + 3, y + 17, x + 1, y + 19, SSD1306_WHITE);
  } else if (code >= 200 && code <= 232) {
    // Thunder cloud and lightning bolt
    display.fillCircle(x + 7, y + 10, 5, SSD1306_WHITE);
    display.fillCircle(x + 14, y + 7, 7, SSD1306_WHITE);
    display.fillCircle(x + 21, y + 11, 5, SSD1306_WHITE);
    display.fillRect(x + 6, y + 10, 16, 6, SSD1306_WHITE);
    display.drawLine(x + 14, y + 15, x + 10, y + 23, SSD1306_WHITE);
    display.drawLine(x + 10, y + 23, x + 16, y + 20, SSD1306_WHITE);
    display.drawLine(x + 16, y + 20, x + 13, y + 27, SSD1306_WHITE);
  } else if ((code >= 300 && code <= 321) || (code >= 500 && code <= 531)) {
    // Cloud with rain
    display.drawCircle(x + 8, y + 10, 6, SSD1306_WHITE);
    display.drawCircle(x + 16, y + 7, 8, SSD1306_WHITE);
    display.drawCircle(x + 24, y + 11, 6, SSD1306_WHITE);
    display.drawLine(x + 4, y + 14, x + 27, y + 14, SSD1306_WHITE);
    display.drawLine(x + 8, y + 19, x + 5, y + 24, SSD1306_WHITE);
    display.drawLine(x + 17, y + 19, x + 14, y + 24, SSD1306_WHITE);
    display.drawLine(x + 26, y + 19, x + 23, y + 24, SSD1306_WHITE);
  } else if (code >= 600 && code <= 622) {
    // Snowflakes
    for (int i = 0; i < 3; i++) {
      int sx = x + 6 + i * 9;
      display.drawLine(sx - 3, y + 10, sx + 3, y + 10, SSD1306_WHITE);
      display.drawLine(sx, y + 7, sx, y + 13, SSD1306_WHITE);
      display.drawLine(sx - 2, y + 8, sx + 2, y + 12, SSD1306_WHITE);
      display.drawLine(sx - 2, y + 12, sx + 2, y + 8, SSD1306_WHITE);
    }
  } else if (code >= 701 && code <= 781) {
    // Fog / mist
    display.drawLine(x + 1, y + 7, x + 27, y + 7, SSD1306_WHITE);
    display.drawLine(x + 5, y + 13, x + 24, y + 13, SSD1306_WHITE);
    display.drawLine(x, y + 19, x + 27, y + 19, SSD1306_WHITE);
    display.drawLine(x + 5, y + 25, x + 22, y + 25, SSD1306_WHITE);
  } else {
    // Cloud / overcast / unknown fallback
    display.drawCircle(x + 8, y + 11, 6, SSD1306_WHITE);
    display.drawCircle(x + 16, y + 8, 8, SSD1306_WHITE);
    display.drawCircle(x + 24, y + 12, 6, SSD1306_WHITE);
    display.drawLine(x + 3, y + 16, x + 28, y + 16, SSD1306_WHITE);
  }
}

// ============================================================
// WEATHER SCREEN
// ============================================================

void showWeatherScreen() {
  // OpenWeatherMap sunrise/sunset are Unix timestamps. If unavailable,
  // use the synchronized local clock as a sensible day/night fallback.
  time_t nowForSky = time(nullptr);
  if (sunriseEpoch > 0 && sunsetEpoch > 0 && nowForSky > 100000UL) {
    isDaytime = ((unsigned long)nowForSky >= sunriseEpoch && (unsigned long)nowForSky < sunsetEpoch);
  } else {
    struct tm localSkyTime;
    if (getLocalTime(&localSkyTime, 20))
      isDaytime = (localSkyTime.tm_hour >= 6 && localSkyTime.tm_hour < 18);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawStatusHeader("MOMO | WEATHER");
  display.setCursor(0, 12);
  display.print(ownerName.length() ? ownerName.substring(0, 13) : String("MOMO"));
  display.print(" ");
  display.print(cityName.length() ? cityName.substring(0, 12) : String("WEATHER"));

  if (weatherCode < 0) {
    display.setCursor(8, 31);
    display.print(offlineMode ? "OFFLINE / NO DATA" : "No weather data");
    display.setCursor(8, 45);
    display.print("Check API / WiFi");
    display.display();
    return;
  }
  display.setTextSize(2);
  display.setCursor(0, 23);
  display.print(temperature, 1);
  display.print(" C");
  drawWeatherIcon(weatherCode, 94, 23);
  display.setTextSize(1);
  display.setCursor(0, 47);
  display.print(weatherText.substring(0, 14));
  display.print(isDaytime ? "  SUN" : "  MOON");
  display.setCursor(0, 57);
  if (hasWeatherAlert()) display.print(windSpeed >= 10.0f ? "ALERT: STRONG WIND" : "ALERT: RAIN/STORM");
  else { display.print("Humidity "); display.print(humidity, 0); display.print("%  Tap: Details"); }
  display.display();
}


// PAGE 3 — detailed live weather information
void showWeatherDetailsScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  drawStatusHeader("WEATHER DETAILS");

  if (weatherCode < 0) {
    display.setCursor(0, 25);
    display.print("No weather data");
    display.display();
    return;
  }

  display.setCursor(0, 13);
  display.print("Wind: ");
  display.print(windSpeed * 3.6f, 1);
  display.print(" km/h");

  display.setCursor(0, 23);
  display.print("Feels like: ");
  display.print(feelsLike, 1);
  display.print(" C");

  display.setCursor(0, 33);
  display.print("Humidity: ");
  display.print(humidity, 0);
  display.print("%");

  display.setCursor(0, 43);
  display.print("Pressure: ");
  display.print(pressureHPa, 0);
  display.print(" hPa");

  display.setCursor(0, 53);
  if (hasWeatherAlert()) display.print(windSpeed >= 10.0f ? "! STRONG WIND ALERT" : "! RAIN / STORM ALERT");
  else {
    display.print("Cloud:"); display.print(cloudiness, 0);
    display.print("% Vis:"); display.print(visibilityMeters / 1000.0f, 1); display.print("km");
  }
  display.display();
}

// PAGE 4 — User-uploaded 128x64 monochrome image only.
// No built-in/default image is shown. Upload an image from the Web Portal.
void showSystemInfoScreen() {
  display.clearDisplay();
  bool drawn = false;
  if (littleFsReady && LittleFS.exists(CUSTOM_IMAGE_PATH)) {
    File f = LittleFS.open(CUSTOM_IMAGE_PATH, "r");
    if (f && f.size() == CUSTOM_IMAGE_BYTES) {
      uint8_t bitmap[CUSTOM_IMAGE_BYTES];
      size_t n = f.read(bitmap, sizeof(bitmap));
      f.close();
      if (n == CUSTOM_IMAGE_BYTES) {
        display.drawBitmap(0, 0, bitmap, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
        drawn = true;
      }
    } else if (f) {
      f.close();
    }
  }
  if (!drawn) {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(15, 20);
    display.print("NO IMAGE UPLOADED");
    display.setCursor(17, 36);
    display.print("Open MOMO Portal");
    display.setCursor(20, 50);
    display.print("Upload 128x64");
  }
  display.display();
}

// ============================================================
// RETURN TO NORMAL FACE
// ============================================================

void closeMenu() {

  menuState = MENU_CLOSED;

  setFace(FACE_NORMAL);

  tapCount = 0;

  lastTouchTime = millis();

  Serial.println("RETURN -> NORMAL");
}


// ============================================================
// TOUCH HANDLER
// ============================================================

void handleTouch() {

  bool touched = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE;

  // ---------------- TOUCH START ----------------

  if (touched && !lastTouch) {

    touchStartTime = millis();

    longPressHandled = false;
  }

  // ---------------- LONG PRESS ----------------

  if (
    touched &&
    !longPressHandled &&
    millis() - touchStartTime >= LONG_PRESS_TIME
  ) {

    longPressHandled = true;

    tapCount = 0;

    if (menuState == MENU_CLOSED) {

      // NORMAL FACE -> TIME

      menuState = SHOW_TIME;

      menuLastInteraction = millis();

      showTimeScreen();

      Serial.println("5 SEC -> TIME");

    } else {

      // TIME / WEATHER -> NORMAL

      closeMenu();

      Serial.println("5 SEC -> NORMAL");
    }
  }

  // ---------------- TOUCH RELEASE ----------------

  if (!touched && lastTouch) {

    unsigned long duration = millis() - touchStartTime;

    if (longPressHandled) {

      lastTouch = touched;

      return;
    }

    if (duration <= TAP_MAX_TIME) {

      unsigned long now = millis();

      lastTouchTime = now;

      if (menuState == SHOW_TIME) {

        // TIME -> WEATHER

        menuState = SHOW_WEATHER;
        drawPageTransition();

        menuLastInteraction = now;

        tapCount = 0;

        showWeatherScreen();

        Serial.println("SINGLE TAP -> WEATHER");

      } else if (menuState == SHOW_WEATHER) {

        // PAGE 2 -> PAGE 3
        menuState = SHOW_DETAILS;
        drawPageTransition();
        menuLastInteraction = now;
        tapCount = 0;
        showWeatherDetailsScreen();

      } else if (menuState == SHOW_DETAILS) {

        // PAGE 3 -> PAGE 4
        menuState = SHOW_SYSTEM;
        drawPageTransition();
        menuLastInteraction = now;
        tapCount = 0;
        showSystemInfoScreen();

      } else if (menuState == SHOW_SYSTEM) {

        // PAGE 4 -> PAGE 1
        drawPageTransition();
        menuState = SHOW_TIME;
        menuLastInteraction = now;
        tapCount = 0;
        showTimeScreen();

      } else {

        // NORMAL FACE

        if (tapCount > 0 && now - lastTapTime > TAP_GAP) {
          tapCount = 0;
        }

        tapCount++;

        lastTapTime = now;

        if (tapCount >= 3) {

          setFace(FACE_ANGRY);

          angryStartTime = millis();

          lastTouchTime = millis();

          tapCount = 0;

          Serial.println("TRIPLE TAP -> ANGRY");

        } else {

          setFace(FACE_HAPPY);

          happyStartTime = millis();

          Serial.println("TAP -> HAPPY");

        }
      }

    } else {

      // Held too long for a tap, but not a long press.

      tapCount = 0;
    }
  }

  lastTouch = touched;
}


// ============================================================
// FACE TIMERS
// ============================================================

void handleFaceTimers() {

  if (menuState != MENU_CLOSED)
    return;

  // HAPPY -> NORMAL

  if (
    currentFace == FACE_HAPPY &&
    millis() - happyStartTime >= HAPPY_TIME
  ) {

    setFace(FACE_NORMAL);

    lastTouchTime = millis();

    tapCount = 0;

    Serial.println("HAPPY -> NORMAL");
  }

  // ANGRY -> NORMAL

  if (
    currentFace == FACE_ANGRY &&
    millis() - angryStartTime >= ANGRY_TIME
  ) {

    setFace(FACE_NORMAL);

    lastTouchTime = millis();

    tapCount = 0;

    Serial.println("ANGRY -> NORMAL");
  }

  // NO TOUCH -> SAD

  if (
    currentFace != FACE_SAD &&
    currentFace != FACE_ANGRY &&
    currentFace != FACE_HAPPY &&
    millis() - lastTouchTime >= NO_TOUCH_TIME
  ) {

    setFace(FACE_SAD);

    sadStartTime = millis();

    Serial.println("1 MINUTE -> SAD");
  }

  // SAD -> NORMAL

  if (
    currentFace == FACE_SAD &&
    millis() - sadStartTime >= SAD_TIME
  ) {

    setFace(FACE_NORMAL);

    lastTouchTime = millis();

    Serial.println("SAD -> NORMAL");
  }
}


// ============================================================
// MENU DISPLAY UPDATE
// ============================================================

void updateMenuDisplay() {

  if (menuState == MENU_CLOSED)
    return;

  if (menuState == SHOW_TIME) {

    static unsigned long lastTimeDraw = 0;

    if (millis() - lastTimeDraw >= 500) {

      lastTimeDraw = millis();

      showTimeScreen();
    }

  } else if (menuState == SHOW_WEATHER) {

    static unsigned long lastWeatherDraw = 0;
    if (millis() - lastWeatherDraw >= 1000) {
      lastWeatherDraw = millis();
      showWeatherScreen();
    }

  } else if (menuState == SHOW_DETAILS) {
    static unsigned long lastDetailsDraw = 0;
    if (millis() - lastDetailsDraw >= 2000) { lastDetailsDraw = millis(); showWeatherDetailsScreen(); }
  } else if (menuState == SHOW_SYSTEM) {
    static unsigned long lastSystemDraw = 0;
    if (millis() - lastSystemDraw >= 1000) { lastSystemDraw = millis(); showSystemInfoScreen(); }
  }

  // V5: pages never auto-close. A 5-second touch hold closes them.
}


// ============================================================
// STARTUP SPLASH
//
// Drawn from loop() so it appears AFTER setup/WiFi finishes
// and roboEyes.begin() can't wipe it. Timer starts when the
// splash is first drawn.
// ============================================================

void drawBootCloud(int x, int y) {
  // Simple monochrome cloud made from overlapping OLED primitives.
  display.fillCircle(x + 10, y + 12, 7, SSD1306_WHITE);
  display.fillCircle(x + 20, y + 8, 10, SSD1306_WHITE);
  display.fillCircle(x + 31, y + 13, 7, SSD1306_WHITE);
  display.fillRect(x + 9, y + 12, 24, 9, SSD1306_WHITE);
}

void drawBootWelcome() {
  String msg = welcomeText.length() ? welcomeText : String("WELCOME TO MOMO");
  if (msg.length() > 20) msg = msg.substring(0, 20);
  display.setTextColor(SSD1306_WHITE);
  uint8_t size = (welcomeTextStyle == 2) ? 1 : 1;
  display.setTextSize(size);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(msg.c_str(), 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2 - x1;
  int y = 45;
  display.setCursor(x, y);
  display.print(msg);
  if (welcomeTextStyle == 1) {
    display.setCursor(x + 1, y);
    display.print(msg); // doubled pixels for a bold-like built-in font style
  }
  if (welcomeTextStyle == 2) {
    display.setCursor(2, 56);
    display.print("SMART DESKTOP ROBOT");
  }
}

void updateStartupSplash() {
  if (!startupSplash) return;

  unsigned long elapsed = splashDrawn ? millis() - splashStartTime : 0;
  if (!splashDrawn) {
    splashDrawn = true;
    splashStartTime = millis();
    elapsed = 0;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  drawSelectedBootAnimation(elapsed);
  if (elapsed < CLOUD_ANIMATION_TIME) {
    display.setTextSize(1);
    display.setCursor(42, 31);
    display.print("MOMO BOOTING");
    int barW = (int)((100UL * elapsed) / CLOUD_ANIMATION_TIME);
    display.drawRect(14, 56, 100, 5, SSD1306_WHITE);
    display.fillRect(15, 57, barW, 3, SSD1306_WHITE);
  } else {
    String brand = ownerName.length() ? ownerName : String("MOMO");
    if (brand.length() > 16) brand = brand.substring(0, 16);
    display.setTextSize(1);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(brand.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - (int)w) / 2 - x1, 32);
    display.print(brand);
    drawBootWelcome();
  }
  display.display();

  if (elapsed >= SPLASH_TIME) {
    startupSplash = false;
    setFace(FACE_NORMAL);
    lastTouchTime = millis();
    Serial.println("MOMO STARTUP COMPLETE");
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  littleFsReady = LittleFS.begin(true);
  Serial.println(littleFsReady ? "LittleFS ready for PAGE 4 images." : "LittleFS mount failed; using built-in image.");

  pinMode(TOUCH_PIN, INPUT);

  // I2C / OLED (ESP32-C3 Super Mini: SDA=GPIO6, SCL=GPIO7)

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {

    Serial.println("SSD1306 failed");

    while (1);
  }

  // OLED protection baseline: normal brightness, no auto-off or auto-dimming.
  // Start-line pixel shifting is handled periodically in updateOledProtection().
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(180);
  display.ssd1306_command(0x40); // Start at display row 0
  lastOledPixelShift = millis();

  // ROBO EYES

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);

  roboEyes.setAutoblinker(ON, 3, 2);

  roboEyes.setIdleMode(ON, 2, 2);

  roboEyes.setMood(DEFAULT);

  currentFace = FACE_NORMAL;

  lastTouchTime = millis();

  // LOAD SAVED CONFIG

  bool configAvailable = loadConfig();

  if (!configAvailable) {

    Serial.println("No saved configuration.");

    startSetupAP(); // never returns; restarts after SAVE
  }

  // CONNECT TO SAVED WIFI

  if (!connectWiFi()) {

    Serial.println("Wi-Fi connection failed.");

    startSetupAP(); // never returns; restarts after SAVE
  }

  // INTERNET SERVICES

  setupNTP();

  updateWeather();

  printBanner("MOMO READY");

  Serial.println("MODE: ONLINE");
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  // Allow owner name and location settings to be edited from a phone browser.
  server.handleClient();

  // ROBO EYES

  if (menuState == MENU_CLOSED && !startupSplash) {
    roboEyes.update();
  }

  handleTouch();

  handleFaceTimers();

  updateStartupSplash();

  updateMenuDisplay();
  updateOledProtection();

  // WEATHER UPDATE

  if (
    !offlineMode &&
    WiFi.status() == WL_CONNECTED &&
    millis() - lastWeatherUpdate >= WEATHER_INTERVAL
  ) {
    updateWeather();
  }

  // WIFI LOST

  if (!offlineMode && WiFi.status() != WL_CONNECTED) {

    Serial.println("Wi-Fi connection lost.");

    offlineMode = true;

    WiFi.disconnect();

    WiFi.mode(WIFI_OFF);

    lastWiFiRetry = millis();
  }

  // OFFLINE WIFI RETRY

  if (
    offlineMode &&
    wifiSSID.length() > 0 &&
    millis() - lastWiFiRetry >= WIFI_RETRY_INTERVAL
  ) {

    lastWiFiRetry = millis();

    retryWiFi();
  }

  delay(2);
}
