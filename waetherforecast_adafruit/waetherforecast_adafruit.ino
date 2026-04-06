/*
 * ============================================================
 *  5-Day Weather Forecast — ESP32-C3 + 240×320 TFT (TFT_eSPI)
 *  Fetches data from OpenWeatherMap every UPDATE_INTERVAL_MS ms
 * ============================================================
 *
 *  Dependencies (install via Arduino Library Manager):
 *    - TFT_eSPI       by Bodmer
 *    - ArduinoJson    by Benoît Blanchon  (v7 recommended)
 *
 *  Board: "ESP32C3 Dev Module"  (esp32 core ≥ 2.0)
 *
 *  TFT_eSPI User_Setup.h pins (adjust to your wiring):
 *    #define TFT_MOSI  6
 *    #define TFT_SCLK  4
 *    #define TFT_CS    7
 *    #define TFT_DC    2
 *    #define TFT_RST   10
 *    #define TFT_BL    3   // optional backlight
 *    #define ST7789_DRIVER
 *    #define TFT_WIDTH  240
 *    #define TFT_HEIGHT 320
 * ============================================================
 */
/**************************************************************************
  Swirl code for ESP32C3 and ST7789 240-320

 **************************************************************************

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include <math.h>



#define TFT_MOSI D10  // Data out
#define TFT_SCLK D8  // Clock out
#define TFT_CS         D0 
#define TFT_RST        D2                                            
#define TFT_DC         D1
  
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
*/

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
Adafruit_ST7789 tft = Adafruit_ST7789(D0, D1, D2);
//t.setSPISpeed(40000000);
GFXcanvas16 spr(240, 320);


#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
//#include <TFT_eSPI.h>
#include <time.h>

// ── User Configuration ──────────────────────────────────────
#define WIFI_SSID        "H4Mesh"
#define WIFI_PASSWORD    "benoitpaquin"

// Get a free API key at https://openweathermap.org/api
#define OWM_API_KEY      "90ea5ee12e5dbecbe533b846bb5f8d10"
#define OWM_CITY_ID      "2618425"          // Copenhagen; find yours at openweathermap.org
#define OWM_UNITS        "metric"           // "imperial" for °F
#define TEMP_UNIT_LABEL  "C"               // "F" if imperial

// Refresh period (ms)  — 10 minutes
#define UPDATE_INTERVAL_MS  (10UL * 60UL * 1000UL)
// ─────────────────────────────────────────────────────────────

// ── Colour Palette ────────────────────────────────────────────
#define COL_BG          0x0A0F          // deep navy
#define COL_PANEL       0x1294          // dark slate
#define COL_PANEL_SEL   0x2359          // slightly lighter panel
#define COL_WHITE       0xFFFF //TFT_WHITE
#define COL_YELLOW      0xFFE0          // warm yellow  (sun)
#define COL_ORANGE      0xFD00          // orange       (sun glow)
#define COL_LTBLUE      0x5D7F          // sky blue     (cloud)
#define COL_GREY        0x8C71          // mid grey     (cloud body)
#define COL_DKGREY      0x5AEB          // dark grey    (rain cloud)
#define COL_BLUE        0x035F          // rain drops
#define COL_LTGREY      0xCE79          // light grey   (snow / wind)
#define COL_CYAN        0x07FF          // wind streaks
#define COL_TEXT_DIM    0x9CF3
#define COL_ACCENT      0xFD40          // top-bar accent
// ─────────────────────────────────────────────────────────────

//TFT_eSPI  tft = TFT_eSPI();
//TFT_eSprite spr = TFT_eSprite(&tft);

// ── Data ─────────────────────────────────────────────────────
struct DayForecast {
    char  dayName[4];   // "Mon", "Tue" …
    int   tempMin;
    int   tempMax;
    int   hum;
    int   weatherId;    // OWM weather condition id
    char  desc[24];
};

DayForecast forecast[5];
bool dataReady = false;

// ── Helpers ───────────────────────────────────────────────────
enum IconType { ICON_SUNNY, ICON_CLOUDY, ICON_RAINY, ICON_WINDY, ICON_SNOWY };

IconType weatherIdToIcon(int id) {
    if (id >= 600 && id < 700) return ICON_SNOWY;
    if (id >= 500 && id < 600) return ICON_RAINY;
    if (id >= 300 && id < 400) return ICON_RAINY;   // drizzle
    if (id >= 200 && id < 300) return ICON_RAINY;   // thunderstorm
    if (id == 771 || id == 781 || (id >= 957 && id <= 961)) return ICON_WINDY;
    if (id >= 700 && id < 800) return ICON_WINDY;   // mist / fog / dust
    if (id == 800)             return ICON_SUNNY;
    if (id >= 801 && id <= 804) return ICON_CLOUDY;
    return ICON_CLOUDY;
}

const char* dayOfWeek(int wday) {
    static const char* names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    return names[wday % 7];
}

// ── Icon Drawing ──────────────────────────────────────────────
// All icons drawn into a sprite (cx,cy = centre, r = ~radius hint)

void drawSun(GFXcanvas16& s, int cx, int cy, int r) {
    // Glow
    for (int i = r + 6; i >= r; i--)
        s.drawCircle(cx, cy, i, COL_ORANGE);
    // Body
    s.fillCircle(cx, cy, r - 2, COL_YELLOW);
    // Rays
    for (int a = 0; a < 360; a += 45) {
        float rad = a * DEG_TO_RAD;
        int x1 = cx + (r + 2) * cos(rad);
        int y1 = cy + (r + 2) * sin(rad);
        int x2 = cx + (r + 8) * cos(rad);
        int y2 = cy + (r + 8) * sin(rad);
        s.drawLine(x1, y1, x2, y2, COL_ORANGE);
        s.drawLine(x1+1, y1, x2+1, y2, COL_ORANGE);
    }
    s.fillCircle(cx, cy, r - 3, COL_YELLOW);
}

void drawCloud(GFXcanvas16& s, int cx, int cy, int r, uint16_t col) {
    int w = r * 2, h = r;
    // Base ellipse
    s.fillEllipse(cx, cy, w, h - 2, col);
    // Left bump
    s.fillCircle(cx - r / 2, cy - h / 3, h / 2, col);
    // Right bump
    s.fillCircle(cx + r / 4, cy - h / 2, h * 2 / 3, col);
}

void drawIconSunny(GFXcanvas16& s, int cx, int cy) {
    drawSun(s, cx, cy, 22);
}

void drawIconCloudy(GFXcanvas16& s, int cx, int cy) {
    // Sun peeking behind
    drawSun(s, cx - 10, cy + 4, 16);
    drawCloud(s, cx + 6, cy - 4, 12, COL_GREY);
    drawCloud(s, cx + 6, cy - 4, 10, COL_WHITE);    
    //drawCloud(s, cx + 6, cy - 4, 22, COL_GREY);
    //drawCloud(s, cx + 6, cy - 4, 20, COL_WHITE);
}

void drawIconRainy(GFXcanvas16& s, int cx, int cy) {
    drawCloud(s, cx, cy - 8, 12, COL_DKGREY);
    // Rain drops
    for (int i = 0; i < 5; i++) {
        int rx = cx - 18 + i * 9;
        int ry = cy + 14 + (i % 2) * 6;
        s.drawLine(rx, ry, rx - 3, ry + 8, COL_BLUE);
        s.drawLine(rx + 1, ry, rx - 2, ry + 8, COL_LTBLUE);
    }
}

void drawIconWindy(GFXcanvas16& s, int cx, int cy) {
    // Wavy wind lines
    int offsets[] = {-10, -2, 6, 14};
    for (int i = 0; i < 4; i++) {
        int y = cy + offsets[i];
        int len = (i % 2 == 0) ? 38 : 28;
        for (int x = cx - 20; x < cx - 20 + len - 4; x += 4) {
            s.drawLine(x,     y + (x & 2 ? 1 : 0),
                       x + 4, y + ((x + 4) & 2 ? 1 : 0),
                       i == 0 ? COL_CYAN : COL_LTGREY);
        }
        // Curl at end
        s.drawCircle(cx - 20 + len, y + 2, 3, i == 0 ? COL_CYAN : COL_LTGREY);
    }
}

void drawIconSnowy(GFXcanvas16& s, int cx, int cy) {
    drawCloud(s, cx, cy - 10, 24, COL_GREY);
    // Snowflakes
    uint16_t sc = COL_WHITE;
    int sx[] = {cx - 16, cx - 4, cx + 8, cx - 10, cx + 2};
    int sy[] = {cy + 14, cy + 20, cy + 14, cy + 26, cy + 28};
    for (int i = 0; i < 5; i++) {
        s.drawLine(sx[i] - 4, sy[i], sx[i] + 4, sy[i], sc);
        s.drawLine(sx[i], sy[i] - 4, sx[i], sy[i] + 4, sc);
        s.drawLine(sx[i] - 3, sy[i] - 3, sx[i] + 3, sy[i] + 3, sc);
        s.drawLine(sx[i] + 3, sy[i] - 3, sx[i] - 3, sy[i] + 3, sc);
    }
}

void drawIcon(GFXcanvas16& s, IconType type, int cx, int cy) {
    switch (type) {
        case ICON_SUNNY:  drawIconSunny(s, cx, cy);  break;
        case ICON_CLOUDY: drawIconCloudy(s, cx, cy); break;
        case ICON_RAINY:  drawIconRainy(s, cx, cy);  break;
        case ICON_WINDY:  drawIconWindy(s, cx, cy);  break;
        case ICON_SNOWY:  drawIconSnowy(s, cx, cy);  break;
    }
}

// ── Layout ────────────────────────────────────────────────────
//  Screen: 240 wide × 320 tall
//  Header: 40 px
//  5 panels, each 56 px tall  (5 × 56 = 280 = 320 - 40)

static const int HEADER_H  = 40;
static const int PANEL_H   = 56;
static const int PANEL_W   = 240;
static const int ICON_SIZE = 44;  // sprite canvas

void drawHeader() {
    tft.fillRect(0, 0, 240, HEADER_H, COL_PANEL);
    tft.drawFastHLine(0, HEADER_H - 1, 240, COL_ACCENT);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_WHITE, COL_PANEL);
    tft.setTextSize(2);
    tft.print("5-Day Forecast", 10, HEADER_H / 2);

    // Pulsing dot to indicate live
    static bool dotOn = true;
    dotOn = !dotOn;
    tft.fillCircle(226, HEADER_H / 2, 5, dotOn ? COL_ACCENT : COL_PANEL);
}

void drawPanel(int index, bool highlight = false) {
    if (index >= 5) return;
    DayForecast& d = forecast[index];
    int y0 = HEADER_H + index * PANEL_H;

    uint16_t bgCol = highlight ? COL_PANEL_SEL : (index % 2 == 0 ? COL_BG : COL_PANEL);
    tft.fillRect(0, y0, PANEL_W, PANEL_H, bgCol);
    // Separator
    tft.drawFastHLine(0, y0 + PANEL_H - 1, PANEL_W, COL_PANEL);

    // Day name
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_WHITE, bgCol);
    tft.setTextSize(2);
    tft.print(d.dayName, 8, y0 + 18);

    // Description (small)
    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT_DIM, bgCol);
    tft.print(d.desc, 8, y0 + 36);

    // Icon sprite (centered vertically in panel)
    spr.createSprite(ICON_SIZE, ICON_SIZE);
    spr.fillSprite(bgCol);
    drawIcon(spr, weatherIdToIcon(d.weatherId), ICON_SIZE / 2, ICON_SIZE / 2);
    spr.pushSprite(96, y0 + (PANEL_H - ICON_SIZE) / 2);
    spr.deleteSprite();

    // Temp range
    char buf[16];
    tft.setTextDatum(MR_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(COL_YELLOW, bgCol);
    snprintf(buf, sizeof(buf), "%dC-%dC", d.tempMin, d.tempMax);
    tft.GFXcanvas16(buf, 222, y0 + 18);

    tft.setTextSize(2);
    tft.setTextColor(COL_YELLOW, bgCol);
    snprintf(buf, sizeof(buf), "%d%s", d.hum, "%");
    tft.GFXcanvas16(buf, 222, y0 + 40);
}

void drawLoading(const char* msg) {
    tft.fillScreen(COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE, COL_BG);
    tft.setTextSize(2);
    tft.print(msg, 120, 150);
    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT_DIM, COL_BG);
    tft.print("please wait...", 120, 175);
}

void renderAll() {
    tft.fillScreen(COL_BG);
    drawHeader();
    for (int i = 0; i < 5; i++) drawPanel(i);
}

// ── WiFi ──────────────────────────────────────────────────────
bool connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t > 15000) return false;
        delay(300);
    }
    return true;
}

// ── Weather Fetch ─────────────────────────────────────────────
/*
  OWM /forecast returns 3-hour slots for the next 5 days (40 entries).
  We group by calendar day, pick the dominant weather id and
  min/max temperatures for each day.
*/
bool fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        if (!connectWiFi()) return false;
    }

    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.openweathermap.org/data/2.5/forecast"
        "?id=%s&units=%s&cnt=40&appid=%s",
        OWM_CITY_ID, OWM_UNITS, OWM_API_KEY);

    Serial.println(url);
    http.begin(url);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    // ── Parse ─────────────────────────────────────────────────
    // Use a streaming filter to keep RAM usage low
    JsonDocument filter;
    filter["list"][0]["dt"]               = true;
    filter["list"][0]["main"]["temp_min"] = true;
    filter["list"][0]["main"]["temp_max"] = true;
    filter["list"][0]["main"]["humidity"] = true;
    filter["list"][0]["weather"][0]["id"] = true;
    filter["list"][0]["weather"][0]["description"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    if (err) return false;

    // Reset accumulators
    struct DayAccum {
        int  minT, maxT, hum, domId;
        char domDesc[24];
        int  count, domCount;
        int  wday;
        bool filled;
    } acc[5] = {};
    for (auto& a : acc) { a.minT = 9999; a.maxT = -9999; a.hum = 9999; a.filled = false; }

    // Get today's date to offset days
    time_t now;
    struct tm* ti;
    time(&now);
    ti = localtime(&now);
    int todayDay = ti->tm_yday;

    int daySlot = -1;
    int lastDay = -1;

    for (JsonObject slot : doc["list"].as<JsonArray>()) {
        time_t dt = slot["dt"].as<time_t>();
        struct tm* st = localtime(&dt);
        int slotDay = st->tm_yday;

        if (slotDay != lastDay) {
            lastDay = slotDay;
            daySlot++;
            if (daySlot >= 5) break;
            acc[daySlot].wday = st->tm_wday;
            acc[daySlot].filled = true;
        }
        if (daySlot < 0 || daySlot >= 5) continue;

        float tMin = slot["main"]["temp_min"];
        float tMax = slot["main"]["temp_max"];
        float hum = slot["main"]["humidity"];
        if ((int)tMin < acc[daySlot].minT) acc[daySlot].minT = (int)tMin;
        if ((int)tMax > acc[daySlot].maxT) acc[daySlot].maxT = (int)tMax;
        acc[daySlot].hum = (int) hum;
        // Simplistic dominant-weather: use mid-day slots (12:00–15:00)
        int hour = st->tm_hour;
        if (hour >= 12 && hour < 15) {
            acc[daySlot].domId = slot["weather"][0]["id"] | 800;
            strncpy(acc[daySlot].domDesc,
                    slot["weather"][0]["description"] | "clear",
                    sizeof(acc[daySlot].domDesc) - 1);
        }
    }

    // Copy into global forecast[]
    for (int i = 0; i < 5; i++) {
        if (!acc[i].filled) continue;
        strncpy(forecast[i].dayName, dayOfWeek(acc[i].wday), 4);
        forecast[i].tempMin  = acc[i].minT == 9999 ? 0 : acc[i].minT;
        forecast[i].tempMax  = acc[i].maxT == -9999 ? 0 : acc[i].maxT;
        forecast[i].hum  = acc[i].hum == 9999 ? 0 :acc[i].hum;
        
        forecast[i].weatherId = acc[i].domId ? acc[i].domId : 800;
        // Capitalise first letter
        strncpy(forecast[i].desc, acc[i].domDesc, sizeof(forecast[i].desc) - 1);
        if (forecast[i].desc[0]) forecast[i].desc[0] = toupper(forecast[i].desc[0]);
    }

    return true;
}

// ── NTP ───────────────────────────────────────────────────────
void syncTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm ti;
    uint32_t t = millis();
    while (!getLocalTime(&ti) && millis() - t < 5000) delay(200);
}

// ── Setup / Loop ─────────────────────────────────────────────
uint32_t lastUpdate = 0;
bool     firstRun   = true;

void setup() {
    Serial.begin(115200);

    // TFT init
    tft.init();
    tft.setRotation(1);               // Portrait 240×320
    tft.fillScreen(COL_BG);
    tft.setSwapBytes(true);

    // Optional: turn on backlight via GPIO if wired
    // pinMode(TFT_BL, OUTPUT);
    // digitalWrite(TFT_BL, HIGH);

    drawLoading("Connecting WiFi");

    if (!connectWiFi()) {
        tft.fillScreen(COL_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_RED, COL_BG);
        tft.setTextSize(2);
        tft.print("WiFi Failed", 120, 160);
        while (true) delay(1000);
    }

    syncTime();
    drawLoading("Fetching weather");
}

void loop() {
    uint32_t now = millis();
    bool needUpdate = firstRun || (now - lastUpdate >= UPDATE_INTERVAL_MS);

    if (needUpdate) {
        if (!firstRun) drawHeader(); // flash the indicator before fetch

        bool ok = fetchWeather();
        if (ok) {
            dataReady = true;
            renderAll();
            lastUpdate = millis();
            firstRun   = false;
        } else {
            // Retry in 30 s on failure
            tft.setTextDatum(BC_DATUM);
            tft.setTextColor(TFT_RED, COL_BG);
            tft.setTextSize(1);
            tft.print("Fetch failed – retrying", 120, 318);
            lastUpdate = millis() - UPDATE_INTERVAL_MS + 30000UL;
        }
    } else {
        // Pulse the live indicator every 2 s without full redraw
        static uint32_t lastPulse = 0;
        if (now - lastPulse > 2000) {
            drawHeader();
            lastPulse = now;
        }
    }

    delay(500);
}
