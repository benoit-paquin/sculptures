// airplane mobile
// 

#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/power.h>
#include <avr/wdt.h> //Needed to enable/disable watch dog timer

// attiny85 pin setup, 
#define motorPin 0 
#define leftLed 1 // green
#define rightLed 2 // red
#define tailLed 3 // tail led
#define balanceLed 4 

unsigned long int loop_counter =  0; //initialise watchdog counter
unsigned long int last_motor_run = 0; // last time the motor ran
long lastVcc = 0; // last voltage read

void goToSleep(int tim) {
  // disable ADC, sleep, enable ADC
  ADCSRA &= ~_BV(ADEN);       // ADC off
  sleep_enable();
  setup_watchdog(tim);
  sleep_mode();
  ADCSRA |= _BV(ADEN);        // ADC on
}


void flashLeds() {
  // blink the heart led, sleep 64 ms while the LED is lit before turning off the led.
  // as the watchdog timer will increase the watchdog_counter value, decrease it by 1 otherwise it will changes the time limits.
  digitalWrite(leftLed, HIGH);
  goToSleep(0); //Setup watchdog to go off after 16ms
  digitalWrite(leftLed, LOW);
  goToSleep(0);
  digitalWrite(rightLed, HIGH);
  goToSleep(0); //Setup watchdog to go off after 16ms
  digitalWrite(rightLed, LOW);
  goToSleep(0);
  digitalWrite(tailLed, HIGH);
  goToSleep(1); //Setup watchdog to go off after 16ms
  digitalWrite(tailLed, LOW);
  goToSleep(0);
  digitalWrite(tailLed, HIGH);
  goToSleep(1); //Setup watchdog to go off after 16ms
  digitalWrite(tailLed, LOW);
  goToSleep(0);
  digitalWrite(balanceLed, HIGH);
  goToSleep(4); //Setup watchdog to go off after 16ms
  digitalWrite(balanceLed, LOW);
  goToSleep(0);
  digitalWrite(motorPin, HIGH);
  goToSleep(3 ); //Setup watchdog to go off after 16ms
  digitalWrite(motorPin, LOW);
}

void motor() {
    digitalWrite(motorPin, HIGH);
    delay(500);
    digitalWrite(motorPin, LOW);
    delay(300);
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
  pinMode(leftLed,OUTPUT);
  pinMode(rightLed, OUTPUT);
  pinMode(tailLed, OUTPUT);
  pinMode(balanceLed, OUTPUT);
  flashLeds();
  //motor();
}

void loop() {
  if (loop_counter%4 == 0){
    lastVcc = readVcc();
  }
  if (loop_counter%5 == 0 && lastVcc > 2400) {
    //motor();
  }
  loop_counter++;
  if (lastVcc > 2400) {
    flashLeds();
  }
  if (lastVcc < 2200) { // not enough voltage. do nothing
    goToSleep(9);
  }
  goToSleep(8);
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
