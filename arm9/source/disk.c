// The original version of this came from Clasic99 (C) Mike Brent 
// who has graciously allowed me to use a bit of this code for
// very simplified DSK support. We utilize the TI99 Disk Controller
// DSR along with support for simple 90K and 180K raw sector disks.
// This should be enough to load up Scott Adam's Adventure Games 
// and Tunnels of Doom quests.  Despite the heavy similification
// of the code, Mike's original copyright remains below:
//
//
// (C) 2013 Mike Brent aka Tursi aka HarmlessLion.com
// This software is provided AS-IS. No warranty
// express or implied is provided.
//
// This notice defines the entire license for this code.
// All rights not explicity granted here are reserved by the
// author.
//
// You may redistribute this software provided the original
// archive is UNCHANGED and a link back to my web page,
// http://harmlesslion.com, is provided as the author's site.
// It is acceptable to link directly to a subpage at harmlesslion.com
// provided that page offers a URL for that purpose
//
// Source code, if available, is provided for educational purposes
// only. You are welcome to read it, learn from it, mock
// it, and hack it up - for your own use only.
//
// Please contact me before distributing derived works or
// ports so that we may work out terms. I don't mind people
// using my code but it's been outright stolen before. In all
// cases the code must maintain credit to the original author(s).
//
// -COMMERCIAL USE- Contact me first. I didn't make
// any money off it - why should you? ;) If you just learned
// something from this, then go ahead. If you just pinched
// a routine or two, let me know, I'll probably just ask
// for credit. If you want to derive a commercial tool
// or use large portions, we need to talk. ;)
//
// If this, itself, is a derived work from someone else's code,
// then their original copyrights and licenses are left intact
// and in full force.
//
// http://harmlesslion.com - visit the web page for contact info
//

#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fat.h>
#include "cpu/tms9900/tms9901.h"
#include "cpu/tms9900/tms9900.h"
#include "cpu/tms9918a/tms9918a.h"
#include "disk.h"

u8 TICC_CRU[8] = {0,0,0,0,0,0,1,0};
u8 TICC_REG[8] = {0,0,0,0,0,0,0,0};
u8 TICC_DIR=0;   // 0 means towards 0

u8 bDiskDeviceInstalled  __attribute__((section(".dtcm"))) = false;
u8 bDiskIsMounted        __attribute__((section(".dtcm"))) = false;
u8 diskSideSelected      = 0;  // Side 0 or Side 1
u8 driveSelected         = 1;  // We only support DSK1 currently
u8 driveReadCounter      = 0;  // Set to some non-zero value to show 'DISK READ' briefly on screen
u8 driveWriteCounter     = 0;  // Set to some non-zero value to show 'DISK WRITE' briefly on screen (takes priority over READ display)

#define ERR_DEVICEERROR     6

void disk_cru_write(u16 address, u8 data)
{
    extern u8 DiskDSR[];
    
    switch (address & 0x07)
    {
        case 0:
            bDiskDeviceInstalled = data;
            if (data)
            {
                memcpy(&MemCPU[0x4000], DiskDSR, 0x2000);
            }
            else
            {
                memset(&MemCPU[0x4000], 0xFF, 0x2000);
            }
            break;
            
        case 4:     // select drive 1
            driveSelected = 1;
            break;

        case 5:     // select drive 2
            driveSelected = 2;
            break;

        case 6:     // select drive 3
            driveSelected = 3;
            break;
            
        case 7: 
            diskSideSelected = data;
            break;
            
        default:
            break;
    }    
}

u8 ReadTICCRegister(u16 address) 
{
    if (address >= 0x5ff8) return 0xFF;
    
    switch (address & 0xFFFE)
    {
    case 0x5ff0:
        // status register
        TICC_REG[0] = 0x20; // head loaded, ready, not busy, not track 0, no index pulse, etc
        if (TICC_REG[1]==0) 
        {
            TICC_REG[0]|=0x04;
        }
        TICC_REG[0]=~TICC_REG[0];
        return TICC_REG[0];

    case 0x5ff2:
        // track register
        return TICC_REG[1];

    case 0x5ff4:
        // sector register
        return TICC_REG[2];

    case 0x5ff6:
        // data register
        return TICC_REG[3];
    }

    return 0x00;
}

void WriteTICCRegister(u16 address, u8 val) 
{
    if ((address < 0x5ff8) || (address > 0x5fff)) return;
    
    switch (address&0xfffe) 
    {
    case 0x5ff8:
        // command register
        switch (val & 0xe0) 
        {
        case 0x00:
            // restore or seek
            if (val&0x10) {
                // seek to data reg
                TICC_REG[1]=TICC_REG[3];
            } else {
                // seek to track 0
                TICC_REG[1]=0;
            }
            break;

        case 0x20:
            // step
            if (val&0x10)   {   // if update track register
                if (TICC_DIR) {
                    if (TICC_REG[1] < 255) TICC_REG[1]++;
                } else {
                    if (TICC_REG[1] > 0) TICC_REG[1]--;
                }
            }
            break;

        case 0x40:
            // step in
            if (val&0x10)   {   // if update track register
                if (TICC_REG[1] < 255) TICC_REG[1]++;
            }
            break;

        case 0x60:
            // step out
            if (val&0x10)   {   // if update track register
                if (TICC_REG[1] > 0) TICC_REG[1]--;
            }
            break;
        }
        break;

    case 0x5ffA:
        // track register
        TICC_REG[1] = val;
        break;

    case 0x5ffC:
        // sector register
        TICC_REG[2] = val;
        break;

    case 0x5ffE:
        // data register
        TICC_REG[3] = val;
        break;
    }
}

void HandleTICCSector(void)
{
    bool success = true;
    extern u8 pVDPVidMem[];
    
    if (driveSelected != 1) // We only support DSK1
    {
        MemCPU[0x8350] = ERR_DEVICEERROR;  //tbd
        tms9900.PC = 0x42a0;       // error 31 (not found)        
    }
    
    // 834A = sector number
    // 834C = drive (1-3)
    // 834D = 0: write, anything else = read
    // 834E = VDP buffer address
    u8  drive        = MemCPU[0x834C];
    u8  isRead       = MemCPU[0x834D];
    u16 sectorNumber = (MemCPU[0x834A]<<8) | MemCPU[0x834B];
    u16 destVDP      = (MemCPU[0x834E]<<8) | MemCPU[0x834F];
    u32 index        = (sectorNumber * 256);
    
    if (drive == 1)
    {
        if (isRead)
        {
            // -----------------------------------------------------------------
            // Move the 256 byte sector from the .DSK image to the VDP memory
            // -----------------------------------------------------------------
            memcpy(&pVDPVidMem[destVDP], &DiskImage[index], 256);
            *((u16*)&MemCPU[0x834A]) = sectorNumber;     // fill in the return data
            MemCPU[0x8350] = 0;                             // should still be 0 if no error occurred
            driveReadCounter = 2;
        } 
        else  // Must be write
        {
            memcpy(&DiskImage[index], &pVDPVidMem[destVDP],256);
            *((u16*)&MemCPU[0x834A]) = sectorNumber;     // fill in the return data
            MemCPU[0x8350] = 0;                             // should still be 0 if no error occurred
            driveWriteCounter = 2;
        } 
    } else success = false;

    
    if (success)
    {
        tms9900.PC = 0x4676;        // return from read or write (write normally goes through read to verify)    
    }
    else
    {
        MemCPU[0x8350] = ERR_DEVICEERROR;  //tbd
        tms9900.PC = 0x42a0;       // error 31 (not found)
    }
}

// End of file