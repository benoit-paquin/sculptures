// Plant watering monitoring system
// Check the moisture of the plant every 8 seconds, blink heart every 8 seconds too 
// Check the moisture: Moisture is higher when the reading is lower. The code reflects this.
//  If it is higher than the previous reading+5%, set maxHum to reading, set lastWatering to watchdog count, blink red fast & short
//  if it is lower than minHum, set minHum to reading.
//  if lastWatering is more than 1 week old then:
//       if the last blink is more than one hour, blink red led at long intervals and set last blink time to current watchdog_counter.


#include <EEPROM.h>
#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/wdt.h> //Needed to enable/disable watch dog timer

// attiny85 pin setup, pin 1 is not used and grounded.
#define balloonLed 4
#define hearthLed 3
#define legPower 1
#define legHum A1
#define legGround 0
unsigned long int noWaterInterval=32400; //32400 is 3 days or 32400 slices of 8 seconds (3x24x60x60)/8
unsigned long int loop_counter =  0; //initialise watchdog counter
unsigned long int lastHum = 9999;
unsigned long int lastWatering = 0;
byte noWaterDays[] = {1, 3, 7};  //array indexed by the eeprom to find out how many days to check before watering


int readWatering() {
  //read humidity level with 10 consecutive readings and average
  digitalWrite(legPower, HIGH);
  int readings = 0;
  for (byte i=0; i < 10; i++) {  //10 reading and average
    delay(10);
    readings += analogRead(legHum);
  }
  readings = readings /10; 
  digitalWrite(legPower, LOW);
  return(readings);
}

void detectWatering(int level) {
  // lower argument 'level' denotes a higher humidity
  if(level < 0.95 * lastHum) {
    lastWatering = loop_counter;
    balloon('w'); // watering blink
    lastHum = level;
  }
}

void detectNoWatering() {
  // if last watering was more noWaterInterval and last blink was more than noWaterBlink, do blink
  if (loop_counter >= (lastWatering + noWaterInterval)) { 
    balloon('n');
  }
}

void heartBeat() {
  // blink the heart led, sleep 64 ms while the LED is lit before turning off the led.
  // as the watchdog timer will increase the watchdog_counter value, decrease it by 1 otherwise it will changes the time limits.
  digitalWrite(hearthLed, HIGH);
  setup_watchdog(0); //Setup watchdog to go off after 32ms
  sleep_mode(); //Go to sleep! Wake up 32 ms later
  digitalWrite(hearthLed, LOW);
}

void balloon(char waterNowater) {
  // blink the red balloon 5/10 times. For short blink, 64ms. Long blink: 500ms.
  for (byte i=0; i < ((waterNowater == 'w')?10:2); i++) {
    digitalWrite(balloonLed,HIGH);
    setup_watchdog((waterNowater == '2')?2:2); //Setup watchdog to go off after 64/500ms
    sleep_mode(); //Go to sleep!
    digitalWrite(balloonLed,LOW);
    if (i!=((waterNowater == 'w')?9:1)) { // do not suspend after the last blink.
      sleep_mode();
    }
  }
}


ISR(WDT_vect) {
  //This runs each time the watch dog wakes us up from sleep
  //wdt_disable();
}

void setup() {
  // Increase eeprom byte 0 by 1. Possible values are 0, 1, 2
  byte eevalue = EEPROM.read(0);
  eevalue++;
  eevalue = eevalue % 3;
  EEPROM.write(0,eevalue);
  noWaterInterval = (noWaterDays[eevalue]*24*60*60)/8; 
  lastWatering = 0;
  lastHum = 9999;
  loop_counter =  0; 
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
  pinMode(balloonLed,OUTPUT);
  pinMode(hearthLed,OUTPUT);
  pinMode(legPower, OUTPUT);
  pinMode(legHum, INPUT);
  pinMode(legGround, OUTPUT);
  digitalWrite(legGround, LOW); 
  for (int i= 0; i < noWaterDays[eevalue]; i++) {
    digitalWrite(balloonLed, HIGH);
    delay(500);
    digitalWrite(balloonLed, LOW);
    delay(500);
  }
}

void loop() {
  heartBeat();
  int level = readWatering();
  detectWatering(level);
  detectNoWatering();
  setup_watchdog(9); //Setup watchdog to go off after 8sec
  sleep_mode(); //Go to sleep! Wake up 8 sec later
  loop_counter++;
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
