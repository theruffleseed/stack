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
#include <string.h> // memset (FillRamWindow)

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

/******************************************************************************
function :	Wait until the busy_pin goes LOW
parameter:
******************************************************************************/
static void EPD_3IN97_ReadBusy(void)
{
    Debug("e-Paper busy\r\n");
    // BUSY asserts within microseconds of the triggering command on this
    // controller; a short settle is plenty. (The Waveshare stock code waited
    // a fixed 100ms first, which added ~300ms of dead time to every init.)
    DEV_Delay_ms(10);
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

UBYTE EPD_3IN97_IsBusy(void)
{
    return DEV_Digital_Read(EPD_BUSY_PIN); // LOW: idle, HIGH: busy
}

/******************************************************************************
function :  Block-write `len` bytes of image data (DC high, CS held for the
            whole run). Used for frame/window payloads instead of the stock
            per-byte loop, which bit-banged GPIO at ~4us/byte.
******************************************************************************/
static void EPD_3IN97_SendDataBlock(const UBYTE *data, UDOUBLE len)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteBlock(data, len);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :  Set the RAM address window + write counters. Pixel coordinates;
            X must be 8-aligned; window is half-open [start,end). Display_*
            call this themselves so they never depend on whatever window a
            previous partial update left behind.
******************************************************************************/
static void EPD_3IN97_SetRamWindow(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
    EPD_3IN97_SendCommand(0x44); //set Ram-X address start/end position
    EPD_3IN97_SendData(Xstart & 0xFF);
    EPD_3IN97_SendData((Xstart >> 8) & 0xFF);
    EPD_3IN97_SendData((Xend - 1) & 0xFF);
    EPD_3IN97_SendData(((Xend - 1) >> 8) & 0xFF);

    EPD_3IN97_SendCommand(0x45); //set Ram-Y address start/end position
    EPD_3IN97_SendData(Ystart & 0xFF);
    EPD_3IN97_SendData((Ystart >> 8) & 0xFF);
    EPD_3IN97_SendData((Yend - 1) & 0xFF);
    EPD_3IN97_SendData(((Yend - 1) >> 8) & 0xFF);

    EPD_3IN97_SendCommand(0x4E); // set RAM x address counter
    EPD_3IN97_SendData(Xstart & 0xFF);
    EPD_3IN97_SendData((Xstart >> 8) & 0xFF);
    EPD_3IN97_SendCommand(0x4F); // set RAM y address counter
    EPD_3IN97_SendData(Ystart & 0xFF);
    EPD_3IN97_SendData((Ystart >> 8) & 0xFF);
}

// Writes a packed window of image data to `ramCmd` (0x24 new / 0x26 old).
static void EPD_3IN97_WriteRamWindow(UBYTE ramCmd, const UBYTE *Image,
                                     UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
    EPD_3IN97_SetRamWindow(Xstart, Ystart, Xend, Yend);
    EPD_3IN97_SendCommand(ramCmd);
    EPD_3IN97_SendDataBlock(Image, (UDOUBLE)((Xend - Xstart) / 8) * (Yend - Ystart));
}

// Fills a RAM window with a constant byte (for Clear/Clear_Black).
static void EPD_3IN97_FillRamWindow(UBYTE ramCmd, UBYTE fill,
                                    UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
    EPD_3IN97_SetRamWindow(Xstart, Ystart, Xend, Yend);
    EPD_3IN97_SendCommand(ramCmd);
    UBYTE chunk[128];
    memset(chunk, fill, sizeof(chunk));
    UDOUBLE remaining = (UDOUBLE)((Xend - Xstart) / 8) * (Yend - Ystart);
    while (remaining > 0) {
        UWORD n = remaining > sizeof(chunk) ? sizeof(chunk) : (UWORD)remaining;
        EPD_3IN97_SendDataBlock(chunk, n);
        remaining -= n;
    }
}

/******************************************************************************
function :  Kick a display update (0x22 mode + 0x20 master activation) and
            wait ONLY until the panel asserts BUSY, then return - the caller
            polls EPD_3IN97_IsBusy() for completion instead of blocking.
            Returns 1 if BUSY asserted, 0 on timeout (no refresh running).
******************************************************************************/
static UBYTE EPD_3IN97_TriggerRefresh(UBYTE mode)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(mode);
    EPD_3IN97_SendCommand(0x20);
    for (int i = 0; i < 100; i++) {
        if (EPD_3IN97_IsBusy()) return 1;
        DEV_Delay_ms(1);
    }
    return 0;
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
    EPD_3IN97_ReadBusy();
}

static void EPD_3IN97_TurnOnDisplay_Fast(void)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(0xD7);
	EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusy();
}

static void EPD_3IN97_TurnOnDisplay_4GRAY(void)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(0xD7);
	EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusy();
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
    EPD_3IN97_FillRamWindow(0x24, 0xFF, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
    EPD_3IN97_FillRamWindow(0x26, 0xFF, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
    EPD_3IN97_TurnOnDisplay(); // blocking: boot/shutdown path only
}

void EPD_3IN97_Clear_Black(void)
{
    EPD_3IN97_FillRamWindow(0x24, 0x00, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
    EPD_3IN97_FillRamWindow(0x26, 0x00, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
    EPD_3IN97_TurnOnDisplay(); // blocking: boot/shutdown path only
}


/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
// All Display_* variants set their own RAM window/counters (never rely on a
// previous init), push the frame with hardware-SPI block writes, and only
// KICK the refresh - they return as soon as BUSY asserts. Poll
// EPD_3IN97_IsBusy() for completion. Return value: 1 = refresh running.
UBYTE EPD_3IN97_Display(const UBYTE *Image)
{
    EPD_3IN97_WriteRamWindow(0x24, Image, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
    return EPD_3IN97_TriggerRefresh(0xF7);
}

UBYTE EPD_3IN97_Display_Base(const UBYTE *Image)
{
    // Both RAMs get the same frame: after the refresh, "old" (0x26) matches
    // the screen exactly, so later differential partial updates are correct.
    EPD_3IN97_WriteRamWindow(0x24, Image, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
    EPD_3IN97_WriteRamWindow(0x26, Image, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
    return EPD_3IN97_TriggerRefresh(0xF7);
}

UBYTE EPD_3IN97_Display_Fast(const UBYTE *Image)
{
    // Writes only 0x24 - the caller must run EPD_3IN97_SyncOldRam() once the
    // refresh completes, or partial updates will ghost against a stale base.
    EPD_3IN97_WriteRamWindow(0x24, Image, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
    return EPD_3IN97_TriggerRefresh(0xD7);
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
function :	Partial (differential-waveform) update. FULL-FRAME despite its
            window parameters: the SSD1677 on this panel works reliably with
            the full-frame convention, and a true windowed partial would need
            the RAM Y window declared gate-reversed (see GxEPD2
            _setPartialRamArea for the panel's quirk) plus matching packed
            data. No hardware reset is done here - a reset clears the data
            entry mode (0x11) back to its default and would desync this 0x24
            write from the 0x26 sync that uses the forward window, so the
            differential LUT would compare mismatched rows and spray noise.
            Instead the same registers Init/Init_Fast leave set are re-asserted
            and the frame is pushed through the same forward full-frame window
            that EPD_3IN97_SyncOldRam() uses for the 0x26 baseline.
parameter :
    Image   : FULL frame buffer, (EPD_3IN97_WIDTH/8)*EPD_3IN97_HEIGHT bytes.
    Window args : ignored (kept for the existing call signature).
    Returns 1 when the refresh triggered (BUSY asserted), else 0. Call
    EPD_3IN97_SyncOldRam() after the refresh completes to keep the
    differential baseline (0x26) in step with what's actually on screen.
******************************************************************************/
UBYTE EPD_3IN97_Display_Partial(const UBYTE *Image, UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
    EPD_3IN97_SendCommand(0x11); // data entry mode
    EPD_3IN97_SendData(0x01);    // x increase, y decrease (same as Init)

    EPD_3IN97_SendCommand(0x18);
    EPD_3IN97_SendData(0x80);

    EPD_3IN97_SendCommand(0x3C); // BorderWaveform: HiZ, required for partial
    EPD_3IN97_SendData(0x80);

    EPD_3IN97_WriteRamWindow(0x24, Image, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
    return EPD_3IN97_TriggerRefresh(0xFF);
}

void EPD_3IN97_SyncOldRam(const UBYTE *Image)
{
    EPD_3IN97_WriteRamWindow(0x26, Image, 0, 0, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT);
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
