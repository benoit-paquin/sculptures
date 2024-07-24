// airplane mobile
// 

#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/power.h>
#include <avr/wdt.h> //Needed to enable/disable watch dog timer


// attiny85 pin setup, 
#define motorPin  1
#define greenLed  4 // green
#define redLed  2 // red
#define tailLed 3 // tail led
#define balanceLed  0


unsigned long int loop_counter =  0; //initialise watchdog counter
unsigned long int last_motor_run = 0; // last time the motor ran
long lastVcc = 0; // last voltage read
bool powerOn = true;
bool motorOn = true;

void goToSleep(int tim) {
  // disable ADC, sleep, enable ADC
  ADCSRA &= ~_BV(ADEN);       // ADC off
  sleep_enable();
  setup_watchdog(tim);
  sleep_mode();
  ADCSRA |= _BV(ADEN);        // ADC on
}

void flashALed(byte led, byte duration, byte count) {
  for (int i = 0; i< count; i++) {
    digitalWrite(led,HIGH);
    goToSleep(duration);
    digitalWrite(led, LOW);
    goToSleep(3);
  }
}

void checkState() {
  pinMode(A1, INPUT); //red 
  pinMode(A2, INPUT); //green
  int red   = analogRead(A1);
  delay(10);
  int green = analogRead(A2);
  delay(10);
  red   = analogRead(A1);
  delay(10);
  green = analogRead(A2);
  delay(10);
  if ((red <= 5) && (green >5)) {
    powerOn != powerOn;
    flashALed(tailLed,3,4);
  }
  if ((green <= 5) && (red >5)) {
    motorOn != motorOn;
    flashALed(tailLed,3,4);
  }
  pinMode(A1, OUTPUT); //red 
  pinMode(A2, OUTPUT); //green
}


void flashLeds() {
  // blink the heart led, sleep 64 ms while the LED is lit before turning off the led.
  // as the watchdog timer will increase the watchdog_counter value, decrease it by 1 otherwise it will changes the time limits.
  flashALed(redLed,0,1);
  flashALed(greenLed,0,1);
  flashALed(tailLed,2,2);
  flashALed(balanceLed,2,2);
}

void propeller() {
  if (motorOn) {
  for (int i = 0; i<10; i++) {
      digitalWrite(motorPin, HIGH);
      goToSleep(3);                                                    
      digitalWrite(motorPin, LOW);
      goToSleep(0);
    }
  }
}

ISR(WDT_vect) {
  //This runs each time the watch dog wakes us up from sleep
  wdt_disable();
}

void setup() {
  // Initialise some variables
  loop_counter =  0;   // timer, each increment is about 8.3 seconds
  last_motor_run = 0;
  lastVcc = 0;
  // prepare sleep parameters.
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
  // Set GPIO pins
  pinMode(motorPin,OUTPUT);
  pinMode(greenLed,OUTPUT);
  pinMode(redLed, OUTPUT);
  pinMode(tailLed, OUTPUT);
  pinMode(balanceLed, OUTPUT);
  flashLeds();
  propeller();
}

void loop() {
  checkState();
  if (powerOn) {
    if (loop_counter%4 == 0){
      lastVcc = readVcc();
    }
    if (loop_counter%4 == 0 && lastVcc > 2600) {
        propeller();
    }
    loop_counter++;
    if (lastVcc > 2200) {
      flashLeds();
    }
    if (lastVcc < 2200) { // not enough voltage. do nothing
      goToSleep(9);
    }
  }
  goToSleep(7);
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
