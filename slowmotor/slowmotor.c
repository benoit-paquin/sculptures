// airplane mobile
// 

#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/power.h>
#include <avr/wdt.h> //Needed to enable/disable watch dog timer


// attiny85 pin setup, 
#define motorPin  1
#define ledPin 0

void goToSleep(int tim) {
  // disable ADC, sleep, enable ADC
  ADCSRA &= ~_BV(ADEN);       // ADC off
  sleep_enable();
  setup_watchdog(tim);
  sleep_mode();
  ADCSRA |= _BV(ADEN);        // ADC on
}


void propeller() {
  for (int i = 0; i<3; i++) {
      digitalWrite(motorPin, HIGH);
      digitalWrite(ledPin, HIGH);
      goToSleep(9);                                                    
      digitalWrite(motorPin, LOW);
      digitalWrite(ledPin, LOW);
      goToSleep(0);
    }
}

ISR(WDT_vect) {
  //This runs each time the watch dog wakes us up from sleep
  wdt_disable();
}

void setup() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
  // Set GPIO pins
  pinMode(motorPin,OUTPUT);
  pinMode(ledPin, OUTPUT);
  propeller();
}

void loop() {
  propeller();
  if (readVcc > 3200) {
    goToSleep(3);
  } else
  goToSleep(9);
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
