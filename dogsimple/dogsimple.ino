/* Dog
Connections
* PB0 Dual puposes: DHT22 date line and PWM output for the nose
* PB1 green led, if HIGH, then led is off
* PB2 red led, if HIGH, then led is off
* PB3 Blue led, if HIGH, then led if off
* PB4 Power to the DTH22

The DHT22 is powered on when required. The data is sent to PB0. As PB0 will go HIGH and LOW, the nose will shrtly blink.
When the DHT power is switched off, the PB0 is used as PWM for all 3 leds. We use a common anode for this.

*/
// includes
#include "TinyDHT.h"
#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/power.h>
#include <avr/wdt.h> //Needed to enable/disable watch dog timer
#define greenLed PB2
#define redLed PB3
#define blueLed PB1
#define pwmLed PB4

// create classes
//DHT dht(DHTPIN, DHTTYPE);

// variables
float temp; // temp and humidity
long vcc; // voltage
float valid_temp = 0;
long mov_avg = 0;
long mov_accum = 0;
unsigned long i=0;
int breathe_delay = 5;   // delay between loops
unsigned long breathe_time = millis();


// simple moving average for the temperature samples
void mov_sample(long sample) {
  mov_accum += sample;
  mov_accum -= mov_avg;
  mov_avg = mov_accum >> 3;
}


void setup() {
  pinMode(redLed,OUTPUT);
  pinMode(greenLed, OUTPUT);
  pinMode(blueLed, OUTPUT);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
  digitalWrite(greenLed,HIGH);
  digitalWrite(redLed,HIGH);
  digitalWrite(blueLed,HIGH);
  
  blink(redLed,3);
  blink(greenLed,3);
  blink(blueLed,3);
  delay(1000);
}

/*
void readSensors() {
  /*digitalWrite(DHTPOWER,HIGH);
  delay(50);
  dht.begin();
  temp = dht.readTemperature();
  digitalWrite(DHTPOWER,LOW);
  pinMode(pwmLed,OUTPUT);
  digitalWrite(pwmLed,LOW);
  if (temp>0 ) valid_temp = temp;
  else {
    blink(redLed,1);
  }
  vcc = readVcc();
}
*/
//This runs each time the watch dog wakes us up from sleep
ISR(WDT_vect) {
  //watchdog_counter++;
}

void blink(byte led, byte cnt) {
  pinMode(pwmLed,OUTPUT);
  digitalWrite(pwmLed,HIGH);
  if (cnt == 0) {
    digitalWrite(led,LOW);
    delay(1000);
    digitalWrite(led,HIGH);
    delay(1000);
  }
  for(int i = 0; i < cnt; i++) {
    digitalWrite(led,LOW);
    delay(200);
    digitalWrite(led,HIGH);
    delay(200);
  } 
  digitalWrite(pwmLed,LOW);
}
void loop() {
  /*readSensors();
  mov_sample(valid_temp*10);
  if ((valid_temp*10) > mov_avg+10) blink(redLed,valid_temp/3);
  else if ((valid_temp*10) < mov_avg-10) blink(blueLed,valid_temp/3);
  else blink(greenLed,valid_temp/3);
  delay(2000);*/
  breath(1+random(7),random(15,25));
  goToSleep(6+random(4));
}

void breath(byte col, int seconds) {
  i=0;
  //1:red, 2: green, 4:blue
  if (col%2 == 1) digitalWrite(redLed,LOW);
  col = col/2;
  if (col%2 == 1) digitalWrite(greenLed,LOW);
  col = col/2;
  if (col%2 == 1) digitalWrite(blueLed,LOW);  
  for (int i = 0; i< (seconds*31) ; i++) {
    nonBlockingBreath(pwmLed);
    delay(50);
  }
  digitalWrite(redLed, HIGH);
  digitalWrite(greenLed, HIGH);
  digitalWrite(blueLed,HIGH);
}

long readVcc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring
  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH
  uint8_t high = ADCH; // unlocks both
  long result = (high<<8) | low;
  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}

void setup_watchdog(int timerPrescaler) {
  // 0=16ms, 1=32ms,2=64ms,3=128ms,4=250ms,5=500ms
  // 6=1 sec,7=2 sec, 8=4 sec, 9= 8sec
  if (timerPrescaler > 9 ) timerPrescaler = 9; //Limit incoming amount to legal settings
  byte bb = timerPrescaler & 7;
  if (timerPrescaler > 7) bb |= (1<<5); //Set the special 5th bit if necessary
  //This order of commands is important and cannot be combined
  MCUSR &= ~(1<<WDRF); //Clear the watch dog reset
  WDTCR |= (1<<WDCE) | (1<<WDE); //Set WD_change enable, set WD enable
  WDTCR = bb; //Set new watchdog timeout value
  WDTCR |= _BV(WDIE); //Set the interrupt enable, this will keep unit from resetting after each int
}

void goToSleep(int tim) {
  // disable ADC, sleep, enable ADC
  ADCSRA &= ~_BV(ADEN);       // ADC off
  sleep_enable();
  setup_watchdog(tim);
  sleep_mode();
  ADCSRA |= _BV(ADEN);        // ADC on
}

void nonBlockingBreath(byte led)
{
  if( (breathe_time + breathe_delay) < millis() ){
    breathe_time = millis();
    float val = (exp(sin(i/600.0*PI*10)) - 0.36787944)*108.0; 
    // this is the math function recreating the effect
    analogWrite(led, val);  // PWM
    i=i+1;
  }
}
