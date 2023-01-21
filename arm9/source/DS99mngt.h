// =====================================================================================
// Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The TI99DS emulator is offered as-is, without any warranty.
// =====================================================================================
#ifndef _DS99MNGT_H
#define _DS99MNGT_H

#include <nds.h>
#include "DS99.h"
#include "cpu/tms9918a/tms9918a.h"
#include "cpu/sn76496/SN76496.h"

extern u32 file_crc;
extern SN76496 sncol;

// --------------------------------------------------
// Some CPU and VDP and SGM stuff that we need
// --------------------------------------------------
extern byte Loop9918(void);
extern u8 TI99Init(char *szGame);
extern void TI99SetPal(void);
extern void TI99UpdateScreen(void);
extern void TI99Run(void);
extern void getfile_crc(const char *path);
extern void TI99LoadState();
extern void TI99SaveState();
extern u8 loadrom(const char *path,u8 * ptr, int nmemb);
extern u32 LoopTMS9900();

#endif
