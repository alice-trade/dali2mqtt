/*###########################################################################
        copyright qqqlab.com / github.com/qqqlab

        This program is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program.  If not, see <http://www.gnu.org/licenses/>.

----------------------------------------------------------------------------
Changelog:
2020-11-14 Rewrite with sampling instead of pinchange
2020-11-10 Split off hardware specific code into separate class
2020-11-08 Created & tested on ATMega328 @ 8Mhz

Patched: daliMQTT Project
###########################################################################*/
#include <inttypes.h>

#include "esp_attr.h"

//-------------------------------------------------
//LOW LEVEL DRIVER DEFINES
#define DALI_BAUD 1200

//low level
#define DALI_OK 0
#define DALI_RESULT_BUS_NOT_IDLE 1       //can't transmit, bus is not idle
#define DALI_RESULT_FRAME_TOO_LONG 2     //can't transmit, attempting to send more than 32 bits
#define DALI_RESULT_COLLISION 3          //bus collision occured
#define DALI_RESULT_TRANSMITTING 4       //currently transmitting
#define DALI_RESULT_RECEIVING 5          //currently receiving

//high level
#define DALI_RESULT_NO_REPLY         101 //cmd() did not receive a reply (i.e. received a 'NO' Backward Frame)
#define DALI_RESULT_TIMEOUT          102 //Timeout waiting for DALI bus
#define DALI_RESULT_DATA_TOO_LONG    103 //Trying to send too many bytes (max 3)
#define DALI_RESULT_INVALID_CMD      104 //The cmd argument in the call to cmd() was invalid
#define DALI_RESULT_INVALID_REPLY    105 //cmd() received an invalid reply (not 8 bits)


//tx collision handling
#define DALI_TX_COLLISSION_AUTO 0 //handle tx collisions for non 8 bit frames
#define DALI_TX_COLLISSION_OFF 1  //don't handle tx collisions
#define DALI_TX_COLLISSION_ON 2   //handle all tx collisions

#define DALI_RX_BUF_SIZE 40
typedef void (*DaliRxCallback)(void*);

class Dali {
public:
  //-------------------------------------------------
  //LOW LEVEL DRIVER PUBLIC
  void begin(uint8_t (*bus_is_high)(), void (*bus_set_low)(), void (*bus_set_high)());
  void timer(); //call this function every 104.167 us (1200 baud 8x oversampled)
  uint8_t tx(uint8_t *data, uint8_t bitlen);  //low level non-blocking transmit
  uint8_t rx(uint8_t *data); //low level non-blocking receive
  uint8_t tx_state(); //low level tx state, returns DALI_RESULT_COLLISION, DALI_RESULT_TRANSMITTING or DALI_OK
  uint8_t txcollisionhandling; //collision handling DALI_TX_COLLISSION_AUTO,DALI_TX_COLLISSION_OFF,DALI_TX_COLLISSION_ON
  uint32_t milli(); //esp32 as 32-bit controller needs millis to be 32-bit to rollover correctly
  Dali() : txcollisionhandling(DALI_TX_COLLISSION_AUTO), busstate(0), /* ticks(0), _milli(0), */ idlecnt(0) {}; //initialize variables
  void setRxCallback(DaliRxCallback cb, void* arg) {
      _rx_callback = cb;
      _rx_callback_arg = arg;
  }
  //-------------------------------------------------
  //HIGH LEVEL PUBLIC
  void     set_level(uint8_t level, uint8_t adr=0xFF); //set arc level
  int16_t  cmd(uint16_t cmd, uint8_t arg); //execute DALI command, use a DALI_xxx command define as cmd argument, returns negative DALI_RESULT_xxx or reply byte
  uint8_t  set_operating_mode(uint8_t v, uint8_t adr=0xFF); //returns 0 on success
  uint8_t  set_max_level(uint8_t v, uint8_t adr=0xFF); //returns 0 on success
  uint8_t  set_min_level(uint8_t v, uint8_t adr=0xFF); //returns 0 on success
  uint8_t  set_system_failure_level(uint8_t v, uint8_t adr=0xFF); //returns 0 on success
  uint8_t  set_power_on_level(uint8_t v, uint8_t adr=0xFF); //returns 0 on success
  uint8_t  tx_wait(uint8_t* data, uint8_t bitlen, uint32_t timeout_ms=500); //blocking transmit bytes
  int16_t  tx_wait_rx(uint8_t cmd0, uint8_t cmd1, uint32_t timeout_ms=500); //blocking transmit and receive

  uint8_t read_memory_bank(uint8_t bank, uint8_t adr);
  uint8_t set_dtr0(uint8_t value, uint8_t adr);
  uint8_t set_dtr1(uint8_t value, uint8_t adr);
  uint8_t set_dtr2(uint8_t value, uint8_t adr);

  //commissioning
  uint8_t  commission(uint8_t init_arg=0xff);
  void     set_searchaddr(uint32_t adr);
  void     set_searchaddr_diff(uint32_t adr_new,uint32_t adr_current);
  uint8_t  compare();
  void     program_short_address(uint8_t shortadr);
  uint8_t  query_short_address();
  uint32_t find_addr();

private:
  //-------------------------------------------------
  //LOW LEVEL DRIVER PRIVATE

  //BUS
  volatile uint8_t busstate;       //current bus state IDLE,TX,RX,COLLISION_RX,COLLISION_TX
  // // volatile uint8_t ticks;          //sample counter, wraps around. 1 tick is approx 0.1 ms, overflow 6.5 seconds
  // // volatile uint16_t _milli;        //millisecond counter, wraps around, overflow 256 ms
  volatile uint8_t idlecnt;        //number of idle samples (capped at 255)

  //RECEIVER
  enum rx_stateEnum { EMPTY, RECEIVING, COMPLETED};
  volatile rx_stateEnum rxstate;   //state of receiver
  volatile uint8_t rxdata[DALI_RX_BUF_SIZE];     //received samples
  volatile uint8_t rxpos;          //pos in rxdata
  volatile uint8_t rxbyte;         //last 8 samples, MSB is oldest
  volatile uint8_t rxbitcnt;       //bitcnt in rxbyte
  volatile uint8_t rxidle;         //idle tick counter during RX


  //TRANSMITTER
  volatile uint8_t txhbdata[9];    //half bit data to transmit (max 32 bits = 2+64+4 half bits = 9 bytes)
  volatile uint8_t txhblen;        //number of half bits to transmit, incl start + stop bits
  volatile uint8_t txhbcnt;        //number of transmitted half bits, incl start + stop bits
  volatile uint8_t txspcnt;        //sample count since last transmitted bit
  volatile uint8_t txhigh;         //currently bus is high
  volatile uint8_t txcollision;    //collision count (capped at 255)

  //hardware abstraction layer
  uint8_t (*bus_is_high)(); //returns !=0 if DALI bus is in high (non-asserted) state
  void (*bus_set_low)(); //set DALI bus in low (asserted) state
  void (*bus_set_high)(); //set DALI bus in high (released) state

  void _init();
  void _set_busstate_idle();
  void _tx_push_2hb(uint8_t hb);

  DaliRxCallback _rx_callback = nullptr;
  void* _rx_callback_arg = nullptr;

  uint8_t _man_weight(uint8_t i);
  uint8_t _man_sample(volatile uint8_t *edata, uint16_t bitpos, uint8_t *stop_coll);
  uint8_t _man_decode(volatile uint8_t *edata, uint8_t ebitlen, uint8_t *ddata);

  //-------------------------------------------------
  //HIGH LEVEL PRIVATE
  uint8_t _check_yaaaaaa(uint8_t yaaaaaa); //check for yaaaaaa pattern
  uint8_t _set_value(uint16_t setcmd, uint16_t getcmd, uint8_t v, uint8_t adr); //set a parameter value, returns 0 on success

};


//-------------------------------------------------
//HIGH LEVEL DEFINES

#define DALI_BAUD 1200
#define DALI_RESET 544 //32 REPEAT - Makes a slave an RESET state.
#define DALI_STORE_ACTUAL_LEVEL_IN_THE_DTR0 545 //33 REPEAT - Saves the current lighting control level to the DTR (DTR0). (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_SAVE_PERSISTENT_VARIABLES 546 //34 REPEAT DALI-2 - Saves a variable in nonvolatile memory (NVM). (Command that exist only in IEC62386-102ed2.0)
#define DALI_SET_OPERATING_MODE 547 //35 REPEAT DALI-2 - Sets data of DTR0 as an operating mode. (Command that exist only in IEC62386-102ed2.0)
#define DALI_RESET_MEMORY_BANK 548 //36 REPEAT DALI-2 - Changes to the reset value the specified memory bank in DTR0. (Command that exist only in IEC62386-102ed2.0)
#define DALI_IDENTIFY_DEVICE 549 //37 REPEAT DALI-2 - Starts the identification state of the device. (Command that exist only in IEC62386-102ed2.0)
#define DALI_QUERY_STATUS 144 //144  - Returns "STATUS INFORMATION"
#define DALI_QUERY_CONTROL_GEAR_PRESENT 145 //145  - Is there a slave that can communicate? (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_QUERY_LAMP_FAILURE 146 //146  - Is there a lamp problem?
#define DALI_QUERY_LAMP_POWER_ON 147 //147  - Is a lamp on?
#define DALI_QUERY_LIMIT_ERROR 148 //148  - Is the specified lighting control level out of the range from the minimum to the maximum values?
#define DALI_QUERY_RESET_STATE 149 //149  - Is the slave in 'RESET STATE'?
#define DALI_QUERY_MISSING_SHORT_ADDRESS 150 //150  - Does the slave not have a short address?
#define DALI_QUERY_VERSION_NUMBER 151 //151  - What is the corresponding IEC standard number?
#define DALI_QUERY_CONTENT_DTR0 152 //152  - What is the DTR content? (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_QUERY_DEVICE_TYPE 153 //153  - What is the device type?  (fluorescent lamp:0000 0000) (IEC62386-207 is 6 fixed)
#define DALI_QUERY_PHYSICAL_MINIMUM_LEVEL 154 //154  - What is the minimum lighting control level specified by the hardware?
#define DALI_QUERY_POWER_FAILURE 155 //155  - Has the slave operated without the execution of reset-command or the adjustment of the lighting control level?
#define DALI_QUERY_CONTENT_DTR1 156 //156  - What is the DTR1 content?
#define DALI_QUERY_CONTENT_DTR2 157 //157  - What is the DTR2 content?
#define DALI_QUERY_OPERATING_MODE_DALI2 158 //158 DALI-2 - What is the Operating Mode? (Only IEC62386-102ed2.0 )
#define DALI_QUERY_LIGHT_SOURCE_TYPE 159 //159 DALI-2 - What is the Light source type? (Only IEC62386-102ed2.0 )
#define DALI_QUERY_ACTUAL_LEVEL 160 //160  - What is the "ACTUAL LEVEL" (the current lighting control level)?
#define DALI_SET_MAX_LEVEL 554 //42 REPEAT - Specifies the DTR data as the maximum lighting control level. (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_SET_MIN_LEVEL 555 //43 REPEAT - Specifies the DTR data as the minimum lighting control level. (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_SET_SYSTEM_FAILURE_LEVEL 556 //44 REPEAT - Specifies the DTR data as the "FAILURELEVEL". (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_SET_POWER_ON_LEVEL 557 //45 REPEAT - Specifies the DTR data as the "POWER ONLEVEL". (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_SET_FADE_TIME 558 //46 REPEAT - Specifies the DTR data as the Fade time. (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_SET_FADE_RATE 559 //47 REPEAT - Specifies the DTR data as the Fade rate. (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_SET_EXTENDED_FADE_TIME 560 //48 REPEAT DALI-2 - Specifies the DTR data as the Extended Fade Time. (Command that exist only in IEC62386-102ed2.0)
#define DALI_QUERY_MAX_LEVEL 161 //161  - What is the maximum lighting control level?
#define DALI_QUERY_MIN_LEVEL 162 //162  - What is the minimum lighting control level?
#define DALI_QUERY_POWER_ON_LEVEL 163 //163  - What is the "POWER ON LEVEL" (the lighting control level when the power is turned on)?
#define DALI_QUERY_SYSTEM_FAILURE_LEVEL 164 //164  - What is the "SYSTEM FAILURE LEVEL" (the lighting control level when a failure occurs)?
#define DALI_QUERY_FADE_TIME_FADE_RATE 165 //165  - What are the Fade time and Fade rate?
#define DALI_QUERY_MANUFACTURER_SPECIFIC_MODE 166 //166 DALI-2 - What is the Specific Mode? (Command that exist only in IEC62386-102ed2.0)
#define DALI_READ_MEMORY_LOCATION 197 //197  - What is the memory location content?
#define DALI_QUERY_GEAR_TYPE 237 //237 IEC62386-207 - Returns ‘GEAR TYPE’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_DIMMING_CURVE 238 //238 IEC62386-207 - Returns ’Dimming curve’in use (Command that exist only in IEC62386-207)
#define DALI_QUERY_POSSIBLE_OPERATING_MODE 239 //239 IEC62386-207 - Returns ‘POSSIBLEG OPERATING MODE’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_FEATURES 240 //240 IEC62386-207 - Returns ‘FEATURES’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_FAILURE_STATUS 241 //241 IEC62386-207 - Returns ‘FAILURE STATUS’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_SHORT_CIRCUIT 242 //242 IEC62386-207 - Returns bit0 short circuit of ‘FAILURE STATUS’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_OPEN_CIRCUIT 243 //243 IEC62386-207 - Returns bit1 open circuit of ‘FAILURE STATUS’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_LOAD_DECREASE 244 //244 IEC62386-207 - Returns bit2 load decrease of ‘FAILURE STATUS’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_LOAD_INDREASE 245 //245 IEC62386-207 - Returns bit3 load increase of‘FAILURE STATUS’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_CURRENT_PROTECTOR_ACTIVE 246 //246 IEC62386-207 - Returns bit4 current protector active of ‘FAILURE STATUS’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_THERMAL_SHUTDOWN 247 //247 IEC62386-207 - Returns bit5 thermal shut down of ‘FAILURE STATUS’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_THERMAL_OVERLOAD 248 //248 IEC62386-207 - Returns bit6 thermal overload with light level reduction of ‘FAILURE STATUS’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_REFARENCE_RUNNING 249 //249 IEC62386-207 - Returns whetherReference System Power is in operation. (Command that exist only in IEC62386-207)
#define DALI_QUERY_REFERENCE_MEASURMENT_FAILED 250 //250 IEC62386-207 - Returns bit7 reference measurement failed  of ‘FAILURE STATUS’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_OPERATING_MODE 252 //252 IEC62386-207 - Returns ‘OPERATING MODE’ (Command that exist only in IEC62386-207)
#define DALI_QUERY_FAST_FADE_TIME 253 //253 IEC62386-207 - Returns set Fast fade time. (Command that exist only in IEC62386-207)
#define DALI_QUERY_MIN_FAST_FADE_TIME 254 //254 IEC62386-207 - Returns set Minimum fast fade time (Command that exist only in IEC62386-207)
#define DALI_QUERY_EXTENDED_VERSION_NUMBER 255 //255 IEC62386-207 - The version number of the extended support? IEC62386-207: 1, Other: NO(no response)
#define DALI_TERMINATE 0x01A1 //256  - Releases the INITIALISE state.
#define DALI_DATA_TRANSFER_REGISTER0 0x01A3 //257  - Stores the data XXXX XXXX to the DTR(DTR0). (In the parenthesis is a name in IEC62386-102ed2.0)
#define DALI_INITIALISE 0x03A5 //258 REPEAT - Sets the slave to the INITIALISE status for15 minutes. Commands 259 to 270 are enabled only for a slave in this status.
#define DALI_RANDOMISE 0x03A7 //259 REPEAT - Generates a random address.
#define DALI_COMPARE 0x01A9 //260  - Is the random address smaller or equal to the search address?
#define DALI_WITHDRAW 0x01AB //261  - Excludes slaves for which the random address and search address match from the Compare process.
#define DALI_RESERVED262 0x01AD //262  - [Reserved]
#define DALI_PING 0x01AF //263 DALI-2 - Ignores in the slave. (Command that exist only in IEC62386-102ed2.0)
#define DALI_SEARCHADDRH 0x01B1 //264  - Specifies the higher 8 bits of the search address.
#define DALI_SEARCHADDRM 0x01B3 //265  - Specifies the middle 8 bits of the search address.
#define DALI_SEARCHADDRL 0x01B5 //266  - Specifies the lower 8 bits of the search address.
#define DALI_PROGRAM_SHORT_ADDRESS 0x01B7 //267  - The slave shall store the received 6-bit address (AAA AAA) as a short address if it is selected.
#define DALI_VERIFY_SHORT_ADDRESS 0x01B9 //268  - Is the short address AAA AAA?
#define DALI_QUERY_SHORT_ADDRESS 0x01BB //269  - What is the short address of the slaveNote 2being selected?
#define DALI_PHYSICAL_SELECTION 0x01BD //270 not DALI-2 - Sets the slave to Physical Selection Mode and excludes the slave from the Compare process. (Excluding IEC62386-102ed2.0) (Command that exist only in IEC62386-102ed1.0, -207ed1.0)
#define DALI_RESERVED271 0x01BF //271  - [Reserved]
#define DALI_ENABLE_DEVICE_TYPE_X 0x01C1 //272  - Adds the device XXXX (a special device).
#define DALI_DATA_TRANSFER_REGISTER1 0x01C3 //273  - Stores data XXXX into DTR1.
#define DALI_DATA_TRANSFER_REGISTER2 0x01C5 //274  - Stores data XXXX into DTR2.
#define DALI_WRITE_MEMORY_LOCATION 0x01C7 //275  - Write data into the specified address of the specified memory bank. (There is BW) (DTR(DTR0)：address, DTR1：memory bank number)
#define DALI_WRITE_MEMORY_LOCATION_NO_REPLY 0x01C9 //276 DALI-2 - Write data into the specified address of the specified memory bank.
/*
SIGNAL CHARACTERISTICS
High Level: 9.5 to 22.5 V (Typical 16 V)
Low Level: -6.5 to + 6.5 V (Typical 0 V)
Te = half cycle = 416.67 us +/- 10 %
10 us <= tfall <= 100 us
10 us <= trise <= 100 us

BIT TIMING
msb send first
 logical 1 = 1Te Low 1Te High
 logical 0 = 1Te High 1Te Low
 Start bit = logical 1
 Stop bit = 2Te High

FRAME TIMING
FF: TX Forward Frame 2 bytes (38Te) = 2*(1start+16bits+2stop)
BF: RX Backward Frame 1 byte (22Te) = 2*(1start+8bits+2stop)
no reply: FF >22Te pause FF
with reply: FF >7Te <22Te pause BF >22Te pause FF


DALI commands
=============
In accordance with the DIN EN 60929 standard, addresses and commands are transmitted as numbers with a length of two bytes.

These commands take the form YAAA AAAS xxXXxx. Each letter here stands for one bit.

Y: type of address
     0bin:    short address
     1bin:    group address or collective call

A: significant address bit

S: selection bit (specifies the significance of the following eight bits):
     0bin:    the 8 xxXXxx bits contain a value for direct control of the lamp power
     1bin:    the 8 xxXXxx bits contain a command number.

x: a bit in the lamp power or in the command number


Type of Addresses
=================
Type of Addresses Byte Description
Short address 0AAAAAAS (AAAAAA = 0 to 63, S = 0/1)
Group address 100AAAAS (AAAA = 0 to 15, S = 0/1)
Broadcast address 1111111S (S = 0/1)
Special command 101CCCC1 (CCCC = command number)


Direct DALI commands for lamp power
===================================
These commands take the form YAAA AAA0 xxXXxx.

xxXXxx: the value representing the lamp power is transmitted in these 8 bits. It is calculated according to this formula:

Pvalue = 10 ^ ((value-1) / (253/3)) * Pmax / 1000

253 values from 1dec to 254dec are available for transmission in accordance with this formula.

There are also 2 direct DALI commands with special meanings:

Command; Command No; Description; Answer
00hex; 0dec; The DALI device dims using the current fade time down to the parameterised MIN value, and then switches off.; -
FFhex; 254dec; Mask (no change): this value is ignored in what follows, and is therefore not loaded into memory.; -


Indirect DALI commands for lamp power
=====================================
These commands take the form YAAA AAA1 xxXXxx.

xxXXxx: These 8 bits transfer the command number. The available command numbers are listed and explained in the following tables in hexadecimal and decimal formats.

Command; Command No; Description; Answer
00hex 0dez Extinguish the lamp (without fading) -
01hex 1dez Dim up 200 ms using the selected fade rate -
02hex 2dez Dim down 200 ms using the selected fade rate -
03hex 3dez Set the actual arc power level one step higher without fading. If the lamp is off, it will be not ignited. -
04hex 4dez Set the actual arc power level one step lower without fading. If the lamp has already it's minimum value, it is not switched off. -
05hex 5dez Set the actual arc power level to the maximum value. If the lamp is off, it will be ignited. -
06hex 6dez Set the actual arc power level to the minimum value. If the lamp is off, it will be ignited. -
07hex 7dez Set the actual arc power level one step lower without fading. If the lamp has already it's minimum value, it is switched off. -
08hex 8dez Set the actual arc power level one step higher without fading. If the lamp is off, it will be ignited. -
09hex ... 0Fhex 9dez ... 15dez reserved -
1nhex
(n: 0hex ... Fhex) 16dez ... 31dez Set the light level to the value stored for the selected scene (n) -


Configuration commands
======================
Command; Command No; Description; Answer
20hex 32dez Reset the parameters to default settings -
21hex 33dez Store the current light level in the DTR (Data Transfer Register) -
22hex ... 29hex 34dez ... 41dez reserved -
2Ahex 42dez Store the value in the DTR as the maximum level -
2Bhex 43dez Store the value in the DTR as the minimum level -
2Chex 44dez Store the value in the DTR as the system failure level -
2Dhex 45dez Store the value in the DTR as the power on level -
2Ehex 46dez Store the value in the DTR as the fade time -
2Fhex 47dez Store the value in the DTR as the fade rate -
30hex ... 3Fhex 48dez ... 63dez reserved -
4nhex
(n: 0hex ... Fhex) 64dez ... 79dez Store the value in the DTR as the selected scene (n) -
5nhex
(n: 0hex ... Fhex) 80dez ... 95dez Remove the selected scene (n) from the DALI slave -
6nhex
(n: 0hex ... Fhex) 96dez ... 111dez Add the DALI slave unit to the selected group (n) -
7nhex
(n: 0hex ... Fhex) 112dez ... 127dez Remove the DALI slave unit from the selected group (n) -
80hex 128dez Store the value in the DTR as a short address -
81hex ... 8Fhex 129dez ... 143dez reserved -
90hex 144dez Returns the status (XX) of the DALI slave XX
91hex 145dez Check if the DALI slave is working yes/no
92hex 146dez Check if there is a lamp failure yes/no
93hex 147dez Check if the lamp is operating yes/no
94hex 148dez Check if the slave has received a level out of limit yes/no
95hex 149dez Check if the DALI slave is in reset state yes/no
96hex 150dez Check if the DALI slave is missing a short address XX
97hex 151dez Returns the version number as XX
98hex 152dez Returns the content of the DTR as XX
99hex 153dez Returns the device type as XX
9Ahex 154dez Returns the physical minimum level as XX
9Bhex 155dez Check if the DALI slave is in power failure mode yes/no
9Chex ... 9Fhex 156dez ... 159dez reserved -
A0hex 160dez Returns the current light level as XX
A1hex 161dez Returns the maximum allowed light level as XX
A2hex 162dez Returns the minimum allowed light level as XX
A3hex 163dez Return the power up level as XX
A4hex 164dez Returns the system failure level as XX
A5hex 165dez Returns the fade time as X and the fade rate as Y XY
A6hex ... AFhex 166dez ... 175dez reserved -
Bnhex
(n: 0hex ... Fhex) 176dez ... 191dez Returns the light level XX for the selected scene (n) XX
C0hex 192dez Returns a bit pattern XX indicating which group (0-7) the DALI slave belongs to XX
C1hex 193dez Returns a bit pattern XX indicating which group (8-15) the DALI slave belongs to XX
C2hex 194dez Returns the high bits of the random address as HH
C3hex 195dez Return the middle bit of the random address as MM
C4hex 196dez Returns the lower bits of the random address as LL
C5hex ... DFhex 197dez ... 223dez reserved -
E0hex ... FFhex 224dez ... 255dez Returns application specific extension commands


Note Repeat of DALI commands
============================
According to IEC 60929, a DALI Master has to repeat several commands within 100 ms, so that DALI-Slaves will execute them.

The DALI Master Terminal KL6811 repeats the commands 32dez to 128dez, 258dez and 259dez (bold marked) automatically to make the the double call from the user program unnecessary.

The DALI Master Terminal KL6811 repeats also the commands 224dez to 255dez, if you have activated this with Bit 1 of the Control-Byte (CB.1) before.


DALI Control Device Type List
=============================
Type DEC Type HEX Name Comments
128 0x80 Unknown Device. If one of the devices below don't apply
129 0x81 Switch Device A Wall-Switch based Controller including, but not limited to ON/OFF devices, Scene switches, dimming device.
130 0x82 Slide Dimmer An analog/positional dimming controller
131 0x83 Motion/Occupancy Sensor. A device that indicates the presence of people within a control area.
132 0x84 Open-loop daylight Controller. A device that outputs current light level and/or sends control messages to actuators based on light passing a threshold.
133 0x85 Closed-loop daylight controller. A device that outputs current light level and/or sends control messages to actuators based on a change in light level.
134 0x86 Scheduler. A device that establishes the building mode based on time of day, or which provides control outputs.
135 0x87 Gateway. An interface to other control systems or communication busses
136 0x88 Sequencer. A device which sequences lights based on a triggering event
137 0x89 Power Supply *). A DALI Power Supply device which supplies power for the communication loop
138 0x8a Emergency Lighting Controller. A device, which is certified for use in control of emergency lighting, or, if not certified, for noncritical backup lighting.
139 0x8b Analog input unit. A general device with analog input.
140 0x8c Data Logger. A unit logging data (can be digital or analog data)


Flash Variables and Offset in Information
=========================================
Memory Name Offset
Power On Level [0]
System Failure Level [1]
Minimum Level [2]
Maximum Level [3]
Fade Rate [4]
Fade Time [5]
Short Address [6]
Group 0 through 7 [7]
Group 8 through 15 [8]
Scene 0 through 15 [9-24]
Random Address [25-27]
Fast Fade Time [28]
Failure Status [29]
Operating Mode [30]
Dimming Curve [31]
*/
