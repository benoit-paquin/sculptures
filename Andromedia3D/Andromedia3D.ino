/*
 * Andromeda Galaxy – 3D Animated for ESP32-C3
 * Display: 240x320 TFT (TFT_eSPI library)
 *
 * 3D features:
 *  - Stars stored as true 3D (x,y,z) coordinates in galaxy space
 *  - Galaxy disk tilted ~60° and slowly tumbling on two axes
 *  - Perspective projection: near stars larger/brighter, far ones dimmer
 *  - Full double-buffered sprite rendering — no flicker
 *
 * Configure TFT_eSPI's User_Setup.h for your driver + pins before flashing.
 */

#include <TFT_eSPI.h>
#include <math.h>

TFT_eSPI    tft    = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// ── Screen ───────────────────────────────────────────────────────────────────
#define SCREEN_H   240
#define SCREEN_W   320
#define CX         (SCREEN_W / 2)
#define CY         (SCREEN_H / 2)

// ── Perspective ───────────────────────────────────────────────────────────────
// Camera sits at z = +CAM_DIST, looking toward origin
#define CAM_DIST   320.0f
#define FOV_SCALE  260.0f   // focal length in pixels

// ── Galaxy geometry ───────────────────────────────────────────────────────────
#define NUM_ARMS         2
#define STARS_PER_ARM    400
#define NUM_CORE_STARS   160
#define NUM_HALO_STARS    70
#define ARM_WIND         4.8f
#define ARM_SPREAD       0.36f
#define MAX_RADIUS       130.0f
#define DISK_THICKNESS    6.0f   // half-thickness of the disk in z (galaxy units)

// ── Animation ─────────────────────────────────────────────────────────────────
// Spin around galaxy Y axis (in-plane spin)
#define SPIN_SPEED      0.018f
// Slow tumble: tilt angle oscillates so we see the disk from different angles
#define TILT_SPEED      0.0187f
#define TILT_AMPLITUDE  0.72f   // radians (~41°), centres around base tilt
#define BASE_TILT       0.55f   // radians (~31°) — never fully face-on

float spinAngle  = 0.0f;
float tiltAngle  = BASE_TILT;
float tiltPhase  = 0.0f;
uint32_t frameCount = 0;

// ── Compile-time RGB565 ───────────────────────────────────────────────────────
#define C565(r,g,b) ((uint16_t)(((r)>>3)<<11)|(uint16_t)(((g)>>2)<<5)|(uint16_t)((b)>>3))

// ── Palette ───────────────────────────────────────────────────────────────────
#define PALETTE_SIZE 16
static const uint16_t PALETTE[PALETTE_SIZE] = {
  C565(180,210,255), C565(210,230,255), C565(255,255,255),   // 0-2 blue-white
//  C565(200,220,255), C565(160,190,255), C565(240,245,255),   // 3-5 blue-white
  C565(240,250,25), C565(20,240,255), C565(240,5,255),   // 3-5 blue-white
  C565(255,240,180), C565(255,220,140), C565(255,200,120), C565(255,230,170), // 6-9 warm
// C565(255,24,18), C565(25,20,240), C565(255,200,120), C565(255,230,170), // 6-9 warm
  C565(140, 80,200), C565(200, 80,160), C565( 60,100,220), C565( 60,180,200), // 10-13 nebula
  C565(255,255,255), C565(255,250,220),                      // 14-15 core
};

// ── Glow layers for nucleus ───────────────────────────────────────────────────
struct GlowLayer { int16_t r; uint8_t alpha; uint16_t col; };
static const GlowLayer GLOW[] = {
  {36,15,C565(60,20,100)}, {26,28,C565(100,50,150)},
  {18,48,C565(160,90,200)},{12,75,C565(210,150,240)},
  { 8,115,C565(255,210,255)},{ 4,190,C565(255,248,255)},
  { 2,255,C565(255,255,255)},
};
#define NUM_GLOW 7

// ── Star (3D) ─────────────────────────────────────────────────────────────────
struct Star {
  float   x, y, z;     // galaxy-space coordinates (galaxy disk is XZ plane)
  float   brightness;
  uint8_t colorIdx;
  uint8_t twinklePhase;
  uint8_t size;         // 0=dot, 1=cross
};

Star armStars [NUM_ARMS][STARS_PER_ARM];
Star coreStars[NUM_CORE_STARS];
Star haloStars[NUM_HALO_STARS];

// ── RNG ───────────────────────────────────────────────────────────────────────
uint32_t rngState = 0xDEADBEEF;
uint32_t fastRand() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}
float frand()       { return (float)(fastRand() & 0xFFFF) / 65535.0f; }
float frandSigned() { return frand() * 2.0f - 1.0f; }

// ── Colour helpers ────────────────────────────────────────────────────────────
uint16_t dimColor(uint16_t c, uint8_t f) {
  if (f < 240) f=f+15;
  uint8_t r = (uint8_t)(((c>>11)&0x1F)*f/255);
  uint8_t g = (uint8_t)(((c>> 5)&0x3F)*f/255);
  uint8_t b = (uint8_t)(((c    )&0x1F)*f/255);
  return (uint16_t)((r<<11)|(g<<5)|b);
}
uint16_t blendColor(uint16_t a, uint16_t b, uint8_t t) {
  uint8_t it=255-t;
  uint8_t r =(uint8_t)((((a>>11)&0x1F)*it+((b>>11)&0x1F)*t)/255);
  uint8_t g =(uint8_t)((((a>> 5)&0x3F)*it+((b>> 5)&0x3F)*t)/255);
  uint8_t bl=(uint8_t)((((a    )&0x1F)*it+((b    )&0x1F)*t)/255);
  return (uint16_t)((r<<11)|(g<<5)|bl);
}

// ── Build 3D galaxy ───────────────────────────────────────────────────────────
// Galaxy disk lies in the XZ plane; Y is up out of the disk.
void initGalaxy() {
  for (int arm = 0; arm < NUM_ARMS; arm++) {
    float armOffset = arm * (float)M_PI;
    for (int i = 0; i < STARS_PER_ARM; i++) {
      float t      = (float)i / STARS_PER_ARM;
      float radius = 10.0f + t * MAX_RADIUS;
      float theta  = armOffset + t * ARM_WIND;
      float spread = ARM_SPREAD * (0.3f + t * 0.7f);
      theta  += frandSigned() * spread;
      radius += frandSigned() * radius * 0.14f;

      float x = radius * cosf(theta);
      float z = radius * sinf(theta);
      float y = frandSigned() * DISK_THICKNESS * (0.2f + t * 0.8f);

      armStars[arm][i].x            = x;
      armStars[arm][i].y            = y;
      armStars[arm][i].z            = z;
      armStars[arm][i].brightness   = 0.4f + frand() * 0.6f;
      armStars[arm][i].twinklePhase = (uint8_t)(fastRand() & 0xFF);

      float dustChance = 0.14f + t * 0.10f;
      if (frand() < dustChance) {
        armStars[arm][i].colorIdx = 10 + (uint8_t)(fastRand() % 4);
        armStars[arm][i].size     = 0;
      } else {
        bool warm = frand() < (0.2f + t * 0.2f);
        armStars[arm][i].colorIdx = warm
          ? (uint8_t)(6 + fastRand() % 4)
          : (uint8_t)(fastRand() % 6);
        armStars[arm][i].size = (frand() < 0.04f) ? 1 : 0;
      }
    }
  }

  for (int i = 0; i < NUM_CORE_STARS; i++) {
    float phi   = frand() * 2.0f * (float)M_PI;
    float rr    = cbrtf(frand()) * 26.0f;
    float theta = frand() * 2.0f * (float)M_PI;
    // Flatten core into an oblate spheroid
    coreStars[i].x = rr * cosf(phi) * cosf(theta);
    coreStars[i].z = rr * sinf(phi) * cosf(theta);
    coreStars[i].y = rr * sinf(theta) * 0.45f;
    coreStars[i].brightness   = 0.6f + frand() * 0.4f;
    coreStars[i].twinklePhase = (uint8_t)(fastRand() & 0xFF);
    coreStars[i].size         = (frand() < 0.07f) ? 1 : 0;
    if      (frand() < 0.5f) coreStars[i].colorIdx = 14;
    else if (frand() < 0.5f) coreStars[i].colorIdx = 15;
    else                     coreStars[i].colorIdx = (uint8_t)(fastRand() % 6);
  }

  for (int i = 0; i < NUM_HALO_STARS; i++) {
    float phi   = frand() * 2.0f * (float)M_PI;
    float rr    = 40.0f + frand() * MAX_RADIUS * 1.05f;
    haloStars[i].x = rr * cosf(phi);
    haloStars[i].z = rr * sinf(phi);
    haloStars[i].y = frandSigned() * DISK_THICKNESS * 1.5f;
    haloStars[i].brightness   = 0.1f + frand() * 0.3f;
    haloStars[i].colorIdx     = (uint8_t)(fastRand() % 6);
    haloStars[i].twinklePhase = (uint8_t)(fastRand() & 0xFF);
    haloStars[i].size         = 0;
  }
}

// ── 3D → 2D projection ────────────────────────────────────────────────────────
// Rotates a star around Y (spin) then X (tilt), then perspective-projects.
// Returns false if the point is behind the camera.
bool project(float gx, float gy, float gz,
             float sinSpin, float cosSpin,
             float sinTilt, float cosTilt,
             int16_t& sx, int16_t& sy, float& depthFactor)
{
  // 1. Rotate around galaxy Y axis (in-plane spin)
  float rx =  gx * cosSpin + gz * sinSpin;
  float ry =  gy;
  float rz = -gx * sinSpin + gz * cosSpin;

  // 2. Rotate around X axis (tilt — tip the disk toward viewer)
  float tx =  rx;
  float ty =  ry * cosTilt - rz * sinTilt;
  float tz =  ry * sinTilt + rz * cosTilt;

  // 3. Perspective divide
  float camZ = CAM_DIST - tz;   // distance from camera plane
  if (camZ < 10.0f) return false;

  float scale = FOV_SCALE / camZ;
  sx = (int16_t)(CX + tx * scale);
  sy = (int16_t)(CY - ty * scale);  // Y flipped for screen

  // depthFactor: 1.0 = at camera distance, <1 = far away
  depthFactor = FOV_SCALE / (FOV_SCALE + camZ * 0.5f);
  depthFactor = depthFactor < 0.05f ? 0.05f : (depthFactor > 1.4f ? 1.4f : depthFactor);

  return (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H);
}

// ── Draw one 3D star ──────────────────────────────────────────────────────────
void drawStar3D(const Star& s,
                float sinSpin, float cosSpin,
                float sinTilt, float cosTilt,
                bool twinkle)
{
  int16_t sx, sy;
  float   depth;
  if (!project(s.x, s.y, s.z, sinSpin, cosSpin, sinTilt, cosTilt, sx, sy, depth))
    return;

  float bri = s.brightness * depth;   // farther = dimmer
  if (twinkle) {
    uint8_t ph = (uint8_t)(s.twinklePhase + (frameCount & 0xFF));
    bri *= 0.72f + 0.28f * sinf(ph * 0.049f);
  }
  if (bri > 1.0f) bri = 1.0f;
  uint8_t  factor = (uint8_t)(bri * 255.0f);
  uint16_t col    = dimColor(PALETTE[s.colorIdx % PALETTE_SIZE], factor);

  if (s.size == 0 || depth < 0.6f) {
    sprite.drawPixel(sx, sy, col);
  } else {
    // Bright near stars rendered as a small cross
    sprite.drawPixel(sx,   sy,   col);
    sprite.drawPixel(sx+1, sy,   dimColor(col, 150));
    sprite.drawPixel(sx-1, sy,   dimColor(col, 150));
    sprite.drawPixel(sx,   sy+1, dimColor(col, 150));
    sprite.drawPixel(sx,   sy-1, dimColor(col, 150));
  }
}

// ── Glowing nucleus (always at screen center after projection) ────────────────
void drawCore() {
  for (int li = 0; li < NUM_GLOW; li++) {
    int16_t  lr = GLOW[li].r;
    uint8_t  la = GLOW[li].alpha;
    uint16_t lc = GLOW[li].col;
    for (int dy = -lr; dy <= lr; dy++) {
      int16_t y = CY + dy;
      if (y < 0 || y >= SCREEN_H) continue;
      int16_t hw = (int16_t)sqrtf((float)(lr*lr - dy*dy));
      for (int dx = -hw; dx <= hw; dx++) {
        int16_t x = CX + dx;
        if (x < 0 || x >= SCREEN_W) continue;
        float   dist = sqrtf((float)(dx*dx + dy*dy)) / lr;
        uint8_t a    = (uint8_t)(la * (1.0f - dist * dist));
        if (a < 4) continue;
        sprite.drawPixel(x, y, blendColor(sprite.readPixel(x, y), lc, a));
      }
    }
  }
  // Diffraction spikes
  uint16_t sp = C565(255,255,255);
  for (int d = 1; d <= 16; d++) {
    uint8_t  a  = (uint8_t)(170 * (1.0f - (float)d / 16.0f));
    uint16_t sc = dimColor(sp, a);
    sprite.drawPixel(CX+d, CY,   sc); sprite.drawPixel(CX-d, CY,   sc);
    sprite.drawPixel(CX,   CY+d, sc); sprite.drawPixel(CX,   CY-d, sc);
    if (d <= 9) {
      uint16_t sc2 = dimColor(sp, a/2);
      sprite.drawPixel(CX+d,CY+d,sc2); sprite.drawPixel(CX-d,CY+d,sc2);
      sprite.drawPixel(CX+d,CY-d,sc2); sprite.drawPixel(CX-d,CY-d,sc2);
    }
  }
}

// ── Static background stars ───────────────────────────────────────────────────
void drawBackgroundStars() {
  uint32_t saved = rngState;
  rngState = 0xCAFEBABE;
  for (int i = 0; i < 55; i++) {
    int16_t x  = (int16_t)(fastRand() % SCREEN_W);
    int16_t y  = (int16_t)(fastRand() % SCREEN_H);
    uint8_t br = (uint8_t)(15 + fastRand() % 50);
    sprite.drawPixel(x, y, dimColor(C565(200,210,255), br));
  }
  rngState = saved;
}

// ── Frame ─────────────────────────────────────────────────────────────────────
void renderFrame() {
  // Pre-compute trig for this frame
  float ss = sinf(spinAngle),  cs = cosf(spinAngle);
  float st = sinf(tiltAngle),  ct = cosf(tiltAngle);

  sprite.fillSprite(TFT_BLACK);
  drawBackgroundStars();

  // Halo — no spin (distant stars, barely move)
  for (int i = 0; i < NUM_HALO_STARS; i++)
    drawStar3D(haloStars[i], 0.0f, 1.0f, st, ct, false);

  // Spiral arms
  for (int arm = 0; arm < NUM_ARMS; arm++)
    for (int i = 0; i < STARS_PER_ARM; i++)
      drawStar3D(armStars[arm][i], ss, cs, st, ct, true);

  // Core bulge (spins slower, like a real bar)
  float ss2 = sinf(spinAngle * 0.3f), cs2 = cosf(spinAngle * 0.3f);
  for (int i = 0; i < NUM_CORE_STARS; i++)
    drawStar3D(coreStars[i], ss2, cs2, st, ct, true);

  drawCore();
  sprite.pushSprite(0, 0);

  // Advance angles
  spinAngle += SPIN_SPEED;
  if (spinAngle > 2.0f*(float)M_PI) spinAngle -= 2.0f*(float)M_PI;

  tiltPhase += TILT_SPEED;
  tiltAngle  = BASE_TILT + TILT_AMPLITUDE * sinf(tiltPhase);

  frameCount++;
}

// ── Arduino ───────────────────────────────────────────────────────────────────
void setup() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  sprite.createSprite(SCREEN_W, SCREEN_H);
  sprite.setColorDepth(16);
  initGalaxy();
}

void loop() {
  renderFrame();
}
