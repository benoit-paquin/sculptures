#!/usr/bin/env python3
"""
Create a GIF animation of precipitation/radar overlay for Copenhagen using OpenWeatherMap tiles.

Two modes:
 - simple current tiles: uses tile.openweathermap.org/map/precipitation/{z}/{x}/{y}.png
 - timestamped frames (historical/forecast): uses maps.openweathermap.org/maps/2.0/radar/forecast/{z}/{x}/{y}?tm={unix_ts}
   (the timestamped endpoint may require a Weather Maps / Global Precipitation subscription)

Adjust ZOOM to control resolution (higher = more tiles / larger image).
"""
import os
import math
import io
import time
from datetime import datetime, timedelta
import requests
from PIL import Image
import imageio
import numpy as np

# ---------------------------
# USER CONFIG
# ---------------------------
OWM_API_KEY = os.getenv("OWM_API_KEY") or "YOUR_API_KEY_HERE"  # <-- Replace or set environment variable
LAYER = "precipitation"  # precipitation layer name (other options: clouds, temp, etc.)
ZOOM = 8                # zoom level (6-12 for city scale; higher = more tiles)
TILE_SIZE = 256         # tile pixel size
OUT_GIF = "copenhagen_precip.gif"
DURATION = 0.5          # seconds per frame in GIF

# Copenhagen bounding-box (lat/lon). We'll center on the city and fetch a small tile block around it
COPENHAGEN_LAT = 55.6761
COPENHAGEN_LON = 12.5683
TILE_RADIUS = 1  # number of tiles around the center tile in x/y direction (1 => 3x3 tiles). Increase for larger area.

# Choose mode:
# mode = "simple" -> uses public tile endpoint (current overlay)
# mode = "timestamped" -> uses the maps/2.0 radar endpoint with 'tm' parameter (requires product access)
MODE = "simple"  # set to "timestamped" if you have access and want multiple times

# If MODE == "timestamped", provide a list of datetimes (UTC). The script will produce one frame per timestamp.
# Example: last 6 frames every 10 minutes
TIMESTAMPS = [
    int((datetime.utcnow() - timedelta(minutes=10 * i)).timestamp())
    for i in reversed(range(6))
]  # list of unix timestamps (UTC). Replace with specific unix timestamps if you like.

# ---------------------------
# Helper functions
# ---------------------------
def latlon_to_tile(lat_deg, lon_deg, zoom):
    """Return tile x,y (integer) for given lat/lon and zoom using Web Mercator."""
    lat_rad = math.radians(lat_deg)
    n = 2.0 ** zoom
    xtile = (lon_deg + 180.0) / 360.0 * n
    ytile = (1.0 - math.log(math.tan(lat_rad) + (1 / math.cos(lat_rad))) / math.pi) / 2.0 * n
    return int(xtile), int(ytile)

def fetch_tile_simple(x, y, z, layer, api_key, timeout=10):
    """Fetch tile from public tile endpoint (current map)."""
    url = f"https://tile.openweathermap.org/map/{layer}/{z}/{x}/{y}.png?appid={api_key}"
    r = requests.get(url, timeout=timeout)
    if r.status_code == 200:
        return Image.open(io.BytesIO(r.content)).convert("RGBA")
    else:
        # Return transparent tile on failure
        return Image.new("RGBA", (TILE_SIZE, TILE_SIZE), (0,0,0,0))

def fetch_tile_timestamped(x, y, z, layer, api_key, unix_ts, timeout=10):
    """Fetch tile from Weather Maps 2.0 timestamped endpoint (may require product access)."""
    # This is an example endpoint. Confirm exact endpoint for your product / account.
    # Example documented endpoints: https://maps.openweathermap.org/maps/2.0/radar/forecast/{z}/{x}/{y}?appid={API key}&tm={unix}
    url = f"https://maps.openweathermap.org/maps/2.0/radar/forecast/{z}/{x}/{y}?appid={api_key}&tm={unix_ts}"
    r = requests.get(url, timeout=timeout)
    if r.status_code == 200:
        return Image.open(io.BytesIO(r.content)).convert("RGBA")
    else:
        return Image.new("RGBA", (TILE_SIZE, TILE_SIZE), (0,0,0,0))

def stitch_tiles(xmin, xmax, ymin, ymax, fetch_tile_fn):
    """Download a block of tiles and stitch into one PIL image."""
    cols = xmax - xmin + 1
    rows = ymax - ymin + 1
    out_w = cols * TILE_SIZE
    out_h = rows * TILE_SIZE
    canvas = Image.new("RGBA", (out_w, out_h), (0,0,0,0))
    for ix, x in enumerate(range(xmin, xmax+1)):
        for iy, y in enumerate(range(ymin, ymax+1)):
            tile = fetch_tile_fn(x, y)
            canvas.paste(tile, (ix * TILE_SIZE, iy * TILE_SIZE), tile)
    return canvas

# ---------------------------
# Main flow
# ---------------------------
def main():
    if OWM_API_KEY == "YOUR_API_KEY_HERE":
        raise SystemExit("Please set OWM_API_KEY in the script or as environment variable OWM_API_KEY.")

    # compute center tile for Copenhagen
    center_x, center_y = latlon_to_tile(COPENHAGEN_LAT, COPENHAGEN_LON, ZOOM)
    xmin = center_x - TILE_RADIUS
    xmax = center_x + TILE_RADIUS
    ymin = center_y - TILE_RADIUS
    ymax = center_y + TILE_RADIUS

    frames = []

    if MODE == "simple":
        print("MODE: simple (current precipitation tiles)")
        def fetch_fn(x, y, z=ZOOM, layer=LAYER, api_key=OWM_API_KEY):
            return fetch_tile_simple(x, y, z, layer, api_key)
        print(f"Downloading and stitching one frame for tiles x={xmin}..{xmax}, y={ymin}..{ymax} at zoom={ZOOM}...")
        img = stitch_tiles(xmin, xmax, ymin, ymax, fetch_fn)
        # Optional: overlay a basemap (OpenStreetMap) — omitted here for clarity.
        frames.append(np.array(img))
    else:
        print("MODE: timestamped (requires Weather Maps 2.0 / Global Precipitation access)")
        for ts in TIMESTAMPS:
            print("Fetching frame for timestamp (UTC):", datetime.utcfromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S"))
            def fetch_fn_ts(x, y, z=ZOOM, layer=LAYER, api_key=OWM_API_KEY, unix_ts=ts):
                return fetch_tile_timestamped(x, y, z, layer, api_key, unix_ts)
            img = stitch_tiles(xmin, xmax, ymin, ymax, fetch_fn_ts)
            frames.append(np.array(img))
            time.sleep(0.2)  # be polite to API

    # Save gif
    print(f"Saving GIF to {OUT_GIF} with {len(frames)} frames...")
    imageio.mimsave(OUT_GIF, frames, duration=DURATION)
    print("Done. Open", OUT_GIF)

if __name__ == "__main__":
    main()
