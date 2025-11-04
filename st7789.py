# st7789.py
# MicroPython driver for ST7789 240x240 displays on ESP32-C3

import time
from machine import Pin, SPI

# Color helper
def color565(r, g, b):
    """Convert RGB888 to RGB565."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

class ST7789:
    def __init__(self, spi, cs, dc, reset=None, width=240, height=240):
        self.spi = spi
        self.cs = Pin(cs, Pin.OUT)
        self.dc = Pin(dc, Pin.OUT)
        self.reset = Pin(reset, Pin.OUT) if reset else None
        self.width = width
        self.height = height

        self.cs.value(1)
        if self.reset:
            self.reset.value(1)
        self.init_display()

    # Low-level commands
    def write_cmd(self, cmd):
        self.dc.value(0)
        self.cs.value(0)
        self.spi.write(bytearray([cmd]))
        self.cs.value(1)

    def write_data(self, data):
        self.dc.value(1)
        self.cs.value(0)
        self.spi.write(data if isinstance(data, bytearray) else bytearray([data]))
        self.cs.value(1)

    def init_display(self):
        # Hardware reset
        if self.reset:
            self.reset.value(0)
            time.sleep_ms(50)
            self.reset.value(1)
            time.sleep_ms(50)

        # Initialization sequence for ST7789 240x240
        self.write_cmd(0x01)  # Software reset
        time.sleep_ms(150)
        self.write_cmd(0x11)  # Sleep out
        time.sleep_ms(500)
        self.write_cmd(0x3A)  # COLMOD: 16-bit color
        self.write_data(0x55)
        self.write_cmd(0x36)  # MADCTL: Memory data access control
        self.write_data(0x00)  # Normal rotation
        self.write_cmd(0x29)  # Display ON
        time.sleep_ms(100)

    def set_window(self, x0, y0, x1, y1):
        # Column address
        self.write_cmd(0x2A)
        self.write_data(bytearray([x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF]))
        # Row address
        self.write_cmd(0x2B)
        self.write_data(bytearray([y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF]))
        self.write_cmd(0x2C)  # Memory write

    def fill_screen(self, color):
        """Fill the entire screen with a single color."""
        self.set_window(0, 0, self.width - 1, self.height - 1)
        high = color >> 8
        low = color & 0xFF
        buf = bytearray([high, low] * self.width)
        for _ in range(self.height):
            self.write_data(buf)

    def draw_pixel(self, x, y, color):
        """Draw a single pixel at (x, y)."""
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return
        self.set_window(x, y, x, y)
        self.write_data(bytearray([color >> 8, color & 0xFF]))

    def blit_buffer(self, buffer, x=0, y=0, w=None, h=None):
        """Draw a raw RGB565 buffer to the display."""
        w = w or self.width
        h = h or self.height
        self.set_window(x, y, x + w - 1, y + h - 1)
        self.write_data(buffer)
