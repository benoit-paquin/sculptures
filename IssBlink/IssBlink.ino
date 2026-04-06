/**
 * ISS Blinker for ESP32-C3  —  Arduino IDE version
 * ═══════════════════════════════════════════════════════════════════════════
 * Blinks an LED whenever the International Space Station is *visually*
 * visible from Copenhagen (55.6761°N, 12.5683°E) – i.e. the ISS is above
 * the horizon AND lit by sunlight while the observer is in darkness.
 *
 * API  →  N2YO  (https://www.n2yo.com/api/)
 *   Free account required. After registering, generate your API key on the
 *   profile page and paste it into N2YO_API_KEY below.
 *   Endpoint used:
 *     GET https://api.n2yo.com/rest/v1/satellite/visualpasses/
 *             25544/{lat}/{lon}/{alt}/{days}/{minVisibility}&apiKey={key}
 *
 * Energy strategy
 * ───────────────
 *  • Deep sleep between events – draws ~5 µA instead of ~80 mA.
 *  • WiFi is only turned on to:
 *      1. Sync time via NTP (once per boot when clock is stale).
 *      2. Fetch the next visual pass from N2YO.
 *  • Pass data is stored in RTC RAM (survives deep sleep).
 *  • If the next pass is >12 h away a mid-sleep re-sync is inserted to
 *    keep the clock accurate and refresh orbital predictions.
 *
 * Hardware
 * ────────
 *  • ESP32-C3 dev board
 *  • LED on GPIO 8  (built-in on Seeed XIAO ESP32-C3, active-LOW).
 *    Change LED_PIN / LED_ACTIVE_LOW below if needed.
 *
 * Arduino IDE setup
 * ─────────────────
 *  1. Add board URL in Preferences:
 *       https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
 *  2. Boards Manager -> install "esp32 by Espressif Systems"
 *  3. Library Manager -> install "ArduinoJson" >= 7
 *  4. Tools -> Board -> ESP32C3 Dev Module  (or XIAO_ESP32C3)
 *  5. Tools -> USB CDC On Boot -> Enabled
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// ── User configuration ────────────────────────────────────────────────────────

const char* WIFI_SSID     = "H4Mesh";
const char* WIFI_PASSWORD = "benoitpaquin";

// Get a free key at https://www.n2yo.com/login/register/
// then find it on your profile page.
const char* N2YO_API_KEY  = "6Z6A3W-4FUN68-ZDPDHG-5PBW";

// Copenhagen city centre
//constexpr float    CPH_LAT  = 55.6761f;
//constexpr float    CPH_LON  = 12.5683f;
constexpr float    CPH_LAT  = 36.0f;
constexpr float    CPH_LON  = -119.0f;
constexpr uint16_t CPH_ALT  = 10;     // metres above sea level

// How many days ahead to request  (1-10, N2YO limit)
constexpr uint8_t  PASS_DAYS = 3;

// Minimum pass duration to consider (seconds). Filters out grazing passes.
constexpr uint16_t MIN_PASS_DURATION_S = 60;

// GPIO - active-LOW (LED cathode -> GPIO, anode -> 3V3 via resistor)
constexpr gpio_num_t LED_PIN        = GPIO_NUM_20;
constexpr bool       LED_ACTIVE_LOW = true;

// Blink pattern while ISS is overhead
constexpr uint32_t BLINK_ON_MS  = 300;
constexpr uint32_t BLINK_OFF_MS = 200;

// Wake this many seconds before predicted pass start
constexpr uint32_t PRE_PASS_WAKE_S = 90;

// Insert a mid-sleep NTP + re-fetch if the pass is further than this
constexpr uint32_t RESYNC_INTERVAL_S = 12UL * 3600UL;  // 12 h

// WiFi connection timeout
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;

// NTP server (UTC)
const char* NTP_SERVER = "pool.ntp.org";

// Minimum plausible Unix timestamp (2024-01-01)
constexpr time_t MIN_VALID_TIME = 1704067200LL;

// ── RTC RAM — survives deep sleep ─────────────────────────────────────────────

RTC_DATA_ATTR time_t  g_passStart = 0;
RTC_DATA_ATTR time_t  g_passEnd   = 0;
RTC_DATA_ATTR bool    g_passValid = false;
RTC_DATA_ATTR int32_t g_bootCount = 0;

// ── LED helpers ───────────────────────────────────────────────────────────────

void ledOn()  { digitalWrite(LED_PIN, LED_ACTIVE_LOW ? HIGH  : HIGH); }//was LOW  : High
void ledOff() { digitalWrite(LED_PIN, LED_ACTIVE_LOW ? LOW : LOW);  } // Was HIGH : LOW

// ── WiFi helpers ──────────────────────────────────────────────────────────────

bool wifiConnect() {
  Serial.printf("[WiFi] Connecting to \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > WIFI_TIMEOUT_MS) {
      Serial.println(" TIMEOUT");
      return false;
    }
    delay(250);
    Serial.print('.');
  }
  Serial.printf(" OK  IP=%s\n", WiFi.localIP().toString().c_str());
  return true;
}

void wifiDisconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[WiFi] OFF");
}

// ── NTP time sync ─────────────────────────────────────────────────────────────

time_t syncNTP() {
  configTime(0, 0, NTP_SERVER);
  struct tm ti{};
  if (!getLocalTime(&ti, 10000)) {
    Serial.println("[NTP] Sync failed");
    return 0;
  }
  time_t now = mktime(&ti);
  Serial.printf("[NTP] Synced -> %s", asctime(&ti));
  return now;
}

// ── N2YO visual pass fetch ────────────────────────────────────────────────────
//
// N2YO visual pass JSON shape:
// {
//   "info":  { "transactionscount": 1, "passescount": N },
//   "passes": [
//     {
//       "startUTC":  <unix>,    <- ISS rises above 10 deg
//       "maxUTC":    <unix>,    <- highest elevation moment
//       "endUTC":    <unix>,    <- ISS sets below 10 deg
//       "duration":  <seconds>,
//       "mag":       <magnitude, lower = brighter>
//     }, ...
//   ]
// }

bool fetchNextPass(time_t now) {
  char url[512];
  snprintf(url, sizeof(url),
    "https://api.n2yo.com/rest/v1/satellite/visualpasses/"
    "25544/%.4f/%.4f/%u/%u/%u/&apiKey=%s",
    CPH_LAT, CPH_LON, CPH_ALT,
    PASS_DAYS, MIN_PASS_DURATION_S,
    N2YO_API_KEY);
  Serial.println(url);
  Serial.println("[N2YO] Fetching visual passes...");

  // N2YO uses HTTPS; skip certificate verification for embedded use
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(15000);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("[N2YO] HTTP error %d\n", code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[N2YO] JSON error: %s\n", err.c_str());
    return false;
  }

  if (!doc["passes"]) {
    Serial.println("[N2YO] No 'passes' key in response:");
    Serial.println(body.substring(0, 300));
    return false;
  }

  JsonArray passes = doc["passes"].as<JsonArray>();
  int passesCount = doc["info"]["passescount"] | 0;
  Serial.printf("[N2YO] %d visual pass(es) found\n", passesCount);

  for (JsonObject pass : passes) {
    time_t   start    = (time_t)pass["startUTC"].as<long>();
    time_t   end      = (time_t)pass["endUTC"].as<long>();
    uint32_t duration = pass["duration"].as<uint32_t>();
    float    mag      = pass["mag"] | 99.0f;

    if (end > now) {
      g_passStart = start;
      g_passEnd   = end;
      g_passValid = true;

      char bufStart[32], bufEnd[32];
      struct tm t{};
      gmtime_r(&start, &t); strftime(bufStart, sizeof(bufStart), "%H:%M:%S UTC", &t);
      gmtime_r(&end,   &t); strftime(bufEnd,   sizeof(bufEnd),   "%H:%M:%S UTC", &t);
      Serial.printf("[N2YO] Next pass: start=%s  end=%s  dur=%us  mag=%.1f\n",
                    bufStart, bufEnd, duration, mag);
      return true;
    }
  }

  Serial.println("[N2YO] No future visual passes in response");
  return false;
}

// ── Deep-sleep helper ─────────────────────────────────────────────────────────

void sleepUntil(time_t wakeAt) {
  time_t now;
  time(&now);

  int64_t sleepUs = ((int64_t)wakeAt - (int64_t)now) * 1000000LL;
  if (sleepUs < 100000LL) sleepUs = 100000LL;

  Serial.printf("[Sleep] %.1f s until wake\n", sleepUs / 1e6);
  Serial.flush();

  ledOff();
  esp_sleep_enable_timer_wakeup((uint64_t)sleepUs);
  esp_deep_sleep_start();
  // never returns
}

// ── Blink loop ────────────────────────────────────────────────────────────────

void blinkUntilPassEnd(time_t passEnd) {
  Serial.println("[LED] ISS visible overhead — blinking!");
  time_t now;
  do {
    ledOn();  delay(BLINK_ON_MS);
    ledOff(); delay(BLINK_OFF_MS);
    time(&now);
  } while (now < passEnd);
  Serial.println("[LED] Pass ended");
}

// ═════════════════════════════════════════════════════════════════════════════
// setup() — runs once per deep-sleep cycle
// ═════════════════════════════════════════════════════════════════════════════

void setup() {
  ++g_bootCount;

  Serial.begin(115200);
  delay(200);
  Serial.printf("\n== ISS Blinker  boot #%d ==\n", g_bootCount);
  pinMode(GPIO_NUM_21,OUTPUT);
  digitalWrite(GPIO_NUM_21,LOW);
  pinMode(LED_PIN, OUTPUT);
  ledOn();
  delay(500);
  ledOff();

  // ── 1. Check if we need WiFi ───────────────────────────────────────────────
  time_t now;
  time(&now);

  bool clockOk  = (now > MIN_VALID_TIME);
  bool needWiFi = !clockOk || !g_passValid || (g_passEnd <= now);

  if (needWiFi) {
    if (!wifiConnect()) {
      Serial.println("[Main] WiFi failed — retry in 5 min");
      time(&now);
      sleepUntil(now + 300);
    }

    now = syncNTP();
    if (now == 0) {
      Serial.println("[Main] NTP failed — retry in 5 min");
      wifiDisconnect();
      time(&now);
      sleepUntil(now + 300);
    }

    fetchNextPass(now);
    wifiDisconnect();
  }

  time(&now);

  // ── 2. Act on pass data ────────────────────────────────────────────────────
  if (!g_passValid) {
    Serial.println("[Main] No pass data — retry in 15 min");
    sleepUntil(now + 60); // Was 900
  }

  // ISS is visible RIGHT NOW
  if (now >= g_passStart && now < g_passEnd) {
    blinkUntilPassEnd(g_passEnd);
    g_passValid = false;
    sleepUntil(now + 60);
  }

  // Pass already ended (stale data)
  if (now >= g_passEnd) {
    Serial.println("[Main] Pass expired — re-fetching");
    g_passValid = false;
    sleepUntil(now + 60);
  }

  // ── 3. Sleep until just before the next pass ───────────────────────────────
  time_t wakeTarget = g_passStart - (time_t)PRE_PASS_WAKE_S;

  if ((wakeTarget - now) > (time_t)RESYNC_INTERVAL_S) {
    wakeTarget  = now + (time_t)RESYNC_INTERVAL_S;
    g_passValid = false;
    Serial.printf("[Main] Pass is far away — mid-sleep resync in %u h\n",
                  RESYNC_INTERVAL_S / 3600);
  }

  if (wakeTarget <= now) wakeTarget = now + 10;

  sleepUntil(wakeTarget);
}

void loop() {
  // Never reached — device always deep-sleeps from setup()
}
