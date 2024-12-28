// =====================================================================================
// Copyright (c) 2023-2025 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated
// readme files, with or without modification, are permitted in any medium without
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The DS994a emulator is offered as-is, without any warranty.
//
// Please see the README.md file as it contains much useful info.
// =====================================================================================

#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fat.h>
#include "tms9901.h"
#include "tms9900.h"
#include "../../disk.h"
#include "../../SAMS.h"

// From https://www.unige.ch/medecine/nouspikel/ti99/tms9901.htm
//
// The main CRU handling is for the lower 32 bits defined as follows:
//
// Bit 0 is used to select the timer mode. When it equals 1, the TMS9901 is in timer mode and bits 1-15 have a special meaning (see below),
// when 0 it starts the timer (if needed) and bits 1-15 are used to control I/O pins, just as bits 16-31.

// Bits 1-15 are used to read the status of the 15 interrupt pins (whether they are used as interrupt pin or as input pin).
// Writing to one of these bits does not output any data, but sets the interrupt mask for the corresponding pin: writing a 1 results
// in issuing interrupts when the pin is held low. The interrupt trigger is synchronized by the PHI* pin.

// Bits 16-31 are used to read the status of the 15 programmable I/O pins, provided they are used as input (or interrupt) pins.
// Note that the 8 versatile pins INT7*/P15 through INT15*/P7 can be read either with bits 7-15 or with bits 23-31. Writing to
// CRU bits 16-31 turns the corresponding pins into output pins and places the bit values on the pins.

// From https://www.unige.ch/medecine/nouspikel/ti99/tms9901.htm
//    =   .   ,   M   N   /  fire1  fire2
// space  L   K   J   H   ;  left1  left2
// enter  O   I   U   Y   P  right1 right2
// (none) 9   8   7   6   0  down1  down2
// fctn   2   3   4   5   1  up1    up2      [AlphaLock]
// shift  S   D   F   G   A  (none) (none)
// ctrl   W   E   R   T   Q  (none) (none)
// (none) X   C   V   B   Z  (none) (none)

// Alpha Lock is special... it's enabled via CRU[>15] and is read back on row 4... so it screws up joysticks when enabled. Sigh.


// ------------------------------------------------------------------------------------------------------------
// TMS Keys form an 8x8 matrix to mirror the spec above and can be scanned using the 3 keyboard column bits
// ------------------------------------------------------------------------------------------------------------
const u8 TIKeys[8][8] =
{
    { TMS_KEY_EQUALS,   TMS_KEY_PERIOD, TMS_KEY_COMMA, TMS_KEY_M,   TMS_KEY_N,   TMS_KEY_SLASH,  TMS_KEY_JOY1_FIRE,     TMS_KEY_JOY2_FIRE   },
    { TMS_KEY_SPACE,    TMS_KEY_L,      TMS_KEY_K,     TMS_KEY_J,   TMS_KEY_H,   TMS_KEY_SEMI,   TMS_KEY_JOY1_LEFT,     TMS_KEY_JOY2_LEFT   },
    { TMS_KEY_ENTER,    TMS_KEY_O,      TMS_KEY_I,     TMS_KEY_U,   TMS_KEY_Y,   TMS_KEY_P,      TMS_KEY_JOY1_RIGHT,    TMS_KEY_JOY2_RIGHT  },
    { TMS_KEY_NONE,     TMS_KEY_9,      TMS_KEY_8,     TMS_KEY_7,   TMS_KEY_6,   TMS_KEY_0,      TMS_KEY_JOY1_DOWN,     TMS_KEY_JOY2_DOWN   },
    { TMS_KEY_FUNCTION, TMS_KEY_2,      TMS_KEY_3,     TMS_KEY_4,   TMS_KEY_5,   TMS_KEY_1,      TMS_KEY_JOY1_UP,       TMS_KEY_JOY2_UP     },
    { TMS_KEY_SHIFT,    TMS_KEY_S,      TMS_KEY_D,     TMS_KEY_F,   TMS_KEY_G,   TMS_KEY_A,      TMS_KEY_NONE,          TMS_KEY_NONE        },
    { TMS_KEY_CONTROL,  TMS_KEY_W,      TMS_KEY_E,     TMS_KEY_R,   TMS_KEY_T,   TMS_KEY_Q,      TMS_KEY_NONE,          TMS_KEY_NONE        },
    { TMS_KEY_NONE,     TMS_KEY_X,      TMS_KEY_C,     TMS_KEY_V,   TMS_KEY_B,   TMS_KEY_Z,      TMS_KEY_NONE,          TMS_KEY_NONE        },
};

// ---------------------------------------------------------------------------------
// Some pins are aliased... so we use a simple look-up table to map them correctly
// ---------------------------------------------------------------------------------
u16 CRU_AliasTable[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,15,16,17,18,19,20,21,22,23};

// --------------------------------------------------------------------------------------------
// The entire TMS9901 struct and state information is placed into .DTCM fast memory on the DS
// so that it's as fast as possible. We will also try to put as much of the CRU logic into the
// .ITCM fast instruction memory to speed up that processing to help the poor DS CPU along...
// --------------------------------------------------------------------------------------------
TMS9901 tms9901      __attribute__((section(".dtcm")));


// --------------------------------------------------------------------
// Clears out the TMS9901 and clears out any pending interrupts...
// --------------------------------------------------------------------
void TMS9901_Reset(void)
{
    // Clear out the entire state of the TMS9901 - this will also force all pins LOW
    memset(&tms9901, 0x00, sizeof(tms9901));

    // -------------------------------------------------------------------------------------------------------------------
    // Set the state of the 32 I/O pins... with the first pin being special to indicate timer active or IO mode active
    // -------------------------------------------------------------------------------------------------------------------
    tms9901.PinState[PIN_TIMER_OR_IO]  =  IO_MODE;

    TMS9900_ClearInterrupt(0xFFFF);
}

// -----------------------------------------------------------------------------------------
// Write up to 16 bits of information to the CRU. This routine handles the data shifting
// as needed to clock out one or more bits (up to the full 16 bits) to the CRU. The CPU
// calls that bring us here will already have shifted down the cruAddress so we're dealing
// with 0-31 for the main CRU bits and adjust appropriately for peripheral CRU use.
//
// The following is the typical TI-99/4a use of CRU address ranges:
//      >0000-07FE   Internal Use (the 32 main CRU bits are mapped here - mirrored)
//      >0800-0FFE   Reserved (CRU paging uses this... SuperSpace II and some Databiotics carts)
//      >1000-10FE   Horizon RAMDisk or IDE Harddisk
//      >1100-11FE   Disk Controller
//      >1200-12FE   Reserved
//      >1300-13FE   RS-232 (Primary)
//      >1400-14FE   Unassigned
//      >1500-15FE   RS-232 (Secondary)
//      >1600-16FE   Unassigned
//      >1700-17FE   HEX-BUS Interface
//      >1800-18FE   Thermal Printer
//      >1900-19FE   Reserved
//      >1A00-1AFE   Unassigned
//      >1B00-1BFE   Unassigned
//      >1C00-1CFE   Video Controller Card
//      >1D00-1DFE   IEEE 488 Bus Controller Card
//      >1E00-1EFE   Unassigned
//      >1F00-1FFE   P-Code Card
// -----------------------------------------------------------------------------------------
ITCM_CODE void TMS9901_WriteCRU(u16 cruAddress, u16 data, u8 num)
{
    if (num == 0) num = 16;     // A zero means write all 16 bits...

    for (u8 bitNum = 0; bitNum < num; bitNum++)
    {
        u16 dataBit = (data & (1<<bitNum)) ? 1:0;  // Get the status of this data bit

        // --------------------------------------------------------------------------------------
        // Check to see if we're in the external peripheral area - this is for Disk DSR and SAMS
        // --------------------------------------------------------------------------------------
        if (cruAddress & 0xFC00) // At or above CRU base >800?
        {
            if ((cruAddress & 0xF80) == (0x1100 >> 1))       // Disk support at CRU base >1100
            {
                disk_cru_write(cruAddress, dataBit);
            }
            else if ((cruAddress & 0xFFE) == (0x1E00 >> 1))  // SAMS support at CRU base >1E00
            {
                SAMS_cru_write(cruAddress, dataBit);
            }
            else if ((cruAddress & 0xF80) == (0x800 >> 1))   // Cart-based CRU bankswitching at CRU base >800
            {
                cart_cru_write(cruAddress, dataBit);
            }
        }
        else  // This is the internal console CRU bits below CRU base >800... the famous 32 CRU bits that must be handled in either TIMER mode or IO mode.
        {
            u8 cruA = cruAddress & 0x1F; // Map down to 32 bits...

            // -------------------------------------------------------------------------------------------------
            // Bit 0 is special as it defines if we are in Timer or I/O mode... We don't yet handle timer
            // mode but it's only used for Cassette IO which is not currently supported but we track it still.
            // -------------------------------------------------------------------------------------------------
            if (cruA == PIN_TIMER_OR_IO)
            {
                tms9901.PinState[PIN_TIMER_OR_IO] = (dataBit ? TIMER_MODE : IO_MODE);
            }
            else
            if (tms9901.PinState[PIN_TIMER_OR_IO] == TIMER_MODE)
            {
                // --------------------------------------------------------------------------------------
                // In Timer Mode we need to handle the bit15 soft reset as well as setting timer data
                // --------------------------------------------------------------------------------------
                if (cruA == 15) // Soft Reset
                {
	                tms9901.PinState[0]=0;	// timer control
	                tms9901.PinState[1]=0;	// peripheral interrupt mask
	                tms9901.PinState[2]=0;	// VDP interrupt mask
	                tms9901.PinState[3]=0;	// timer interrupt mask
	                tms9901.TimerCounter=0; // timer counter
                    tms9901.TimerStart=0;   // timer start
                }
                else if ((cruA >= 1) && (cruA <= 14))    // Bits 1-14 represent the the 14 bit counter/timer ...
                {
                    if (dataBit) tms9901.TimerStart |= (1 << (cruA-1));     // Clear or set bit in Timer
                    else tms9901.TimerStart &= ~(1 << (cruA-1));
                    tms9901.TimerStart &= 0x3FFF;                           // 14 bits of Timer
                    tms9901.TimerCounter = tms9901.TimerStart;              // Timer will countdown only in IO mode
                    TMS9900_SetAccurateEmulationFlag(ACCURATE_EMU_TIMER);   // Force timer to be dealt with...
                }
                else if (cruA > 15)
                {
                    tms9901.PinState[PIN_TIMER_OR_IO] = IO_MODE;        // Writes to pin 16 or more result in exit back to IO mode
                }
            }
            else    // We're in I/O Mode
            {
                // --------------------------------------------------------------------------------------
                // Just save the data bit (0 or 1) for the pin in I/O mode. We can decode the keyboard
                // column and alpha-lock easily enough with the use of defines from tms9901.h
                // --------------------------------------------------------------------------------------
                tms9901.PinState[cruA] = dataBit;
                if (cruA == PIN_TIMER_INT)
                {
                    // Any write to pin 3 will clear the timer interrupt
                    TMS9901_ClearTimerInterrupt();
                }
                else if (cruA == PIN_VDP_INT && dataBit) // Are we unmasking... Need to pass through the interrupt state (River Rescue requires this)
                {
                    if (tms9901.VDPIntteruptInProcess) TMS9900_RaiseInterrupt(INT_VDP); else TMS9900_ClearInterrupt(INT_VDP);
                }
            }
        }
        cruAddress++;   // Move to the next CRU bit (if any)
    }
}

// --------------------------------------------------------------------------------------------------
// Read from 1 to 16 bits of CRU information from the desired CRU address. This address has already
// been shifted down 1 by the CPU core that called it... 
// --------------------------------------------------------------------------------------------------
ITCM_CODE u16 TMS9901_ReadCRU(u16 cruAddress, u8 num)
{
    u16 retVal = 0x0000;        // Accumulate bits below

    if (num == 0) num = 16;     // A zero means read all 16 bits...

    for (u8 bitNum = 0; bitNum < num; bitNum++)
    {
        u8 bitState = 1;        // Default output to a '1' until proven otherwise below...
        
        if (cruAddress & 0xFC00)
        {
            if ((cruAddress & 0xF80) == 0x880)    // Disk support from >880 to >888 (CRU base >1000)
            {
                bitState = disk_cru_read(cruAddress);
            }
            else if ((cruAddress & 0xF80) == 0xF00)  // SAMS support at >F00 and >F01 (CRU base >1E00)
            {
                bitState = SAMS_cru_read(cruAddress);
            }
            else if ((cruAddress & 0xF80) == 0x400)  // Cart-based CRU bankswitching... (CRU base >800)
            {
                bitState = cart_cru_read(cruAddress);
            }
        }
        else
        {
            cruAddress &= 0x1F;     // CRU mirrors

            if (tms9901.PinState[PIN_TIMER_OR_IO] == TIMER_MODE)    // We handle bit 0 thru 15 in Timer Mode
            {
                switch (cruAddress)
                {
                    case 0:     bitState = 1;                                                                               break;     // Bit 0 in timer mode always returns '1'
                    case 15:    bitState = ((tms9901.VDPIntteruptInProcess && tms9901.PinState[PIN_VDP_INT]) ||
                                            (tms9901.TimerIntteruptInProcess && tms9901.PinState[PIN_TIMER_INT]) ? 0:1);    break;     // Pin 15 is for either VDP or Timer interrupt... report if that's set
                    default:    bitState = (tms9901.TimerCounter & (1<<(cruAddress-1))) ? 1:0;                              break;     // Otherwise get the timer bit and report it
                }
            }
            else    // This is IO mode - there are some aliased pins we need to be careful of...
            {
                // --------------------------------------------------
                //0 >0000   I/O 0: I/O mode 1: timer mode
                //1 >0002   I+  Peripheral interrupt incoming line
                //2 >0004   I+  VDP interrupts incoming line
                // --------------------------------------------------

                // Some pins are aliased - this will correct the pin number
                cruAddress = CRU_AliasTable[cruAddress];

                switch (cruAddress)
                {
                    case 0:     bitState = 0;                                           break;      // Bit 0 in IO mode always returns '0'

                    case 1:     bitState = 1;                                           break;      // We don't allow external interrupts
                    case 2:     bitState = (tms9901.VDPIntteruptInProcess ? 0 : 1);     break;      // Pin 2 is for reporting the VDP interrupt

                    case 7:     // Keyboard Row 4
                                if (tms9901.PinState[PIN_ALPHA_LOCK] == PIN_LOW)
                                {
                                    // The Alpha Lock is scanned on row 4 but only when the Alpha Lock scanning pin was driven LOW
                                    if (tms9901.CapsLock) bitState = 0;
                                }
                                // No break is INTENTIONAL!
                    case 3:     // Keyboard Row 0
                    case 4:     // Keyboard Row 1
                    case 5:     // Keyboard Row 2
                    case 6:     // Keyboard Row 3
                    case 8:     // Keyboard Row 5
                    case 9:     // Keyboard Row 6
                    case 10:    // Keyboard Row 7
                        {
                            // ------------------------------------------------------------------------------------------------------------------------
                            // This handles both Keybaord and Joystick (P1 and P2) inputs in a unified manner... to the TI-99/4a, it's all the same.
                            // ------------------------------------------------------------------------------------------------------------------------
                            u8 column = (tms9901.PinState[PIN_COL3]<<2) | (tms9901.PinState[PIN_COL2]<<1) | (tms9901.PinState[PIN_COL1]<<0);
                            if (tms9901.Keyboard[TIKeys[cruAddress-3][column]]) bitState = 0;
                        }
                        break;

                    default:    bitState = tms9901.PinState[cruAddress]; break;                        // Otherwise loopback: returned bit will be last value written
                }
            }
        }
        
        // ----------------------------------------------------------------------------------------------
        // If we've decoded a '1' write that bit into the 16-bit return value into the appropriate spot.
        // ----------------------------------------------------------------------------------------------
        if (bitState)
        {
            retVal |= (1 << bitNum);
        }
        cruAddress++;   // Move to the next CRU bit (if any)
    }

    return retVal;  // Return the assembled word to the caller...
}

// -----------------------------------------------------------------------------------------
// On each pass of the main loop (in DS99.c) we want to clear out the joystick and
// keyboard information and re-accumulate any joystick presses or keyboard presses.
// -----------------------------------------------------------------------------------------
void TMS9901_ClearJoyKeyData(void)
{
    memset(tms9901.Keyboard,  0x00, sizeof(tms9901.Keyboard));
}

// -----------------------------------------------------------------------------------------
// Handle VDP Interrupt
// -----------------------------------------------------------------------------------------
void TMS9901_RaiseVDPInterrupt(void)
{
    if (!tms9901.VDPIntteruptInProcess)                     // Do nothing if we've already fired this interrupt...
    {
        tms9901.VDPIntteruptInProcess = 1;                  // Remember that we raised this interrupt
        if (tms9901.PinState[PIN_VDP_INT] == PIN_HIGH)      // Raise the interrupt if the CPU wants to see it
        {
            TMS9900_RaiseInterrupt(INT_VDP);                // Tell the TMS9900 that we have an interrupt...
        }
    }
}

void TMS9901_ClearVDPInterrupt(void)
{
    if (tms9901.VDPIntteruptInProcess)
    {
        tms9901.VDPIntteruptInProcess = 0;                  // Remember that we cleared this interrupt
        if (tms9901.PinState[PIN_VDP_INT] == PIN_HIGH)      // Clear the interrupt if the CPU wants to see it
        {
            TMS9900_ClearInterrupt(INT_VDP);                // Tell the TMS9900 that we have cleared an interrupt...
        }
    }
}

// -----------------------------------------------------------------------------------------
// Handle Timer Interrupt
// -----------------------------------------------------------------------------------------
void TMS9901_RaiseTimerInterrupt(void)
{
    if (!tms9901.TimerIntteruptInProcess)                     // Do nothing if we've already fired this interrupt...
    {
        tms9901.TimerIntteruptInProcess = 1;                  // Remember that we raised this interrupt
        if (tms9901.PinState[PIN_TIMER_INT] == PIN_HIGH)      // Raise the interrupt if the CPU wants to see it
        {
            TMS9900_RaiseInterrupt(INT_TIMER);                // Tell the TMS9900 that we have an interrupt...
        }
    }
}

void TMS9901_ClearTimerInterrupt(void)
{
    if (tms9901.TimerIntteruptInProcess)
    {
        tms9901.TimerIntteruptInProcess = 0;                  // Remember that we cleared this interrupt
        if (tms9901.PinState[PIN_TIMER_INT] == PIN_HIGH)      // Clear the interrupt if the CPU wants to see it
        {
            TMS9900_ClearInterrupt(INT_TIMER);                // Tell the TMS9900 that we have cleared an interrupt...
        }
    }
}

// End of file...
