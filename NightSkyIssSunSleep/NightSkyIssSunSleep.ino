 /*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║       COPENHAGEN NIGHT SKY  –  ESP32-C3                      ║
 * ║                                                              ║
 * ║  Renders stars, planets, Moon, Sun and the ISS as seen from  ║
 * ║  Copenhagen (55.68°N 12.57°E) on a 240×320 TFT.              ║
 * ║  Time is fetched via NTP.  ISS: api.open-notify.org          ║
 * ║                                                              ║
 * ║  POWER / INTERACTION                                         ║
 * ║    • Display stays on for 5 minutes, then deep-sleep.        ║
 * ║    • Push button (BTN_PIN) wakes the display for 5 minutes.  ║
 * ║    • While sleeping, ESP32 wakes every 60 s to check ISS.    ║
 * ║    • LED (LED_PIN) blinks whenever ISS is above the horizon, ║
 * ║      whether the display is on or off.                       ║
 * ║                                                              ║
 * ║  Projection: zenithal (azimuthal equidistant)                ║
 * ║    – zenith at centre, horizon at circle edge                ║
 * ║    – North at top, East to the right                         ║
 * ║                                                              ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <math.h>
#include <esp_sleep.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "DrawSky.h"


class MyST7789 : public Adafruit_ST7789 {
  public:
    // Utilizziamo i costruttori della classe base
    using Adafruit_ST7789::Adafruit_ST7789;

    // Aggiungi il tuo metodo personalizzato
    void drawString(const char* title, uint16_t x, uint16_t y) {
      setCursor(x, y);                         // Metodo originale
      print(title);                          // Metodo originale
    }
};
// ─────────────────────────────────────────────────────────────
//  !! EDIT THESE !!
// ─────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "H4Mesh";
const char* WIFI_PASSWORD = "benoitpaquin";

// ─────────────────────────────────────────────────────────────
//  HARDWARE PINS  –  adjust to your wiring
// ─────────────────────────────────────────────────────────────
#define LED_PIN   D6    // GPIO driving the ISS-visible LED
#define BTN_PIN   5    // GPIO connected to wake-up push button
#define PIN_BL    D4
                       // (active LOW – button connects pin to GND)
                       // GPIO 9 is the BOOT button on most ESP32-C3 boards

// ─────────────────────────────────────────────────────────────
//  TIMING
// ─────────────────────────────────────────────────────────────
#define DISPLAY_ON_MS       (60UL  * 60UL * 1000UL)  // display window
#define REFRESH_MS          (1UL  * 60UL * 1000UL)  // sky redraw interval
#define ISS_SLEEP_US        (60ULL * 1000000ULL)     // deep-sleep ISS check period
#define LED_ON_MS           150UL                    // blink on-time
#define LED_PERIOD_MS       1000UL                   // blink period

// ─────────────────────────────────────────────────────────────
//  RTC MEMORY  –  survives deep sleep
// ─────────────────────────────────────────────────────────────
// g_issVisible : was ISS above horizon on the last check?
// g_displayMode: true = button/first-boot wake (show display)
//                false = timer wake (LED-only, no display)
RTC_DATA_ATTR bool g_issVisible  = false;
RTC_DATA_ATTR bool g_displayMode = true;

// ─────────────────────────────────────────────────────────────
//  OBSERVER  (Copenhagen)
// ─────────────────────────────────────────────────────────────
const double OBS_LAT =  55.6761;   // degrees North
const double OBS_LON =  12.5683;   // degrees East

// Copenhagen timezone: CET (UTC+1) / CEST (UTC+2, last Sun Mar–Oct)
const char* TZ_STR  = "CET-1CEST,M3.5.0,M10.5.0/3";

// ─────────────────────────────────────────────────────────────
//  DISPLAY
// ─────────────────────────────────────────────────────────────
#define SCREEN_W   240
#define SCREEN_H   320
#define SKY_CX     120          // sky circle centre X
#define SKY_CY     150          // sky circle centre Y
#define SKY_R      112          // horizon radius in pixels

MyST7789 tft = MyST7789(D0,D1,D2);
void blink(int nbtimes) {
  for (int i=0; i<nbtimes; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN,LOW);
    delay(100);
  }
}


#define D2R  (M_PI / 180.0)
#define R2D  (180.0 / M_PI)


// ─────────────────────────────────────────────────────────────
//  STAR CATALOGUE  (RA hours · Dec degrees · visual magnitude)
//  Biased towards northern-sky / circumpolar stars visible from
//  Copenhagen, plus the brightest southern stars when up.
// ─────────────────────────────────────────────────────────────
struct Star { const char *name; float ra, dec, mag; uint16_t col; };

// 16-bit RGB565 colours by spectral class
#define TFT_LIGHTGREY 0x8410
#define TFT_BLACK     0x0000
#define TFT_RED       0xF800
#define TFT_WHITE     0xFFFF
#define TFT_YELLOW    0xFD20
#define TFT_ORANGE    0xF810
#define TFT_DARKGREY  0x4208
#define COL_O  0xAEFF   // O  –  blue
#define COL_B  0xBDFF   // B  –  blue-white
#define COL_A  TFT_WHITE
#define COL_F  0xF7BE   // F  –  yellow-white
#define COL_G  TFT_YELLOW
#define COL_K  TFT_ORANGE
#define COL_M  0xF810   // M  –  orange-red

static const Star STARS[] = {
  // ── Absolute brightest ──────────────────────────────────
  { "Sirius",       6.7525, -16.716, -1.46, COL_A  },
  { "Canopus",      6.3992, -52.696, -0.74, COL_F  },
  { "Arcturus",    14.2612,  19.182, -0.05, COL_K  },
  { "Vega",        18.6157,  38.784,  0.03, COL_A  },
  { "Capella",      5.2781,  45.998,  0.08, COL_G  },
  { "Rigel",        5.2423,  -8.202,  0.13, COL_B  },
  { "Procyon",      7.6551,   5.225,  0.34, COL_F  },
  { "Achernar",     1.6286, -57.237,  0.46, COL_B  },
  { "Betelgeuse",   5.9195,   7.407,  0.50, COL_M  },
  { "Altair",      19.8463,   8.868,  0.77, COL_A  },
  { "Aldebaran",    4.5987,  16.509,  0.87, COL_K  },
  { "Antares",     16.4901, -26.432,  1.09, COL_M  },
  { "Spica",       13.4199, -11.161,  0.97, COL_B  },
  { "Pollux",       7.7553,  28.026,  1.16, COL_K  },
  { "Fomalhaut",   22.9608, -29.622,  1.16, COL_A  },
  { "Deneb",       20.6905,  45.280,  1.25, COL_A  },
  { "Regulus",     10.1395,  11.967,  1.36, COL_B  },
  { "Castor",       7.5767,  31.888,  1.58, COL_A  },
  // ── Orion belt & neighbours ─────────────────────────────
  { "Bellatrix",    5.4186,   6.350,  1.64, COL_B  },
  { "Elnath",       5.4381,  28.608,  1.65, COL_B  },
  { "Mintaka",      5.5335,  -0.299,  2.23, COL_B  },
  { "Alnilam",      5.6036,  -1.202,  1.69, COL_B  },
  { "Alnitak",      5.6793,  -1.943,  1.74, COL_B  },
  { "Adhara",       6.9771, -28.972,  1.50, COL_B  },
  // ── Ursa Major (Big Dipper) ─────────────────────────────
  { "Dubhe",       11.0621,  61.751,  1.81, COL_K  },
  { "Merak",       11.0308,  56.382,  2.37, COL_A  },
  { "Phecda",      11.8976,  53.695,  2.44, COL_A  },
  { "Megrez",      12.2568,  57.033,  3.31, COL_A  },
  { "Alioth",      12.9004,  55.960,  1.77, COL_A  },
  { "Mizar",       13.3990,  54.926,  2.27, COL_A  },
  { "Alkaid",      13.7923,  49.313,  1.86, COL_B  },
  // ── Ursa Minor ──────────────────────────────────────────
  { "Polaris",      2.5301,  89.264,  1.97, COL_F  },   // North Star!
  { "Kochab",      14.8451,  74.156,  2.08, COL_K  },
  { "Pherkad",     15.3453,  71.834,  3.05, COL_A  },
  // ── Cassiopeia ──────────────────────────────────────────
  { "Schedar",      0.6752,  56.537,  2.24, COL_K  },
  { "Caph",         0.1528,  59.150,  2.28, COL_F  },
  { "Navi",         0.9453,  60.717,  2.47, COL_B  },
  { "Ruchbah",      1.4306,  60.235,  2.66, COL_A  },
  { "Segin",        1.9062,  63.670,  3.38, COL_B  },
  // ── Perseus / Auriga ────────────────────────────────────
  { "Mirfak",       3.4081,  49.861,  1.81, COL_F  },
  { "Algol",        3.1361,  40.957,  2.12, COL_B  },
  { "Menkalinan",   5.9922,  44.947,  1.90, COL_A  },
  // ── Cepheus ─────────────────────────────────────────────
  { "Alderamin",   21.3097,  62.585,  2.45, COL_A  },
  // ── Andromeda / Pegasus ─────────────────────────────────
  { "Mirach",       1.1622,  35.621,  2.07, COL_M  },
  { "Almach",       2.0650,  42.330,  2.26, COL_K  },
  { "Hamal",        2.1196,  23.462,  2.00, COL_K  },
  { "Scheat",      23.0629,  28.083,  2.42, COL_M  },
  { "Markab",      23.0794,  15.205,  2.49, COL_B  },
  { "Alpheratz",    0.1395,  29.091,  2.06, COL_B  },
  // ── Lyra / Cygnus / Aquila (Summer Triangle) ────────────
  { "Sulafat",     18.9822,  32.690,  3.25, COL_B  },
  { "Sadr",        20.3702,  40.257,  2.23, COL_F  },
  { "Gienah",      20.7704,  33.970,  2.46, COL_F  },
  // ── Draco ───────────────────────────────────────────────
  { "Eltanin",     17.9436,  51.489,  2.24, COL_K  },
  { "Rastaban",    17.5074,  52.301,  2.79, COL_G  },
  // ── Boötes / Hercules / Corona ──────────────────────────
  { "Alphecca",    15.5782,  26.715,  2.23, COL_A  },
  { "Rasalhague",  17.5822,  12.560,  2.08, COL_A  },
  // ── Leo ─────────────────────────────────────────────────
  { "Denebola",    11.8177,  14.572,  2.14, COL_A  },
  { "Algieba",     10.3328,  19.842,  2.01, COL_K  },
  // ── Gemini ──────────────────────────────────────────────
  { "Alhena",       6.6285,  16.399,  1.93, COL_A  },
  // ── Virgo / Libra ───────────────────────────────────────
  { "Alphecca",    15.5782,  26.715,  2.23, COL_A  },
};

static const int NUM_STARS = sizeof(STARS) / sizeof(STARS[0]);

// ─────────────────────────────────────────────────────────────
//  PLANET METADATA
// ─────────────────────────────────────────────────────────────
static const char*    PNAME[] = { "Mercury","Venus",  "Mars",  "Jupiter","Saturn" };
static const uint16_t PCOL [] = {  0xC618,   0xFFE0,  0xF800,  0xFCC0,   0xFFD0  };
//                                 grey      bright   red      amber     pale-yel

// ─────────────────────────────────────────────────────────────
//  DRAW HELPERS
// ─────────────────────────────────────────────────────────────

// Scale a star to a pixel size based on visual magnitude
static void drawStar(int x, int y, float mag, uint16_t col) {
  if      (mag < 0.0f)  tft.fillCircle(x, y, 3, col);
  else if (mag < 1.0f)  tft.fillCircle(x, y, 2, col);
  else if (mag < 2.0f) {
    tft.drawPixel(x,   y,   col);
    tft.drawPixel(x+1, y,   col);
    tft.drawPixel(x-1, y,   col);
    tft.drawPixel(x,   y+1, col);
    tft.drawPixel(x,   y-1, col);
  } else {
    tft.drawPixel(x, y, col);
  }
}

// Moon with phase: 0=new, 0.25=first quarter, 0.5=full, 0.75=last quarter
static void drawMoon(int cx, int cy, double phase) {
  const int r = 8;
  // Elongation angle in radians (0 at new moon, π at full moon)
  double E = 2.0 * M_PI * phase;

  for (int dy = -r; dy <= r; dy++) {
    double xhalf = sqrt((double)(r*r - dy*dy));
    double x_term = xhalf * cos(E);
    for (int dx = (int)(-xhalf); dx <= (int)(xhalf); dx++) {
      // Waxing (0–0.5): right side lit; Waning (0.5–1): left side lit
      bool lit = (phase < 0.5) ? ((double)dx >= x_term)
                                : ((double)dx <= -x_term);
      uint16_t c = lit ? TFT_WHITE : 0x3186;  // white or dark grey
      tft.drawPixel(cx + dx, cy + dy, c);
    }
  }
  tft.drawCircle(cx, cy, r, TFT_LIGHTGREY);  // limb
}

// ─────────────────────────────────────────────────────────────
//  SUN DRAW + SKY COLOUR
// ─────────────────────────────────────────────────────────────

// Returns RGB565 sky dome colour based on solar elevation:
//   > 6°   full daylight blue
//   -6°…6° twilight gradient (orange → indigo)
//   < -6°  night (near-black)
static uint16_t skyDomeColor(double sunAlt) {
  // RGB components (0-255)
  int r, g, b;
  if (sunAlt >= 6.0) {
    // Daytime – scale from deep blue (low) to sky blue (high)
    double t = constrain((sunAlt - 6.0) / 60.0, 0.0, 1.0);
    r = (int)(20  + t * 55);   //  20 → 75
    g = (int)(80  + t * 100);  //  80 → 180
    b = (int)(160 + t * 60);   // 160 → 220
  } else if (sunAlt >= -6.0) {
    // Civil twilight – orange-to-indigo gradient
    double t = (sunAlt + 6.0) / 12.0;  // 0 at -6°, 1 at +6°
    r = (int)(80  + t * 0   );  // 80  night-side
    g = (int)(20  + t * 40  );  // 20 → 60
    b = (int)(60  + t * 80  );  // 60 → 140
    // Add orange flush near sunrise/sunset
    r = (int)(r   + t * (1.0 - t) * 4 * 120);
    g = (int)(g   + t * (1.0 - t) * 4 * 40 );
  } else {
    // Night
    r = 0; g = 0; b = 2;
  }
  r = constrain(r, 0, 255);
  g = constrain(g, 0, 255);
  b = constrain(b, 0, 255);
  return tft.color565(r, g, b);
}

// ─────────────────────────────────────────────────────────────
//  STATUS BAR (bottom strip) – two lines
// ─────────────────────────────────────────────────────────────
static void drawStatusBar(const struct tm &ti,
                          bool issOk = false,
                          double issAlt = -90, double issAz = 0) {
  tft.fillRect(0, SCREEN_H - 30, SCREEN_W, 30, 0x000F);

  // ── Line 1: time + date ───────────────────────────────────
  char buf[48];
  snprintf(buf, sizeof(buf), "%02d:%02d  %02d/%02d/%04d",
           ti.tm_hour, ti.tm_min, ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
  tft.setTextSize(1);
  tft.setTextColor(0xC618, TFT_BLACK);
  tft.drawString(buf, 4, SCREEN_H - 26);
  tft.setTextColor(0x4A49, TFT_BLACK);
  tft.drawString("CPH 55.7N", 162, SCREEN_H - 26);

  // ── Line 2: ISS status ───────────────────────────────────
  if (!issOk) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("ISS: no data", 4, SCREEN_H - 13);
  } else if (issAlt < 0) {
    snprintf(buf, sizeof(buf), "ISS: below horizon  (el %+.0f)", issAlt);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, 4, SCREEN_H - 13);
  } else {
    snprintf(buf, sizeof(buf), "ISS: el %+.0f  az %.0f", issAlt, issAz);
    tft.setTextColor(0x07FF, TFT_BLACK);   // cyan = visible
    tft.drawString(buf, 4, SCREEN_H - 13);
  }
}


// ─────────────────────────────────────────────────────────────
//  ISS  –  live position from Open Notify
// ─────────────────────────────────────────────────────────────
#define ISS_ALT_KM  408.0          // mean ISS orbital altitude
#define EARTH_R_KM 6371.0

// Pull current ISS sub-point from api.open-notify.org
// Returns true and fills lat/lon on success.
bool fetchISSPosition(double &lat, double &lon) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin("http://api.open-notify.org/iss-now.json");
  http.setTimeout(4000);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String payload = http.getString();
  http.end();

  // Payload: {"iss_position": {"longitude": "12.34", "latitude": "-5.67"}, ...}
  // Simple substring parse – no extra library needed.
  int latIdx = payload.indexOf("\"latitude\":");
  int lonIdx = payload.indexOf("\"longitude\":");
  if (latIdx < 0 || lonIdx < 0) return false;

  // Skip past key + colon + optional space + optional quote
  auto extractVal = [&](int idx, int keyLen) -> double {
    int start = idx + keyLen;
    while (start < (int)payload.length() &&
           (payload[start] == ' ' || payload[start] == '"')) start++;
    int end = start;
    while (end < (int)payload.length() &&
           (isdigit(payload[end]) || payload[end] == '-' || payload[end] == '.')) end++;
    return payload.substring(start, end).toDouble();
  };

  lat = extractVal(latIdx, 11);   // len("\"latitude\":") = 11
  lon = extractVal(lonIdx, 12);   // len("\"longitude\":") = 12
  return true;
}

// Convert ISS sub-satellite point + altitude → local Alt/Az from Copenhagen
// Uses the standard satellite elevation formula (spherical Earth).
void issToAltAz(double satLat, double satLon,
                double obsLat, double obsLon,
                double &alt_deg, double &az_deg) {
  double phi1  = obsLat * D2R;
  double phi2  = satLat * D2R;
  double dLon  = (satLon - obsLon) * D2R;

  // Central angle between observer and sub-satellite point
  double cosRho = sin(phi1)*sin(phi2) + cos(phi1)*cos(phi2)*cos(dLon);
  cosRho = constrain(cosRho, -1.0, 1.0);
  double rho = acos(cosRho);

  // Elevation (nadir angle formula)
  double rRatio = EARTH_R_KM / (EARTH_R_KM + ISS_ALT_KM);
  // sin(elevation) = (cos(rho) - rRatio) / sqrt(1 - 2*rRatio*cos(rho) + rRatio^2)
  double sinEl = (cos(rho) - rRatio) /
                 sqrt(1.0 - 2.0*rRatio*cos(rho) + rRatio*rRatio + 1e-12);
  alt_deg = asin(constrain(sinEl, -1.0, 1.0)) * R2D;

  // Azimuth: bearing from observer to sub-satellite point
  double y = sin(dLon) * cos(phi2);
  double x = cos(phi1)*sin(phi2) - sin(phi1)*cos(phi2)*cos(dLon);
  az_deg = norm360(atan2(y, x) * R2D);
}

// ─────────────────────────────────────────────────────────────
//  MAIN RENDER PASS
// ─────────────────────────────────────────────────────────────
void drawSky() {
  struct tm ti;
  if (!getLocalTime(&ti, 5000)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(1);
    tft.drawString("NTP sync failed – check WiFi", 10, 150);
    return;
  }

  double JD  = julianDate(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                           ti.tm_hour, ti.tm_min, ti.tm_sec);
  double LST = localSiderealTime(JD, OBS_LON);

  // ── Sun position (needed for sky colour before drawing anything) ──
  double sunRA, sunDec, sunAlt, sunAz;
  {
    sunRADec(JD, sunRA, sunDec);
    raDecToAltAz(sunRA, sunDec, LST, OBS_LAT, sunAlt, sunAz);
  }

  // ── Background ──────────────────────────────────────────────
  tft.fillScreen(TFT_BLACK);
  uint16_t domeCol = skyDomeColor(sunAlt);
  tft.fillCircle(SKY_CX, SKY_CY, SKY_R, domeCol);

  drawGrid();
  // Horizon ring: green-grey at night, warm at twilight, bright at day
  uint16_t horizCol = (sunAlt > 6) ? tft.color565(80,160,80)
                    : (sunAlt > -6) ? tft.color565(120,80,40)
                    : 0x4208;
  tft.drawCircle(SKY_CX, SKY_CY, SKY_R, horizCol);
  drawCompass();
  drawTitle(tft);

  // ── Stars (suppressed in full daylight) ──────────────────────
  int sx, sy;
  bool showStars = (sunAlt < 6.0);   // hide stars when Sun is well up
  if (showStars) {
    for (int i = 0; i < NUM_STARS; i++) {
      double alt, az;
      raDecToAltAz(STARS[i].ra, STARS[i].dec, LST, OBS_LAT, alt, az);
      if (!altAzToXY(alt, az, sx, sy)) continue;
      drawStar(sx, sy, STARS[i].mag, STARS[i].col);
      if (STARS[i].mag < 1.0f && alt > 5.0) {
        tft.setTextColor(0x4228, TFT_BLACK);
        tft.setTextSize(1);
        tft.drawString(STARS[i].name, sx + 5, sy - 4);
      }
    }
  }

  // ── Planets ──────────────────────────────────────────────────
  for (uint8_t p = 0; p < 5; p++) {
    double ra_h, dec_d, alt, az;
    planetRADec(p, JD, ra_h, dec_d);
    raDecToAltAz(ra_h, dec_d, LST, OBS_LAT, alt, az);
    if (!altAzToXY(alt, az, sx, sy)) continue;
    tft.fillCircle(sx, sy, 3, PCOL[p]);
    tft.setTextColor(PCOL[p], TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString(PNAME[p], sx + 5, sy - 4);
  }

  // ── Moon ─────────────────────────────────────────────────────
  {
    double ra_h, dec_d, alt, az;
    moonRADec(JD, ra_h, dec_d);
    raDecToAltAz(ra_h, dec_d, LST, OBS_LAT, alt, az);
    if (altAzToXY(alt, az, sx, sy)) {
      double phase = moonPhase(JD);
      drawMoon(sx, sy, phase);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.setTextSize(1);
      tft.drawString("Moon", sx + 11, sy - 4);
    }
  }

  // ── Sun ──────────────────────────────────────────────────────
  {
    if (altAzToXY(sunAlt, sunAz, sx, sy))
      drawSun(sx, sy);
    // Even when below horizon show rise/set indicator at edge
    else if (sunAlt >= -18.0) {
      // Project onto the horizon circle edge
      double r = SKY_R;
      int ex = SKY_CX + (int)(r * sin(sunAz * D2R));
      int ey = SKY_CY - (int)(r * cos(sunAz * D2R));
      tft.fillCircle(ex, ey, 4, tft.color565(255, 160, 0));
      tft.drawCircle(ex, ey, 5, tft.color565(255, 220, 100));
    }
  }

  // ── ISS ──────────────────────────────────────────────────────
  {
    double issLat = 0, issLon = 0, issAlt = -90, issAz = 0;
    bool issOk = fetchISSPosition(issLat, issLon);
    if (issOk) {
      issToAltAz(issLat, issLon, OBS_LAT, OBS_LON, issAlt, issAz);
      g_issVisible = (issAlt >= 0.0);   // persist for LED handler
      if (altAzToXY(issAlt, issAz, sx, sy))
        drawISS(sx, sy);
    } else {
      g_issVisible = false;
    }
    drawStatusBar(ti, issOk, issAlt, issAz);
  }
}

// ─────────────────────────────────────────────────────────────
//  POWER & LED HELPERS
// ─────────────────────────────────────────────────────────────

// Enter deep sleep; wake on button OR after ISS_SLEEP_US.
// On wakeup the ESP32 reboots → setup() runs again.
void enterDeepSleep() {
  Serial.println("Entering deep sleep...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Button (active LOW) wakes as display mode
  // ESP32-C3 uses esp_deep_sleep_enable_gpio_wakeup, not ext1
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);

  // Timer wakes for ISS check (LED-only mode)
  esp_sleep_enable_timer_wakeup(ISS_SLEEP_US);

  esp_deep_sleep_start();
}

// Turn display off (backlight + sleep command) without full deep sleep
void displayOff() {
  pinMode(PIN_BL,OUTPUT);
  digitalWrite(PIN_BL, LOW);
  tft.writeCommand(0x28);  // DISPOFF
  tft.writeCommand(0x10);  // SLPIN
}

// Connect to WiFi + sync NTP.  Returns true on success.
bool connectWiFiAndNTP(bool showOnDisplay = false) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000)
    delay(200);

  if (WiFi.status() != WL_CONNECTED) return false;

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  setenv("TZ", TZ_STR, 1);
  tzset();

  struct tm ti;
  return getLocalTime(&ti, 8000);
}

// Quick ISS visibility check (no display update).
// Sets g_issVisible.  Assumes WiFi is already connected.
bool quickISSCheck() {
  double issLat = 0, issLon = 0, issAlt = -90, issAz = 0;
  if (!fetchISSPosition(issLat, issLon)) return false;
  issToAltAz(issLat, issLon, OBS_LAT, OBS_LON, issAlt, issAz);
  g_issVisible = (issAlt >= 0.0);
  if (g_issVisible) g_displayMode = true;
  return true;
}

// Non-blocking LED blink handler – call repeatedly from loop().
// Blinks the LED with period LED_PERIOD_MS when g_issVisible is true.
void handleLED() {
  if (!g_issVisible) {
    digitalWrite(LED_PIN, LOW);
    return;
  }
  uint32_t phase = millis() % LED_PERIOD_MS;
  digitalWrite(LED_PIN, phase < LED_ON_MS ? HIGH : LOW);
}

// Block and blink LED for `durationMs`, then leave LED off.
// Used in ISS-only (no display) wakeup mode.
void blinkLEDBlocking(uint32_t durationMs) {
  uint32_t end = millis() + durationMs;
  while (millis() < end) {
    handleLED();
    delay(20);
  }
  digitalWrite(LED_PIN, LOW);
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // ── GPIO init ────────────────────────────────────────────────
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(PIN_BL,OUTPUT);
  digitalWrite(PIN_BL,HIGH);

  // ── Determine wakeup cause ───────────────────────────────────
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    // Button pressed → show display, reset display-mode flag
    g_displayMode = true;
    Serial.println("Wakeup: button");
    blink(3);
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    // Timer wakeup → ISS-only check, no display
    g_displayMode = false;
    Serial.println("Wakeup: timer (ISS check)");
    blink(2);
  } else {
    // First power-on
    g_displayMode = true;
    g_issVisible  = false;
    Serial.println("Wakeup: power-on");
    blink(4);
  }

  // ── Connect WiFi + NTP (always needed) ──────────────────────
  if (g_displayMode) {
    // Show splash while connecting
    tft.init(240,320);
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(0xAD75);
    tft.drawString("COPENHAGEN",          68, 115);
    tft.drawString("NIGHT  SKY",          72, 130);
    tft.setTextColor(0x4A69);
    tft.drawString("55.68 N  /  12.57 E", 40, 150);
    tft.setTextColor(TFT_DARKGREY);
    tft.drawString("Connecting to WiFi...", 30, 185);
  }

  bool ok = connectWiFiAndNTP(g_displayMode);

  if (g_displayMode) {
    if (ok) {
      tft.setTextColor(0x07E0);
      tft.drawString("Connected & synced!", 35, 205);
    } else {
      tft.setTextColor(TFT_RED);
      tft.drawString("WiFi/NTP failed", 48, 205);
    }
    delay(600);
    drawSky();   // drawSky also sets g_issVisible via ISS block
  } else {
    // ISS-only mode: check and blink for ~10 s then sleep again
    quickISSCheck();
    Serial.printf("ISS-only mode: visible=%d\n", g_issVisible);
    if (g_issVisible) blinkLEDBlocking(10000);  // blink 10 s
    enterDeepSleep();
  }
}

// ─────────────────────────────────────────────────────────────
//  LOOP  –  runs only in display mode (g_displayMode == true)
// ─────────────────────────────────────────────────────────────
void loop() {
  static uint32_t lastRefresh  = 0;
  static uint32_t displayStart = millis();

  // ── Display timeout → deep sleep ────────────────────────────
  if ((millis() - displayStart >= DISPLAY_ON_MS) &&(g_issVisible == false) ) {
    Serial.println("Display timeout – sleeping");
    displayOff();
    g_displayMode = false;
    enterDeepSleep();
  }

  // ── Periodic sky refresh ─────────────────────────────────────
  if (millis() - lastRefresh >= REFRESH_MS) {
    drawSky();   // also updates g_issVisible
    lastRefresh = millis();
  }

  // ── LED blink (non-blocking) ─────────────────────────────────
  handleLED();

  delay(20);   // tight loop for smooth LED blinking
}
