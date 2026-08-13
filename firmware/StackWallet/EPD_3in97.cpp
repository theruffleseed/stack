/*****************************************************************************
* | File      	:   EPD_3IN97.c
* | Author      :   Waveshare team
* | Function    :   1.54inch e-paper V2
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2019-06-11
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "EPD_3in97.h"
#include "Debug.h"
#include <time.h> 

/******************************************************************************
function :	Software reset
parameter:
******************************************************************************/
static void EPD_3IN97_Reset(void)
{
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(2);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
}

/******************************************************************************
function :	send command
parameter:
     Reg : Command register
******************************************************************************/
static void EPD_3IN97_SendCommand(UBYTE Reg)
{
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Reg);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :	send data
parameter:
    Data : Write data
******************************************************************************/
static void EPD_3IN97_SendData(UBYTE Data)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Data);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static void EPD_3IN97_SendDataBlock(const UBYTE *Data, UDOUBLE len)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteBlock(Data, len);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :	Wait until the busy_pin goes LOW
parameter:
******************************************************************************/
static void EPD_3IN97_ReadBusy(void)
{
    Debug("e-Paper busy\r\n");
    DEV_Delay_ms(100);
    // Bounded wait: the panel is expected to release BUSY (GPIO3) within a
    // couple of seconds. Without this cap, a wiring/power issue or a stuck
    // strapping pin turns into a silent infinite hang with no diagnostic -
    // exactly the symptom this timeout is meant to surface instead.
    UDOUBLE waited_ms = 0;
    const UDOUBLE timeout_ms = 5000;
    while(DEV_Digital_Read(EPD_BUSY_PIN) == 1) {      //LOW: idle, HIGH: busy
        DEV_Delay_ms(10);
        waited_ms += 10;
        if (waited_ms >= timeout_ms) {
            Debug("e-Paper busy TIMEOUT - BUSY (GPIO3) never went low, check panel power/wiring\r\n");
            return;
        }
    }
    Debug("e-Paper busy release\r\n");
}

/******************************************************************************
function :	Wait until the busy_pin goes LOW, without the fixed head-start
            delay the stock routine adds before polling. For kick paths only
            (0x20 already issued): waits for the update-asserted rising edge
            (bounded), then for the release falling edge (bounded), so an
            idle panel costs ~0ms instead of a blanket 100ms per kick.
parameter:
******************************************************************************/
static void EPD_3IN97_ReadBusyNoPad(void)
{
    UDOUBLE waited_ms = 0;
    const UDOUBLE rise_timeout_ms = 500;
    const UDOUBLE fall_timeout_ms = 5000;

    while (DEV_Digital_Read(EPD_BUSY_PIN) == 0 && waited_ms < rise_timeout_ms) {
        DEV_Delay_ms(1);
        waited_ms += 1;
    }

    waited_ms = 0;
    while (DEV_Digital_Read(EPD_BUSY_PIN) == 1) {
        DEV_Delay_ms(10);
        waited_ms += 10;
        if (waited_ms >= fall_timeout_ms) {
            Debug("e-Paper busy TIMEOUT - BUSY (GPIO3) never went low, check panel power/wiring\r\n");
            return;
        }
    }
    Debug("e-Paper busy release\r\n");
}

/******************************************************************************
function :	Turn On Display full
parameter:
******************************************************************************/
static void EPD_3IN97_TurnOnDisplay(void)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(0xF7);
	EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusyNoPad();
}

static void EPD_3IN97_TurnOnDisplay_Fast(void)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(0xD7);
	EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusyNoPad();
}

static void EPD_3IN97_TurnOnDisplay_4GRAY(void)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(0xD7);
	EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusyNoPad();
}

static void EPD_3IN97_TurnOnDisplay_Part(void)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(0xFF);
    EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusyNoPad();
}

/******************************************************************************
function :	Initialize the e-Paper register
parameter:
******************************************************************************/
void EPD_3IN97_Init(void)
{
    EPD_3IN97_Reset();

    EPD_3IN97_ReadBusy();
    EPD_3IN97_SendCommand(0x12);  //SWRESET
    EPD_3IN97_ReadBusy();

    EPD_3IN97_SendCommand(0x18);
    EPD_3IN97_SendData(0x80);

    EPD_3IN97_SendCommand(0x0C);
	EPD_3IN97_SendData(0xAE);
	EPD_3IN97_SendData(0xC7);
	EPD_3IN97_SendData(0xC3);
	EPD_3IN97_SendData(0xC0);
	EPD_3IN97_SendData(0x80);

    EPD_3IN97_SendCommand(0x01); //Driver output control
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
    EPD_3IN97_SendData(0x02);

    EPD_3IN97_SendCommand(0x3C); //BorderWavefrom
    EPD_3IN97_SendData(0x01);

    EPD_3IN97_SendCommand(0x11); //data entry mode       
	EPD_3IN97_SendData(0x01);

	EPD_3IN97_SendCommand(0x44); //set Ram-X address start/end position   
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)/256);

	EPD_3IN97_SendCommand(0x45); //set Ram-Y address start/end position    
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);  
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);

    EPD_3IN97_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendCommand(0x4F);   // set RAM y address count to 0X199;    
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
    EPD_3IN97_ReadBusy();

}
//Fast update initialization
void EPD_3IN97_Init_Fast(void)
{
	EPD_3IN97_Reset(); 
	
	EPD_3IN97_ReadBusy();   
	EPD_3IN97_SendCommand(0x12);  //SWRESET
	EPD_3IN97_ReadBusy();   
	
	EPD_3IN97_SendCommand(0x0C);
	EPD_3IN97_SendData(0xAE);
	EPD_3IN97_SendData(0xC7);
	EPD_3IN97_SendData(0xC3);
	EPD_3IN97_SendData(0xC0);
	EPD_3IN97_SendData(0x80);
	
	EPD_3IN97_SendCommand(0x01); //Driver output control      
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);   
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
	EPD_3IN97_SendData(0x02);

	EPD_3IN97_SendCommand(0x11); //data entry mode       
	EPD_3IN97_SendData(0x01);

	EPD_3IN97_SendCommand(0x44); //set Ram-X address start/end position   
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)/256);

	EPD_3IN97_SendCommand(0x45); //set Ram-Y address start/end position    
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);  
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);


	EPD_3IN97_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendCommand(0x4F);   // set RAM y address count to 0X199;    
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
    EPD_3IN97_ReadBusy();

	EPD_3IN97_SendCommand(0x3C); //BorderWavefrom
	EPD_3IN97_SendData(0x01);	
	
	EPD_3IN97_SendCommand(0x18);   
	EPD_3IN97_SendData(0x80); 
	//Fast(1.5s)
	EPD_3IN97_SendCommand(0x1A); 
	EPD_3IN97_SendData(0x6A);

}
//4 Gray update initialization
void EPD_3IN97_Init_4GRAY(void)
{
	EPD_3IN97_Reset();
	
	EPD_3IN97_ReadBusy();   
	EPD_3IN97_SendCommand(0x12);  //SWRESET
	EPD_3IN97_ReadBusy();   
	
	EPD_3IN97_SendCommand(0x0C);
	EPD_3IN97_SendData(0xAE);
	EPD_3IN97_SendData(0xC7);
	EPD_3IN97_SendData(0xC3);
	EPD_3IN97_SendData(0xC0);
	EPD_3IN97_SendData(0x80);
	
	EPD_3IN97_SendCommand(0x01); //Driver output control      
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);   
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
	EPD_3IN97_SendData(0x02);

	EPD_3IN97_SendCommand(0x11); //data entry mode       
	EPD_3IN97_SendData(0x01);

	EPD_3IN97_SendCommand(0x44); //set Ram-X address start/end position   
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)/256);

	EPD_3IN97_SendCommand(0x45); //set Ram-Y address start/end position    
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);  
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);


	EPD_3IN97_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendCommand(0x4F);   // set RAM y address count to 0X199;    
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
    EPD_3IN97_ReadBusy();

	EPD_3IN97_SendCommand(0x3C); //BorderWavefrom
	EPD_3IN97_SendData(0x01);	
	
	EPD_3IN97_SendCommand(0x18);   
	EPD_3IN97_SendData(0x80); 
	//4 Gray
	EPD_3IN97_SendCommand(0x1A); 
	EPD_3IN97_SendData(0x5A);

}
/******************************************************************************
function :	Clear screen
parameter:
******************************************************************************/
void EPD_3IN97_Clear(void)
{
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x24);
    for (UDOUBLE i = 0; i < (UDOUBLE)Width * Height; i++) {
        EPD_3IN97_SendData(0XFF);
    }
    EPD_3IN97_SendCommand(0x26);
    for (UDOUBLE i = 0; i < (UDOUBLE)Width * Height; i++) {
        EPD_3IN97_SendData(0XFF);
    }

    EPD_3IN97_TurnOnDisplay();
}

void EPD_3IN97_Clear_Black(void)
{
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x24);
    for (UDOUBLE i = 0; i < (UDOUBLE)Width * Height; i++) {
        EPD_3IN97_SendData(0X00);
    }
    EPD_3IN97_SendCommand(0x26);
    for (UDOUBLE i = 0; i < (UDOUBLE)Width * Height; i++) {
        EPD_3IN97_SendData(0X00);
    }

    EPD_3IN97_TurnOnDisplay();
}


/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
void EPD_3IN97_Display(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x24);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_3IN97_SendData(Image[i + j * Width]);
        }
        DEV_Delay_ms(1);
    }
    EPD_3IN97_TurnOnDisplay();
}

void EPD_3IN97_Display_Base(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x24);
    EPD_3IN97_SendDataBlock(Image, (UDOUBLE)Width * Height);

    EPD_3IN97_SendCommand(0x26);
    EPD_3IN97_SendDataBlock(Image, (UDOUBLE)Width * Height);

    EPD_3IN97_TurnOnDisplay();
}

void EPD_3IN97_Display_Fast(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x24);
    EPD_3IN97_SendDataBlock(Image, (UDOUBLE)Width * Height);

    EPD_3IN97_TurnOnDisplay_Fast();
}

void EPD_3IN97_Display_Fast_Base(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x24);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_3IN97_SendData(Image[i + j * Width]);
        }
        DEV_Delay_ms(1);
    }

    EPD_3IN97_SendCommand(0x26);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_3IN97_SendData(Image[i + j * Width]);
        }
        DEV_Delay_ms(1);
    }
    EPD_3IN97_TurnOnDisplay_Fast();
}

void EPD_3IN97_V2_Display_Window(const UBYTE *Image, UWORD xstart, UWORD ystart, UWORD image_width, UWORD image_heigh)
{
	unsigned long i, j;
	UWORD Width, Height;
	Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
	Height = EPD_3IN97_HEIGHT;
	
	EPD_3IN97_SendCommand(0x24);
	for(i=0; i<Height; i++) {
		for(j=0; j<Width; j++) {
			if(i<image_heigh+ystart && i>=ystart && j<(image_width+xstart)/8 && j>=xstart/8) {
				EPD_3IN97_SendData(Image[(j-xstart/8) + (image_width/8*(i-ystart))]);
			}
			else {
				EPD_3IN97_SendData(0xff);
			}
		}
        DEV_Delay_ms(1);
	}
	EPD_3IN97_TurnOnDisplay();
}

void EPD_3IN97_V2_Display_Window_Base(const UBYTE *Image, UWORD xstart, UWORD ystart, UWORD image_width, UWORD image_heigh)
{
	unsigned long i, j;
	UWORD Width, Height;
	Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
	Height = EPD_3IN97_HEIGHT;
	
	EPD_3IN97_SendCommand(0x24);
	for(i=0; i<Height; i++) {
		for(j=0; j<Width; j++) {
			if(i<image_heigh+ystart && i>=ystart && j<(image_width+xstart)/8 && j>=xstart/8) {
				EPD_3IN97_SendData(Image[(j-xstart/8) + (image_width/8*(i-ystart))]);
			}
			else {
				EPD_3IN97_SendData(0xff);
			}
		}
        DEV_Delay_ms(1);
	}

    EPD_3IN97_SendCommand(0x26);
	for(i=0; i<Height; i++) {
		for(j=0; j<Width; j++) {
			if(i<image_heigh+ystart && i>=ystart && j<(image_width+xstart)/8 && j>=xstart/8) {
				EPD_3IN97_SendData(Image[(j-xstart/8) + (image_width/8*(i-ystart))]);
			}
			else {
				EPD_3IN97_SendData(0xff);
			}
		}
        DEV_Delay_ms(1);
	}
	EPD_3IN97_TurnOnDisplay();
}

/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
void EPD_3IN97_Display_Partial(const UBYTE *Image, UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
	if((Xstart % 8 + Xend % 8 == 8 && Xstart % 8 > Xend % 8) || Xstart % 8 + Xend % 8 == 0 || (Xend - Xstart)%8 == 0)
    {
        Xstart = Xstart / 8 ;
        Xend = Xend / 8;
    }
    else
    {
        Xstart = Xstart / 8 ;
        Xend = Xend % 8 == 0 ? Xend / 8 : Xend / 8 + 1;
    }
    

    UWORD i, Width;
	Width = Xend -  Xstart;
	UDOUBLE IMAGE_COUNTER = Width * (Yend-Ystart);

    Xend -= 1;
	Yend -= 1;	

    EPD_3IN97_Reset();

    EPD_3IN97_SendCommand(0x18);
    EPD_3IN97_SendData(0x80);

    EPD_3IN97_SendCommand(0x3C);
    EPD_3IN97_SendData(0x80);

	EPD_3IN97_SendCommand(0x44);
    EPD_3IN97_SendData((Xstart*8) & 0xFF);
    EPD_3IN97_SendData(((Xstart*8) >> 8) & 0xFF);
    EPD_3IN97_SendData((Xend*8) & 0xFF);
    EPD_3IN97_SendData(((Xend*8) >> 8) & 0xFF);
	
    EPD_3IN97_SendCommand(0x45);
    EPD_3IN97_SendData(Ystart & 0xFF);
    EPD_3IN97_SendData((Ystart >> 8) & 0xFF);
    EPD_3IN97_SendData(Yend & 0xFF);
    EPD_3IN97_SendData((Yend >> 8) & 0xFF);
    
    EPD_3IN97_SendCommand(0x4E); 
    EPD_3IN97_SendData((Xstart*8) & 0xFF);
    EPD_3IN97_SendData(((Xstart*8) >> 8) & 0xFF);
	
    EPD_3IN97_SendCommand(0x4F);
    EPD_3IN97_SendData(Ystart & 0xFF);
    EPD_3IN97_SendData((Ystart >> 8) & 0xFF);

	EPD_3IN97_SendCommand(0x24);   //Write Black and White image to RAM
    for (i = 0; i < IMAGE_COUNTER; i++) {
	    EPD_3IN97_SendData(Image[i]);
	}

	EPD_3IN97_TurnOnDisplay_Part();
}

/******************************************************************************
function :	Differential full-frame partial refresh
parameter:
    OldImage : the frame currently shown (RAM 0x26 baseline)
    NewImage : the new frame (RAM 0x24)
Info    :
    SSD1677 differential update. Earlier attempts (v0.1.14-v0.1.17) that
    declared their own RAM window or reset the chip desynced the 0x24/0x26
    gate traversal from this panel's reversed-gate layout and sprayed noise.
    This variant re-asserts the EXACT register state EPD_3IN97_Init() leaves
    (0x11=0x01 x-inc/y-dec, 0x44 full X, 0x45 end-first full Y, counters 0) -
    the same state the known-good full refresh EPD_3IN97_Display_Base writes
    its frames with - so both RAM banks use identical, verified gate mapping.
    The baseline frame is written to 0x26 from a caller-supplied copy of the
    last pushed frame, the new frame to 0x24, then the panel is kicked with
    GxEPD2's partial LUT for this exact panel (GDEM0397T81, _Update_Part):
    0x21=0x00,0x00 (RED normal) + 0x22=0xFC, and BUSY is waited out.
    EXPERIMENTAL: 0x1A=0x6A (fast temperature compensation, the value
    WaveShare's fast mode uses) is written before the kick to try to cut the
    partial waveform's busy time. 0x3C is deliberately left at Init's 0x01
    (GxEPD2 does not touch it for partials).
******************************************************************************/
void EPD_3IN97_DisplayPartial_Diff(const UBYTE *OldImage, const UBYTE *NewImage)
{
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x11); //data entry mode - same as Init
    EPD_3IN97_SendData(0x01);    //x increase, y decrease

    EPD_3IN97_SendCommand(0x44); //set Ram-X address start/end position (full)
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)%256);
    EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)/256);

    EPD_3IN97_SendCommand(0x45); //set Ram-Y address start/end position (full)
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendData(0x00);

    EPD_3IN97_SendCommand(0x4E); //set RAM x address counter to 0
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendCommand(0x4F); //set RAM y address counter to 0
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendData(0x00);

    EPD_3IN97_SendCommand(0x26); //baseline: what is currently shown
    EPD_3IN97_SendDataBlock(OldImage, (UDOUBLE)Width * Height);

    EPD_3IN97_SendCommand(0x24); //new frame
    EPD_3IN97_SendDataBlock(NewImage, (UDOUBLE)Width * Height);

    EPD_3IN97_SendCommand(0x21); //display update control: RED normal, single panel
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendData(0x00);
    EPD_3IN97_SendCommand(0x1A); //temperature register: fast waveform (experimental)
    EPD_3IN97_SendData(0x6A);
    EPD_3IN97_SendCommand(0x22); //display update sequence: partial LUT
    EPD_3IN97_SendData(0xFC);
    EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusyNoPad();
}

void EPD_3IN97_Display_4Gray(const UBYTE *Image)
{
    UDOUBLE i,j,k;
    UBYTE temp1,temp2,temp3;
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;
    UDOUBLE IMAGE_COUNTER = Width * Height;
    // old  data
    EPD_3IN97_SendCommand(0x24);
    for(i=0; i<IMAGE_COUNTER; i++) { 
        temp3=0;
        for(j=0; j<2; j++) {
            temp1 = Image[i*2+j];
            for(k=0; k<2; k++) {
                temp2 = temp1&0xC0;
                if(temp2 == 0xC0)
                    temp3 |= 0x00;
                else if(temp2 == 0x00)
                    temp3 |= 0x01; 
                else if(temp2 == 0x80)
                    temp3 |= 0x01; 
                else //0x40
                    temp3 |= 0x00; 
                temp3 <<= 1;

                temp1 <<= 2;
                temp2 = temp1&0xC0 ;
                if(temp2 == 0xC0) 
                    temp3 |= 0x00;
                else if(temp2 == 0x00) 
                    temp3 |= 0x01;
                else if(temp2 == 0x80)
                    temp3 |= 0x01; 
                else    //0x40
                    temp3 |= 0x00;	
                if(j!=1 || k!=1)
                    temp3 <<= 1;

                temp1 <<= 2;
            }
        }
        EPD_3IN97_SendData(temp3);
        // printf("%x",temp3);
    }
    DEV_Delay_ms(1);
    EPD_3IN97_SendCommand(0x26); 
    for(i=0; i<IMAGE_COUNTER; i++) {
        temp3=0;
        for(j=0; j<2; j++) {
            temp1 = Image[i*2+j];
            for(k=0; k<2; k++) {
                temp2 = temp1&0xC0 ;
                if(temp2 == 0xC0)
                    temp3 |= 0x00;//white
                else if(temp2 == 0x00)
                    temp3 |= 0x01;  //black
                else if(temp2 == 0x80)
                    temp3 |= 0x00;  //gray1
                else //0x40
                    temp3 |= 0x01; //gray2
                temp3 <<= 1;

                temp1 <<= 2;
                temp2 = temp1&0xC0 ;
                if(temp2 == 0xC0)  //white
                    temp3 |= 0x00;
                else if(temp2 == 0x00) //black
                    temp3 |= 0x01;
                else if(temp2 == 0x80)
                    temp3 |= 0x00; //gray1
                else    //0x40
                    temp3 |= 0x01;	//gray2
                if(j!=1 || k!=1)
                    temp3 <<= 1;

                temp1 <<= 2;
            }
        }
        EPD_3IN97_SendData(temp3);
        // printf("%x",temp3);
    }
    EPD_3IN97_TurnOnDisplay_4GRAY();
}

void EPD_3IN97_WritePicture_4Gray(const UBYTE *Image)
{
    UDOUBLE i,j,k,o;
    UBYTE temp1,temp2,temp3;
    UWORD Width, Height;
	Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
	Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x24);
    for(i=0; i<Height; i++) {
        for(o=0; o<Width; o++) {
            if((o < Width/4)&&(i < Height/2))
            {
                temp3=0;
                for(j=0; j<2; j++) {
                    temp1 = Image[(i*Width/4+o)*2+j];
                    for(k=0; k<2; k++) {
                        temp2 = temp1&0xC0;
                        if(temp2 == 0xC0)
                            temp3 |= 0x00;
                        else if(temp2 == 0x00)
                            temp3 |= 0x01; 
                        else if(temp2 == 0x80)
                            temp3 |= 0x01; 
                        else //0x40
                            temp3 |= 0x00; 
                        temp3 <<= 1;

                        temp1 <<= 2;
                        temp2 = temp1&0xC0 ;
                        if(temp2 == 0xC0) 
                            temp3 |= 0x00;
                        else if(temp2 == 0x00) 
                            temp3 |= 0x01;
                        else if(temp2 == 0x80)
                            temp3 |= 0x01; 
                        else    //0x40
                            temp3 |= 0x00;	
                        if(j!=1 || k!=1)
                            temp3 <<= 1;

                        temp1 <<= 2;
                    }

                }
                EPD_3IN97_SendData(temp3);
                // printf("%x",temp3);
            }
            else
            {
                EPD_3IN97_SendData(0x00);
            }
        }
    }

    DEV_Delay_ms(1);
    EPD_3IN97_SendCommand(0x26);
    for(i=0; i<Height; i++) {
        for(o=0; o<Width; o++) {
            if((o < Width/4)&&(i < Height/2))
            {
                for(j=0; j<2; j++) {
                    temp1 = Image[(i*Width/4+o)*2+j];
                    for(k=0; k<2; k++) {
                        temp2 = temp1&0xC0 ;
                        if(temp2 == 0xC0)
                            temp3 |= 0x00;//white
                        else if(temp2 == 0x00)
                            temp3 |= 0x01;  //black
                        else if(temp2 == 0x80)
                            temp3 |= 0x00;  //gray1
                        else //0x40
                            temp3 |= 0x01; //gray2
                        temp3 <<= 1;

                        temp1 <<= 2;
                        temp2 = temp1&0xC0 ;
                        if(temp2 == 0xC0)  //white
                            temp3 |= 0x00;
                        else if(temp2 == 0x00) //black
                            temp3 |= 0x01;
                        else if(temp2 == 0x80)
                            temp3 |= 0x00; //gray1
                        else    //0x40
                            temp3 |= 0x01;	//gray2
                        if(j!=1 || k!=1)
                            temp3 <<= 1;

                        temp1 <<= 2;
                    }
                }
                EPD_3IN97_SendData(temp3);
                // printf("%x",temp3);
            }
            else
            {
                EPD_3IN97_SendData(0x00);
            }
        }
    }
    EPD_3IN97_TurnOnDisplay_4GRAY();
}


/******************************************************************************
function :	Enter sleep mode
parameter:
******************************************************************************/
void EPD_3IN97_Sleep(void)
{
    EPD_3IN97_SendCommand(0x10); //enter deep sleep
    EPD_3IN97_SendData(0x01);
    DEV_Delay_ms(100);
}
