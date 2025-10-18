#!/usr/bin/env python3
"""
Create a GIF animation of precipitation radar over Copenhagen,
overlayed on an OpenStreetMap basemap using OpenWeatherMap tiles.
"""

import os, math, io, time
from datetime import datetime, timedelta
import requests
from PIL import Image
import imageio
import numpy as np

# ---------------------------
# CONFIG
# ---------------------------
OWM_API_KEY = "90ea5ee12e5dbecbe533b846bb5f8d10" #os.getenv("OWM_API_KEY") or "YOUR_API_KEY_HERE"
ZOOM = 9                 # 6–12 works; higher = more detailed
TILE_SIZE = 128 #256
LAYER = "precipitation"
OUT_GIF = "copenhagen_radar_osm.gif"
DURATION = 0.7           # seconds per frame
TILE_RADIUS = 1          # 1 → 3x3 tile block; increase for larger map

# Copenhagen center
#LAT, LON = 55.6761, 12.5683
# Sudbury
LAT, LON = 46.5, -81
# Time steps (use simple "current" mode)
FRAME_COUNT = 6
FRAME_INTERVAL_MIN = 10  # pretend 10-min steps (repeats current)
# ---------------------------


def latlon_to_tile(lat_deg, lon_deg, zoom):
    lat_rad = math.radians(lat_deg)
    n = 2.0 ** zoom
    xtile = (lon_deg + 180.0) / 360.0 * n
    ytile = (1.0 - math.log(math.tan(lat_rad) + 1 / math.cos(lat_rad)) / math.pi) / 2.0 * n
    return int(xtile), int(ytile)


# --- Tile downloaders -------------------------------------------------
def get_tile_osm(x, y, z, timeout=10):
    headers = {"User-Agent":"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_4) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.97 Safari/537.36"}
    url = f"https://tile.openstreetmap.org/{z}/{x}/{y}.png"
    r = requests.get(url, headers=headers, timeout=timeout)
    if r.status_code == 200:
        return Image.open(io.BytesIO(r.content)).convert("RGBA")
    return Image.new("RGBA", (TILE_SIZE, TILE_SIZE), (255,255,255,255))

def get_tile_owm_precip(x, y, z, layer, api_key, timeout=10):
    url = f"https://tile.openweathermap.org/map/{layer}/{z}/{x}/{y}.png?appid={api_key}"
    r = requests.get(url, timeout=timeout)
    if r.status_code == 200:
        return Image.open(io.BytesIO(r.content)).convert("RGBA")
    return Image.new("RGBA", (TILE_SIZE, TILE_SIZE), (0,0,0,0))


# --- Stitch a rectangular block of tiles into one image ---------------
def stitch_tiles(xmin, xmax, ymin, ymax, fetch_fn):
    cols = xmax - xmin + 1
    rows = ymax - ymin + 1
    out = Image.new("RGBA", (cols * TILE_SIZE, rows * TILE_SIZE))
    for ix, x in enumerate(range(xmin, xmax + 1)):
        for iy, y in enumerate(range(ymin, ymax + 1)):
            tile = fetch_fn(x, y)
            out.paste(tile, (ix * TILE_SIZE, iy * TILE_SIZE))
    return out


# --- Main -------------------------------------------------------------
def main():
    if OWM_API_KEY == "YOUR_API_KEY_HERE":
        raise SystemExit("⚠️ Please set your OpenWeatherMap API key.")

    cx, cy = latlon_to_tile(LAT, LON, ZOOM)
    xmin, xmax = cx - TILE_RADIUS, cx + TILE_RADIUS
    ymin, ymax = cy - TILE_RADIUS, cy + TILE_RADIUS

    frames = []

    print("Downloading base map (OSM)...")
    base = stitch_tiles(xmin, xmax, ymin, ymax,
                        lambda x, y: get_tile_osm(x, y, ZOOM))

    print("Fetching precipitation overlays and composing frames...")
    for i in range(FRAME_COUNT):
        # NOTE: The OWM tile endpoint always shows *current* precipitation.
        # This loop just repeats it to create a short animated GIF.
        overlay = stitch_tiles(xmin, xmax, ymin, ymax,
                               lambda x, y: get_tile_owm_precip(x, y, ZOOM, LAYER, OWM_API_KEY))
        frame = Image.alpha_composite(base, overlay)
        frames.append(np.array(frame))
        time.sleep(0.2)  # be polite to servers

    print(f"Saving GIF with {len(frames)} frames to {OUT_GIF}...")
    imageio.mimsave(OUT_GIF, frames, duration=DURATION)
    print("✅ Done:", OUT_GIF)


if __name__ == "__main__":
    main()
