// ───────────────────────────────────────────────────────────── 
//  MATH HELPERS
// ─────────────────────────────────────────────────────────────


static inline double norm360(double a) {
  a = fmod(a, 360.0);
  return (a < 0.0) ? a + 360.0 : a;
}

// ─────────────────────────────────────────────────────────────
//  ASTRONOMY
// ─────────────────────────────────────────────────────────────

// ──── Planets (Paul Schlyter's low-precision elements, ~1°) ───
struct PlanetEl { double N, i, w, a, e, M; } ;

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



// ──── Sun ─────────────────────────────────────────────────────
// Low-precision solar coordinates (Schlyter, ~1° accuracy)
void sunRADec(double JD, double &ra_h, double &dec_d) {
  double d   = JD - 2451545.0;
  double w   = norm360(282.9404 + 4.70935e-5 * d);   // longitude of perihelion
  double e   =         0.016709 - 1.151e-9   * d;    // eccentricity
  double M   = norm360(356.0470 + 0.9856002585 * d); // mean anomaly
  double E   = solveKepler(M, e);
  double xv  = cos(E) - e;
  double yv  = sqrt(1.0 - e*e) * sin(E);
  double v   = atan2(yv, xv) * R2D;
  double lon = norm360(v + w);                        // true longitude
  double eps = 23.4393 - 3.563e-7 * d;               // obliquity
  double lr  = lon * D2R, er = eps * D2R;
  double ra  = atan2(sin(lr)*cos(er), cos(lr));
  double dec = asin( sin(er)*sin(lr));
  ra_h  = norm360(ra  * R2D) / 15.0;
  dec_d = dec * R2D;
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
