// Plant watering monitoring system
// Check the moisture of the plant every 8 seconds, blink heart every 8 seconds too 
// Check the moisture: Moisture is higher when the reading is lower. The code reflects this.
//  If it is higher than the previous reading+5%, set maxHum to reading, set lastWatering to watchdog count, blink slowly red
//  if it is lower than minHum, set minHum to reading.
//  if lastWatering is more than 1 week old then:
//       if the last blink is more than one hour, blink red led at short intervals and set last blink time to current watchdog_counter.


#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/wdt.h> //Needed to enable/disable watch dog timer

// attiny85 pin setup, pin 2 is not used.
#define balloonLed 4
#define hearthLed 3
#define legPower 0
#define legHum A1
#define wakeupInterval 8

// define time limits and global variables
int oneHour= (10*60)/wakeupInterval; // changed to 10 minutes for test. number of watchdog timers during 1 hour is you wake up every 8 seconds
int oneWeek= (60*60)/wakeupInterval; // number of watchdog timers during a full week.
int watchdog_counter =  0; //initialise watchdog counter
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
  if(level < 0.95*lastHum) {
    lastWatering = watchdog_counter;
    lastHum = level;
    balloon('l'); // long blink
  }
}

void detectNoWatering() {
  // if last watering was more and a week old and last blink was more than an hour, do short blinks
  if (watchdog_counter > lastWatering + oneWeek) {
    if (watchdog_counter > lastBalloonBlink + oneHour){
      lastBalloonBlink = watchdog_counter;
      balloon('s');
    }
  }
}

void hearthBeat() {
  // blink the heart led, sleep 64 ms while the LED is lit before turning off the led.
  // as the watchdog timer will increase the watchdog_counter value, decrease it by 1 otherwise it will changes the time limits.
  digitalWrite(hearthLed, HIGH);
  setup_watchdog(2); //Setup watchdog to go off after 64ms
  sleep_mode(); //Go to sleep! Wake up 64 ms later
  digitalWrite(hearthLed, LOW);
  watchdog_counter--;
}

void balloon(char shortLong) {
  // blink the red balloon 5 times. For short blink, 64ms. Long blink: 500ms.
  for (byte i=0; i < 5; i++) {
    digitalWrite(balloonLed,HIGH);
    setup_watchdog((shortLong == 's')?2:5); //Setup watchdog to go off after 64/500ms
    sleep_mode(); //Go to sleep!
    watchdog_counter--;
    digitalWrite(balloonLed,LOW);
    if (i!=4) { // do not suspend after the last blink.
      sleep_mode();
      watchdog_counter--;
    }
  }
}


ISR(WDT_vect) {
  //This runs each time the watch dog wakes us up from sleep
  // the value of watchdog_counter*8seconds determine the elapsed time between events.
  watchdog_counter++;
}

void setup() {
  //Power down various bits of hardware to lower power usage  
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
  pinMode(balloonLed,OUTPUT);
  pinMode(hearthLed,OUTPUT);
  pinMode(legPower, OUTPUT);
  pinMode(legHum, INPUT);  
}

void loop() {
  hearthBeat();
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
