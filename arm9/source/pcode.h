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
#ifndef _PCODE_H_
#define _PCODE_H_

#include "DS99.h"
#include "DS99_utils.h"

extern u8  pCodeEmulation;
extern u8  pcode_bank;
extern u8  pcode_visible;
extern u8  pcode_gromWriteLoHi;
extern u8  pcode_gromReadLoHi;
extern u16 pcode_gromAddress;

extern void pcode_init(void);

extern void pcode_cru_write(u16 address, u8 data);
extern u8   pcode_cru_read(u16 address);

extern void pcode_dsr_write(u16 address, u8 data);
extern u8   pcode_dsr_read(u16 address);

#endif // _PCODE_H_

// End of file
