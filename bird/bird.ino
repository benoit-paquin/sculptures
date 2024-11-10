#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/wdt.h> //Needed to enable/disable watch dog timer
#include <math.h>
#define ledPin 0 
int i=0;
int breathe_delay = 5;   // delay between loops
unsigned long breathe_time = millis();
byte redLed = 0;
byte greenLed = 2;
byte solarVcc = A3;
byte yellowLed = 4;
int oldVcc=9999;
int vcc=0;

void blink(byte led, byte cnt, int del) {
  for (byte i = 0; i < cnt; i++) {
    digitalWrite(led, HIGH);
    goToSleep(3);
    digitalWrite(led, LOW);
    goToSleep(3); 
  }
}

void setup() {
  pinMode(greenLed, OUTPUT);
  pinMode(redLed, OUTPUT);
  pinMode(yellowLed, OUTPUT);
  pinMode(solarVcc, INPUT);
  // test sequence
  vcc = readVcc();
  //ADCSRA &= ~(1<<ADEN); //Disable ADC, saves ~230uA
  //Power down various bits of hardware to lower power usage  
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
  blink(redLed,2,200);
  blink(greenLed,2,200);
  blink(yellowLed,2,200);
}

//This runs each time the watch dog wakes us up from sleep
ISR(WDT_vect) {
  //watchdog_counter++;
}

void candleFlicker() {
  // set the brightness of the LED based on the random number
  analogWrite(yellowLed, random(0,220));
  delay(random(10, 80));
}

void goToSleep(byte arg) {
  setup_watchdog(arg);
  sleep_mode();
}

void allOff() {
  digitalWrite(redLed, LOW);
  digitalWrite(yellowLed, LOW);
  digitalWrite(greenLed, LOW);
}

void loop() {
  if (0 == i%(random(500,900))) {
    allOff();
    delay(2);
    vcc=readVcc();
    for(i = 0; i < random(1,5); i++) {
      goToSleep(9);
    }
    i=1;
  }
  if (vcc < 2500) { // shudown to save capacitor
    allOff();
    oldVcc = vcc;
    blink(redLed,2,100);
    for(i = 0; i < 4; i++) {
      goToSleep(9);
    }    
    vcc=readVcc();
  } else if (vcc > oldVcc and vcc <3800) { //charge capacitor as there is sun
    allOff();
    oldVcc = vcc;
    blink(greenLed,2,100);
    goToSleep(9);
    goToSleep(9);
    vcc=readVcc();
  } else {
    if (analogRead(solarVcc) > 500) {
      // it is sunny
      allOff();
      goToSleep(9);
    } else {
      // dark in the room
      oldVcc = vcc;
      nonBlockingBreath();//BreathIn();
      candleFlicker();
      if (0 == i%random(5,20)) {
        digitalWrite(greenLed,9 < random(1,11)?HIGH:LOW);
      }
    }
  }
}

//Sets the watchdog timer to wake us up, but not reset
//0=16ms, 1=32ms, 2=64ms, 3=128ms, 4=250ms, 5=500ms
//6=1sec, 7=2sec, 8=4sec, 9=8sec
//From: http://interface.khm.de/index.php/lab/experiments/sleep_watchdog_battery/
void setup_watchdog(int timerPrescaler) {
  if (timerPrescaler > 9 ) timerPrescaler = 9; //Limit incoming amount to legal settings
  byte bb = timerPrescaler & 7; 
  if (timerPrescaler > 7) bb |= (1<<5); //Set the special 5th bit if necessary
  //This order of commands is important and cannot be combined
  MCUSR &= ~(1<<WDRF); //Clear the watch dog reset
  WDTCR |= (1<<WDCE) | (1<<WDE); //Set WD_change enable, set WD enable
  WDTCR = bb; //Set new watchdog timeout value
  WDTCR |= _BV(WDIE); //Set the interrupt enable, this will keep unit from resetting after each int
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
 

void nonBlockingBreath()
{
  if( (breathe_time + breathe_delay) < millis() ){
    breathe_time = millis();
    float val = (exp(sin(i/400.0*PI*10)) - 0.36787944)*108.0; 
    // this is the math function recreating the effect
    analogWrite(redLed, val);  // PWM
    i=i+1;
  }
}
