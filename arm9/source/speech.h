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
#ifndef _SPEECH_H_
#define _SPEECH_H_

// ---------------------------------------------------------------------
// A sentinel value that we can use for fake-rendering speech samples
// ---------------------------------------------------------------------
#define SPEECH_SENTINEL_VAL     0x994a

extern u8  speech_dampen;
extern u16 readSpeech;
extern u32 speechData32;

extern void SpeechDataWrite(u8 data);
extern u8   SpeechDataRead(void);
extern void SpeechInit(void);

#endif
