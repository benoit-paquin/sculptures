// Plant watering monitoring system
// Check the moisture of the plant every 8 seconds, blink heart every 8 seconds too 
// Check the moisture: Moisture is higher when the reading is lower. The code reflects this.
//  If it is higher than the previous reading+5%, set maxHum to reading, set lastWatering to watchdog count, blink red fast & short
//  if it is lower than minHum, set minHum to reading.
//  if lastWatering is more than 1 week old then:
//       if the last blink is more than one hour, blink red led at long intervals and set last blink time to current watchdog_counter.


#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/wdt.h> //Needed to enable/disable watch dog timer

// attiny85 pin setup, pin 1 is not used and grounded.
#define balloonLed 4
#define hearthLed 3
#define legPower 1
#define legHum A1
#define legGround 0
#define wakeupInterval 8
#define oneDay 86400 // number of seconds in a day
#define oneHour 3600 // number of seconds in an hour

// define time limits and global variables
// set to 1 day and 1 minute for testing
int noWaterBlink= 1; //# (1*60)/wakeupInterval; // balloon will blink at every cycle if set to 1, otherwise at time interval.
int noWaterInterval= (3*oneDay)/wakeupInterval; // number of loop iteration during 3 days.
int loop_counter =  0; //initialise watchdog counter
int lastHum = 9999;
int lastWatering = 0;
int lastBalloonBlink = 0;

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
  if(level < 0.90*lastHum) {
    lastWatering = loop_counter;
    balloon('w'); // watering blink
  }
  lastHum = level;
}

void detectNoWatering() {
  // if last watering was more and a week old and last blink was more than an hour, do short blinks
  if (loop_counter > lastWatering + noWaterInterval) {
    if (loop_counter > lastBalloonBlink + noWaterBlink){
      lastBalloonBlink = loop_counter;
      balloon('n');
    }
  }
}

void heartBeat() {
  // blink the heart led, sleep 64 ms while the LED is lit before turning off the led.
  // as the watchdog timer will increase the watchdog_counter value, decrease it by 1 otherwise it will changes the time limits.
  digitalWrite(hearthLed, HIGH);
  setup_watchdog(1); //Setup watchdog to go off after 32ms
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
}

void setup() {
  //Power down various bits of hardware to lower power usage  
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
  pinMode(balloonLed,OUTPUT);
  pinMode(hearthLed,OUTPUT);
  pinMode(legPower, OUTPUT);
  pinMode(legHum, INPUT);
  pinMode(legGround, OUTPUT);
  digitalWrite(legGround, LOW); 
  balloon('w');
  heartBeat();
  delay(50);
  heartBeat();
  balloon('n'); 
}

void loop() {
  loop_counter++;
  heartBeat();
  int level = readWatering();
  detectWatering(level);
  detectNoWatering();
  setup_watchdog(9); //Setup watchdog to go off after 8sec
  sleep_mode(); //Go to sleep! Wake up 8 sec later
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
