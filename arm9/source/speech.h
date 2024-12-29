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

enum SPEECH_STATE
{
    SS_IDLE,
    SS_READDATA,
};

typedef struct
{
    u8  speechState;
    u8  speechData;
    u8  speechStatus;
    u8  speechDampen;
    u16 speechAddress;
    u16 reserved;
    
    u32 speechData32;
    u32 prevData32;
} Speech_t;

extern Speech_t Speech;

extern void SpeechDataWrite(u8 data);
extern u8   SpeechDataRead(void);
extern void SpeechInit(void);

#endif
