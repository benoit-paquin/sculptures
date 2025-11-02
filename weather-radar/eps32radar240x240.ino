/*
  ESP32-C3 + ST7789 240x240
  OpenStreetMap + OpenWeatherMap precipitation overlay
  True alpha blending, SPIFFS caching, auto 10-min refresh
  Timestamp overlay: box fades in/out, text fully opaque
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>
#include <time.h>

#define WIDTH  240
#define HEIGHT 240

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define TFT_MOSI   3   // D3
#define TFT_SCLK   2   // D2
#define TFT_CS     -1   // D6
#define TFT_DC     4   // D4
#define TFT_RST    5   // D5

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT

const char* WIFI_SSID     = "BELL082";
const char* WIFI_PASSWORD = "6DF1F5D5AA45";
const char* OWM_API_KEY   = "90ea5ee12e5dbecbe533b846bb5f8d10";

const double LAT  = 55.6761;
const double LON  = 12.5683;
const int    ZOOM = 13;

const unsigned long FRAME_DELAY_MS   = 20000;   // each frame
const unsigned long REFRESH_INTERVAL = 600000;  // 10 min

TFT_eSPI tft = TFT_eSPI();
PNG png;

// Buffers
static uint8_t* osmRGBA;
static uint8_t* radarRGBA;
static uint16_t* blendedRGB565;

unsigned long lastRefresh = 0;

// --- lat/lon → tile ---
void latLonToTile(double lat, double lon, int zoom, long &xtile, long &ytile) {
  double lat_rad = lat * M_PI / 180.0;
  double n = pow(2.0, zoom);
  xtile = (long)((lon + 180.0) / 360.0 * n);
  ytile = (long)((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n);
}

// --- HTTP download ---
bool downloadToSPIFFS(const String &url, const char *destPath) {
  if (SPIFFS.exists(destPath)) return true;
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  File f = SPIFFS.open(destPath, FILE_WRITE);
  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[1024]; int len;
  while ((len = s->readBytes(buf, sizeof(buf))) > 0) f.write(buf, len);
  f.close(); http.end();
  return true;
}

// --- PNG decode to RGBA ---
bool decodePNGToRGBA(const char* path, uint8_t* outBuf) {
  struct Local {
    static void draw(PNGDRAW *pDraw) {
      uint8_t* dest = (uint8_t*)pDraw->pUser;
      uint8_t* src  = (uint8_t*)pDraw->pPixels;
      memcpy(dest + pDraw->y * pDraw->iWidth * 4, src, pDraw->iWidth * 4);
    }
  };
  if (png.open(path, Local::draw, outBuf)) {
    int rc = png.decode(NULL, 0);
    png.close();
    return rc == PNG_SUCCESS;
  }
  return false;
}

// --- Alpha blend + save RGB565 ---
void blendAndSave(const char* osmPath, const char* radarPath, const char* out565Path) {
  if (SPIFFS.exists(out565Path)) return;
  if (!decodePNGToRGBA(osmPath, osmRGBA)) return;
  if (!decodePNGToRGBA(radarPath, radarRGBA)) return;

  for (int i=0; i<WIDTH*HEIGHT; i++) {
    uint8_t br = osmRGBA[i*4+0];
    uint8_t bg = osmRGBA[i*4+1];
    uint8_t bb = osmRGBA[i*4+2];
    uint8_t orr = radarRGBA[i*4+0];
    uint8_t og = radarRGBA[i*4+1];
    uint8_t ob = radarRGBA[i*4+2];
    uint8_t oa = radarRGBA[i*4+3];
    uint8_t r = (orr * oa + br * (255 - oa)) / 255;
    uint8_t g = (og  * oa + bg * (255 - oa)) / 255;
    uint8_t b = (ob  * oa + bb * (255 - oa)) / 255;
    blendedRGB565[i] = ((r & 0xF8)<<8) | ((g & 0xFC)<<3) | (b>>3);
  }

  File f = SPIFFS.open(out565Path, FILE_WRITE);
  f.write((uint8_t*)blendedRGB565, WIDTH*HEIGHT*2);
  f.close();
}

// --- Draw timestamp box with fade, text opaque ---
void drawTimestamp(const char* buf, uint8_t alpha) {
  int boxW = 140, boxH = 18;
  for(int y=0;y<boxH;y++){
    for(int x=0;x<boxW;x++){
      uint16_t bg = tft.readPixel(x,y);
      uint8_t br = (bg >> 11) << 3;
      uint8_t bgc = ((bg >> 5) & 0x3F) << 2;
      uint8_t bb = (bg & 0x1F) << 3;
      uint8_t nr = (0 * alpha + br * (255-alpha))/255;
      uint8_t ng = (0 * alpha + bgc*(255-alpha))/255;
      uint8_t nb = (0 * alpha + bb * (255-alpha))/255;
      uint16_t newColor = ((nr & 0xF8)<<8)|((ng & 0xFC)<<3)|(nb>>3);
      tft.drawPixel(x,y,newColor);
    }
  }
  // Text fully opaque
  tft.setTextColor(TFT_YELLOW);
  tft.setTextFont(2);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(buf, 2, 2);
}

// --- Display frame with fade-in/out box ---
void displayFrameWithFade(const char* path) {
  File f = SPIFFS.open(path, FILE_READ);
  if (!f) return;
  f.read((uint8_t*)blendedRGB565, WIDTH*HEIGHT*2);
  f.close();
  tft.pushImage(0, 0, WIDTH, HEIGHT, blendedRGB565);

  const int fadeDuration = 1000; // 1s
  unsigned long start = millis();
  while (millis() - start < FRAME_DELAY_MS) {
    unsigned long t = millis() - start;
    uint8_t alpha = 255;
    if(t < fadeDuration) alpha = t * 255 / fadeDuration;
    else if(t > FRAME_DELAY_MS - fadeDuration)
      alpha = (FRAME_DELAY_MS - t) * 255 / fadeDuration;

    char buf[20] = "";
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)) strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&timeinfo);
    else strcpy(buf,"Time N/A");

    drawTimestamp(buf, alpha);
    delay(50);
  }
}

// --- Refresh radar frames ---
void refreshRadarFrames() {
  long xt, yt;
  latLonToTile(LAT, LON, ZOOM, xt, yt);
  downloadToSPIFFS(String("https://tile.openstreetmap.org/") + ZOOM + "/" + xt + "/" + yt + ".png","/osm.png");

  time_t nowSec = time(NULL);
  if(nowSec<100000) nowSec=millis()/1000;
  unsigned long ts[3] = {nowSec, nowSec-600, nowSec-1200};

  const char* layer = "precipitation_new";
  char radarFile[32], outFile[32];

  for(int i=0;i<3;i++){
    snprintf(radarFile,sizeof(radarFile),"/radar_%d.png",i);
    snprintf(outFile,sizeof(outFile),"/frame_%d.rgb",i);
    String url = String("https://tile.openweathermap.org/map/") + layer + "/" + ZOOM + "/" + xt + "/" + yt +
                 ".png?appid=" + OWM_API_KEY + "&dt=" + String(ts[i]);
    if(!downloadToSPIFFS(url, radarFile)) {
      String fb = String("https://tile.openweathermap.org/map/") + layer + "/" + ZOOM + "/" + xt + "/" + yt +
                  ".png?appid=" + OWM_API_KEY;
      downloadToSPIFFS(fb, radarFile);
    }
    blendAndSave("/osm.png", radarFile, outFile);
  }
  lastRefresh = millis();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  if(!SPIFFS.begin(true)) while(1) delay(100);

  osmRGBA  = (uint8_t*) malloc(WIDTH*HEIGHT*4);
  radarRGBA = (uint8_t*) malloc(WIDTH*HEIGHT*4);
  blendedRGB565 = (uint16_t*) malloc(WIDTH*HEIGHT*2);

  tft.init(); tft.setRotation(0); tft.fillScreen(TFT_BLACK);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while(WiFi.status()!=WL_CONNECTED) delay(250);

  configTime(0,0,"pool.ntp.org","time.nist.gov");
  delay(2000);

  refreshRadarFrames();
}

void loop() {
  if(millis() - lastRefresh > REFRESH_INTERVAL) refreshRadarFrames();

  for(int i=0;i<3;i++){
    char path[32];
    snprintf(path,sizeof(path),"/frame_%d.rgb",i);
    displayFrameWithFade(path);
  }
}
