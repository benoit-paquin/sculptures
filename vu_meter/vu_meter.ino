/* ATtiny85 Sound Level Meter

   David Johnson-Davies - www.technoblogy.com - 1st January 2016
   ATtiny85 @ 1 MHz (internal oscillator; BOD disabled)
   CC BY 4.0
   Licensed under a Creative Commons Attribution 4.0 International license:
   http://creativecommons.org/licenses/by/4.0/
*/

volatile int Display = 0;

// Display **********************************************

int LogBar (unsigned int x) {
  x = x | x>>1;
  x = x | x>>2;
  x = x | x>>4;
  x = x | x>>8;
  return x;
}

// Interrupt **********************************************

volatile int Row = 0;

// Interrupt multiplexes display
ISR(TIMER0_COMPA_vect) {
  int Data;
  Row = (Row + 1) % 5;
  DDRB = 0; // Make all pins inputs
  if (Row == 0) Data = (Display & 0xC0)>>3 | (Display & 0x20)>>4;
  else if (Row == 1) Data = Display & 0x18;
  else if (Row == 3) Data = Display>>8 & 0x03;
  else if (Row == 4) Data = (Display & 0x03) | (Display & 0x04)<<1;
  else {
    Display = LogBar(ReadADC()); return;
  } // When Row=2 read ADC
  PORTB = (PORTB | Data) & ~(1<<Row); // Take row low
  DDRB = DDRB | Data | 1<<Row; // Make row an output
}

// Analogue to Digital **********************************

unsigned int ReadADC() {
  static int Read;
  unsigned char low,
  high;
  ADCSRA = ADCSRA | 1<<ADSC; // Start
  do; while (ADCSRA & 1<<ADSC); // Wait while conversion in progress
  low = ADCL;
  high = ADCH;
  Read = max(Read>>1, high<<8 | low); // Add delay
  if (Read < 50) {
    Read = Read/8;
  }
  //if (Read < 64) {  Read = Read-8;}
  return Read;
}

// Setup **********************************************

void setup() {
  // Turn off Timer/Counter1 and USI to save 0.12mA
  PRR = 1<<PRTIM1 | 1<<PRUSI;
  // Set up Timer/Counter0 to generate 625Hz interrupt
  TCCR0A = 2<<WGM00; // CTC mode
  TCCR0B = 3<<CS00; // /64 prescaler
  OCR0A = 24; // 625 Hz interrupt
  TIMSK = TIMSK | 1<<OCIE0A; // Enable interrupt
  // Set up ADC
  ADMUX = 2<<REFS0 | 0<<REFS2 | 11<<MUX0; // Internal 1.1V ref, ADC0 vs ADC1 x20
  ADCSRA = 1<<ADEN | 3<<ADPS0; // Enable, 125kHz ADC clock
}

void loop() {}
// Nothing to do in loop