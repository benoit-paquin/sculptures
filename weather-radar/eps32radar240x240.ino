/*
  ESP32-C3 + ST7789 240x240
  OpenStreetMap + OpenWeatherMap precipitation overlay
  Alpha blending + SPIFFS caching + timestamp overlay + hourly deep-sleep refresh
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>
#include <time.h>
#include "esp_sleep.h"

const char* WIFI_SSID     = "BELL082";
const char* WIFI_PASSWORD = "6DF1F5D5AA45";
const char* OWM_API_KEY   = "90ea5ee12e5dbecbe533b846bb5f8d10";

const double LAT = 55.6761;
const double LON = 12.5683;
const int    ZOOM = 13;

const unsigned long FRAME_DELAY_MS = 20000;
const unsigned long REFRESH_INTERVAL_MS = 360000; // 6 minutes
const uint64_t SLEEP_INTERVAL_US = REFRESH_INTERVAL_MS * 1000ULL;

const char* OSM_FILE   = "/osm.png";
const char* RADAR_FILE_TEMPLATE = "/radar_%d.png";
const char* FRAME_FILE_TEMPLATE = "/frame_%d.raw";

TFT_eSPI tft = TFT_eSPI();
PNG png;

const int WIDTH = 240;
const int HEIGHT = 240;

static uint8_t* osmRGBA = nullptr;
static uint8_t* radarRGBA = nullptr;
static uint16_t* blendedRGB565 = nullptr;

// --- Utility: lat/lon to tile ---
void latLonToTile(double lat, double lon, int zoom, long &xtile, long &ytile) {
  double lat_rad = lat * M_PI / 180.0;
  double n = pow(2.0, zoom);
  xtile = (long)((lon + 180.0) / 360.0 * n);
  ytile = (long)((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n);
}

// --- Download file ---
bool downloadToSPIFFS(const String &url, const char *destPath) {
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("GET failed (%d): %s\n", code, url.c_str());
    http.end();
    return false;
  }
  File f = SPIFFS.open(destPath, FILE_WRITE);
  if (!f) { Serial.printf("Open %s failed\n", destPath); http.end(); return false; }
  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[1024]; int len;
  while ((len = s->readBytes(buf, sizeof(buf))) > 0) f.write(buf, len);
  f.close(); http.end();
  Serial.printf("Saved %s\n", destPath);
  return true;
}

// --- Decode PNG to RGBA ---
bool decodePNGToRGBA(const char* path, uint8_t* outBuf) {
  struct LocalData {
    static void draw(PNGDRAW *pDraw) {
      uint8_t* dest = (uint8_t*)pDraw->pUser;
      uint8_t* src  = (uint8_t*)pDraw->pPixels;
      int lineBytes = pDraw->iWidth * 4;
      memcpy(dest + (pDraw->y * lineBytes), src, lineBytes);
    }
  };
  if (png.open(path, LocalData::draw, outBuf)) {
    int rc = png.decode(NULL, 0);
    png.close();
    return (rc == PNG_SUCCESS);
  }
  return false;
}

// --- Alpha blend ---
void blendRGBA(const uint8_t* base, const uint8_t* overlay, uint16_t* out565, int w, int h) {
  for (int i = 0; i < w * h; i++) {
    uint8_t br = base[i*4+0];
    uint8_t bg = base[i*4+1];
    uint8_t bb = base[i*4+2];
    uint8_t orr = overlay[i*4+0];
    uint8_t og = overlay[i*4+1];
    uint8_t ob = overlay[i*4+2];
    uint8_t oa = overlay[i*4+3];

    uint8_t r = (orr * oa + br * (255 - oa)) / 255;
    uint8_t g = (og  * oa + bg * (255 - oa)) / 255;
    uint8_t b = (ob  * oa + bb * (255 - oa)) / 255;

    out565[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }
}

// --- Create blended frame ---
void makeAndSaveFrame(const char* osmPath, const char* radarPath, const char* framePath) {
  if (!decodePNGToRGBA(osmPath, osmRGBA)) return;
  if (!decodePNGToRGBA(radarPath, radarRGBA)) return;
  blendRGBA(osmRGBA, radarRGBA, blendedRGB565, WIDTH, HEIGHT);

  File f = SPIFFS.open(framePath, FILE_WRITE);
  if (!f) { Serial.printf("Failed to open %s\n", framePath); return; }
  f.write((uint8_t*)blendedRGB565, WIDTH * HEIGHT * 2);
  f.close();
  Serial.printf("Cached %s\n", framePath);
}

// --- Display cached frame with timestamp overlay ---
void showCachedFrame(const char* framePath, const char* timeStr) {
  File f = SPIFFS.open(framePath, FILE_READ);
  if (!f) { Serial.printf("Cannot open %s\n", framePath); return; }
  f.read((uint8_t*)blendedRGB565, WIDTH * HEIGHT * 2);
  f.close();

  tft.pushImage(0, 0, WIDTH, HEIGHT, blendedRGB565);

  // Draw timestamp
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(timeStr, WIDTH - 4, HEIGHT - 4);
}

// --- Refresh radar data ---
void refreshRadarData() {
  long xt, yt;
  latLonToTile(LAT, LON, ZOOM, xt, yt);

  // OSM base (cached indefinitely)
  if (!SPIFFS.exists(OSM_FILE)) {
    String osmURL = String("https://tile.openstreetmap.org/") + ZOOM + "/" + xt + "/" + yt + ".png";
    downloadToSPIFFS(osmURL, OSM_FILE);
  }

  const char* layer = "precipitation_new";
  time_t nowSec = time(NULL);
  if (nowSec < 100000) nowSec = millis()/1000;
  unsigned long ts[3] = { nowSec, nowSec - 600, nowSec - 1200 };

  char radarPath[32], framePath[32];
  for (int i=0; i<3; i++) {
    snprintf(radarPath, sizeof(radarPath), RADAR_FILE_TEMPLATE, i);
    snprintf(framePath, sizeof(framePath), FRAME_FILE_TEMPLATE, i);

    String radarURL = String("https://tile.openweathermap.org/map/") + layer + "/" + ZOOM + "/" + xt + "/" + yt +
                      ".png?appid=" + OWM_API_KEY + "&dt=" + String(ts[i]);
    Serial.printf("Downloading radar %d...\n", i);
    if (!downloadToSPIFFS(radarURL, radarPath)) {
      String fb = String("https://tile.openweathermap.org/map/") + layer + "/" + ZOOM + "/" + xt + "/" + yt +
                  ".png?appid=" + OWM_API_KEY;
      downloadToSPIFFS(fb, radarPath);
    }
    makeAndSaveFrame(OSM_FILE, radarPath, framePath);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed!");
    while (1) delay(100);
  }

  osmRGBA  = (uint8_t*) malloc(WIDTH * HEIGHT * 4);
  radarRGBA = (uint8_t*) malloc(WIDTH * HEIGHT * 4);
  blendedRGB565 = (uint16_t*) malloc(WIDTH * HEIGHT * 2);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 40) { delay(300); Serial.print("."); retry++; }
  Serial.println(WiFi.isConnected() ? " OK" : " FAILED");

  if (WiFi.isConnected()) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    delay(1500);
    refreshRadarData();
  }

  // Get current local time for timestamp
  struct tm timeinfo;
  char timeStr[16] = "Updated: --:--";
  if (getLocalTime(&timeinfo)) {
    strftime(timeStr, sizeof(timeStr), "Updated: %H:%M", &timeinfo);
  }

  // Show cached frames
  for (int i=0; i<3; i++) {
    char framePath[32];
    snprintf(framePath, sizeof(framePath), FRAME_FILE_TEMPLATE, i);
    if (SPIFFS.exists(framePath)) {
      showCachedFrame(framePath, timeStr);
      delay(FRAME_DELAY_MS);
    }
  }

  // Power-down before sleep
  tft.fillScreen(TFT_BLACK);
  tft.writecommand(0x10); // ST7789 sleep mode
  tft.writecommand(0x28); // Display off
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.println("Entering deep sleep for 1 hour...");
  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
  esp_deep_sleep_start();
}

void loop() {
  // never reached
}
