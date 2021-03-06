/*
 *******************************************************************************************************
 * Compiler:                IAR C Compiler for 8051                                                    *
 * Target platform:         Texas Instruments CC111/CC2511                                             *
 * Author:                  TBR, KHO, ESY, MJB                                                         *
 *******************************************************************************************************
*/

#ifndef TWOWAY_H
#define TWOWAY_H

//-------------------------------------------------------------------------------------------------------
// Includes

#include "hal.h"
#include "iocc2510.h"

// Radio Base Frequenncy (Channel 0) in kHz

#define BASE_FREQUENCY_IN_KHZ  902500

// Channel spacing in kHz

#define CHANNEL_SPACING_IN_KHZ  250

//-------------------------------------------------------------------------------------------------------
// Compile-time options

//-------------------------------------------------------------------------------------------------------

// Global definitions

// Subroutine Definitions

void init_peripherals(void);

#define LISTEN_FOR_BASE_MODE    0x01
#define PAIRED_WITH_BASE_MODE   0x02

#define MAC_ADDR 0xC3                       // The system's 1 byte MAC address

#define RSSI_OFFSET 77                      // RSSI Offset (dB)

// Number of ADC samples per packet. Each ADC sample is 16 bits (2 bytes).
#define ADC_SAMPLES  96

// Audio frame and buffer size
#define AF_BUF_SIZE  ADC_SAMPLES            // Size of audio buffer in words

// Packet format and segment lengths (bytes)
#define PHY_FIELD_LEN               1       // Length byte
#define MAC_FIELD_LEN               1       // MAC address (NOT TO BE CHANGED FROM DEFAULT VALUE (1) without changing througout the code!)

#define RX_PAYLOAD_LEN  (MAC_FIELD_LEN + ADC_SAMPLES*2)

// Define packet errors
#define PKT_ERROR_CNT_THR               10

// Frequency hopping
#define TOTAL_NUM_CHANNELS   4          // number of channels in the frequency hopping list

// DMA Channels
#define DMA_RX  1
#define DMA_TX  2

#define GDO0MSK 0x20        // GDO0 appears on bit 5 of P1

// Timer flags
#define T3OVFIF                         0x01

// RX Off modes
#define RXOFFMODE_IDLE                  0x00
#define RXOFFMODE_FSTXON                0x04
#define RXOFFMODE_TX                    0x08
#define RXOFFMODE_RX                    0x0C

// Marcstates
#define MARCSTATE_TX                    0x13
#define MARCSTATE_RX                    0x0D
#define MARCSTATE_IDLE                  0x01

// T2 (Frame timer) defines
// Note that T2 is a 'count down' timer; it starts at the vlaue set into T2CT and counts down to zero.
// For the CC2510, T2 Tick period is 128 * T2PR clock cycles (128*6/26.0 = 29.538 usec)
// For the CC2511, T2 Tick period is 128 * T2PR clock cycles (128*6/24.0 = 32.0 usec)
// The Master mode, the frametimer is started (set to 'FRAME_TIMEOUT_DEFAULT') immediatly prior to sending a packet.
// In Slave mode, the frametimer is started (set to 'FRAME_TIMEOUT_DEFAULT') immediately after a packet is sucessfully received

// "FRAME_TIMEOUT_DEFAULT" is the time to collect ADC_SAMPLES samples at the ADC sample rate frequency, plus 250 usec
// "Frame_Time" = sample rate in msec * ADC_SAMPLES
// Examples:
//
// 12 kHz sample rate, 48 samples per packet: Frame_Time = .0833 * 48 = 4.000 msec
// 16 kHz sample rate, 96 samples per packet: Frame_Time = .0625 * 96 = 6.000 msec
// 24 kHz sample rate, 96 samples per packet: Frame_Time = .04167 * 96 = 4.000 msec

#define FRAME_TIMEOUT_DEFAULT           195       // (T2CT) Timeout = 195 * 32.000 us = 6.240 msec
                                                  // Note that this is 250 usec longer than a frame, so T2 should never reach zero
#define END_OF_FRAME                      7       // T2CT count at end of frame (6.000 msec)

// "LISTENFORMASTER" value calculation:
// Packet length (bytes) = 2 * ADC_SAMPLES + preamble bytes + sync bytes + CRC bytes + 3
// Packet length (usec) = packet length (bytes) * 8000 / data_rate in kbps
// Start Listening for Master approximately 250 usec before it is sent. This should occurr when T2CT
// Reaches a vaulue of 'FRAME_TIMEOUT_DEFAULT' minus 'END_OF_FRAME' tics
//
// Examples:
//
// data rate = 300 kbps, sample rate = 16 kHz, ADC samples = 96 
// LISTENFORMASTER = 195 - 10 = 185

#define LISTENFORMASTER      185      // Start listening for Master 250 usec before a packet is expected 

#define RX_SYNC_TIMEOUT       82      // If SYNC has not been detected by this time (in T2 tics), abort the read 

// T3 Timeout defines (First parameter is timeout in 5.3333 us tics, Second parameter is multiplier since this is a 8 bit timer only)
// For RX, the T3 timer sets the point (with reference to RX turn on) at which RX activity is checked. This is done by
// looking for high level on P1_5 (GDO0), which will be high from the time that SYNC is detected until the end of a pack

// The 'packet length', in usec, is determined by the lenght of the Tx\Rx buffer, the number of 'overhead' (preamble, SYNC, and CRC) bytes
// and the RD data rate
// Herein, the number of bytes in the packet is PREAMBLE + SYNC + PHY_FIELD_LEN + MAC_FIELD_LEN + ADC_SAMPLES*2 + CRC
// (4 + 4 + 1 + 1 + 192 + 2) = 204 bytes
// Packet length = 109 * 8000 / 300 = 5440 usec
// GDO0 goes high after SYNC is sent/received and low after CRC is sent/received so
// GDO0 length = 196 * 8000 / 300 = 5226.7 usec

#define LISTEN_FOR_BEACON_TIMEOUT       188, 26   // Timeout = 188 *5.3333 us * 26 = 26.069 msec

// TX status flags
#define TX_IN_PROGRESS                  0x80
#define TX_SUCCESSFUL                   0x40
#define DEST_UNREACHABLE                0x20
#define TX_WAIT                         0x10
#define TX_IDLE                         0x00

// RX status flags
#define RX_IN_PROGRESS                  0x80
#define PACKET_RECEIVED                 0x40
#define RX_WAIT                         0x20
#define RX_COMPLETE                     0x10
#define SYNC_DETECTED                   0x08
#define SYNC_TIMEOUT                    0x04
#define PKT_TIMEOUT                     0x02
#define RX_IDLE                         0x00

// RX packet/channel status
#define PKT_OK                          0x01
#define CRC_ERROR                       0x02
#define	TIMEOUT_ERROR                   0x04
#define PKT_ERROR                       0x08
#define DMA_ERROR                       0x10

// Packet status values/bits
#define CRC_OK_MASK     0x80
#define CRC_LQI_VAL_IDX 1
#define RSSI_VAL_IDX 0

//----------------------------

// RX struct
typedef struct {
  BYTE macPayloadLen;                     // Length byte
  BYTE macField;                          // MAC address
  UINT16 payload[ADC_SAMPLES];            // Audio samples
} __xdata RX_SLAVE_STRUCT;

//-------------------------------------------------------------------------------------------------------
// Function prototype declarations

// init_peripherals.c
void init_peripherals(void);

// tw_dma.c
void dmaFromRadio(WORD length, WORD dstAddr);
void dmaToAudioIn(void);
void dmaToUSBfifo(void);
void dmaToAudioOut(void);

// tw_rf.c
void SetRadioFrequency(UINT32 frequency);
BOOL initRf(void);
BOOL rfSendPacket(UINT8 timeout, UINT8 multiplier);
void rfReceivePacket(RX_SLAVE_STRUCT __xdata * rxData, UINT8 synctimeout, UINT8 t3_multiplier, BYTE dmaNumber, UINT8 packetlen, UINT8 timeout);
void ListenforMaster(RX_SLAVE_STRUCT __xdata * rxData, UINT8 timeout, UINT8 t3_multiplier, BYTE dmaNumber, UINT8 packetlen);

// tw_mac.c
void initFrameTimer(void);
void startFrameTimer(UINT8 cnt);
void macTimer3Init(void);
void macsetT3TimeoutAndWait(UINT8 timeout, UINT8 multiplier);
void setChannel (UINT8 ch);

//-------------------------------------------------------------------------------------------------------
// Global extern variables

extern RX_SLAVE_STRUCT __xdata SLVrxData;       // Data packet struct

extern volatile BYTE __xdata rxStatus;          // Status flags
extern volatile UINT8 __xdata rxPacketStatus;

extern UINT8  __xdata activeTable[TOTAL_NUM_CHANNELS];

extern UINT8  __xdata audioIn[2*ADC_SAMPLES];   // Audio In (playback) Buffer
extern UINT8  __xdata activeIn;                 // Audio In active buffer (0 | 1)

struct USB_FIFO_STRUCT {
  UINT8 fifoA[96];
  UINT8 fifoB[96]; 
  UINT8 fifoC[96];
  UINT8 fifoD[96];
  UINT8 fifoE[96]; 
  UINT8 fifoF[96]; 
};

struct AUDIO_OUT_STRUCT {
  struct USB_FIFO_STRUCT A;
  struct USB_FIFO_STRUCT B;
};

// DMA
extern DMA_DESC __xdata DmaDesc0;
extern DMA_DESC __xdata DmaDesc1_4[];

extern UINT8 audioInindx;
extern UINT16 timerSampleRate;
extern BOOL __xdata audioFrameReady;
extern UINT8 __xdata ActiveChIdx;
extern BOOL sfdDetected;
extern UINT8 headerByte;
#endif

/***********************************************************************************
  Copyright 2011 Texas Instruments Incorporated. All rights reserved.

  IMPORTANT: Your use of this Software is limited to those specific rights
  granted under the terms of a software license agreement between the user
  who downloaded the software, his/her employer (which must be your employer)
  and Texas Instruments Incorporated (the "License").  You may not use this
  Software unless you agree to abide by the terms of the License. The License
  limits your use, and you acknowledge, that the Software may not be modified,
  copied or distributed unless embedded on a Texas Instruments microcontroller
  or used solely and exclusively in conjunction with a Texas Instruments radio
  frequency transceiver, which is integrated into your product.  Other than for
  the foregoing purpose, you may not use, reproduce, copy, prepare derivative
  works of, modify, distribute, perform, display or sell this Software and/or
  its documentation for any purpose.

  YOU FURTHER ACKNOWLEDGE AND AGREE THAT THE SOFTWARE AND DOCUMENTATION ARE
  PROVIDED �AS IS� WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED,
  INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE,
  NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
  TEXAS INSTRUMENTS OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT,
  NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER
  LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
  INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE
  OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT
  OF SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
  (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.

  Should you have any questions regarding your right to use this Software,
  contact Texas Instruments Incorporated at www.TI.com.
***********************************************************************************/