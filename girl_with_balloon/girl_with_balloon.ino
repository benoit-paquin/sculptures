// Plant watering monitoring system
// Check the moisture of the plant every 8 seconds, blink heart every 8 seconds too 
// Check the moisture: Moisture is higher when the reading is lower. The code reflects this.
//  If it is higher than the previous reading+5%, set maxHum to reading, set lastWatering to watchdog count, blink red fast & short
//  if it is lower than minHum, set minHum to reading.
//  if lastWatering is more than 1 week old then:
//       if the last blink is more than one hour, blink red led at long intervals and set last blink time to current watchdog_counter.


#include <EEPROM.h>
#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/power.h>
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
byte wateringDays = 0;

int readWatering() {
  //read humidity level with 10 consecutive readings and average
  digitalWrite(legPower, HIGH);
  int readings = 0;
  for (byte i=0; i < 10; i++) {  //10 reading and average
    int reading;
    reading = analogRead(legHum);
    delay(10);
    readings += reading;
  }
  readings = readings /10; 
  digitalWrite(legPower, LOW);
  return(readings);
}

void detectWatering(int level) {
  // Detect that the plant has been watered
  // lower argument 'level' denotes a higher humidity
  if(level < 0.98 * lastHum) {
    lastWatering = loop_counter;
    balloon('w'); // watering blink
    goToSleep(6); // wait 1 second
    displayWateringDays(wateringDays); // Blink the balloon the number of days between checks
  }
  lastHum = level;
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
  goToSleep(0); //Setup watchdog to go off after 16ms
  digitalWrite(hearthLed, LOW);
}

void balloon(char waterNowater) {
  // blink the red balloon 5/10 times. For short blink, 64ms. Long blink: 500ms.
  for (byte cnt=0; cnt < ((waterNowater == 'w')?10:2); cnt++) {
    digitalWrite(balloonLed,HIGH);
    goToSleep(4); //Setup watchdog to go off after 64/500ms
    digitalWrite(balloonLed,LOW);
    goToSleep(2);   
  }
}

void goToSleep(int tim) {
  // disable ADC, sleep, enable ADC
  ADCSRA &= ~_BV(ADEN);       // ADC off
  sleep_enable();
  setup_watchdog(tim);
  sleep_mode();
  ADCSRA |= _BV(ADEN);        // ADC on
}

ISR(WDT_vect) {
  //This runs each time the watch dog wakes us up from sleep
  wdt_disable();
}

void displayWateringDays(byte wateringDays) {
  // blink the balloon corresponding to the number of days to check for no watering 
    for (int i= 0; i < wateringDays; i++) {
    digitalWrite(balloonLed, HIGH);
    delay(500);  // Half a second
    digitalWrite(balloonLed, LOW);
    delay(500);
  }
}

void setup() {
  // at each restart, the delay before the no water blinks, torates from 1 to 4 to 7 days. The information is stored in the EEPROM, byte 0.
  // Increase eeprom byte 0 by 1. Possible values are 0, 1, 2
  byte eevalue = EEPROM.read(0);  // read stored value
  eevalue++; 
  eevalue = eevalue % 3;
  delay(500); // set delay in case of infinite reboot, we do not want to constantly rewrite the eeprom. 
  EEPROM.write(0,eevalue);   // save for next time. 
  wateringDays = noWaterDays[eevalue];
  noWaterInterval = wateringDays*3600; // 12 hours is about 3600 loop. for debugging*10800; //10800 number of 8 seconds slices in a day
  // Initialise some variables
  lastWatering = 0;    // last time water was added to the plant
  lastHum = 9999;      // last humidity reading
  loop_counter =  0;   // timer, each increment is about 8.3 seconds
  // prepare sleep parameters.
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
  // Set GPIO pins
  pinMode(balloonLed,OUTPUT);
  pinMode(hearthLed,OUTPUT);
  pinMode(legPower, OUTPUT);
  //pinMode(legHum, INPUT); This line is replaced by DDRB, below 
  DDRB &= ~(1 << DDB2);
  pinMode(legGround, OUTPUT);
  digitalWrite(legGround, LOW); 
  // Tell the user of the watering interval
  displayWateringDays(wateringDays);
}

void loop() {
  heartBeat();
  int level = readWatering();
  detectWatering(level);
  detectNoWatering();
  goToSleep(9); //Setup watchdog to go off after 8sec
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
