/***********************************************************************************

    Filename:    simple_hid_dongle.c

    Description: Main program of HID dongle.

***********************************************************************************/

/***********************************************************************************
* INCLUDES
*/
#include "hal_defs.h"
#include "hal_board.h"
#include "hal_led.h"
#include "hal_mcu.h"
#include "usb_audio.h"
#include "usb_framework.h"
#include "usb_audio_controls.h"
#include "twoway.h"
#include "stdlib.h"


/***********************************************************************************
* LOCAL VARIABLES
*/

UINT8  __xdata fscal3[4];
UINT8  __xdata fscal2[4];
UINT8  __xdata fscal1[4];
UINT8  __xdata freqest;
UINT8  __xdata RFIF_good = 0;
UINT8  __xdata RFIF_bad = 0;
UINT8  __xdata dmaarm_radio = 0;
UINT8  __xdata time_of_srx;
INT16  __xdata freq_offset_abs;             // absolute value of frequency offset
INT32  __xdata freq_offset;
UINT32 __xdata base_freq_in_khz = BASE_FREQUENCY_IN_KHZ;
UINT16 __xdata tics_sense_1stpkt = 0;
UINT8  __xdata start_counting = 0;

UINT8 volatile __xdata activeOut = 0;       // Audio Out active buffer (0 | 1)
UINT8  __xdata audioIn[2*ADC_SAMPLES];      // Audio in from remote microphone (96 samples, 192 bytes)
UINT8  __xdata USB_fifo_all[4*96];          // Audio data in USB format (24 repeated samples = 48 samples)

__xdata struct AUDIO_OUT_STRUCT audioOut;

BOOL  waitingforbeacon = TRUE;
UINT8 __xdata lostpkts = 0;

volatile BYTE __xdata rxStatus = 0;
volatile BYTE __xdata txStatus = 0;

UINT8 whichactive;

UINT16 test_data;           // DEBUG
UINT16 confirmedcrc = 0;    // DEBUG
UINT16 crcerror = 0;

// External variables

extern const UINT8 HoppingChannels[TOTAL_NUM_CHANNELS];
const char channel[3][4] = {{ 0, 20,  4, 16},     // Band 1; f = 902.5 MHz, 907.5 MHz, 903.5 MHz, 906.5 MHz
                            {74, 98, 78, 94},     // Band 2; f = 921.0 MHz, 927.0 MHz, 922.0 MHz, 926.0 MHz
                            { 8, 82, 12, 90}};    // Band 3; f = 904.5 MHz, 923.0 MHz, 905.5 MHz, 925.0 MHz

// Active table index variables 

UINT8 __xdata ActiveChIdx, band;

extern int8  null;

/***********************************************************************************
* @fn          main
*
* @brief       This is the main entry of the RF HID application. It sets
*              distinct short addresses for the nodes, initalises and runs
*              receiver and sender tasks sequentially in an endless loop.
*
* @return      none
*/
void main(void)
{
  halMcuInit();
  init_peripherals();  
 
  DMA_SET_ADDR_DESC0(&DmaDesc0);          // Set up DMA configuration table for channel 0
  DMA_SET_ADDR_DESC1234(&DmaDesc1_4[0]);  // Set up DMA configuration table for channels 1 - 4
  dmaToUSBfifo();                         // Set up DMA Channel 0 to transfer data into the USB fifo
  dmaToAudioIn();                         // Set up DMA Channel 3 to transfer data into the AudioIn buffer
  dmaToAudioOut();                        // Set up DMA Channel 4 to transfer data from the AudioIn buffer into the active AudioOut buffer
  
  initRf();                               // Set radio base frequency and reserve DMA channel 1 for RX buffers
 
// Initilize Timer 2 (Frame Timer)

  T2CT = 0;         // Stop the timer
  T2PR = 6;         // Timer Prescale Multipler
  T2CTL = 0x01;     // Set Tick period to 128 * T2PR clock cycles (128*6/26.0 = 29.538 usec)   
 
  macTimer3Init();
  
// Initialise USB

  usbAudioInit();
   
//  Main processing loop
  while (TRUE) {
    if ((start_counting > 0) && (tics_sense_1stpkt < 3000))           // number of 4 msec 'tics' since 1st pkt was received
      tics_sense_1stpkt++;
      
    usbAudioProcessEvents();                // Process USB standard requests 
     
    if (waitingforbeacon) {
      CHANNR = channel[0][ActiveChIdx];     // Listen on channel[0] for the beacon
      FSCAL3 |= 0x20;                       // Enable charge pump calibration
      SCAL();                               // Start PLL calibration at new channel
      while (MARCSTATE != MARCSTATE_IDLE);  // Wait for calibration to complete

      while (1) {                           // Stay in this loop until the Beacon is heard
        ListenforMaster(&SLVrxData, LISTEN_FOR_BEACON_TIMEOUT, DMA_RX, RX_PAYLOAD_LEN);
        if (rxPacketStatus == PKT_OK)
          break;
        usbAudioProcessEvents();            // Process USB standard requests 
      }   // end of 'while (1)' loop

      freq_offset = (INT32) FREQEST;
      if ((UINT8) FREQEST < 127)                  // if FREQEST is positive ...   
        freq_offset_abs = FREQEST;
      else {                                      // FREQEST is a negative number
        freq_offset_abs = (FREQEST ^ 0xFF) + 1;   // Calculate the absolute value
        freq_offset |= 0xFFFFFF00;                // extend the sign bit
      }
      
      if (freq_offset_abs > 20) {             // if the frequency offset exceeds about 30 kHz
        freq_offset *= 1465;                  // calculate the frequency offset in kHz
        freq_offset /= 1000;
        base_freq_in_khz += freq_offset;      // and adjust the base frequency
        SetRadioFrequency(base_freq_in_khz);
        SCAL();                               // Start PLL calibration at adjusted base frequency
        while (MARCSTATE != MARCSTATE_IDLE);  // Wait for calibration to complete
      }
      else {
        lostpkts = 0;
        ActiveChIdx = 1;                      // set up next channel
        waitingforbeacon = FALSE;             // Exit 'waitingforbeacon' mode
        T2CT = FRAME_TIMEOUT_DEFAULT;         // Reset the frame timer
        FSCAL3 &= ~0x30;                      // Disable charge pump calibration
        start_counting++;
      }
    }     // end of 'waitingforbeacon' if
    else {
      while (T2CT > LISTENFORMASTER);       // Wait until it's time to listen for the Master

      P0 &= ~0x0A;               // Reset pins P0_1 and P0_3 (JP4 pins 5 and 7)
            
// Turn on Receiver       
      
      rxStatus = RX_WAIT;                       // Set rxStatus, will be updated if SFD detected
      RFIM = 0x00;                              // Disable all RF interrupts
      RFIF = 0x00;                              // Clear all RF interrupts
      INT_SETFLAG(INUM_RF, INT_CLR);            // Clearing main RF interrupt flags in S1CON[1:0]
      INT_SETFLAG(INUM_DMA, INT_CLR);
      RFTXRXIF = 0x00;

      if ((DMAARM & DMA_CHANNEL_1) != 0x00)     // If DMA Channel 1 is busy ...
        DMA_ABORT_CHANNEL(DMA_RX);              // Abort DMA channel 1

      DMA_ARM_CHANNEL(DMA_RX);                  // Arm DMA channel
      
      P0 |= 0x10;                               // Set pin P0_5 (JP4 pin 4)
      time_of_srx = T2CT;
      
      SRX();                                    // Strobe RX
  
      while (1) {                               // Loop until SYNC is detected or the SYNC timeout value has been reached
//        if ((P1 & GDO0MSK) > 0) {
        if ((PKTSTATUS & 0x08) > 0) {
          rxStatus = SYNC_DETECTED;
          P0 |= 0x01;                           // Set PO_0 (JP4 pin 8)
          break;
        }
        if (T2CT < RX_SYNC_TIMEOUT) {
          rxStatus = SYNC_TIMEOUT;
          SIDLE();                              // Shut down the receiver
          DMA_ABORT_CHANNEL(DMA_RX);            // Abort DMA channel
          break;
        }
      }

// While we are waiting for the End of the Packet to arrive, take the 96 samples last received and move them into the USB buffers
// Note that each sample will be used twice, since the Transmitter samples at 24 kHz, whereas the USB driver expects samples at a 48 kHz rate      
 
      int j = 0;
      
      for (int k = 0; k < 192; k+=2) {
        USB_fifo_all[j+1] = USB_fifo_all[j+3] = audioIn[k];
        USB_fifo_all[j] = USB_fifo_all[j+2] = audioIn[k+1];
        j += 4;
      }

      INT_GLOBAL_ENABLE(INT_OFF);         // Disable all interrupts
      
      SET_WORD(DmaDesc1_4[3].SRCADDRH, DmaDesc1_4[3].SRCADDRL, &USB_fifo_all[0]);    // Set the USB_fifo_all as the source
      if (activeOut == 1) {
        SET_WORD(DmaDesc1_4[3].DESTADDRH, DmaDesc1_4[3].DESTADDRL, audioOut.A.fifoA);
      }
      else {
        SET_WORD(DmaDesc1_4[3].DESTADDRH, DmaDesc1_4[3].DESTADDRL, audioOut.B.fifoA);
      }

      DMAARM |= DMA_CHANNEL_4;
      DMAREQ |= DMA_CHANNEL_4;                    // Enable memory-to-memory transfer using DMA channel 4
      while ((DMAARM & DMA_CHANNEL_4) > 0);       // Wait for transfer to complete
            
      SET_WORD(DmaDesc1_4[3].SRCADDRH, DmaDesc1_4[3].SRCADDRL, &USB_fifo_all[192]);    // Set the USB_fifo_all as the source
      if (activeOut == 1)
        SET_WORD(DmaDesc1_4[3].DESTADDRH, DmaDesc1_4[3].DESTADDRL, audioOut.A.fifoC);
      else
        SET_WORD(DmaDesc1_4[3].DESTADDRH, DmaDesc1_4[3].DESTADDRL, audioOut.B.fifoC);
           
      DMAARM |= DMA_CHANNEL_4;
      DMAREQ |= DMA_CHANNEL_4;                  // Enable memory-to-memory transfer using DMA channel 4
      while ((DMAARM & DMA_CHANNEL_4) > 0);     // Wait for transfer to complete

      INT_GLOBAL_ENABLE(INT_ON);                // Re-enable all interrupts

      while (rxStatus == SYNC_DETECTED) {       // SYNC has been detected. Loop until end of packet or the T2 Timer reaches 0
//        if ((P1 & GDO0MSK) == 0) {              // IF GDO0 drops, end of packet has been reached
        if (MARCSTATE == MARCSTATE_IDLE)        // Wait for Rx to complete
          break;
//        }
        if (T2CT == 0) {                        // if T2 reaches 0, a timeout error has occurred
          SIDLE();                              // Shut down the receiver
          DMA_ABORT_CHANNEL(DMA_RX);            // Abort DMA channel
          rxStatus = PKT_TIMEOUT;               // Return Timeout error
        }
      }
      P0 &= ~0x01;                              // Reset PO_0 (JP4 pin 8)    
    
      if ((rxStatus == SYNC_TIMEOUT) || (rxStatus == PKT_TIMEOUT)) {
        P0 |= 0x08;               // Set P0_3 (JP4 pin 5)
        rxPacketStatus = TIMEOUT_ERROR;
        while (T2CT > END_OF_FRAME);            // let the frame timer run down to the end of a frame
      }
      else if ((PKTSTATUS & CRC_OK_MASK) == 0) {
        rxPacketStatus = CRC_ERROR;                         // Return CRC error
//        test_data = 0x0000;
//        for (int i = 0; i < ADC_SAMPLES; i++) {
//          if (SLVrxData.payload[i] != test_data)
//            confirmedcrc++;
//          test_data += 0x0101;
//        }
        if (tics_sense_1stpkt < 3000)
          crcerror++;
        else {
          if (crcerror > 6) {           // if more tha 6 CRC erros occured in the first 12 seconds following program statrt ...
            WDCTL = 0x03;               // Set watchdog timer interval to 64 periods (approximately 2 msec)
            WDCTL |= 0x08;              // Enable the watchdog timer
            while (1);                  // Allow watchdog timer to time out
          }
        }
        P0 |= 0x02;               // Set P0_1 (JP4 pin 7)
 #ifdef BLINK_ON_CRC 
        P1 |= 0x02;               // Light the LED
#endif          
      }
      else {
        rxPacketStatus = PKT_OK;
        RFIF_good = RFIF;
      }
     
      T2CT = FRAME_TIMEOUT_DEFAULT;       // Reset the Frame Timer
    }     // end of waitingforbeacon 'else' loop
    
    P0 &= ~0x10;                // Reset pin P0_5 (JP4 pin 4)
    
    SIDLE();          // Force the Receiver into the Idle state

// Set the next channel at start calibration

    CHANNR = (unsigned char)channel[ActiveChIdx];     
    ActiveChIdx = (ActiveChIdx + 1) & 0x03;
    SCAL();
    
    if (rxPacketStatus == TIMEOUT_ERROR) {

// fill the audioIn buffer with 'nulls', so as to mute the audio

      SET_WORD(DmaDesc1_4[2].SRCADDRH, DmaDesc1_4[2].SRCADDRL, &null);     // Set the address of 'null' as the source address
      DmaDesc1_4[2].SRCINC = SRCINC_0;            // Source address is constant
      DMAARM |= DMA_CHANNEL_3;
      DMAREQ |= DMA_CHANNEL_3;                    // Enable memory-to-memory transfer using DMA channel 3
      while ((DMAARM & DMA_CHANNEL_3) > 0);       // Wait for transfer to complete
      
      lostpkts++;
      if (lostpkts > 4) {           // if 4 consecutive packets have been lost
        lostpkts = 0;
        waitingforbeacon = TRUE;    // enter 'waiting for beacon' (resync) mode
      }
    }
    else {                          // A packet was received
      lostpkts = 0;
      
      if (rxPacketStatus == PKT_OK) {                       // if a packet was sucessfully received ...
        
// Transfer audio data from the receive buffer to the audioIn buffer using DMA        
       
        SET_WORD(DmaDesc1_4[2].SRCADDRH, DmaDesc1_4[2].SRCADDRL, SLVrxData.payload);     // Set the rx data buffer as the source address
        DmaDesc1_4[2].SRCINC = SRCINC_1;            // Increment Source address
        DMAARM |= DMA_CHANNEL_3;
        DMAREQ |= DMA_CHANNEL_3;                    // Enable memory-to-memory transfer using DMA channel 3
        while ((DMAARM & DMA_CHANNEL_3) > 0);       // Wait for transfer to complete
      }
      else {

// fill the audioIn buffer with 'nulls', so as to mute the audio

        SET_WORD(DmaDesc1_4[2].SRCADDRH, DmaDesc1_4[2].SRCADDRL, &null);     // Set the address of 'null' as the source address
        DmaDesc1_4[2].SRCINC = SRCINC_0;            // Source address is constant
        DMAARM |= DMA_CHANNEL_3;
        DMAREQ |= DMA_CHANNEL_3;                    // Enable memory-to-memory transfer using DMA channel 3
        while ((DMAARM & DMA_CHANNEL_3) > 0);       // Wait for transfer to complete
      
      }
    }     // end of TIMEOUT_ERROR 'else' (i.e., packet received)
    while (MARCSTATE != MARCSTATE_IDLE);            // Wait for calibration to complete 
  }
}


/*
+------------------------------------------------------------------------------
|  Copyright 2004-2007 Texas Instruments Incorporated. All rights reserved.
|
|  IMPORTANT: Your use of this Software is limited to those specific rights
|  granted under the terms of a software license agreement between the user who
|  downloaded the software, his/her employer (which must be your employer) and
|  Texas Instruments Incorporated (the "License"). You may not use this Software
|  unless you agree to abide by the terms of the License. The License limits
|  your use, and you acknowledge, that the Software may not be modified, copied
|  or distributed unless embedded on a Texas Instruments microcontroller or used
|  solely and exclusively in conjunction with a Texas Instruments radio
|  frequency transceiver, which is integrated into your product. Other than for
|  the foregoing purpose, you may not use, reproduce, copy, prepare derivative
|  works of, modify, distribute, perform, display or sell this Software and/or
|  its documentation for any purpose.
|
|  YOU FURTHER ACKNOWLEDGE AND AGREE THAT THE SOFTWARE AND DOCUMENTATION ARE
|  PROVIDED �AS IS� WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED,
|  INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE,
|  NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
|  TEXAS INSTRUMENTS OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT,
|  NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER
|  LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES INCLUDING
|  BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
|  CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
|  SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
|  (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
|
|  Should you have any questions regarding your right to use this Software,
|  contact Texas Instruments Incorporated at www.TI.com.
|
+------------------------------------------------------------------------------
*/





