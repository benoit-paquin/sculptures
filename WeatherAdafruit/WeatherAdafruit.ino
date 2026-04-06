/*
 * ============================================================
 *  5-Day Weather Forecast Display
 *  Board  : ESP32-C3
 *  Display: ST7789 240×320 TFT (driven in landscape: 320×240)
 *  Library: Adafruit GFX + Adafruit ST7789
 *  API    : Open-Meteo (free, no API key required)
 *  Refresh: every REFRESH_INTERVAL_MS milliseconds
 * ============================================================
 *
 *  Required libraries (install via Arduino Library Manager):
 *    - Adafruit GFX Library
 *    - Adafruit ST7735 and ST7789 Library
 *    - ArduinoJson  (v6 or v7)
 *    - WiFi  (bundled with ESP32 Arduino core)
 *    - HTTPClient (bundled with ESP32 Arduino core)
 *
 *  Wiring (ESP32-C3 → ST7789):
 *    GPIO 4  → SCK  (SPI clock)
 *    GPIO 6  → SDA  (MOSI / data)
 *    GPIO 7  → CS   (chip select)
 *    GPIO 5  → DC   (data/command)
 *    GPIO 8  → RES  (reset)
 *    GPIO 3  → BLK  (backlight – connect to 3.3 V if unused)
 *    3.3 V   → VCC
 *    GND     → GND
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// ── WiFi credentials ────────────────────────────────────────
#define WIFI_SSID     "H4Mesh"
#define WIFI_PASSWORD "benoitpaquin"

// ── Location (default: Copenhagen, Denmark) ─────────────────
#define LATITUDE   "55.68"
#define LONGITUDE  "12.57"
#define TIMEZONE   "Europe%2FCopenhagen"
#define CITY_NAME  "Copenhagen"

// ── Display SPI pins (ESP32-C3) ─────────────────────────────
#define TFT_SCK   D8
#define TFT_MOSI  D10
#define TFT_CS    D0
#define TFT_DC    D1
#define TFT_RST   D2
#define TFT_BL    D3   // set -1 if backlight is wired directly to 3.3 V

// ── Timing ───────────────────────────────────────────────────
#define REFRESH_INTERVAL_MS (5UL * 60UL * 1000UL)   // 5 minutes

// ── Layout constants (landscape 320×240) ────────────────────
#define SCREEN_W   320
#define SCREEN_H   240
#define HEADER_H   36          // top status / title bar height
#define COL_W      64          // width of each day column (5 × 64 = 320)
#define COL_H      (SCREEN_H - HEADER_H)   // 204 px per column

// ── Colour palette (RGB-565) ─────────────────────────────────
#define C_BG        0x0C0C     // very dark blue-grey background
#define C_HEADER    0x1082     // deep navy header
#define C_PANEL     0x1904     // panel background (subtle)
#define C_PANEL_HL  0x2145     // highlighted (today) panel
#define C_DIVIDER   0x2965     // vertical divider
#define C_WHITE     0xFFFF
#define C_LTGRAY    0xBDF7
#define C_DKGRAY    0x528A
#define C_YELLOW    0xFFE0     // sun body
#define C_ORANGE    0xFCC0     // sun rays
#define C_SKYBLUE   0x867F     // sky / cloud top
#define C_CLOUD     0xD6BA     // cloud grey
#define C_RAIN      0x259F     // rain drops
#define C_SNOW      0xC7FF     // snowflake / crystals
#define C_WIND      0x9EFF     // wind lines
#define C_RED       0xF800
#define C_BLUE      0x001F
#define C_CYAN      0x07FF
#define C_TODAY_BG  0x2965     // today column highlight

// ── Weather condition enum ────────────────────────────────────
enum WeatherCondition { SUNNY, PARTLY_CLOUDY, CLOUDY, RAINY, SNOWY, WINDY, STORMY, UNKNOWN };

// ── Per-day forecast record ───────────────────────────────────
struct DayForecast {
    char   dayLabel[4];   // "MON", "TUE", …
    char   dateLabel[6];  // "31/03"
    int8_t tempMax;
    int8_t tempMin;
    float  windSpeed;     // km/h
    int    wmoCode;
    char   sunrise[6];
    char   sunset[6];
    WeatherCondition condition;
};

// ── Globals ───────────────────────────────────────────────────
//Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);
Adafruit_ST7789 tft(D0, D1, D2);

DayForecast forecast[5];
bool        dataReady    = false;
bool        wifiOK       = false;
unsigned long lastRefresh = 0;

// ─────────────────────────────────────────────────────────────
//  WMO weather-code → WeatherCondition
// ─────────────────────────────────────────────────────────────
WeatherCondition wmoToCondition(int code, float windKmh) {
    if (code == 0)                         return SUNNY;
    if (code <= 2)                         return PARTLY_CLOUDY;
    if (code == 3 || (code >= 45 && code <= 48)) return CLOUDY;
    if ((code >= 51 && code <= 67) ||
        (code >= 80 && code <= 82))        return RAINY;
    if ((code >= 71 && code <= 77) ||
        (code >= 85 && code <= 86))        return SNOWY;
    if (code >= 95)                        return STORMY;
    if (windKmh > 35.0f)                   return WINDY;
    return UNKNOWN;
}

// Short label for the condition
const char* conditionLabel(WeatherCondition c) {
    switch (c) {
        case SUNNY:        return "Sunny";
        case PARTLY_CLOUDY:return "P.Cloudy";
        case CLOUDY:       return "Cloudy";
        case RAINY:        return "Rainy";
        case SNOWY:        return "Snowy";
        case WINDY:        return "Windy";
        case STORMY:       return "Stormy";
        default:           return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────
//  Day-of-week helper
// ─────────────────────────────────────────────────────────────
const char* dayOfWeek(int dow) {   // 0=Sun
    const char* names[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    return names[dow % 7];
}

// ─────────────────────────────────────────────────────────────
//  WiFi connect
// ─────────────────────────────────────────────────────────────
bool connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    return WiFi.status() == WL_CONNECTED;
}

// ─────────────────────────────────────────────────────────────
//  Fetch & parse Open-Meteo forecast
// ─────────────────────────────────────────────────────────────
bool fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        if (!connectWiFi()) return false;
    }

    String url =
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=" LATITUDE
        "&longitude=" LONGITUDE
        "&daily=weathercode,temperature_2m_max,temperature_2m_min,windspeed_10m_max,sunrise,sunset"
        "&timezone=" TIMEZONE
        "&forecast_days=5";

    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    int httpCode = http.GET();

    if (httpCode != 200) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Parse JSON ─────────────────────────────────────────────
    JsonDocument doc;   // ArduinoJson v7; use DynamicJsonDocument(4096) for v6
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;

    JsonObject daily = doc["daily"];
    JsonArray  times  = daily["time"];
    JsonArray  sunrise  = daily["sunrise"];
    JsonArray  sunset  = daily["sunset"];

    JsonArray  wCodes = daily["weathercode"];
    JsonArray  tMax   = daily["temperature_2m_max"];
    JsonArray  tMin   = daily["temperature_2m_min"];
    JsonArray  wind   = daily["windspeed_10m_max"];

    for (int i = 0; i < 5; i++) {
        // Parse date string "YYYY-MM-DD"
        const char* dateStr = times[i];
        int year  = 0, month = 0, day = 0;
        sscanf(dateStr, "%d-%d-%d", &year, &month, &day);

        // Zeller's congruence for day-of-week
        if (month < 3) { month += 12; year--; }
        int k = year % 100, j = year / 100;
        int dow = (day + (13*(month+1))/5 + k + k/4 + j/4 + 5*j) % 7;
        // Zeller gives 0=Sat,1=Sun... remap to 0=Sun
        dow = (dow + 6) % 7;

        snprintf(forecast[i].dayLabel,  sizeof(forecast[i].dayLabel),  "%s",
                 (i == 0) ? "TODAY" : dayOfWeek(dow));
        snprintf(forecast[i].dateLabel, sizeof(forecast[i].dateLabel), "%02d/%02d",
                 day, month % 100);

        forecast[i].tempMax   = (int8_t)round((float)tMax[i]);
        forecast[i].tempMin   = (int8_t)round((float)tMin[i]);
        forecast[i].windSpeed = (float)wind[i];
        forecast[i].wmoCode   = (int)wCodes[i];
        forecast[i].condition = wmoToCondition(forecast[i].wmoCode, forecast[i].windSpeed);
        dateStr = sunrise[i];
        int hour=0, minute=0;
        sscanf(dateStr, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute);
        snprintf(forecast[i].sunrise, sizeof(forecast[i].sunrise), "%02d:%02d", hour, minute);
        dateStr = sunset[i];
        sscanf(dateStr, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute);
        snprintf(forecast[i].sunset, sizeof(forecast[i].sunset), "%02d:%02d", hour, minute);
    }
    return true;
}

// ═════════════════════════════════════════════════════════════
//  ICON DRAWING — all icons fit inside a cx,cy centred box
// ═════════════════════════════════════════════════════════════

// ── Helper: draw a cloud shape centred at (cx,cy), radius r ─
void drawCloud(int cx, int cy, int r, uint16_t colour) {
    int r2 = r * 2 / 3;
    tft.fillCircle(cx,      cy,      r,  colour);
    tft.fillCircle(cx - r,  cy + 3,  r2, colour);
    tft.fillCircle(cx + r,  cy + 3,  r2, colour);
    // flat base
    tft.fillRect(cx - r - r2 + 2, cy + 3, (r + r2) * 2 - 4, r, colour);
}

// ── Helper: draw a small cloud ──────────────────────────────
void drawSmallCloud(int cx, int cy, uint16_t colour) {
    drawCloud(cx, cy, 10, colour);
}

// ── SUNNY ────────────────────────────────────────────────────
void drawIconSunny(int cx, int cy) {
    // Rays
    for (int a = 0; a < 360; a += 45) {
        float rad = a * M_PI / 180.0f;
        int x1 = cx + (int)(cos(rad) * 17);
        int y1 = cy + (int)(sin(rad) * 17);
        int x2 = cx + (int)(cos(rad) * 25);
        int y2 = cy + (int)(sin(rad) * 25);
        tft.drawLine(x1, y1, x2, y2, C_ORANGE);
        tft.drawLine(x1+1, y1, x2+1, y2, C_ORANGE);  // 2-px thick
    }
    // Sun disc
    tft.fillCircle(cx, cy, 14, C_YELLOW);
    tft.fillCircle(cx, cy, 11, 0xFFFF - 0x2000);  // slightly lighter centre
}

// ── PARTLY CLOUDY ────────────────────────────────────────────
void drawIconPartlyCloudy(int cx, int cy) {
    // Small sun behind cloud
    int sx = cx - 8, sy = cy - 8;
    for (int a = 0; a < 360; a += 60) {
        float rad = a * M_PI / 180.0f;
        int x1 = sx + (int)(cos(rad) * 10);
        int y1 = sy + (int)(sin(rad) * 10);
        int x2 = sx + (int)(cos(rad) * 16);
        int y2 = sy + (int)(sin(rad) * 16);
        tft.drawLine(x1, y1, x2, y2, C_ORANGE);
    }
    tft.fillCircle(sx, sy, 9, C_YELLOW);
    // Cloud in front
    drawCloud(cx + 4, cy + 4, 12, C_CLOUD);
}

// ── CLOUDY ───────────────────────────────────────────────────
void drawIconCloudy(int cx, int cy) {
    // Two stacked clouds for depth
    drawCloud(cx + 5, cy - 5, 11, C_DKGRAY);
    drawCloud(cx,     cy + 4, 14, C_CLOUD);
}

// ── RAINY ────────────────────────────────────────────────────
void drawIconRainy(int cx, int cy) {
    drawCloud(cx, cy - 6, 14, C_CLOUD);
    // Rain drops (diagonal lines)
    for (int i = 0; i < 4; i++) {
        int rx = cx - 18 + i * 12;
        int ry = cy + 14;
        tft.drawLine(rx,   ry,   rx - 4, ry + 10, C_RAIN);
        tft.drawLine(rx+1, ry,   rx - 3, ry + 10, C_RAIN);
    }
}

// ── SNOWY ────────────────────────────────────────────────────
void drawIconSnowy(int cx, int cy) {
    drawCloud(cx, cy - 8, 14, C_CLOUD);
    // Snowflake dots in a grid
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 4; col++) {
            int sx = cx - 17 + col * 11;
            int sy = cy + 12 + row * 10;
            // 6-arm asterisk
            tft.drawLine(sx - 4, sy,     sx + 4, sy,     C_SNOW);
            tft.drawLine(sx,     sy - 4, sx,     sy + 4, C_SNOW);
            tft.drawLine(sx - 3, sy - 3, sx + 3, sy + 3, C_SNOW);
            tft.drawLine(sx - 3, sy + 3, sx + 3, sy - 3, C_SNOW);
        }
    }
}

// ── WINDY ────────────────────────────────────────────────────
void drawIconWindy(int cx, int cy) {
    drawSmallCloud(cx, cy - 14, C_CLOUD);
    // Wavy wind lines
    for (int i = 0; i < 3; i++) {
        int wy = cy - 2 + i * 10;
        int len = (i == 1) ? 30 : 22;
        // Approximated wave with short segments
        int x0 = cx - len / 2;
        for (int s = 0; s < 4; s++) {
            int wx1 = x0 + s * (len / 4);
            int wx2 = wx1 + (len / 4);
            int wy1 = wy + (s % 2 == 0 ? -3 : 3);
            int wy2 = wy + (s % 2 == 0 ? 3 : -3);
            tft.drawLine(wx1, wy1, wx2, wy2, C_WIND);
            tft.drawLine(wx1, wy1 + 1, wx2, wy2 + 1, C_WIND);
        }
    }
}

// ── STORMY ───────────────────────────────────────────────────
void drawIconStormy(int cx, int cy) {
    // Dark cloud
    drawCloud(cx, cy - 8, 14, C_DKGRAY);
    // Lightning bolt
    int bx = cx, by = cy + 8;
    tft.fillTriangle(bx,     by,     bx - 8, by + 10, bx - 2, by + 10, C_YELLOW);
    tft.fillTriangle(bx - 2, by + 10, bx + 6, by + 10, bx - 6, by + 22, C_YELLOW);
    // Rain on the sides
    tft.drawLine(cx - 20, by,     cx - 24, by + 9,  C_RAIN);
    tft.drawLine(cx + 12, by,     cx +  8, by + 9,  C_RAIN);
}

// ── UNKNOWN ──────────────────────────────────────────────────
void drawIconUnknown(int cx, int cy) {
    tft.drawCircle(cx, cy, 16, C_DKGRAY);
    tft.setCursor(cx - 4, cy - 8);
    tft.setTextSize(2);
    tft.setTextColor(C_DKGRAY);
    tft.print("?");
}

// Dispatch to correct icon
void drawWeatherIcon(int cx, int cy, WeatherCondition cond) {
    switch (cond) {
        case SUNNY:         drawIconSunny(cx, cy);        break;
        case PARTLY_CLOUDY: drawIconPartlyCloudy(cx, cy); break;
        case CLOUDY:        drawIconCloudy(cx, cy);       break;
        case RAINY:         drawIconRainy(cx, cy);        break;
        case SNOWY:         drawIconSnowy(cx, cy);        break;
        case WINDY:         drawIconWindy(cx, cy);        break;
        case STORMY:        drawIconStormy(cx, cy);       break;
        default:            drawIconUnknown(cx, cy);      break;
    }
}

// ═════════════════════════════════════════════════════════════
//  DISPLAY RENDERING
// ═════════════════════════════════════════════════════════════

void drawHeader(bool ok) {
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, C_HEADER);

    // City name left-aligned
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE);
    tft.setCursor(8, (HEADER_H - 16) / 2);
    tft.print(CITY_NAME);

    // Status indicator right-aligned
    tft.setTextSize(1);
    tft.setTextColor(ok ? 0x07E0 : C_RED);
    tft.setCursor(SCREEN_W - 50, (HEADER_H - 8) / 2);
    tft.print(ok ? "WiFi OK" : "No WiFi");

    // Bottom accent line
    tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, C_WIND);
}

void drawColumnPanel(int col, const DayForecast& d, bool isToday) {
    int x = col * COL_W;
    int y = HEADER_H;

    // Panel background
    uint16_t panelBg = isToday ? C_TODAY_BG : C_BG;
    tft.fillRect(x, y, COL_W, COL_H, panelBg);

    // Vertical divider (skip leftmost)
    if (col > 0)
        tft.drawFastVLine(x, y, COL_H, C_DIVIDER);

    int cx = x + COL_W / 2;   // column centre X

    // ── Day label ───────────────────────────────────────────
    tft.setTextSize(1);
    tft.setTextColor(isToday ? C_YELLOW : C_LTGRAY);

    // Centre the day label text
    int16_t tbx, tby; uint16_t tbw, tbh;
    tft.getTextBounds(d.dayLabel, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor(cx - tbw / 2, y + 6);
    tft.print(d.dayLabel);

    // Date label below
    tft.setTextColor(C_WHITE);
    tft.getTextBounds(d.dateLabel, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor(cx - tbw / 2, y + 17);
    tft.print(d.dateLabel);

    // ── Weather icon (centred vertically in available space) ─
    int iconCY = y + 16 + 40;   // ~32px below top label, 40px = icon half-height
    drawWeatherIcon(cx, iconCY, d.condition);

    // ── Condition label ──────────────────────────────────────
    int lblY = y + 6 + 80 + 4;
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE);
    const char* lbl = conditionLabel(d.condition);
    tft.getTextBounds(lbl, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor(cx - tbw / 2, lblY);
    tft.print(lbl);

    // ── Temperatures ────────────────────────────────────────
    int tempY = lblY + 14;

    // High temp (warm colour)
    char buf[8];
    snprintf(buf, sizeof(buf), "%d-%d", d.tempMin,d.tempMax);   // °
    tft.setTextSize(2);
    tft.setTextColor(d.tempMax >= 0 ? C_ORANGE : C_CYAN);
    tft.getTextBounds(buf, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor(cx - tbw / 2, tempY);
    tft.print(buf);

    // Low temp (cool colour)
    snprintf(buf, sizeof(buf), "%d\xB0", d.tempMin);
    tft.setTextSize(1);
    tft.setTextColor(d.tempMin >= 0 ? C_LTGRAY : C_CYAN);
    tft.getTextBounds(buf, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor(cx - tbw / 2, tempY + 18);
    tft.print(buf);

    // ── Wind speed ───────────────────────────────────────────
    int windY = tempY + 30;
    snprintf(buf, sizeof(buf), "%.0fkm/h", d.windSpeed);
    tft.setTextSize(1);
    tft.setTextColor(C_CYAN);
    tft.getTextBounds(buf, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor(cx - tbw / 2, windY);
    tft.print(buf);
    // ── sunrise ───────────────────────────────────────────
    int sunrY = windY + 10;
    snprintf(buf, sizeof(buf), "%s", d.sunrise);
    tft.setTextSize(1);
    tft.setTextColor(C_CYAN);
    tft.getTextBounds(buf, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor(cx - tbw / 2, sunrY);
    tft.print(buf);
    // ── sunrise ───────────────────────────────────────────
    int sunsY = sunrY + 10;
    snprintf(buf, sizeof(buf), "%s", d.sunset);
    tft.setTextSize(1);
    tft.setTextColor(C_CYAN);
    tft.getTextBounds(buf, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor(cx - tbw / 2, sunsY);
    tft.print(buf);
}

void renderForecast() {
    tft.fillScreen(C_BG);
    drawHeader(wifiOK);
    for (int i = 0; i < 5; i++) {
        drawColumnPanel(i, forecast[i], i == 0);
    }
}

void renderLoadingScreen(const char* msg) {
    tft.fillScreen(C_BG);
    drawHeader(false);
    tft.setTextSize(1);
    tft.setTextColor(C_LTGRAY);
    int16_t tbx, tby; uint16_t tbw, tbh;
    tft.getTextBounds(msg, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor((SCREEN_W - tbw) / 2, SCREEN_H / 2 - 4);
    tft.print(msg);
}

void renderErrorScreen(const char* msg) {
    tft.fillScreen(C_BG);
    drawHeader(false);
    tft.setTextSize(1);
    tft.setTextColor(C_RED);
    int16_t tbx, tby; uint16_t tbw, tbh;
    tft.getTextBounds("Error", 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor((SCREEN_W - tbw) / 2, SCREEN_H / 2 - 16);
    tft.print("Error");
    tft.setTextColor(C_LTGRAY);
    tft.getTextBounds(msg, 0, 0, &tbx, &tby, &tbw, &tbh);
    tft.setCursor((SCREEN_W - tbw) / 2, SCREEN_H / 2 + 2);
    tft.print(msg);
}

// Countdown bar shown at bottom of screen (subtle progress)
void renderCountdownBar(unsigned long elapsed, unsigned long total) {
    int barW = (int)((float)elapsed / total * SCREEN_W);
    barW = constrain(barW, 0, SCREEN_W);
    tft.drawFastHLine(0,    SCREEN_H - 2, SCREEN_W, C_DIVIDER);
    tft.drawFastHLine(0,    SCREEN_H - 2, barW,     C_WIND);
    tft.drawFastHLine(barW, SCREEN_H - 2, SCREEN_W - barW, C_DIVIDER);
}

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);

    // Backlight on
    if (TFT_BL >= 0) {
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, HIGH);
    }

    // Initialise display in landscape (rotation 1 = 320×240)
    tft.init(240, 320);
    tft.setRotation(1);
    tft.fillScreen(C_BG);
    tft.setSPISpeed(40000000UL);   // 40 MHz SPI

    renderLoadingScreen("Connecting to WiFi...");

    wifiOK = connectWiFi();
    if (!wifiOK) {
        renderErrorScreen("Check SSID/Password");
        delay(3000);
    }

    renderLoadingScreen("Fetching forecast...");

    dataReady = fetchWeather();
    if (dataReady) {
        renderForecast();
    } else {
        renderErrorScreen("Could not fetch data");
    }

    lastRefresh = millis();
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════
void loop() {
    unsigned long now     = millis();
    unsigned long elapsed = now - lastRefresh;

    // Draw progress bar showing time until next refresh
    if (dataReady) {
        renderCountdownBar(elapsed, REFRESH_INTERVAL_MS);
    }

    // Refresh when interval has elapsed
    if (elapsed >= REFRESH_INTERVAL_MS) {
        lastRefresh = millis();

        // Re-check WiFi
        if (WiFi.status() != WL_CONNECTED) {
            wifiOK = connectWiFi();
        } else {
            wifiOK = true;
        }

        bool ok = fetchWeather();
        if (ok) {
            dataReady = true;
            renderForecast();
        } else {
            // Keep old data, update header to show connectivity issue
            drawHeader(false);
        }
    }

    delay(200);   // update progress bar ~5×/sec, keep CPU cool
}
