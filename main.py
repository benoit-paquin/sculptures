# main.py — ESP32-C3 + ST7789 240x240 continuous radar overlay with clock

import network, time, ntptime, urequests as requests, math, gc, os
from machine import Pin, SPI
import upng
from st7789 import ST7789
import vga1_bold_16x32 as font  # bold 16x32 font

# ---------- USER CONFIG ----------
WIFI_SSID = "your_wifi"
WIFI_PASS = "your_password"
OWM_API_KEY = "your_openweathermap_api_key"

LAT, LON = 55.6761, 12.5683
ZOOM = 8
WIDTH, HEIGHT = 240, 240
FRAME_DELAY = 20
REFRESH_INTERVAL = 600      # 10 min
CACHE_MAX_AGE = 86400       # 1 day

# ---------- XIAO ESP32-C3 PINS ----------
SPI_SCK = 8
SPI_MOSI = 10
TFT_DC = 3
TFT_RST = 2
TFT_CS = 1
TFT_BL = 7

# ---------- DISPLAY UTILS ----------
def text_center(tft, text, y, color=0xFFFF):
    x = (WIDTH - len(text) * 16) // 2
    tft.text(font, text, x, y, color)

def text_topright(tft, text, y, color=0xFFFF):
    x = WIDTH - len(text) * 16
    tft.text(font, text, x, y, color)

def init_display():
    spi = SPI(1, baudrate=40000000, sck=Pin(SPI_SCK), mosi=Pin(SPI_MOSI))
    tft = ST7789(spi, WIDTH, HEIGHT, reset=Pin(TFT_RST, Pin.OUT),
                 dc=Pin(TFT_DC, Pin.OUT), cs=Pin(TFT_CS, Pin.OUT),
                 backlight=Pin(TFT_BL, Pin.OUT), rotation=0)
    tft.init()
    Pin(TFT_BL, Pin.OUT).value(1)
    tft.fill(0)
    text_center(tft, "Starting...", 100)
    return tft

# ---------- NETWORK ----------
def connect_wifi(tft=None):
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    wlan.connect(WIFI_SSID, WIFI_PASS)
    for _ in range(20):
        if wlan.isconnected():
            print("Wi-Fi connected:", wlan.ifconfig())
            if tft:
                tft.fill(0)
                text_center(tft, "Wi-Fi OK", 100, 0x07E0)
            return True
        time.sleep(0.5)
    if tft:
        tft.fill(0)
        text_center(tft, "Wi-Fi FAIL", 100, 0xF800)
    raise RuntimeError("Wi-Fi failed")

def sync_time(tft=None):
    try:
        ntptime.settime()
        if tft:
            tft.fill(0)
            text_center(tft, "Time Sync OK", 100, 0x07E0)
    except:
        if tft:
            tft.fill(0)
            text_center(tft, "NTP FAIL", 100, 0xF800)
        print("NTP sync failed")

# ---------- IMAGE HELPERS ----------
def latlon_to_tilexy(lat, lon, zoom):
    lat_rad = math.radians(lat)
    n = 2 ** zoom
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1.0 - math.log(math.tan(lat_rad) + 1 / math.cos(lat_rad)) / math.pi) / 2.0 * n)
    return x, y

def fetch(url):
    r = requests.get(url)
    if r.status_code == 200:
        data = r.content
        r.close()
        return data
    r.close()
    raise RuntimeError("HTTP", r.status_code)

def rgba_to_rgb565(rgba, w, h):
    buf = bytearray(w * h * 2)
    bi = 0
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i:i+4]
        val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        buf[bi] = val >> 8
        buf[bi+1] = val & 0xFF
        bi += 2
    return buf

def blend_rgba_over_rgb565(dest, w, h, src):
    di, si = 0, 0
    for _ in range(w * h):
        high, low = dest[di], dest[di+1]
        val = (high << 8) | low
        dr, dg, db = (val >> 11) & 31, (val >> 5) & 63, val & 31
        dr, dg, db = int(dr*255/31), int(dg*255/63), int(db*255/31)
        sr, sg, sb, sa = src[si:si+4]
        si += 4
        if sa:
            a = sa / 255
            nr = int(sr*a + dr*(1-a))
            ng = int(sg*a + dg*(1-a))
            nb = int(sb*a + db*(1-a))
            valn = ((nr & 0xF8) << 8) | ((ng & 0xFC) << 3) | (nb >> 3)
            dest[di], dest[di+1] = valn >> 8, valn & 0xFF
        di += 2

# ---------- CACHING ----------
def ensure_dir(dirname):
    try:
        os.mkdir(dirname)
    except OSError:
        pass

def clean_cache(dirname="/tiles"):
    now = time.time()
    try:
        for fname in os.listdir(dirname):
            path = dirname + "/" + fname
            try:
                st = os.stat(path)
                if now - st[8] > CACHE_MAX_AGE:
                    os.remove(path)
            except OSError:
                pass
    except OSError:
        pass

def cached_get(path, url):
    if path in os.listdir("/tiles"):
        with open("/tiles/"+path, "rb") as f:
            return f.read()
    data = fetch(url)
    with open("/tiles/"+path, "wb") as f:
        f.write(data)
    return data

# ---------- URL BUILDERS ----------
def osm_url(z, x, y):
    return f"https://tile.openstreetmap.org/{z}/{x}/{y}.png"

def owm_url(z, x, y, apikey, ts):
    return f"https://maps.openweathermap.org/maps/2.0/radar/{z}/{x}/{y}?date={int(ts)}&appid={apikey}"

# ---------- BUILD FRAME ----------
def build_frame(tx, ty, ts):
    osm_name = f"{ZOOM}_{tx}_{ty}_osm.png"
    rain_name = f"{ZOOM}_{tx}_{ty}_rain_{int(ts)}.png"
    osm_data = cached_get(osm_name, osm_url(ZOOM, tx, ty))
    rain_data = cached_get(rain_name, owm_url(ZOOM, tx, ty, OWM_API_KEY, ts))
    base = upng.decode(osm_data)
    rain = upng.decode(rain_data)
    buf = rgba_to_rgb565(base['rgba'], base['width'], base['height'])
    blend_rgba_over_rgb565(buf, base['width'], base['height'], rain['rgba'])
    return buf

# ---------- CLOCK OVERLAY ----------
def overlay_clock(tft):
    tm = time.localtime()
    timestr = "{:02d}:{:02d}".format(tm[3], tm[4])
    datestr = "{:02d}/{:02d}".format(tm[2], tm[1])

    rect_width = max(len(timestr), len(datestr)) * 16 + 4
    rect_height = 32*2 + 4
    x = WIDTH - rect_width
    y = 0
    # Draw semi-transparent rectangle
    tft.fill_rect(x, y, rect_width, rect_height, 0x0000 >> 1)  # dark rectangle
    # Draw text
    tft.text(font, timestr, x+2, y+2, 0xFFFF)
    tft.text(font, datestr, x+2, y+2+32, 0xFFFF)

# ---------- MAIN LOOP ----------
def show_frames(tft, tx, ty):
    now = time.time()
    timestamps = [now, now-600, now-1200]
    for ts in timestamps:
        gc.collect()
        buf = build_frame(tx, ty, ts)
        tft.blit_buffer(buf, 0, 0, WIDTH, HEIGHT)
        overlay_clock(tft)
        time.sleep(FRAME_DELAY)

def main():
    ensure_dir("/tiles")
    tft = init_display()
    connect_wifi(tft)
    sync_time(tft)
    clean_cache()
    tx, ty = latlon_to_tilexy(LAT, LON, ZOOM)
    last_refresh = 0

    while True:
        now = time.time()
        if now - last_refresh >= REFRESH_INTERVAL:
            clean_cache()
            last_refresh = now
        show_frames(tft, tx, ty)

if __name__ == "__main__":
    main()
