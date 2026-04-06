#include "DrawSky.h"
DrawSky::DrawSky(int8_t cs, int8_t dc, int8_t rst) {
  _tft=new Adafruit_ST7789(cs,dc,rst);
  
}
void DrawSky::myFunction(int blinkRate){
digitalWrite(_pin, HIGH);
delay(blinkRate);
digitalWrite(_pin, LOW);
delay(blinkRate);
}