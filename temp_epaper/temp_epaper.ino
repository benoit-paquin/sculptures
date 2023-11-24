// Temperature and Humdity with e-paper display.
// systems is mostly sleeping for 8 seconds at a time. After 4 sleep cycles, the temp/hum is read. 
// If the temp/hum has changed since last display, update the e-paper.
// If the humidity is above 60 or below 40, blink a led.
// The e-paper is updated using the work done by upiir: https://github.com/upiir/arduino_eink_temperature
// ---- Includes
#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/wdt.h> //Needed to enable/disable watch dog timer
#include <stdio.h>
#include <TinyWireM.h>
#include "EPD_1in9.h"
#include "DHT20.h"
// ---- GLobals
DHT20 DHT;
float temp = 0.0;
float hum = 0.0;
float old_temp = -1.0; // only update display if the temp changed.
float old_hum = -1.0;
long old_vcc = 9999; //if vcc is greater than old_vcc, blink green longer.  
int watchdog_counter = 0; // number of interrupt
// ---- Utilities
void blink (bool green, byte cnt, bool long_blink) {
  // blink led cnt time.
  cnt = green ? cnt : 3*cnt;
  for (byte i = 0; i < cnt; i++){
    pinMode(4,OUTPUT);
    digitalWrite(4,green ? HIGH : LOW);
    delay(long_blink ? 120 : 240);
    pinMode(4,INPUT);
    delay(60);
  }
  
}

void init_temp() {
  // Init the temperature sensor.
  DHT.begin();
}

void init_epaper() {
  // init the epaper display
  GPIOInit();
  EPD_1in9_init();
  EPD_1in9_lut_5S(); // boot unit
  EPD_1in9_Write_Screen(DSPNUM_1in9_off); // write all white
  delay(500);
  EPD_1in9_lut_GC();
  EPD_1in9_Write_Screen1(DSPNUM_1in9_on);
  delay(500);
  EPD_1in9_lut_DU_WB(); // black out screen
  EPD_1in9_sleep();
}

void read_temp(float *temp, float *hum){
  // read the DHT20 sensor
  int status = DHT.read();
  while (status != DHT20_OK) {
    delay(50);
    status = DHT.read(); 
  }
  *temp = DHT.getTemperature();
  *hum = DHT.getHumidity()-2; // reduce by 2 percent after calibration.
}

void setup()
{
  // set up sleeping for the MCU
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
  TinyWireM.begin();
  pinMode(4,INPUT); // 2-Led with 1 GPIO pin. 
  init_temp();
  init_epaper();
  blink(true, 3, false);
  // display 10 x voltage at start. 3.45 V will show as 34.5C celcius
  update_epaper((float)readVcc()/100.0,0,true,true);
  delay(1000); // Leave VCC 
}

ISR(WDT_vect) { // called everytime the watchdog timer wakes up the CPU 
  watchdog_counter++;
}

void loop()
{ 
  if(4999 == watchdog_counter % 5000) { // reset epaper twice a day.
    init_epaper();
  }
  if (0 == watchdog_counter % 4) { // only update temp or screen every 9 seconds *4;
    // determine battery state
    long vcc = readVcc(); 
    if (vcc > old_vcc) {
      blink(true, 2, true);
    }
    old_vcc = vcc;
    bool high_bat = false;
    bool low_bat = false;
    high_bat = (vcc>3800); // more than 3.8V means high, display bluetooth icon
    low_bat = (vcc<2900);  // less than 3.2V means low, display battery icon
    read_temp(&temp, &hum);
    if (old_temp != temp || old_hum != hum){ // if any changes from last read
      update_epaper(temp, hum , high_bat, low_bat);
      old_temp = temp;
      old_hum = hum;
    }
  }
  if (hum < 40 || hum> 60) { //blink 1-2-3 or 4 times if humidity is too low/high.
    blink(false, (abs(50-hum)/10), false);
    } else {
      blink(true,1, false);
    }
  setup_watchdog(9); //Setup watchdog to go off after 8sec
  sleep_mode(); //Go to sleep! Wake up 8 sec later
}

//---------epaper driver, credit to upiir
char digit_left[] = {0xbf, 0x00, 0xfd, 0xf5, 0x47, 0xf7, 0xff, 0x21, 0xff, 0xf7, 0x00};  // individual segments for the left part od the digit, index 10 is empty
char digit_right[] ={0x1f, 0x1f, 0x17, 0x1f, 0x1f, 0x1d, 0x1d, 0x1f, 0x1f, 0x1f, 0x00};  // individual segments for the right part od the digit, index 10 is empty
char temp_dig[] = {1, 2, 3, 4}; // temperature digits > 1, 2, 3, 4 = 123.4°C
char hum_dig[] = {5, 6, 7}; // humidity digits > 5, 6, 7 = 56.7%
unsigned char eink_segments[]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,};  // all white, updated later

void update_epaper(float temp, float hum, bool high_bat, bool low_bat) {
  // create digits for temp and hum
  temp_dig[0] = int(temp / 100) % 10;
  temp_dig[1] = int(temp / 10) % 10;
  temp_dig[2] = int(temp ) % 10;
  temp_dig[3] = int(temp * 10) % 10;
  hum_dig[0] = int(hum / 10) % 10;
  hum_dig[1] = int(hum ) % 10;
  hum_dig[2] = int(hum * 10) % 10;
  delay(500);
  EPD_1in9_lut_GC();
  EPD_1in9_lut_DU_WB();
  // do not show leading zeros for values <100 and <10 both temperature and humidity
  if (temp < 100) {temp_dig[0] = 10;}
  if (temp < 10) {temp_dig[1] = 10;}  
  if (hum < 10) {hum_dig[0] = 10;}    
  // temperature digits
  eink_segments[0] = digit_right[temp_dig[0]]; // can only be one for 100+ degrees
  eink_segments[1] = digit_left[temp_dig[1]]; // second digit of the temp, left side
  eink_segments[2] = digit_right[temp_dig[1]];   
  eink_segments[3] = digit_left[temp_dig[2]];  // third digit of the temp
  eink_segments[4] = digit_right[temp_dig[2]] | B00100000 /* decimal point */;   
  eink_segments[11] = digit_left[temp_dig[3]];  // lest temp digit is after the humidity date
  eink_segments[12] = digit_right[temp_dig[3]];    
  // humidity digits
  eink_segments[5] = digit_left[hum_dig[0]];
  eink_segments[6] = digit_right[hum_dig[0]];    
  eink_segments[7] = digit_left[hum_dig[1]];
  eink_segments[8] = digit_right[hum_dig[1]] | B00100000 /* decimal point */;        
  eink_segments[9] = digit_left[hum_dig[2]];
  eink_segments[10] = digit_right[hum_dig[2]] | B00100000 /* percentage sign */;   
  // special symbols - °C / °F, bluetooth, battery
  eink_segments[13] = 0x05; /* °C */ 
  if (high_bat) {eink_segments[13] |=  B00001000;}  // Bluetooth sign if high battery
  if (low_bat) {eink_segments[13] |= B00010000;} // Battery sign if low battery
  // write segments to the e-ink screen
  EPD_1in9_Write_Screen(eink_segments);
  EPD_1in9_sleep();
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
  if (timerPrescaler > 9 ) timerPrescaler = 9; //Limit incoming amount to legal settings
  byte bb = timerPrescaler & 7; 
  if (timerPrescaler > 7) bb |= (1<<5); //Set the special 5th bit if necessary
  //This order of commands is important and cannot be combined
  MCUSR &= ~(1<<WDRF); //Clear the watch dog reset
  WDTCR |= (1<<WDCE) | (1<<WDE); //Set WD_change enable, set WD enable
  WDTCR = bb; //Set new watchdog timeout value
  WDTCR |= _BV(WDIE); //Set the interrupt enable, this will keep unit from resetting after each int
}