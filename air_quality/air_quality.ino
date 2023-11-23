/*


*/
// declarations for the oled screen
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_AM2320.h"
#include "sensor.h"
#include "oled.h"
#include <LowPower.h>
Adafruit_AM2320 am2320 = Adafruit_AM2320();

const byte wakeUpPin = 2;
const int psm5003pin = 3;

int lastread;
int lastblink;
int lastshake;
bool shaken;
float temp;
float hum;
int interruptCnt = 0; // For every interupt, we add 1 to this value


void shake() {
  shaken = true;

}

void setup() {
  am2320.begin(); // start temperature reader
  shaken = false;
  pinMode(wakeUpPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(wakeUpPin), shake, CHANGE);
  displayInit();
  // sensor baud rate is 9600
  //Serial.begin(9600);
  //Wire.begin();
  pinMode(wakeUpPin, INPUT);
  pinMode(psm5003pin, OUTPUT);
  digitalWrite(psm5003pin,HIGH);
  lastread = 0;
  lastblink = 0;
  displayInit();
  pinMode(redLed,OUTPUT);
  pinMode(greenLed,OUTPUT);
  pinMode(blueLed,OUTPUT);
  pinMode(yellowLed,OUTPUT);
  digitalWrite(redLed,LOW);
  digitalWrite(greenLed,LOW);
  digitalWrite(blueLed,LOW);
  digitalWrite(yellowLed,LOW);
  Serial.begin(9600);
  Wire.begin();
  blink(redLed);
  blink(yellowLed);
  blink(greenLed);
  blink(blueLed);
  temp = 0.6;
  hum = 0.7;
}

void blinkPeriod(int value) {
  if (interruptCnt > lastblink + 2){ // blink every 2 interrupt or 8 seconds.
    lastblink = interruptCnt;
    if (value > 50) {
      blink(redLed);
    } else if (value > 10) {
      blink(yellowLed);
    } else {
      blink(greenLed);
    }
  }
}

void loop() {
  if (shaken) {
    shaken = false;
    blink(redLed);
    lastshake = interruptCnt;
    display.dim(false);
    Wire.begin();
    am2320.readTemperatureAndHumidity(&temp, &hum);
    updateDisplay(data.pm25_standard, data.pm100_standard, temp, hum);
  }
  if (interruptCnt > lastshake+2) { // display for 2*8 seconds
    display.dim(true);
    Wire.end();
  }
  blinkPeriod(data.pm25_standard);
  am2320.readTemperatureAndHumidity(&temp, &hum);
  readPm();
  pause();
}
void updateDisplay(uint16_t pm25, uint16_t pm100, float temp, float hum){
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(64,5);
  display.print("PM2.5");
  display.setTextSize(2);
  display.setCursor(64,16);
  display.print(pm25);
  display.setTextSize(1);
  display.setCursor(0,5);
  display.print("Temp");
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(String(temp,1));
  display.setTextSize(1);
  display.cp437(true);
  display.write(167);
  // bottom portion of screen
  display.setTextSize(1);
  display.setCursor(64,40);
  display.print("PM10");
  display.setTextSize(2);
  display.setCursor(64,50);
  display.print(pm100);
  display.print(" ");
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.print("Humidity");
  display.setTextSize(2);
  display.setCursor(0, 50);
  display.print(String(hum,1));
  display.print("%");
  display.display();
}
void readPm() {
  if (lastread == 0|| interruptCnt > lastread+900) { // 900 interrupt is 2 hours.
    digitalWrite(psm5003pin,HIGH);
    Serial.begin(9600);
    delay(2000);
    unsigned long startread = millis();
    //display.dim(false);
    while (millis() < (startread + 20*1000)) { // read for 20 seconds
      if (readPMSdata(&Serial)) {
        blink(redLed);
        blink(yellowLed);
        //updateDisplay(data.pm25_standard, data.pm100_standard, temp, hum);
      }
    }
    //Serial.flush();
    Serial.end();
    lastread = interruptCnt;
    display.dim(true);
    digitalWrite(psm5003pin,LOW);
  }
}
void pause() {
  LowPower.idle(SLEEP_8S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF, 
                SPI_OFF, USART0_OFF, TWI_OFF);
  interruptCnt++;
}
