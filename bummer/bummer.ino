#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/wdt.h> //Needed to enable/disable watch dog timer
#include <math.h>
#define ledPin 1 
int i=0;
int breathe_delay = 15;   // delay between loops
unsigned long breathe_time = millis();
byte redLed = 0;
byte greenLed = 1;

void setup() {
  pinMode(0, OUTPUT);// LED connected to pin 5 which is recognised as pin 0 by arduino
  pinMode(1, OUTPUT);
  //ADCSRA &= ~(1<<ADEN); //Disable ADC, saves ~230uA
  //Power down various bits of hardware to lower power usage  
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
}

//This runs each time the watch dog wakes us up from sleep
ISR(WDT_vect) {
  //watchdog_counter++;
}
void loop() {
  int vcc;
  vcc = readVcc();
  if (false) { //vcc > 4500) {
    nonBlockingBreath();
  }
  else {
    digitalWrite(greenLed, LOW);
    digitalWrite(redLed, LOW);
    setup_watchdog(8); //Setup watchdog to go off after 8sec
    sleep_mode(); //Go to sleep! Wake up 4 sec later
    int loopcnt = 2+roll(6);
    for(int j=0;j<5;j++)
    {
      digitalWrite(redLed, (8<=roll(10)) ? HIGH : HIGH);
      digitalWrite(greenLed,LOW);
      delay(10+roll(25));
      digitalWrite(redLed, LOW);
      digitalWrite(greenLed,(8<=roll(10)) ? HIGH : HIGH);
      delay(10+roll(25));
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
    float val = (exp(sin(i/2000.0*PI*10)) - 0.36787944)*108.0; 
    // this is the math function recreating the effect
    analogWrite(ledPin, val);  // PWM
    analogWrite(0, val);  // PWM
    i=i+1;
  }
}
int roll(int max)
{
   int random  = rand()%max+ 1;
   return random;
}
