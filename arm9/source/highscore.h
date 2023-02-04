// =====================================================================================
// Copyright (c) 2023 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The DS994a emulator is offered as-is, without any warranty.
// =====================================================================================

#ifndef __HIGHSCORE_H
#define __HIGHSCORE_H

#include <nds.h>

extern void highscore_init(void);
extern void highscore_save(void);
extern void highscore_display(u32 crc);

#endif
