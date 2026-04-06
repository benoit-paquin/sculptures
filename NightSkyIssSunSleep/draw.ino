// ─────────────────────────────────────────────────────────────
//  TITLE / HEADER BAR
// ─────────────────────────────────────────────────────────────
static void drawTitle(MyST7789 tft) {
  tft.fillRect(0, 0, SCREEN_W, 16, 0x000F);
  tft.setTextSize(1);
  tft.setTextColor(0x4A49, TFT_BLACK);
  tft.drawString("COPENHAGEN NIGHT SKY", 26, 4);
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

// Compass labels + tick marks around the horizon circle
static void drawCompass() {
  tft.setTextSize(1);
  tft.setTextColor(0x8C71, TFT_BLACK);  // medium grey

  // Cardinal points
  tft.drawString("N",  SKY_CX - 3,          SKY_CY - SKY_R - 11);
  tft.drawString("S",  SKY_CX - 3,          SKY_CY + SKY_R + 3 );
  tft.drawString("E",  SKY_CX + SKY_R + 2,  SKY_CY - 4         );
  tft.drawString("W",  SKY_CX - SKY_R - 8, SKY_CY - 4         );

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
// Draw the ISS marker: a small cyan cross + "ISS" label
void drawISS(int cx, int cy) {
  const uint16_t C = 0x07FF;   // cyan
  // Cross
  tft.drawLine(cx - 5, cy,     cx + 5, cy,     C);
  tft.drawLine(cx,     cy - 5, cx,     cy + 5, C);
  // Small diamond outline for visibility
  tft.drawLine(cx - 3, cy,     cx,     cy - 3, C);
  tft.drawLine(cx,     cy - 3, cx + 3, cy,     C);
  tft.drawLine(cx + 3, cy,     cx,     cy + 3, C);
  tft.drawLine(cx,     cy + 3, cx - 3, cy,     C);
  tft.setTextColor(C, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("ISS", cx + 7, cy - 4);
}

// Draw the Sun: filled yellow disc with a white corona halo
static void drawSun(int cx, int cy) {
  // Halo (soft glow)
  tft.fillCircle(cx, cy, 11, tft.color565(255, 220, 100));
  // Core
  tft.fillCircle(cx, cy,  8, tft.color565(255, 255, 150));
  // Bright centre
  tft.fillCircle(cx, cy,  4, TFT_WHITE);
  // Label
  tft.setTextColor(tft.color565(255, 230, 80), TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("Sun", cx + 13, cy - 4);
}

// ─────────────────────────────────────────────────────────────
//  STATUS BAR (bottom strip) – two lines
// ─────────────────────────────────────────────────────────────
static void drawStatusBar1  (const struct tm &ti,
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

