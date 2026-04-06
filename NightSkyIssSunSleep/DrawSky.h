#ifndef DrawSky_h
#define DrawSky_h
#include "Arduino.h" 
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

class DrawSky {
public:
	DrawSky(int8_t cs, int8_t dc, int8_t rst);
	//void drawTitle(MyST7789 tft);
	void myFunction(int blinkRate);
private:
	int8_t _pin;

	Adafruit_ST7789* _tft;
};
#endif