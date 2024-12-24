// =====================================================================================
// Copyright (c) 2023-2024 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The DS994a emulator is offered as-is, without any warranty.
//
// Bits of this code came from Clasic99 Copyright (c) Mike Brent who has graciously allowed
// me to use it to help with the core TMS9900 emualation. Please see Classic99 for
// the original CPU core and please adhere to the copyright wishes of Mike Brent
// for any code that is duplicated here.
//
// Please see the README.md file as it contains much useful info.
// =====================================================================================

#ifndef TMS9900_H_
#define TMS9900_H_

extern u32   debug[];   // For debugging on the DS... 

// -----------------------------------------------------------------------------------------------------------------
// The TMS9900 Opcodes... there are 69 of these plus we reserve the first one for 'bad' and the last one for 'max'
// We pre-decode all possible (65535) 16-bit values into one of these opcodes for relatively blazingly fast speed.
// -----------------------------------------------------------------------------------------------------------------
enum _OPCODES
{
    op_bad,
    op_sra,
    op_srl,
    op_sla,
    op_src,
    op_li,
    op_ai,
    op_andi,
    op_ori,
    op_ci,
    op_stwp,
    op_stst,
    op_lwpi,
    op_limi,
    op_idle,
    op_rset,
    op_rtwp,
    op_ckon,
    op_ckof,
    op_lrex,
    op_blwp,
    op_b,
    op_x,   
    op_clr,
    op_neg, 
    op_inv, 
    op_inc, 
    op_inct,
    op_dec,
    op_dect,
    op_bl,
    op_swpb,
    op_seto,
    op_abs,
    op_jmp,
    op_jlt,
    op_jle,
    op_jeq,
    op_jhe,
    op_jgt,
    op_jne,
    op_jnc,
    op_joc,
    op_jno,
    op_jl,
    op_jh,
    op_jop,
    op_sbo,
    op_sbz,
    op_tb,
    op_coc,
    op_czc, 
    op_xor, 
    op_xop, 
    op_ldcr,
    op_stcr,
    op_mpy,
    op_div, 
    op_szc,
    op_szcb,
    op_s,
    op_sb,  
    op_c,  
    op_cb,
    op_a,  
    op_ab,  
    op_mov, 
    op_movb,
    op_soc,
    op_socb,
    op_max
};

extern u8   MemCPU[];
extern u8   MemGROM[];
extern u8   DiskDSR[];
extern u16  BankMasks[];
extern u8   MemType[0x10000>>4];

extern u8 cart_cru_shadow[16];
extern u8 super_bank;

// ----------------------------------------------------------------------------
// The entire state of the TMS9900 so we can easily save/load for save states.
// ----------------------------------------------------------------------------
typedef struct _TMS9900
{
    u32     PC;
    u32     WP;
    u32     ST;
    u32     cycles;
    s32     cycleDelta;
    u32     bankOffset;
    u8*     cartBankPtr;
    u16     bankMask;
    u16     gromAddress;
    u16     currentOp;
    u16     cpuInt;
    u16     srcAddress;
    u16     dstAddress;
    u16     idleReq;
    u16     accurateEmuFlags;
    u16     gromWriteLoHi;
    u16     gromReadLoHi;
    u16     lastIllegalOP;
    u16     illegalOPs;
} TMS9900;

extern TMS9900 tms9900;

#define WP_REG(x)  (tms9900.WP + ((x)<<1))  // Registers are every 16-bits from the WP... no bounds check so we assume program is well-behaved

// --------------------------------------------
// Some common cycle times for GROM access
// --------------------------------------------
#define GROM_READ_CYCLES            19
#define GROM_READ_ADDR_CYCLES       13
#define GROM_WRITE_ADDR_LO_CYCLES   15
#define GROM_WRITE_ADDR_HI_CYCLES   21

#define SOURCE_BYTE                 1
#define SOURCE_WORD                 2

// --------------------------------------------------------------------------------------------
// We track the flags but don't worry about the Interrupt mask at the bottom of the word
// so that we can more quickly mask/AND/OR these bits to produce the fastest possible speeds.
// We only handle one interrupt source - the VDP and that's good enough for the majority of 
// the TI library...
// --------------------------------------------------------------------------------------------
enum _STATUS_FLAGS
{
	ST_LGT        = 0x8000,   // L> Logical greater than
	ST_AGT        = 0x4000,   // A> Arithmetic greater than
	ST_EQ         = 0x2000,   // Equal
	ST_C          = 0x1000,   // Carry
	ST_OV         = 0x0800,   // Overflow
	ST_OP         = 0x0400,   // Odd parity
	ST_X          = 0x0200,   // Extended operation (not supported)
    ST_INTMASK    = 0x000F    // The Interrupt Mask 
};

// ------------------------------------------------------------
// For source addressing that only uses the lower nibble...
// ------------------------------------------------------------
#define REG_GET_FROM_OPCODE() (tms9900.currentOp & 0xf)

// ---------------------------------------------------------------------------------------------------------
// Flag handling is a mix of table lookup using the Classic99 StatusLookup16[] and StatusLookup8[] tables 
// plus some handling for when we add values, subtract values or compare values...
// ---------------------------------------------------------------------------------------------------------
#define STATUS_CLEAR_LAE        (tms9900.ST & ~(ST_LGT | ST_AGT | ST_EQ))
#define STATUS_CLEAR_LAEC       (tms9900.ST & ~(ST_LGT | ST_AGT | ST_EQ | ST_C))
#define STATUS_CLEAR_LAEP       (tms9900.ST & ~(ST_LGT | ST_AGT | ST_EQ | ST_OP))
#define STATUS_CLEAR_LAECO      (tms9900.ST & ~(ST_LGT | ST_AGT | ST_EQ | ST_C | ST_OV))
#define STATUS_CLEAR_LAEOP      (tms9900.ST & ~(ST_LGT | ST_AGT | ST_EQ | ST_OV | ST_OP))
#define STATUS_CLEAR_LAECOP     (tms9900.ST & ~(ST_LGT | ST_AGT | ST_EQ | ST_C | ST_OV | ST_OP))

// --------------------------------------------------------------------------------------------------
// The accurate emulation flags.... either of these will put the emulator into a more accurate mode
// but it will come at the cost of some slowdown... mostly of relevance to the old DS hardware.
// --------------------------------------------------------------------------------------------------
#define ACCURATE_EMU_IDLE       0x01
#define ACCURATE_EMU_TIMER      0x02
#define ACCURATE_EMU_SAMS       0x04

// --------------------------------------------------------
// Interrupt Masks... we only handle VDP and Timer
// --------------------------------------------------------
#define INT_VDP                 2
#define INT_TIMER               1

// ---------------------------------------------------------------------
// A sentinal value that we can use for fake-rendering speech samples
// ---------------------------------------------------------------------
#define SPEECH_SENTINAL_VAL     0x994a

// -------------------------------------------------------------------------------------------------
// The memory type tell us what's in a particular memory location. Be careful to keep MF_MEM16
// as a zero value which helps with fast determination of timing penalties during run-time.
//
// Any memory access to a non-zero value here will incur a timing pentalty. This pentalty is also
// incurred by the VDP which is technically on the 16-bit bus but is wired to the high byte (which 
// I guess incurs the pentaly... though I'm unsure why... we follow Classic99 timing here...)
//
// Be careful if you change this table as we save out the MemTypes[] as part of the Save State
// in case we have mapped in/out some DSR specific registers... the reserved spots can be 
// repurposed in the future to add more memory types / register access for other peripherals.
// -------------------------------------------------------------------------------------------------
enum _MEM_TYPE
{
    MF_MEM16 = 0,   // This is the 2000h byte console ROM or the 16-bit Scratchpad RAM... Actung!! Must be zero as we want to make a fast determination at run-time.
    MF_RAM8,        // This is the 32K of expanded 8-bit RAM (no banking... MF_SAMS8 is used for that)
    MF_SOUND,       // This is normal TI sound chip access
    MF_SPEECH,      // This is TI Speech chip access
    MF_CART,        // This is banked TI Cart access at >6000
    MF_CART_NB,     // This is non-banked TI Cart access at >6000
    MF_VDP_R,       // This is the TMS9918a Video chip access - Read
    MF_VDP_W,       // This is the TMS9918a Video chip access - Write
    MF_GROMR,       // This is the system GROM read access
    MF_GROMW,       // This is the system GROM write access
    MF_SAMS,        // This is the SAMS memory expanded access registers at >4000
    MF_SAMS8,       // This is RAM8 except that it's banked into a larger SAMS memory pool
    MF_MBX,         // This is the MBX register that causes a bank switch at >7000
    MF_PERIF,       // This is the Peripheral ROM space (Disk Controller DSR)
    MF_DISK,        // This is the TI Disk Controller Registers
    MF_RES1,        // Reserved for future use...
    MF_RES2,        // Reserved for future use...
    MF_RES3,        // Reserved for future use...
    MF_RES4,        // Reserved for future use...
    MF_RES5,        // Reserved for future use...
    MF_RES6,        // Reserved for future use...
    MF_UNUSED,      // This is some unused memory space... will return 0xFF (or, really, whatever was left in the MemCPU[] buffer at that address)
};

extern void TMS9900_Reset(void);
extern void TMS9900_Run(void);
extern void TMS9900_Kickoff(void);
extern void TMS9900_RaiseInterrupt(u16 iMask);
extern void TMS9900_ClearInterrupt(u16 iMask);
extern void TMS9900_SetAccurateEmulationFlag(u16 flag);
extern void TMS9900_ClearAccurateEmulationFlag(u16 flag);
extern u32  SAMS_Read32(u32 address);
extern void SAMS_Write32(u32 address, u32 data);
extern void SAMS_MapDSR(u8 dataBit);
extern void SAMS_cru_write(u16 address, u8 dataBit);
extern void cart_cru_write(u16 cruAddress, u8 dataBit);
extern u8   cart_cru_read(u16 cruAddress);
extern void WriteBankMBX(u8 bank);

#endif
