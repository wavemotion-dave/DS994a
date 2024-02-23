// =====================================================================================
// Copyright (c) 2023-2024 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided this copyright notice is used and wavemotion-dave is thanked profusely.
//
// The DS994a emulator is offered as-is, without any warranty.
//
// Bits of this code came from Clasic99 (C) Mike Brent who has graciously allowed
// me to use it to help with the core TMS9900 emualation. Please see Classic99 for
// the original CPU core and please adhere to the copyright wishes of Mike Brent
// for any code that is duplicated here.
//
//
// Please note: Opcode timing is not exact... I used the timings from Classic99 
// but in a few spots (I'm looking at you DIV) I've approximated and don't get
// down to a fully cycle-exact timing. It's good enough to play games on.
//
// I've run both cputest and MP test against this code... those are acid tests
// for the TMS9900 CPU and it passes the cputest and fails only the XOP on the MP
// test which is expected since I'm not handling that.
//
// Please see the README.md file as it contains much useful info.
// =====================================================================================


// ---------------------------------------------------------------------------------------------
// GCC at --O2 and above optmization will turn an 8-bit switch into a jump table which will 
// produce the fastest code possible. Each instruction below handles their own cycle count 
// and includes all memory fetches except the 4 cycle penalty and extra waits for GROM access.
// ---------------------------------------------------------------------------------------------
        case op_sra:
        {
            AddCycleCount(12);      // base value
            u16 rData = REG_GET_FROM_OPCODE();                          // Workspace register to shift
            u16 numBits = (tms9900.currentOp >> 4) & 0x0F;              // Number of bits to shift
            
            if (!numBits) // Special handling per TMS9900 data manual - we now use the least significant 4 bits of R0 as the shift count...
            {
                AddCycleCount(8);                                       // Pentalty for redirect fetch of shift bits from R0
                numBits = ReadRAM16(WP_REG(0)) & 0x0F;                  // It's possible THIS can also be zero in which case the shift count is 16
                if (!numBits) numBits = 16;                             // Maximum shift count...
            }          

            AddCycleCount(2*numBits);                                   // Each bit shifted costs 2 CPU cycles
            data16 = ReadRAM16(WP_REG(rData));                          // Read data that will be shifted
            tms9900.ST = STATUS_CLEAR_LAEC;                             // Clear the 4 status bits for the shift operation - we'll set the bits below
            
            // ---------------------------------------------------------------------------------------------
            // This is taken from Classic99 which does it the long-handed way... but it's bullet-accurate!
            // ---------------------------------------------------------------------------------------------
            u16 x3=0;  
            u16 x4=data16&0x8000;
            for (u16 x2=0; x2<numBits; x2++)
            { 
                x3=data16&1;        /* save carry */
                data16=data16>>1;   /* shift once */
                data16=data16|x4;   /* extend sign bit */
            }

            if (x3) tms9900.ST |= ST_C;
            tms9900.ST |= CompareZeroLookup16[data16];                  // And set the zero-compare bits
            
            MemoryWrite16(WP_REG(rData), data16);                       // Write the data back to the proper memory location    
        }
        break;

    case op_srl:
        {
            AddCycleCount(12);      // base value
            u16 rData = REG_GET_FROM_OPCODE();                          // Workspace register to shift
            u16 numBits = (tms9900.currentOp >> 4) & 0x0F;              // Number of bits to shift
            
            if (!numBits) // Special handling per TMS9900 data manual - we now use the least significant 4 bits of R0 as the shift count...
            {
                AddCycleCount(8);                                       // Pentalty for redirect fetch of shift bits from R0
                numBits = ReadRAM16(WP_REG(0)) & 0x0F;                  // It's possible THIS can also be zero in which case the shift count is 16
                if (!numBits) numBits = 16;                             // Maximum shift count...
            }          

            AddCycleCount(2*numBits);                                   // Each bit shifted costs 2 CPU cycles
            data16 = ReadRAM16(WP_REG(rData));                          // Read data that will be shifted
            tms9900.ST = STATUS_CLEAR_LAEC;                             // Clear the 4 status bits for the shift operation - we'll set the bits below
            
            // ---------------------------------------------------------------------------------------------
            // This is taken from Classic99 which does it the long-handed way... but it's bullet-accurate!
            // ---------------------------------------------------------------------------------------------
            u16 x3=0;  
            for (u16 x2=0; x2<numBits; x2++)
            { 
                x3=data16&1;
                data16=data16>>1;
            }
            if (x3) tms9900.ST |= ST_C;
            tms9900.ST |= CompareZeroLookup16[data16];                  // And set the zero-compare bits
            
            MemoryWrite16(WP_REG(rData), data16);                       // Write the data back to the proper memory location           
        }
        break;

    case op_src:
        {
            AddCycleCount(12);      // base value
            u16 rData = REG_GET_FROM_OPCODE();                          // Workspace register to shift
            u16 numBits = (tms9900.currentOp >> 4) & 0x0F;              // Number of bits to shift
            
            if (!numBits) // Special handling per TMS9900 data manual - we now use the least significant 4 bits of R0 as the shift count...
            {
                AddCycleCount(8);                                       // Pentalty for redirect fetch of shift bits from R0
                numBits = ReadRAM16(WP_REG(0)) & 0x0F;                  // It's possible THIS can also be zero in which case the shift count is 16
                if (!numBits) numBits = 16;                             // Maximum shift count...
            }          

            AddCycleCount(2*numBits);                                   // Each bit shifted costs 2 CPU cycles
            data16 = ReadRAM16(WP_REG(rData));                          // Read data that will be shifted
            tms9900.ST = STATUS_CLEAR_LAEC;                             // Clear the 4 status bits for the shift operation - we'll set the bits below
            
            // ---------------------------------------------------------------------------------------------
            // This is taken from Classic99 which does it the long-handed way... but it's bullet-accurate!
            // ---------------------------------------------------------------------------------------------
            u16 x4=0;
            for (u16 x2=0; x2<numBits; x2++)
            {
                x4=data16&0x1;
                data16=data16>>1;
                if (x4) 
                {
                    data16=data16|0x8000;
                } else data16 &= 0x7FFF;
            }
            if (x4) tms9900.ST |= ST_C;                                 // If we ever saw a low-bit shift out, the Carry will be set
            tms9900.ST |= CompareZeroLookup16[data16];                  // And set the zero-compare bits
            
            MemoryWrite16(WP_REG(rData), data16);                       // Write the data back to the proper memory location           
        }
        break;

    case op_sla:
        {
            AddCycleCount(12);      // base value
            u16 rData = REG_GET_FROM_OPCODE();                          // Workspace register to shift
            u16 numBits = (tms9900.currentOp >> 4) & 0x0F;              // Number of bits to shift
            
            if (!numBits) // Special handling per TMS9900 data manual - we now use the least significant 4 bits of R0 as the shift count...
            {
                AddCycleCount(8);                                       // Pentalty for redirect fetch of shift bits from R0
                numBits = ReadRAM16(WP_REG(0)) & 0x0F;                  // It's possible THIS can also be zero in which case the shift count is 16
                if (!numBits) numBits = 16;                             // Maximum shift count...
            }          
            
            AddCycleCount(2*numBits);                                   // Each bit shifted costs 2 CPU cycles
            data16 = ReadRAM16(WP_REG(rData));                          // Read data that will be shifted
            tms9900.ST = STATUS_CLEAR_LAECO;                            // Clear the 5 status bits for the shift operation - we'll set the bits below
            
            // ---------------------------------------------------------------------------------------------
            // This is taken from Classic99 which does it the long-handed way... but it's bullet-accurate!
            // ---------------------------------------------------------------------------------------------
            u16 x4=data16&0x8000;
            u16 x3=0;
            for (u16 x2=0; x2<numBits; x2++)
            {
                x3=data16&0x8000;
                data16=data16<<1;
                if ((data16&0x8000)!=x4) tms9900.ST |= ST_OV;
            }            
            if (x3) tms9900.ST |= ST_C;                                 // If we ever saw a high-bit shift out, the Carry will be set
            tms9900.ST |= CompareZeroLookup16[data16];                  // And set the zero-compare bits
            
            MemoryWrite16(WP_REG(rData), data16);                       // Write the data back to the proper memory location            
        }
        break;

    case op_li:
        {
            AddCycleCount(12);
            u16 rData = REG_GET_FROM_OPCODE();
            data16 = ReadPC16();
            WriteRAM16(WP_REG(rData), data16);                          // Load immediate will pull the next word from memory and store it into the desired register.
            tms9900.ST = STATUS_CLEAR_LAE | CompareZeroLookup16[data16];
        }
        break;

    case op_stwp:
        {
            AddCycleCount(8);
            u16 rData = REG_GET_FROM_OPCODE();
            WriteRAM16(WP_REG(rData), tms9900.WP);
        }
        break;

    case op_stst:
        {
            AddCycleCount(8);
            u16 rData = REG_GET_FROM_OPCODE();
            WriteRAM16(WP_REG(rData), tms9900.ST);
        }
        break;

    case op_lwpi:
        AddCycleCount(10);
        tms9900.WP = ReadPC16();
        break;

    case op_limi:
        AddCycleCount(16);
        tms9900.ST = (tms9900.ST & ~ST_INTMASK);
        tms9900.ST |= (ReadPC16() & ST_INTMASK);
        break;

    case op_andi:
        {
            AddCycleCount(14);
            u16 rData = REG_GET_FROM_OPCODE();
            u16 sData = ReadRAM16(WP_REG(rData));
            u16 dData = ReadPC16();
            data16 = (sData & dData);
            WriteRAM16(WP_REG(rData), data16);
            tms9900.ST = STATUS_CLEAR_LAE | CompareZeroLookup16[data16];
        }
        break;

    case op_ori:
        {
            AddCycleCount(14);
            u16 rData = REG_GET_FROM_OPCODE();
            u16 sData = ReadRAM16(WP_REG(rData));
            u16 dData = ReadPC16();
            data16 = (sData | dData);
            WriteRAM16(WP_REG(rData), data16);
            tms9900.ST = STATUS_CLEAR_LAE | CompareZeroLookup16[data16];
        }
        break;

    case op_ai:
        {
            AddCycleCount(14);
            u16 rData = REG_GET_FROM_OPCODE();
            u16 sData = ReadRAM16(WP_REG(rData));
            u16 dData = ReadPC16();
            data16 = sData + dData;
            WriteRAM16(WP_REG(rData), data16);
            
            // Set the status flags the Classic99 way...
            tms9900.ST = STATUS_CLEAR_LAECO | CompareZeroLookup16[data16];
            if (data16 < sData) tms9900.ST |= ST_C;                                                         // Data wrapped... set C
            if (((sData&0x8000)==(dData&0x8000))&&((data16&0x8000)!=(dData&0x8000))) tms9900.ST |= ST_OV;   // if signed math overflow... set OV
        }
        break;

    case op_ci:
        {
            AddCycleCount(14);
            u16 rData = REG_GET_FROM_OPCODE();
            u16 dData = ReadRAM16(WP_REG(rData));   // The destination word to compare
            u16 sData = ReadPC16();                 // For the compare immediate we need the next PC word as the source
            
            // Set the status flags the Classic99 way...
            tms9900.ST = STATUS_CLEAR_LAE;
            if (dData > sData)        tms9900.ST |= ST_LGT;
            else if (dData == sData)  tms9900.ST |= ST_EQ;
            if ((dData&0x8000)==(sData&0x8000))
            {
                if (dData > sData)    tms9900.ST |= ST_AGT;
            }
            else if (sData&0x8000)    tms9900.ST |= ST_AGT;
        }
        break;

    case op_rtwp:
        AddCycleCount(14);
        tms9900.ST = ReadRAM16(WP_REG(15));  // Restore Status
        tms9900.PC = ReadRAM16(WP_REG(14));  // Restore Program Counter
        tms9900.WP = ReadRAM16(WP_REG(13));  // Restore Working Pointer - must me done last or the register accesses above will be wrong
        break;

    case op_blwp:
        AddCycleCount(26);
        Ts(SOURCE_WORD);
        TMS9900_ContextSwitch(tms9900.srcAddress);
        break;

    case op_clr:
        AddCycleCount(10);
        Ts(SOURCE_WORD);
        MemoryReadHidden(tms9900.srcAddress);
        MemoryWrite16(tms9900.srcAddress, 0x0000);
        break;

    case op_x:
        AddCycleCount(4);   // Plus the instruction below which will add cycles. Do we need to check for recursion?!
        Ts(SOURCE_WORD);
        tms9900.currentOp = MemoryRead16(tms9900.srcAddress);
        ExecuteOneInstruction(tms9900.currentOp);
        break;

    case op_neg:
        AddCycleCount(12);
        Ts(SOURCE_WORD);
        data16 = MemoryRead16(tms9900.srcAddress);
        data16 = (~data16) + 1;
        tms9900.ST = STATUS_CLEAR_LAECO | CompareZeroLookup16[data16];
        if (data16 == 0) tms9900.ST |= ST_C;
        else if (data16 == 0x8000) tms9900.ST |= ST_OV;
        MemoryWrite16(tms9900.srcAddress, data16);
        break;

    case op_inv:
        AddCycleCount(10);
        Ts(SOURCE_WORD);
        data16 = MemoryRead16(tms9900.srcAddress);
        data16 = ~data16;
        tms9900.ST = STATUS_CLEAR_LAE | CompareZeroLookup16[data16];
        MemoryWrite16(tms9900.srcAddress, data16);
        break;

    case op_inc:
        {
            AddCycleCount(10);
            Ts(SOURCE_WORD);
            u16 sData = MemoryRead16(tms9900.srcAddress);
            u16 dData = 1;
            data16 = sData + dData;
            MemoryWrite16(tms9900.srcAddress, data16);
            
            // Set the status flags the Classic99 way...
            tms9900.ST = STATUS_CLEAR_LAECO | CompareZeroLookup16[data16];
            if (data16 < sData) tms9900.ST |= ST_C;                                                         // Data wrapped... set C
            if (((sData&0x8000)==(dData&0x8000))&&((data16&0x8000)!=(dData&0x8000))) tms9900.ST |= ST_OV;   // if signed math overflow... set OV
        }
        break;

    case op_inct:
        {
            AddCycleCount(10);
            Ts(SOURCE_WORD);
            u16 sData = MemoryRead16(tms9900.srcAddress);
            u16 dData = 2;
            data16 = sData + dData;
            MemoryWrite16(tms9900.srcAddress, data16);
            
            // Set the status flags the Classic99 way...
            tms9900.ST = STATUS_CLEAR_LAECO | CompareZeroLookup16[data16];
            if (data16 < sData) tms9900.ST |= ST_C;                                                         // Data wrapped... set C
            if (((sData&0x8000)==(dData&0x8000))&&((data16&0x8000)!=(dData&0x8000))) tms9900.ST |= ST_OV;   // if signed math overflow... set OV
        }
        break;

    case op_dec:
        {
            AddCycleCount(10);
            Ts(SOURCE_WORD);
            u16 sData = 1;
            u16 dData = MemoryRead16(tms9900.srcAddress);
            data16 = dData - sData;
            MemoryWrite16(tms9900.srcAddress, data16);
            
            // Set the status flags the Classic99 way... Tursi discovered that any number minus 0 is seting the carry on actual hardware so we do the same...
            tms9900.ST = STATUS_CLEAR_LAECO | CompareZeroLookup16[data16];
            if ((data16 < dData) || (sData == 0))                                    tms9900.ST |= ST_C;
            if (((sData&0x8000)!=(dData&0x8000))&&((data16&0x8000)!=(dData&0x8000))) tms9900.ST |= ST_OV;
        }
        break;

    case op_dect:
        {
            AddCycleCount(10);
            Ts(SOURCE_WORD);
            u16 sData = 2;
            u16 dData = MemoryRead16(tms9900.srcAddress);
            data16 = dData - sData;
            MemoryWrite16(tms9900.srcAddress, data16);
            
            // Set the status flags the Classic99 way... Tursi discovered that any number minus 0 is seting the carry on actual hardware so we do the same...
            tms9900.ST = STATUS_CLEAR_LAECO | CompareZeroLookup16[data16];
            if ((data16 < dData) || (sData == 0))                                    tms9900.ST |= ST_C;
            if (((sData&0x8000)!=(dData&0x8000))&&((data16&0x8000)!=(dData&0x8000))) tms9900.ST |= ST_OV;
        }
        break;

    case op_bl:
        {
            AddCycleCount(12);
            Ts(SOURCE_WORD);
            WriteRAM16(WP_REG(11), tms9900.PC);
            tms9900.PC = tms9900.srcAddress;
        }
        break;

    case op_swpb:
        {
            AddCycleCount(10);
            Ts(SOURCE_WORD);
            data16 = MemoryRead16(tms9900.srcAddress);
            MemoryWrite16(tms9900.srcAddress, ((data16<<8) | (data16>>8)));
        }
        break;

    case op_seto:
        {
            AddCycleCount(10);
            Ts(SOURCE_WORD);
            MemoryReadHidden(tms9900.srcAddress);
            MemoryWrite16(tms9900.srcAddress, 0xFFFF);
        }
        break;

    case op_abs:
        {
            AddCycleCount(12);
            Ts(SOURCE_WORD);
            data16 = MemoryRead16(tms9900.srcAddress);
            tms9900.ST = STATUS_CLEAR_LAECO | CompareZeroLookup16[data16];
            if (data16 & 0x8000)
            {
                AddCycleCount(2);
                if (data16 == 0x8000) tms9900.ST |= ST_OV;
                data16 = -data16;
                MemoryWrite16(tms9900.srcAddress, data16);
            }
        }
        break;

    case op_b:
        AddCycleCount(8);
        Ts(SOURCE_WORD);
        tms9900.PC = tms9900.srcAddress;
        break;

    case op_sbo:
        {
            AddCycleCount(12);
            u16 cruAddress = ReadRAM16(WP_REG(12)) & 0x1FFE;  // R12 is the CRU Base register using bits 3 to 14
            cruAddress = (cruAddress>>1) + (s8)(tms9900.currentOp & 0xFF);  // Displacement is 8-bit signed
            TMS9901_WriteCRU(cruAddress, 1, 1);
        }
        break;

    case op_sbz:
        {
            AddCycleCount(12);
            u16 cruAddress = ReadRAM16(WP_REG(12)) & 0x1FFE;  // R12 is the CRU Base register using bits 3 to 14
            cruAddress = (cruAddress>>1) + (s8)(tms9900.currentOp & 0xFF);  // Displacement is 8-bit signed
            TMS9901_WriteCRU(cruAddress, 0, 1);
        }
        break;

    case op_tb:
        {
            AddCycleCount(12);
            u16 cruAddress = ReadRAM16(WP_REG(12)) & 0x1FFE;  // R12 is the CRU Base register using bits 3 to 14
            cruAddress = (cruAddress>>1) + (s8)(tms9900.currentOp & 0xFF);  // Displacement is 8-bit signed
            if (TMS9901_ReadCRU(cruAddress, 1) & 1) tms9900.ST |= ST_EQ;
            else tms9900.ST &= ~ST_EQ;
        }
        break;

    case op_coc:
        {
            AddCycleCount(14);
            Ts(SOURCE_WORD); TdWA();
            u16 s = MemoryRead16(tms9900.srcAddress);
            u16 d = MemoryRead16(tms9900.dstAddress);
            if ((s & d) == s) tms9900.ST |= ST_EQ;
            else tms9900.ST &= ~ST_EQ;
        }
        break;

    case op_czc:
        {
            AddCycleCount(14);
            Ts(SOURCE_WORD); TdWA();
            u16 s = MemoryRead16(tms9900.srcAddress);
            u16 d = MemoryRead16(tms9900.dstAddress);
            if ((s & ~d) == s) tms9900.ST |= ST_EQ;
            else tms9900.ST &= ~ST_EQ;
        }
        break;

    case op_xor:
        {
            AddCycleCount(14);            
            u16 rData = (tms9900.currentOp >> 6) & 0x0F;
            Ts(SOURCE_WORD);  // Forces 16-bit source address mode
            data16 = ReadRAM16(WP_REG(rData)) ^ MemoryRead16(tms9900.srcAddress);
            tms9900.ST = STATUS_CLEAR_LAE | CompareZeroLookup16[data16];            
            WriteRAM16(WP_REG(rData), data16);
        }
        break;

    case op_ldcr:
        {
            u16 cruAddress = ReadRAM16(WP_REG(12)) & 0x1FFE;  // R12 is the CRU Base register using bits 3 to 14
            u8 numBits = (tms9900.currentOp >> 6) & 0x0F;     // And this is the number of bits to transfer
            if (!numBits) numBits = 16; // Zero bits means transfer a full WORD complement of 16 bits
            
            AddCycleCount(20 + (numBits*2));    // Base cycles is 20 plus 2 for every bit transferred... not sure why STCR isn't the same            
            
            Ts((numBits > 8) ? 2:1);  // Forces either 8-bit or 16-bit addressing depending on number of bits...
            
            if (numBits > 8)    // Is word access
            {
                data16 = MemoryRead16(tms9900.srcAddress);
                tms9900.ST = STATUS_CLEAR_LAEOP | CompareZeroLookup16[data16];
                TMS9901_WriteCRU(cruAddress>>1, data16, numBits);      // The CRU is expecting the bits to already be divided by 2 so it's easier for CRU handling
            }
            else    // Is byte access
            {
                data8 = MemoryRead8(tms9900.srcAddress);
                tms9900.ST = STATUS_CLEAR_LAEOP | CompareZeroLookup8[data8];
                TMS9901_WriteCRU(cruAddress>>1, (u16)data8, numBits);      // The CRU is expecting the bits to already be divided by 2 so it's easier for CRU handling
            }
        }
        break;

    case op_stcr:
        {
            AddCycleCount(42);  // base value
            
            u16 cruAddress = ReadRAM16(WP_REG(12)) & 0x1FFE;  // R12 is the CRU Base register using bits 3 to 14
            u8 numBits = (tms9900.currentOp >> 6) & 0x0F;     // And this is the number of bits to transfer
            if (!numBits) numBits = 16; // Zero bits means transfer a full WORD complement of 16 bits

            Ts((numBits > 8) ? 2:1);  // Forces either 8-bit or 16-bit addressing depending on number of bits...

            if (numBits > 8)    // Is word access
            {
                AddCycleCount(16);
                data16 = TMS9901_ReadCRU(cruAddress>>1, numBits);       // The CRU is expecting the bits to already be divided by 2 so it's easier for CRU handling
                tms9900.ST = STATUS_CLEAR_LAEOP | CompareZeroLookup16[data16];
                MemoryReadHidden(tms9900.srcAddress);
                MemoryWrite16(tms9900.srcAddress, data16);
            }
            else    // Is byte access
            {
                AddCycleCount(2);
                data8 = (u8)TMS9901_ReadCRU(cruAddress>>1, numBits);   // The CRU is expecting the bits to already be divided by 2 so it's easier for CRU handling
                tms9900.ST = STATUS_CLEAR_LAEOP | CompareZeroLookup8[(u8)data8];
                MemoryReadHidden(tms9900.srcAddress);
                MemoryWrite8(tms9900.srcAddress, data8);
            }
        }
        break;

    // TMS9900 Data Manual talks about special case for when W15 is used... but not sure yet what it entails (if anything)
    case op_div:
        {
            AddCycleCount(16);
            Ts_NoInline(SOURCE_WORD); TdWA();
            u16 sData = MemoryRead16(tms9900.srcAddress);
            u32 dData = (u32)MemoryRead16(tms9900.dstAddress);

            // If the source operand is greater than the desination operand, we can perform the division
            if (sData > dData)
            {
                AddCycleCount(76);  // Not accurate but good enough. See Classic99 for more accurate handling by using a bitwise division scheme
                dData = (dData << 16) | MemoryRead16(tms9900.dstAddress+2);
                MemoryWrite16(tms9900.dstAddress+0, (u16)(dData / sData));    // quotient
                MemoryWrite16(tms9900.dstAddress+2, (u16)(dData % sData));    // remainder
                tms9900.ST &= ~ST_OV;
            }
            else // Otherwise we set the overflow per the TMS9900 data manual
            {
                tms9900.ST |= ST_OV;
            }
        }
        break;

    // TMS9900 Data Manual talks about special case for when W15 is used... but not sure yet what it entails (if anything)
    case op_mpy:
        {
            AddCycleCount(52);
            Ts_NoInline(SOURCE_WORD); TdWA();
            u16 sData = MemoryRead16(tms9900.srcAddress);
            u16 dData = MemoryRead16(tms9900.dstAddress);
            u32 result = (u32)sData * (u32)dData;
            // No flags needed on this one!! Wish they were all so easy...
            MemoryWrite16(tms9900.dstAddress+0, (u16)(result>>16)); // Most significant word
            MemoryWrite16(tms9900.dstAddress+2, (u16)(result>>0));  // Least significant word
        }
        break;

    case op_szc:
        {
            AddCycleCount(14);
            Ts_NoInline(SOURCE_WORD);
            u16 sData = MemoryRead16(tms9900.srcAddress);
            Td_NoInline(SOURCE_WORD);
            u16 dData = MemoryRead16(tms9900.dstAddress);
            data16 = (~sData) & dData;
            tms9900.ST = STATUS_CLEAR_LAE | CompareZeroLookup16[data16];
            MemoryWrite16(tms9900.dstAddress, data16);
        }
        break;

    case op_szcb:
        {
            AddCycleCount(14);
            Ts_NoInline(SOURCE_BYTE);
            u8 sData = MemoryRead8(tms9900.srcAddress);
            Td_NoInline(SOURCE_BYTE);
            u8 dData = MemoryRead8(tms9900.dstAddress);
            data8 = (~sData) & dData;
            tms9900.ST = STATUS_CLEAR_LAEP | CompareZeroLookup8[data8];
            MemoryWrite8(tms9900.dstAddress, data8);
        }
        break;

    case op_s:
        {
            AddCycleCount(14);
            Ts_NoInline(SOURCE_WORD);
            u16 sData = MemoryRead16(tms9900.srcAddress);
            Td_NoInline(SOURCE_WORD);
            u16 dData = MemoryRead16(tms9900.dstAddress);
            data16 = dData - sData;
            MemoryWrite16(tms9900.dstAddress, data16);
            
            // Set the status flags the Classic99 way... Tursi discovered that any number minus 0 is seting the carry on actual hardware so we do the same...
            tms9900.ST = STATUS_CLEAR_LAECO | CompareZeroLookup16[data16];
            if ((data16 < dData) || (sData == 0))                                    tms9900.ST |= ST_C;
            if (((sData&0x8000)!=(dData&0x8000))&&((data16&0x8000)!=(dData&0x8000))) tms9900.ST |= ST_OV;
        }
        break;

    case op_sb:
        {
            AddCycleCount(14);
            Ts_NoInline(SOURCE_BYTE);
            u8 sData = MemoryRead8(tms9900.srcAddress);
            Td_NoInline(SOURCE_BYTE);
            u8 dData = MemoryRead8(tms9900.dstAddress);
            data8 = dData - sData;
            MemoryWrite8(tms9900.dstAddress, data8);
            
            // Set the status flags the Classic99 way... Tursi discovered that any number minus 0 is seting the carry on actual hardware so we do the same...
            tms9900.ST = STATUS_CLEAR_LAECOP | CompareZeroLookup8[data8];
            if ((data8 < dData) || (sData == 0))                                    tms9900.ST |= ST_C;
            if (((sData&0x80)!=(dData&0x80))&&((data8&0x80)!=(dData&0x80)))         tms9900.ST |= ST_OV;
        }
        break;

    case op_c:
        {
            AddCycleCount(14);
            Ts_NoInline(SOURCE_WORD);
            u16 sData = MemoryRead16(tms9900.srcAddress);
            Td(SOURCE_WORD);
            u16 dData = MemoryRead16(tms9900.dstAddress);

            // Set the status flags the Classic99 way...
            tms9900.ST = STATUS_CLEAR_LAE;
            if (sData > dData)          tms9900.ST |= ST_LGT;
            else if (sData==dData)      tms9900.ST |= ST_EQ;
            if ((sData&0x8000)==(dData&0x8000))
            {
                if (sData > dData)      tms9900.ST |= ST_AGT;
            }
            else
            {
                if (dData&0x8000)       tms9900.ST |= ST_AGT;
            }
        }
        break;

    case op_cb:
        {
            AddCycleCount(14);
            Ts_NoInline(SOURCE_BYTE);
            u8 sData = MemoryRead8(tms9900.srcAddress);
            Td(SOURCE_BYTE);
            u8 dData = MemoryRead8(tms9900.dstAddress);

            // Set the status flags the Classic99 way...
            tms9900.ST = STATUS_CLEAR_LAEP;
            tms9900.ST |= ParityTable[sData];
            if (sData > dData)          tms9900.ST |= ST_LGT;
            else if (sData==dData)      tms9900.ST |= ST_EQ;
            if ((sData&0x80)==(dData&0x80))
            {
                if (sData > dData)      tms9900.ST |= ST_AGT;
            }
            else
            {
                if (dData&0x80)         tms9900.ST |= ST_AGT;
            }
        }
        break;

    case op_a:
        {
            AddCycleCount(14);
            Ts(SOURCE_WORD);
            u16 sData = MemoryRead16(tms9900.srcAddress);
            Td(SOURCE_WORD);
            u16 dData = MemoryRead16(tms9900.dstAddress);
            data16 = sData + dData;
            MemoryWrite16(tms9900.dstAddress, data16);
            
            // Set the status flags the Classic99 way...
            tms9900.ST = STATUS_CLEAR_LAECO | CompareZeroLookup16[data16];
            if (data16 < sData) tms9900.ST |= ST_C;                                                         // Data wrapped... set C
            if (((sData&0x8000)==(dData&0x8000))&&((data16&0x8000)!=(dData&0x8000))) tms9900.ST |= ST_OV;   // if signed math overflow... set OV
        }
        break;

    case op_ab:
        {
            AddCycleCount(14);
            Ts(SOURCE_BYTE);
            u8 sData = MemoryRead8(tms9900.srcAddress);
            Td(SOURCE_BYTE);
            u8 dData = MemoryRead8(tms9900.dstAddress);
            data8 = sData + dData;
            MemoryWrite8(tms9900.dstAddress, data8);

            // Set the status flags the Classic99 way...
            tms9900.ST = STATUS_CLEAR_LAECOP | CompareZeroLookup8[data8];
            if (data8 < sData) tms9900.ST |= ST_C;                                                 // Data wrapped... set C
            if (((sData&0x80)==(dData&0x80))&&((data8&0x80)!=(dData&0x80))) tms9900.ST |= ST_OV;   // if signed math overflow... set OV
        }
        break;

    // In experiments the mov and movb instructions are heavy hitters on the TI99... look to optimize this as much as possible...
    case op_mov:
        AddCycleCount(14);
        Ts(SOURCE_WORD);
        data16 = MemoryRead16(tms9900.srcAddress);
        tms9900.ST = STATUS_CLEAR_LAE | CompareZeroLookup16[data16];
        Td(SOURCE_WORD);
        MemoryReadHidden(tms9900.dstAddress);
        MemoryWrite16(tms9900.dstAddress, data16);
        break;

    case op_movb:
        AddCycleCount(14);
        Ts(SOURCE_BYTE);
        data8 = MemoryRead8(tms9900.srcAddress);
        tms9900.ST = STATUS_CLEAR_LAEP | CompareZeroLookup8[data8];
        Td(SOURCE_BYTE);
        MemoryReadHidden(tms9900.dstAddress);
        MemoryWrite8(tms9900.dstAddress, data8);
        break;

    case op_soc:
        AddCycleCount(14);
        TsTd(); // Not quite accurate as the source and dest should be split but good enough
        data16 = MemoryRead16(tms9900.srcAddress) | MemoryRead16(tms9900.dstAddress);
        tms9900.ST = STATUS_CLEAR_LAE | CompareZeroLookup16[data16];
        MemoryWrite16(tms9900.dstAddress, data16);
        break;

    case op_socb:
        AddCycleCount(14);
        TsTd(); // Not quite accurate as the source and dest should be split but good enough
        data8 = MemoryRead8(tms9900.srcAddress) | MemoryRead8(tms9900.dstAddress);
        tms9900.ST = STATUS_CLEAR_LAEP | CompareZeroLookup8[data8];
        MemoryWrite8(tms9900.dstAddress, data8);
        break;

    // ----------------------------------------------------------------------------------------
    // All of the jumps work the same - as signed displacements. Some of these instructions 
    // are hit hard - especially the jmp and jne... so look to optmize this at some point. 
    // ----------------------------------------------------------------------------------------
    case op_jmp:
        AddCycleCount(10);
        s8 displacement = (s8)tms9900.currentOp;
        tms9900.PC += displacement<<1;
        break;

    case op_jlt:
        if (!(tms9900.ST & (ST_AGT | ST_EQ)))
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jle:
        if ((!(tms9900.ST & ST_LGT)) | (tms9900.ST & ST_EQ))
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jeq:
        if (tms9900.ST & ST_EQ)
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jhe:
        if (tms9900.ST & (ST_LGT | ST_EQ))
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jgt:
        if (tms9900.ST & ST_AGT)
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jne:
        if (!(tms9900.ST & ST_EQ))
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jnc:
        if (!(tms9900.ST & ST_C))
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_joc:
        if (tms9900.ST & ST_C)
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jno:
        if (!(tms9900.ST & ST_OV))
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jl:
        if (!(tms9900.ST & (ST_LGT | ST_EQ)))
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jh:
        if ((tms9900.ST & ST_LGT) && !(tms9900.ST & ST_EQ))
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_jop:
        if (tms9900.ST & ST_OP)
        {
            AddCycleCount(10);
            tms9900.PC += ((s8)tms9900.currentOp)<<1;
        } else AddCycleCount(8);
        break;

    case op_idle:
        tms9900.idleReq = true;
        // --------------------------------------------------------
        // If we haven't set the IDLE flag, we turn it on
        // and advance 228 clocks. Not accurate but will
        // break us out of the main loop and the next time
        // we loop on the TMS9900 we will be in 'Accurate' mode
        // --------------------------------------------------------
        if (!(tms9900.accurateEmuFlags & ACCURATE_EMU_IDLE))
        {
            tms9900.accurateEmuFlags |= ACCURATE_EMU_IDLE;
            AddCycleCount(228);
        }
        break;

    case op_rset:
        AddCycleCount(12);
        tms9900.ST &= ~ST_INTMASK;
        break;

    case op_xop:
        AddCycleCount(36);
        Ts_NoInline(SOURCE_WORD);  // Forces 16-bit source address mode
        data16 = ((tms9900.currentOp & 0x03c0) >> 6);
        TMS9900_ContextSwitch(data16);
        MemoryWrite16(WP_REG(11), tms9900.srcAddress);
        tms9900.ST |= ST_X;
        break;
    
    case op_bad:
    case op_ckon:   // No support for external instrutions...
    case op_ckof:   // No support for external instrutions...
    case op_lrex:   // No support for external instrutions...
    default:
        tms9900.illegalOPs++;        // We use debug register 15 for this
        tms9900.lastIllegalOP = tms9900.currentOp;
        AddCycleCount(6);   // Unused instructions seem to chew up 6 cycles... We aren't trapping on any "illegal" opcodes but we might track it someday
        break;
