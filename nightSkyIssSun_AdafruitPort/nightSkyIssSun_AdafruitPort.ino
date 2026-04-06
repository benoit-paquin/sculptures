/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║       COPENHAGEN NIGHT SKY  –  ESP32-C3 + TFT_eSPI          ║
 * ║                                                              ║
 * ║  Renders stars, planets and the Moon as seen from            ║
 * ║  Copenhagen (55.68°N 12.57°E) on a 240×320 TFT.             ║
 * ║  Time is fetched via NTP and the view refreshes every        ║
 * ║  5 minutes.                                                  ║
 * ║                                                              ║
 * ║  Projection: zenithal (azimuthal equidistant)                ║
 * ║    – zenith at centre, horizon at circle edge                ║
 * ║    – North at top, East to the right                         ║
 * ║                                                              ║
 * ║  Required libraries (Arduino Library Manager):               ║
 * ║    • TFT_eSPI  (Bodmer)                                      ║
 * ║                                                              ║
 * ║  !! Configure TFT_eSPI for your display by editing           ║
 * ║     User_Setup.h inside the TFT_eSPI library folder.         ║
 * ║     Set ST7789_DRIVER (or your driver), TFT_WIDTH 240,       ║
 * ║     TFT_HEIGHT 320, and your SPI pin assignments.            ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>

// ─────────────────────────────────────────────────────────────
//  !! EDIT THESE !!
// ─────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "H4Mesh";
const char* WIFI_PASSWORD = "benoitpaquin";

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

#define REFRESH_MS (5UL * 60UL * 1000UL)   // 5 minutes

Adafruit_ST7789 tft(D0, D1, D2);

// ─────────────────────────────────────────────────────────────
//  MATH HELPERS
// ─────────────────────────────────────────────────────────────
#define D2R  (M_PI / 180.0)
#define R2D  (180.0 / M_PI)

static inline double norm360(double a) {
  a = fmod(a, 360.0);
  return (a < 0.0) ? a + 360.0 : a;
}

// ─────────────────────────────────────────────────────────────
//  ASTRONOMY
// ─────────────────────────────────────────────────────────────

// Julian Day Number for a UTC date/time
double julianDate(int y, int mo, int d, int h, int mi, int s) {
  if (mo <= 2) { y--; mo += 12; }
  int A = y / 100;
  int B = 2 - A + A / 4;
  double JD = (long)(365.25 * (y + 4716))
            + (int)(30.6001 * (mo + 1))
            + d + B - 1524.5;
  return JD + (h + mi / 60.0 + s / 3600.0) / 24.0;
}

// Local Sidereal Time (degrees) from Julian Date and longitude
double localSiderealTime(double JD, double lon_deg) {
  double T = (JD - 2451545.0) / 36525.0;
  double GMST = 280.46061837
              + 360.98564736629 * (JD - 2451545.0)
              + 0.000387933 * T * T
              - T * T * T / 38710000.0;
  return norm360(GMST + lon_deg);
}

// RA (hours) + Dec (°)  →  Altitude / Azimuth (°)
void raDecToAltAz(double ra_h, double dec_d, double lst_d, double lat_d,
                  double &alt, double &az) {
  double ha  = (lst_d - ra_h * 15.0) * D2R;
  double dec = dec_d * D2R;
  double lat = lat_d * D2R;

  double sinAlt = sin(dec) * sin(lat) + cos(dec) * cos(lat) * cos(ha);
  sinAlt = constrain(sinAlt, -1.0, 1.0);
  alt = asin(sinAlt) * R2D;

  double cosA = (sin(dec) - sin(lat) * sinAlt)
              / (cos(lat) * cos(alt * D2R) + 1e-10);
  cosA = constrain(cosA, -1.0, 1.0);
  az   = acos(cosA) * R2D;
  if (sin(ha) > 0.0) az = 360.0 - az;
}

// Zenithal projection: Alt/Az  →  pixel (x, y)
// Returns false when below the horizon (with a 1° tolerance for labelling)
bool altAzToXY(double alt, double az, int &x, int &y) {
  if (alt < -1.0) return false;
  double r = SKY_R * (90.0 - alt) / 90.0;
  if (r > SKY_R + 2) return false;
  x = SKY_CX + (int)round(r * sin(az * D2R));
  y = SKY_CY - (int)round(r * cos(az * D2R));
  return true;
}

// ──── Moon ────────────────────────────────────────────────────
// Simplified lunar coordinates (accuracy ~1°)
void moonRADec(double JD, double &ra_h, double &dec_d) {
  double d   = JD - 2451545.0;
  double L   = norm360(218.316 + 13.176396 * d);
  double M   = norm360(134.963 + 13.064993 * d);
  double F   = norm360( 93.272 + 13.229350 * d);
  double lon = L + 6.289 * sin(M * D2R);
  double lat =     5.128 * sin(F * D2R);
  double eps = 23.4393 - 3.563e-7 * d;
  double lr  = lon * D2R, latr = lat * D2R, er = eps * D2R;
  double ra  = atan2(sin(lr)*cos(er) - tan(latr)*sin(er), cos(lr));
  double dec = asin( sin(latr)*cos(er) + cos(latr)*sin(er)*sin(lr));
  ra_h  = norm360(ra  * R2D) / 15.0;
  dec_d = dec * R2D;
}

// Moon phase: 0 = new moon, 0.5 = full moon
double moonPhase(double JD) {
  double d = JD - 2451545.0;
  double elong = norm360(
    (218.316 + 13.176396 * d) -   // Moon longitude
    (280.459 +  0.98564736 * d)); // Sun  longitude
  return elong / 360.0;
}

// ──── Planets (Paul Schlyter's low-precision elements, ~1°) ───
//typedef struct { double N, i, w, a, e, M; } PlanetEl;
struct PlanetEl  { double N, i, w, a, e, M; } ;
struct PlanetEl getPlanetElements(uint8_t p, double d) {
  PlanetEl el = {};
  switch (p) {
    case 0: // Mercury
      el.N=norm360( 48.3313+3.24587e-5*d); el.i=7.0047+5.00e-8*d;
      el.w=norm360( 29.1241+1.01444e-5*d); el.a=0.387098;
      el.e=0.205635+5.59e-10*d;            el.M=norm360(168.6562+4.0923344368*d); break;
    case 1: // Venus
      el.N=norm360( 76.6799+2.46590e-5*d); el.i=3.3946+2.75e-8*d;
      el.w=norm360( 54.8910+1.38374e-5*d); el.a=0.723330;
      el.e=0.006773-1.302e-9*d;            el.M=norm360( 48.0052+1.6021302244*d); break;
    case 2: // Mars
      el.N=norm360( 49.5574+2.11081e-5*d); el.i=1.8497-1.78e-8*d;
      el.w=norm360(286.5016+2.92961e-5*d); el.a=1.523688;
      el.e=0.093405+2.516e-9*d;            el.M=norm360( 18.6021+0.5240207766*d); break;
    case 3: // Jupiter
      el.N=norm360(100.4542+2.76854e-5*d); el.i=1.3030-1.557e-7*d;
      el.w=norm360(273.8777+1.64505e-5*d); el.a=5.20256;
      el.e=0.048498+4.469e-9*d;            el.M=norm360( 19.8950+0.0830853001*d); break;
    case 4: // Saturn
      el.N=norm360(113.6634+2.38980e-5*d); el.i=2.4886-1.081e-7*d;
      el.w=norm360(339.3939+2.97661e-5*d); el.a=9.55475;
      el.e=0.055546-9.499e-9*d;            el.M=norm360(316.9670+0.0334442282*d); break;
  }
  return el;
}

// Solve Kepler's equation E - e·sin(E) = M  (Newton-Raphson)
static double solveKepler(double M_deg, double e) {
  double M = M_deg * D2R, E = M;
  for (int i = 0; i < 12; i++)
    E -= (E - e * sin(E) - M) / (1.0 - e * cos(E));
  return E;
}

// Heliocentric ecliptic Cartesian (AU)
void helioXYZ(const PlanetEl &el, double &xh, double &yh, double &zh) {
  double E   = solveKepler(el.M, el.e);
  double xv  = el.a * (cos(E) - el.e);
  double yv  = el.a * sqrt(1.0 - el.e * el.e) * sin(E);
  double v   = atan2(yv, xv);
  double r   = sqrt(xv*xv + yv*yv);
  double lon = v + el.w * D2R;
  double N   = el.N * D2R, inc = el.i * D2R;
  xh = r*(cos(N)*cos(lon) - sin(N)*sin(lon)*cos(inc));
  yh = r*(sin(N)*cos(lon) + cos(N)*sin(lon)*cos(inc));
  zh = r* sin(lon)*sin(inc);
}

// Planet geocentric RA/Dec
void planetRADec(uint8_t p, double JD, double &ra_h, double &dec_d) {
  double d = JD - 2451545.0;
  PlanetEl el = getPlanetElements(p, d);
  double xh, yh, zh;
  helioXYZ(el, xh, yh, zh);

  // Sun's geocentric ecliptic position (Earth's orbit)
  double w_sun = norm360(282.9404 + 4.70935e-5 * d);
  double e_sun = 0.016709 - 1.151e-9 * d;
  double M_sun = norm360(356.0470 + 0.9856002585 * d);
  double Es    = solveKepler(M_sun, e_sun);
  double xvs   = cos(Es) - e_sun;
  double yvs   = sqrt(1.0 - e_sun * e_sun) * sin(Es);
  double vs    = atan2(yvs, xvs);
  double rs    = sqrt(xvs*xvs + yvs*yvs);
  double lons  = vs + w_sun * D2R;
  double xs    =  rs * cos(lons);
  double ys    =  rs * sin(lons);

  // Geocentric ecliptic
  double xg = xh + xs, yg = yh + ys, zg = zh;

  // Ecliptic → equatorial  (obliquity of ecliptic)
  double eps = (23.4393 - 3.563e-7 * d) * D2R;
  double xe  = xg;
  double ye  = yg * cos(eps) - zg * sin(eps);
  double ze  = yg * sin(eps) + zg * cos(eps);

  ra_h  = norm360(atan2(ye, xe) * R2D) / 15.0;
  dec_d = atan2(ze, sqrt(xe*xe + ye*ye)) * R2D;
}

// ─────────────────────────────────────────────────────────────
//  STAR CATALOGUE  (RA hours · Dec degrees · visual magnitude)
//  Biased towards northern-sky / circumpolar stars visible from
//  Copenhagen, plus the brightest southern stars when up.
// ─────────────────────────────────────────────────────────────
struct Star { const char *name; float ra, dec, mag; uint16_t col; };

// 16-bit RGB565 colours by spectral class
#define COL_O  0xAEFF   // O  –  blue
#define COL_B  0xBDFF   // B  –  blue-white
#define COL_A  0xFFFF
#define COL_F  0xF7BE   // F  –  yellow-white
#define COL_G  0xFFE0
#define COL_K  0xFD20
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
      uint16_t c = lit ? 0xFFFF : 0x3186;  // white or dark grey
      tft.drawPixel(cx + dx, cy + dy, c);
    }
  }
  #define TFT_LIGHTGREY 0x8410
  #define TFT_BLACK     0x0000
  #define TFT_RED       0xF800
  #define TFT_WHITE     0xFFFF
  
  tft.drawCircle(cx, cy, r, TFT_LIGHTGREY);  // limb
}



// Compass labels + tick marks around the horizon circle
static void drawCompass() {
  tft.setTextSize(1);
  tft.setTextColor(0x8C71, TFT_BLACK);  // medium grey

  // Cardinal points
  //tft.drawString("N",  SKY_CX - 3,          SKY_CY - SKY_R - 11);
  tft.setCursor(SKY_CX - 3,SKY_CY - SKY_R - 11);
  tft.print("N");
  //tft.drawString("S",  SKY_CX - 3,          SKY_CY + SKY_R + 3 );
  tft.setCursor( SKY_CX - 3,          SKY_CY + SKY_R + 3 );
  tft.print("S");
  //tft.drawString("E",  SKY_CX + SKY_R + 3,  SKY_CY - 4         );
  tft.setCursor( SKY_CX + SKY_R + 3,  SKY_CY - 4  );
  tft.print("E");
  //tft.drawString("W",  SKY_CX - SKY_R - 10, SKY_CY - 4         );
  tft.setCursor( SKY_CX - SKY_R - 10, SKY_CY - 4  );
  tft.print("W");
  

  // Tick marks every 15° (major at 90°, minor at 45°, tiny elsewhere)
  for (int az = 0; az < 360; az += 15) {
    double a  = az * D2R;
    int len   = (az % 90 == 0) ? 7 : (az % 45 == 0) ? 5 : 3;
    uint16_t c = (az % 90 == 0) ? 0x6B4D : 0x39E7;
    int x1 = SKY_CX + (int)((SKY_R - len) * sin(a));
    int y1 = SKY_CY - (int)((SKY_R - len) * cos(a));
    int x2 = SKY_CX + (int)( SKY_R        * sin(a));
    int y2 = SKY_CY - (int)( SKY_R        * cos(a));
    tft.drawLine(x1, y1, x2, y2, c);
  }
}

// Altitude grid circles (60° and 30° altitude lines) + crosshairs
static void drawGrid() {
  const uint16_t gc = 0x2104;   // very dark blue-grey
  // 30° and 60° altitude rings
  tft.drawCircle(SKY_CX, SKY_CY, (int)(SKY_R * 30.0 / 90.0), gc);  // alt 60°
  tft.drawCircle(SKY_CX, SKY_CY, (int)(SKY_R * 60.0 / 90.0), gc);  // alt 30°
  // N-S and E-W crosshairs
  tft.drawLine(SKY_CX - SKY_R, SKY_CY, SKY_CX + SKY_R, SKY_CY, gc);
  tft.drawLine(SKY_CX, SKY_CY - SKY_R, SKY_CX, SKY_CY + SKY_R, gc);
}

// ─────────────────────────────────────────────────────────────
//  STATUS BAR (bottom strip)
// ─────────────────────────────────────────────────────────────
static void drawStatusBar(const struct tm &ti) {
  tft.fillRect(0, SCREEN_H - 22, SCREEN_W, 22, 0x000F);
  tft.setTextSize(1);
  char buf[40];
  // Time + date
  snprintf(buf, sizeof(buf), "%02d:%02d  %02d/%02d/%04d",
           ti.tm_hour, ti.tm_min, ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
  tft.setTextColor(0xC618, TFT_BLACK);  // light grey
  tft.setCursor( 4, SCREEN_H - 15);
  tft.print(buf);
  // Location tag
  tft.setTextColor(0x4A49, TFT_BLACK);  // dim
  tft.setCursor(140, SCREEN_H - 15);
  tft.print("CPH 55.7N 12.6E");
}

// ─────────────────────────────────────────────────────────────
//  TITLE / HEADER BAR
// ─────────────────────────────────────────────────────────────
static void drawTitle() {
  tft.fillRect(0, 0, SCREEN_W, 16, 0x000F);
  tft.setTextSize(1);
  tft.setTextColor(0x4A49, TFT_BLACK);
  tft.setCursor(26,4);
  //tft.drawString("COPENHAGEN NIGHT SKY", 26, 4);
  tft.print("COPENHAGEN");
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
    tft.setCursor(10,150);
    tft.print("NTP sync failed – check WiFi");
    return;
  }

  double JD  = julianDate(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                           ti.tm_hour, ti.tm_min, ti.tm_sec);
  double LST = localSiderealTime(JD, OBS_LON);

  // ── Background ──────────────────────────────────────────────
  tft.fillScreen(TFT_BLACK);
  tft.fillCircle(SKY_CX, SKY_CY, SKY_R, 0x0002);   // near-black sky dome

  drawGrid();
  tft.drawCircle(SKY_CX, SKY_CY, SKY_R, 0x4208);   // horizon ring (dim olive)
  drawCompass();
  drawTitle();

  // ── Stars ────────────────────────────────────────────────────
  int sx, sy;
  for (int i = 0; i < NUM_STARS; i++) {
    double alt, az;
    raDecToAltAz(STARS[i].ra, STARS[i].dec, LST, OBS_LAT, alt, az);
    if (!altAzToXY(alt, az, sx, sy)) continue;
    drawStar(sx, sy, STARS[i].mag, STARS[i].col);
    // Label the very brightest stars
    if (STARS[i].mag < 1.0f && alt > 5.0) {
      tft.setTextColor(0x4228, TFT_BLACK);  // very dim label
      tft.setTextSize(1);
      tft.setCursor(sx + 5, sy - 4);
      tft.print(STARS[i].name);
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
    tft.setCursor(sx + 5, sy - 4);
    tft.print(PNAME[p]);
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
      tft.setCursor( sx + 11, sy - 4);
      tft.print("Moon");
    }
  }

  drawStatusBar(ti);
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  tft.init(240,320);
  tft.setRotation(2);       // portrait, connector at bottom
  //tft.writecommand(TFT_MADCTL);
  //tft.writedata(0x40);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);

  // ── Splash screen ────────────────────────────────────────────
  tft.setTextColor(0xAD75);
  tft.setCursor(68,115);
  tft.print("COPENHAGEN");
  tft.setCursor(72,130);
  tft.print("NIGHT  SKY");
  tft.setTextColor(0x4A69);
  tft.setCursor(40,150);
  tft.print("55.68 N  /  12.57 E");
  #define TFT_DARKGREY 0x4208
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(30,185);
  tft.print("Connecting to WiFi...");

  // ── WiFi ─────────────────────────────────────────────────────
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000)
    delay(250);

  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(0x07E0);
    tft.setCursor(55,205);
    tft.print("WiFi connected");

    // ── NTP sync ─────────────────────────────────────────────
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    setenv("TZ", TZ_STR, 1);
    tzset();

    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(52,225);
    tft.print("Syncing time...");
    struct tm ti;
    if (getLocalTime(&ti, 10000)) {
      tft.setTextColor(0x07E0);
      tft.setCursor(62,245);
      tft.print("Time synced!");
    } else {
      tft.setTextColor(TFT_RED);
      tft.setCursor(68,245);
      tft.print("NTP failed");
    }
  } else {
    tft.setTextColor(TFT_RED);
    tft.setCursor(10,205);
    tft.print("WiFi failed – check credentials");
    delay(3000);
  }

  delay(800);
  drawSky();
}

// ─────────────────────────────────────────────────────────────
//  LOOP  –  refresh every REFRESH_MS
// ─────────────────────────────────────────────────────────────
void loop() {
  delay(REFRESH_MS);
  drawSky();
}
