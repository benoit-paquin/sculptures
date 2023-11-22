#include <TinyWireM.h>
#include <stdlib.h>
#include "EPD_1in9.h"

//////////////////////////////////////full screen update LUT////////////////////////////////////////////

unsigned char DSPNUM_1in9_on[]   = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,       };  // all black
unsigned char DSPNUM_1in9_off[]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       };  // all white
unsigned char DSPNUM_1in9_WB[]   = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,       };  // All black font
unsigned char DSPNUM_1in9_W0[]   = {0x00, 0xbf, 0x1f, 0xbf, 0x1f, 0xbf, 0x1f, 0xbf, 0x1f, 0xbf, 0x1f, 0xbf, 0x1f, 0x00, 0x00,       };  // 0
unsigned char DSPNUM_1in9_W1[]   = {0xff, 0x1f, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00, 0x00,       };  // 1
unsigned char DSPNUM_1in9_W2[]   = {0x00, 0xfd, 0x17, 0xfd, 0x17, 0xfd, 0x17, 0xfd, 0x17, 0xfd, 0x17, 0xfd, 0x37, 0x00, 0x00,       };  // 2
unsigned char DSPNUM_1in9_W3[]   = {0x00, 0xf5, 0x1f, 0xf5, 0x1f, 0xf5, 0x1f, 0xf5, 0x1f, 0xf5, 0x1f, 0xf5, 0x1f, 0x00, 0x00,       };  // 3
unsigned char DSPNUM_1in9_W4[]   = {0x00, 0x47, 0x1f, 0x47, 0x1f, 0x47, 0x1f, 0x47, 0x1f, 0x47, 0x1f, 0x47, 0x3f, 0x00, 0x00,       };  // 4
unsigned char DSPNUM_1in9_W5[]   = {0x00, 0xf7, 0x1d, 0xf7, 0x1d, 0xf7, 0x1d, 0xf7, 0x1d, 0xf7, 0x1d, 0xf7, 0x1d, 0x00, 0x00,       };  // 5
unsigned char DSPNUM_1in9_W6[]   = {0x00, 0xff, 0x1d, 0xff, 0x1d, 0xff, 0x1d, 0xff, 0x1d, 0xff, 0x1d, 0xff, 0x3d, 0x00, 0x00,       };  // 6
unsigned char DSPNUM_1in9_W7[]   = {0x00, 0x21, 0x1f, 0x21, 0x1f, 0x21, 0x1f, 0x21, 0x1f, 0x21, 0x1f, 0x21, 0x1f, 0x00, 0x00,       };  // 7
unsigned char DSPNUM_1in9_W8[]   = {0x00, 0xff, 0x1f, 0xff, 0x1f, 0xff, 0x1f, 0xff, 0x1f, 0xff, 0x1f, 0xff, 0x3f, 0x00, 0x00,       };  // 8
unsigned char DSPNUM_1in9_W9[]   = {0x00, 0xf7, 0x1f, 0xf7, 0x1f, 0xf7, 0x1f, 0xf7, 0x1f, 0xf7, 0x1f, 0xf7, 0x1f, 0x00, 0x00,       };  // 9

unsigned char VAR_Temperature=20; 


/******************************************************************************
function :  GPIO Init
parameter:
******************************************************************************/
void GPIOInit(void)
{
  pinMode(EPD_BUSY_PIN, INPUT);
  pinMode(EPD_RST_PIN, OUTPUT);
}


/******************************************************************************
function :  Software reset
parameter:
******************************************************************************/
void EPD_1in9_Reset(void)
{
    digitalWrite(EPD_RST_PIN, 1);
    delay(200);
    digitalWrite(EPD_RST_PIN, 0);
    delay(20);
    digitalWrite(EPD_RST_PIN, 1);
    delay(200);
}

/******************************************************************************
function :  send command
parameter:
     Reg : Command register
******************************************************************************/
void EPD_1in9_SendCommand(unsigned char Reg)
{
  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(Reg);
  TinyWireM.endTransmission(false);
}

/******************************************************************************
function :  send data
parameter:
    Data : Write data
******************************************************************************/
void EPD_1in9_SendData(unsigned char Data)
{
    TinyWireM.beginTransmission(adds_data);
  TinyWireM.write(Data);
  TinyWireM.endTransmission();
}

/******************************************************************************
function :  read command
parameter:
     Reg : Command register
******************************************************************************/
unsigned char EPD_1in9_readCommand(unsigned char Reg)
{
  unsigned char a;
  TinyWireM.beginTransmission(adds_com);
  delay(10);
  TinyWireM.write(Reg);
  a = TinyWireM.read();
  TinyWireM.endTransmission();
  return a;
}

/******************************************************************************
function :  read data
parameter:
    Data : Write data
******************************************************************************/
unsigned char EPD_1in9_readData(unsigned char Data)
{
  unsigned char a;
    TinyWireM.beginTransmission(adds_data);
  delay(10);
  TinyWireM.write(Data);
  a = TinyWireM.read();
  TinyWireM.endTransmission();
  return a;
}

/******************************************************************************
function :  Wait until the busy_pin goes LOW
parameter:
******************************************************************************/
void EPD_1in9_ReadBusy(void)
{
   // Serial.println("e-Paper busy");
  delay(10);
  while(1)
  {  //=1 BUSY;
    if(digitalRead(EPD_BUSY_PIN)==1) 
      break;
    delay(1);
  }
  delay(10);
//    Serial.println("e-Paper busy release");  
}

/*
# DU waveform white extinction diagram + black out diagram
# Bureau of brush waveform
*/
void EPD_1in9_lut_DU_WB(void)
{
  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0x82);
  TinyWireM.write(0x80);
  TinyWireM.write(0x00);
  TinyWireM.write(0xC0);
  TinyWireM.write(0x80);
  TinyWireM.write(0x80);
  TinyWireM.write(0x62);
  TinyWireM.endTransmission();
}

/*   
# GC waveform
# The brush waveform
*/
void EPD_1in9_lut_GC(void)
{
  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0x82);
  TinyWireM.write(0x20);
  TinyWireM.write(0x00);
  TinyWireM.write(0xA0);
  TinyWireM.write(0x80);
  TinyWireM.write(0x40);
  TinyWireM.write(0x63);
  TinyWireM.endTransmission();
}

/* 
# 5 waveform  better ghosting
# Boot waveform
*/
void EPD_1in9_lut_5S(void)
{
  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0x82);
  TinyWireM.write(0x28);
  TinyWireM.write(0x20);
  TinyWireM.write(0xA8);
  TinyWireM.write(0xA0);
  TinyWireM.write(0x50);
  TinyWireM.write(0x65);
  TinyWireM.endTransmission(); 
}

/*
# temperature measurement
# You are advised to periodically measure the temperature and modify the driver parameters
# If an external temperature sensor is available, use an external temperature sensor
*/
void EPD_1in9_Temperature(void)
{
  TinyWireM.beginTransmission(adds_com);
  if( VAR_Temperature < 10 )
  {
    TinyWireM.write(0x7E);
    TinyWireM.write(0x81);
    TinyWireM.write(0xB4);
  }
  else
  {
    TinyWireM.write(0x7E);
    TinyWireM.write(0x81);
    TinyWireM.write(0xB4);
  }
  TinyWireM.endTransmission();

    delay(10);        

  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0xe7);    // Set default frame time
        
  // Set default frame time
  if(VAR_Temperature<5)
    TinyWireM.write(0x31); // 0x31  (49+1)*20ms=1000ms
  else if(VAR_Temperature<10)
    TinyWireM.write(0x22); // 0x22  (34+1)*20ms=700ms
  else if(VAR_Temperature<15)
    TinyWireM.write(0x18); // 0x18  (24+1)*20ms=500ms
  else if(VAR_Temperature<20)
    TinyWireM.write(0x13); // 0x13  (19+1)*20ms=400ms
  else
    TinyWireM.write(0x0e); // 0x0e  (14+1)*20ms=300ms
  TinyWireM.endTransmission();
}

/*
# Note that the size and frame rate of V0 need to be set during initialization, 
# otherwise the local brush will not be displayed
*/
void EPD_1in9_init(void)
{
  unsigned char i = 0;
  EPD_1in9_Reset();
  delay(100);

  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0x2B); // POWER_ON
  TinyWireM.endTransmission();

  delay(10);

  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0xA7); // boost
  TinyWireM.write(0xE0); // TSON 
  TinyWireM.endTransmission();

  delay(10);

  EPD_1in9_Temperature();
}

void EPD_1in9_Write_Screen( unsigned char *image)
{
  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0xAC); // Close the sleep
  TinyWireM.write(0x2B); // turn on the power
  TinyWireM.write(0x40); // Write RAM address
  TinyWireM.write(0xA9); // Turn on the first SRAM
  TinyWireM.write(0xA8); // Shut down the first SRAM
  TinyWireM.endTransmission();

  TinyWireM.beginTransmission(adds_data);
  for(char j = 0 ; j<15 ; j++ )
    TinyWireM.write(image[j]);

  TinyWireM.write(0x00);
  TinyWireM.endTransmission();


  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0xAB); // Turn on the second SRAM
  TinyWireM.write(0xAA); // Shut down the second SRAM
  TinyWireM.write(0xAF); // display on
  TinyWireM.endTransmission();

  EPD_1in9_ReadBusy();
  //delay(2000);
  
  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0xAE); // display off
  TinyWireM.write(0x28); // HV OFF
  TinyWireM.write(0xAD); // sleep in 
  TinyWireM.endTransmission();
}

void EPD_1in9_Write_Screen1( unsigned char *image)
{
  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0xAC); // Close the sleep
  TinyWireM.write(0x2B); // turn on the power
  TinyWireM.write(0x40); // Write RAM address
  TinyWireM.write(0xA9); // Turn on the first SRAM
  TinyWireM.write(0xA8); // Shut down the first SRAM
  TinyWireM.endTransmission();

  TinyWireM.beginTransmission(adds_data);
  for(char j = 0 ; j<15 ; j++ )
    TinyWireM.write(image[j]);

  TinyWireM.write(0x03);
  TinyWireM.endTransmission();

  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0xAB); // Turn on the second SRAM
  TinyWireM.write(0xAA); // Shut down the second SRAM
  TinyWireM.write(0xAF); // display on
  TinyWireM.endTransmission();

  EPD_1in9_ReadBusy();
  //delay(2000);

  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0xAE); // display off
  TinyWireM.write(0x28); // HV OFF
  TinyWireM.write(0xAD); // sleep in 
  TinyWireM.endTransmission();

}

void EPD_1in9_sleep(void)
{
  TinyWireM.beginTransmission(adds_com);
  TinyWireM.write(0x28); // POWER_OFF
  EPD_1in9_ReadBusy();
  TinyWireM.write(0xAD); // DEEP_SLEEP
  TinyWireM.endTransmission();

  delay(2000);
  // digitalWrite(EPD_RST_PIN, 0);
}
