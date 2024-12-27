// =====================================================================================
// Copyright (c) 2023-2024 Dave Bernazzani (wavemotion-dave)
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
#include <string.h>
#include <ctype.h>
#include "rpk.h"
#include "lowzip.h"
#include "yxml.h"
#include "../DS99.h"
#include "../DS99mngt.h"
#include "../DS99_utils.h"
#include "../cpu/tms9900/tms9900.h"

typedef struct {
	FILE *input;
	unsigned int  input_length;
	unsigned char input_chunk[0x400];   // 1024 byte buffer
	unsigned int  input_chunk_start;
	unsigned int  input_chunk_end;
} read_state;

read_state read_st; // A bit too large to put into fast memory... but it's fast enough as normal memory
lowzip_state st         __attribute__((section(".dtcm")));
yxml_t xml              __attribute__((section(".dtcm")));
char xml_value[64]      __attribute__((section(".dtcm")));
Layout_t cart_layout    __attribute__((section(".dtcm")));

// -----------------------------------------------------------------------
// We pass this into the lowzip handler who will call us back to read  a
// single byte from the file. Most of the time this will return quickly
// as the byte will be in cached memory - but if not, it will read in a
// single cached chunk (see input_chunk[] above) to minimize file I/O.
// -----------------------------------------------------------------------
unsigned int rpk_read_file(void *udata, unsigned int offset)
{
	read_state *st;
	size_t got;
	int chunk_start;

	st = (read_state *) udata;

	/* Most reads should be cached here with no file I/O. */
	if (offset >= st->input_chunk_start && offset < st->input_chunk_end) {
		return (unsigned int) st->input_chunk[offset - st->input_chunk_start];
	}

	/* Out-of-bounds read, no file I/O. */
	if (offset >= st->input_length) {
		return 0x100;
	}

	/* Load in new chunk so that desired offset is in the middle.
	 * This makes backwards and forwards scanning reasonably
	 * efficient.
	 */
	chunk_start = offset - sizeof(st->input_chunk) / 2;
	if (chunk_start < 0) {
		chunk_start = 0;
	}
	if (fseek(st->input, (size_t) chunk_start, SEEK_SET) != 0) {
		return 0x100;
	}
	got = fread((void *) st->input_chunk, 1, sizeof(st->input_chunk), st->input);
	st->input_chunk_start = chunk_start;
	st->input_chunk_end = chunk_start + got;

	/* Recheck original request. */
	if (offset >= st->input_chunk_start && offset < st->input_chunk_end) {
		return (unsigned int) st->input_chunk[offset - st->input_chunk_start];
	}
	return 0x100;
}

// ------------------------------------------------------------------------------------------------
// This will call into lowzip to extract the file directly into our memory area - we don't even
// bother buffering - if the load fails, we'll simply put 0xFF into memory to prevent the TI system
// from seeing anything that resembles a program. lowzip is great - minimal size and very few
// resources consumed ... but it is not the fastest. A 512K load takes a few seconds. Good enough!
// This returns zero if everything went smoothly on unpacking the file. Non-zero otherwise.
// ------------------------------------------------------------------------------------------------
static u8 rpk_extract_located_file(lowzip_state *st, lowzip_file *fileinfo, u8 *buf, int max_size)
{
	st->output_start = buf;
	st->output_end = buf + max_size;
	st->output_next = st->output_start;

	lowzip_get_data(st);

	return (st->have_error) ? 1:0;
}

// ------------------------------------------------------------------------------------------------
// Match a ROM to a socket so we know where to load this into our memory. It's not the fastest
// lookup but we're going to look up at most 3 roms and so the speed here doesn't matter.
// ------------------------------------------------------------------------------------------------
u8 rpk_match_rom_to_socket(u8 rom_idx)
{
    for (u8 socket_num = 0; socket_num < cart_layout.num_sockets; socket_num++)
    {
        if (strcasecmp(cart_layout.roms[rom_idx].rom_id, cart_layout.sockets[socket_num].socket_uses) == 0)
        {
            return socket_num;
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------------------------------
// At this point we have extracted the XML into one big-ass string which is passed in for parsing.
// Using YXML we parse through the XML string and pull out the relevant fields we need to aid us
// in figuring out what kind of PCB type this cartridge is and what roms are loaded in which sockets.
// ---------------------------------------------------------------------------------------------------
u8 rpk_parse_xml(char *xml_str)
{
    // Get YXML ready to parse - it needs some buffer space and we steal into the fileBuf[] mid-way
    yxml_init(&xml, &fileBuf[4096], 4096);

    // Setup for parsing the XML into our cart_layout structure
    memset(&cart_layout, 0x00, sizeof(cart_layout));
    u8 val_idx=0;

    for (int i=0; i<strlen(xml_str); i++)
    {
        yxml_ret_t y = yxml_parse(&xml, (int)xml_str[i]);
        switch (y)
        {
            case YXML_ELEMSTART:
                if ((strcasecmp(xml.elem, "rom") == 0))    cart_layout.num_roms++;
                if ((strcasecmp(xml.elem, "socket") == 0)) cart_layout.num_sockets++;
                break;
            case YXML_ELEMEND:   break;
            case YXML_ATTRSTART: xml_value[0]=0; val_idx=0; break;
            case YXML_ATTREND:
                if ((strcasecmp(xml.elem, "pcb") == 0) && (strcasecmp(xml.attr, "type") == 0))
                {
                    if (strcasecmp(xml_value, "standard")   == 0)  cart_layout.pcb = PCB_STANDARD;
                    if (strcasecmp(xml_value, "paged")      == 0)  cart_layout.pcb = PCB_PAGED;
                    if (strcasecmp(xml_value, "paged16k")   == 0)  cart_layout.pcb = PCB_PAGED;
                    if (strcasecmp(xml_value, "paged12k")   == 0)  cart_layout.pcb = PCB_PAGED;
                    if (strcasecmp(xml_value, "gromemu")    == 0)  cart_layout.pcb = PCB_GROMEMU;
                    if (strcasecmp(xml_value, "paged377")   == 0)  cart_layout.pcb = PCB_PAGED377;
                    if (strcasecmp(xml_value, "paged378")   == 0)  cart_layout.pcb = PCB_PAGED378;
                    if (strcasecmp(xml_value, "paged379i")  == 0)  cart_layout.pcb = PCB_PAGED379i;
                    if (strcasecmp(xml_value, "mbx")        == 0)  cart_layout.pcb = PCB_MBX;
                    if (strcasecmp(xml_value, "minimem")    == 0)  cart_layout.pcb = PCB_MINIMEM;
                    if (strcasecmp(xml_value, "pagedcru")   == 0)  cart_layout.pcb = PCB_PAGEDCRU;
                    if (strcasecmp(xml_value, "super")      == 0)  cart_layout.pcb = PCB_SUPER;
                    if (strcasecmp(xml_value, "paged7")     == 0)  cart_layout.pcb = PCB_PAGED7;
                }
                if ((strcasecmp(xml.elem, "rom") == 0) && (strcasecmp(xml.attr, "id") == 0))
                {
                    strcpy(cart_layout.roms[cart_layout.num_roms-1].rom_id, xml_value);
                }
                if ((strcasecmp(xml.elem, "rom") == 0) && (strcasecmp(xml.attr, "file") == 0))
                {
                    strcpy(cart_layout.roms[cart_layout.num_roms-1].rom_file, xml_value);
                }
                if ((strcasecmp(xml.elem, "socket") == 0) && (strcasecmp(xml.attr, "id") == 0))
                {
                    strcpy(cart_layout.sockets[cart_layout.num_sockets-1].socket_id, xml_value);
                }
                if ((strcasecmp(xml.elem, "socket") == 0) && (strcasecmp(xml.attr, "uses") == 0))
                {
                    strcpy(cart_layout.sockets[cart_layout.num_sockets-1].socket_uses, xml_value);
                }
                if ((strcasecmp(xml.elem, "romset") == 0) && (strcasecmp(xml.attr, "listname") == 0))
                {
                    strcpy(cart_layout.listname, xml_value);
                }
                break;

            case YXML_ATTRVAL:
                xml_value[val_idx++] = xml.data[0];
                xml_value[val_idx] = 0;
                break;

            case YXML_EEOF:
            case YXML_EREF:
            case YXML_ECLOSE:
            case YXML_ESTACK:
            case YXML_ESYN:
                return 1; // Error
                break;

            case YXML_CONTENT:
            case YXML_PISTART:
            case YXML_PICONTENT:
            case YXML_PIEND:
            case YXML_OK:
                break;
        }
    }

    return 0; // No error
}

// ----------------------------------------------------------------------
// The standard loader will allow for any sized 'C' ROM in the normal
// rom_socket (setting up banking if the ROM is > 8K) and also allow
// for a GROM load as well up to 40K. The 'rom2_socket' is not used.
// ----------------------------------------------------------------------
u8 rpk_load_standard(void)
{
    u16 numCartBanks = 1;
    u8 err = 0;

    tms9900.bankMask = 0x0000;

    // ------------------------------------------------------------------------------------
    // For each ROM in our layout we load it in to the appopriate CPU/GROM memory area...
    // ------------------------------------------------------------------------------------
    for (u8 i=0; i<cart_layout.num_roms; i++)
    {
        // Check for load into the standard ROM socket at >6000
        if (strcasecmp(cart_layout.sockets[rpk_match_rom_to_socket(i)].socket_id, "rom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                // -----------------------------------------------------------------------------
                // We allow larger than 8K in which case we will turn on banking automatically
                // -----------------------------------------------------------------------------
                numCartBanks = (fileinfo->uncompressed_size / 0x2000) + ((fileinfo->uncompressed_size % 0x2000) ? 1:0);
                if (rpk_extract_located_file(&st, fileinfo, MemCART, MAX_CART_SIZE) == 0)
                {
                    memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // The first 8K of this cart gets loaded directly into main memory

                    if (numCartBanks > 1) // If more than 8K we need to bank
                    {
                        tms9900.bankMask = BankMasks[numCartBanks-1];
                    }
                } else err = 1;
            } else err = 1;
        }

        // Check for load into the GROM socket at GROM offset >6000
        if (strcasecmp(cart_layout.sockets[rpk_match_rom_to_socket(i)].socket_id, "grom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                if (rpk_extract_located_file(&st, fileinfo, &MemGROM[0x6000], 0xA000) != 0)
                {
                    err = 1;
                }
            } else err = 1;
        }
    }

    return err;
}

// -----------------------------------------------------------------------
// This is the banked format with 8K of 'C' memory and 8K of 'D' memory
// that is banswitched by writing to the cart space. We also allow a
// GROM load as well.  This is the equivilent for non-RPK as C/D/G loads.
// This routine is also capable of handling a 4K 'C' ROM in which case
// it assumes that this is a 12K ROM load (such as the real dump of
// Extended BASIC) and will rework the buffers to create a 16K paged ROM.
// -----------------------------------------------------------------------
u8 rpk_load_paged()
{
    u8 err = 0;
    u8 bPaged12k = 0;

    tms9900.bankMask = 0x0001;  // Paged is always 8K of 'C' and 8K of 'D' and possibly GROM

    // ------------------------------------------------------------------------------------
    // For each ROM in our layout we load it in to the appopriate CPU/GROM memory area...
    // ------------------------------------------------------------------------------------
    for (u8 i=0; i<cart_layout.num_roms; i++)
    {
        // Check for an 8K ROM load into the standard ROM socket at >6000
        if (strcasecmp(cart_layout.sockets[rpk_match_rom_to_socket(i)].socket_id, "rom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo->uncompressed_size == 4096) bPaged12k = 1;
            if (fileinfo)
            {
                if (rpk_extract_located_file(&st, fileinfo, MemCART, MAX_CART_SIZE) == 0)
                {
                    memcpy(&MemCPU[0x6000], MemCART, 0x2000);        // This cart gets loaded directly into main memory
                }  else err = 1;
            } else err = 1;
        }

         // Check for the paged 8K ROM load
        if (strcasecmp(cart_layout.sockets[rpk_match_rom_to_socket(i)].socket_id, "rom2_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                if (rpk_extract_located_file(&st, fileinfo, MemCART+0x2000, MAX_CART_SIZE-0x2000) != 0)
                {
                    err = 1;
                }
            } else err = 1;
        }

        // Check for load into the GROM socket at GROM offset >6000
        if (strcasecmp(cart_layout.sockets[rpk_match_rom_to_socket(i)].socket_id, "grom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                if (rpk_extract_located_file(&st, fileinfo, &MemGROM[0x6000], 0xA000) != 0)
                {
                    err = 1;
                }
            } else err = 1;
        }
    }

    // If we are a 12K paged cart, we re-arrange memory so it just works like normal Paged 16k
    // This is done by copying and manipulating the cart buffer as follows:
    // 4K ROM1 + first 4K of ROM2a = 8K bank 0 ROM
    // 4K ROM1 + second 4K of ROM2b = 8K bank 1 ROM
    if (bPaged12k && !err)
    {
        memcpy(MemCART+0x1000, MemCART+0x2000, 0x1000);  // Bank 0 ROM is created and now in place in the cart buffer
        memcpy(MemCART+0x2000, MemCART+0x0000, 0x1000);  // Bank 1 ROM is created and now in place in the cart buffer
        memcpy(MemCPU +0x6000, MemCART+0x0000, 0x2000);  // And place our new doctored ROM (bank 0) into main memory...
        tms9900.bankMask = 0x0001;                       // We have one bank
    }

    return err;
}

// ----------------------------------------------------------------------
// This is the normal non-inverted load - possibly with a GROM file.
// This is heavily used by the homebrew and FinalGROM community - the
// files in non-RPK format usually end with '8'
// ----------------------------------------------------------------------
u8 rpk_load_paged378(void)
{
    u8 err = 0;

    // ------------------------------------------------------------------------------------
    // For each ROM in our layout we load it in to the appopriate CPU/GROM memory area...
    // ------------------------------------------------------------------------------------
    for (u8 i=0; i<cart_layout.num_roms; i++)
    {
        // Find the main ROM which will likely be banked...
        if (strcasecmp(cart_layout.sockets[rpk_match_rom_to_socket(i)].socket_id, "rom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                u16 numCartBanks = (fileinfo->uncompressed_size / 0x2000) + ((fileinfo->uncompressed_size % 0x2000) ? 1:0);
                if (rpk_extract_located_file(&st, fileinfo, MemCART, MAX_CART_SIZE) == 0)
                {
                    tms9900.bankMask = BankMasks[numCartBanks-1];   // Full load cart will usually have manu banks
                    memcpy(&MemCPU[0x6000], MemCART, 0x2000);       // First bank loaded into main memory
                } else err = 1;
            } else err = 1;

            // Check for load into the GROM socket at GROM offset >6000
            // This is non-standard as MAME 378 does not support GROMs for this type but we do...
            if (strcasecmp(cart_layout.sockets[rpk_match_rom_to_socket(i)].socket_id, "grom_socket") == 0)
            {
                lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
                if (fileinfo)
                {
                    if (rpk_extract_located_file(&st, fileinfo, &MemGROM[0x6000], 0xA000) != 0)
                    {
                        err = 1;
                    }
                } else err = 1;
            }
        }
    }

    return err;
}

// ----------------------------------------------------------------------
// This is the 'inverted' cart load - this isn't used as much these days
// and we actually take the time to swap the banks and lay them out
// in non-inverted manner to make the rest of the code logic simpler.
// Only one ROM is allowed here - no GROMs.
// ----------------------------------------------------------------------
u8 rpk_load_paged379i(void)
{
    u8 err = 0;

    // There should be just one ROM here - and we load it up into banked memory. GROMs are not supported for this type.
    lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[0].rom_file);
    if (fileinfo)
    {
        u16 numCartBanks = (fileinfo->uncompressed_size / 0x2000) + ((fileinfo->uncompressed_size % 0x2000) ? 1:0);

        if (rpk_extract_located_file(&st, fileinfo, MemCART, MAX_CART_SIZE) == 0)
        {
            // Full load cart
            tms9900.bankMask = BankMasks[numCartBanks-1];

            // Swap all 8k banks... this cart is inverted and this will make the page selecting work
            for (u16 i=0; i<numCartBanks/2; i++)
            {
                memcpy(fileBuf, MemCART + (i*0x2000), 0x2000);
                memcpy(MemCART+(i*0x2000), MemCART + ((numCartBanks-i-1)*0x2000), 0x2000);
                memcpy(MemCART + ((numCartBanks-i-1)*0x2000), fileBuf, 0x2000);
            }

            memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // First bank loaded into main memory
        } else err = 1;
    } else err = 1;

    return err;
}

// ----------------------------------------------------------------------
// Only for DataBiotics carts which use CRU for cart paging into >6000
// ----------------------------------------------------------------------
u8 rpk_load_pagedcru(void)
{
    u8 err = 0;
    
    tms9900.bankMask = 0x0000;  // Paged CRU does not use traditional banking but we still need to mask it - see below.
    
    // ------------------------------------------------------------------------------------
    // For each ROM in our layout we load it in to the appopriate CPU/GROM memory area...
    // ------------------------------------------------------------------------------------
    for (u8 i=0; i<cart_layout.num_roms; i++)
    {
        // Check for load into the standard ROM socket at >6000
        if (strcasecmp(cart_layout.sockets[rpk_match_rom_to_socket(i)].socket_id, "rom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                if (rpk_extract_located_file(&st, fileinfo, MemCART, MAX_CART_SIZE) == 0)
                {
                    memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // This cart gets loaded directly into main memory
                    myConfig.cartType = CART_TYPE_PAGEDCRU;
                    
                    u16 numCartBanks = (fileinfo->uncompressed_size / 0x2000) + ((fileinfo->uncompressed_size % 0x2000) ? 1:0);
                    tms9900.bankMask = BankMasks[numCartBanks-1];   // Ensure we mask to the size of the uncompressed ROM
                } else err = 1;
            } else err = 1;
        }
    }

    return err;
}

// ----------------------------------------------------------------------
// MBX is a standard load and then we mark the cart as 'MBX with RAM'
// which will map in 1K of RAM and set the appopriate banking hotspots.
// ----------------------------------------------------------------------
u8 rpk_load_mbx(void)
{
    u8 err = 0;

    err = rpk_load_standard();
    myConfig.cartType = CART_TYPE_MBX_WITH_RAM;

    return err;
}

// ----------------------------------------------------------------------
// MINIMEM is a standard load and then we mark the cart as 'MINIMEM'
// which will map in 4K of RAM at >7000. This is not yet persisted.
// ----------------------------------------------------------------------
u8 rpk_load_minimem(void)
{
    u8 err = 0;

    err = rpk_load_standard();
    myConfig.cartType = CART_TYPE_MINIMEM;

    return err;
}

// Super Cart is 32K of RAM that is CRU mapped - otherwise a standard load
u8 rpk_load_super(void)
{
    u8 err = 0;

    err = rpk_load_standard();
    myConfig.cartType = CART_TYPE_SUPERCART;

    return err;
}

// ----------------------------------------------------------------------------
// Paged7 only used for TI-Calc. It's basically a paged ROM at >7000 but for
// simplicity, we are going to read the two 8K ROMs into memory and then
// manipulate them to create the illusion of a normal paged ROM.  This
// isn't quite accurate as the banks should only swap on writes to >7000
// and above - but TI-Calc appears well behaved so we make it simple. Magic!!
// ----------------------------------------------------------------------------
u8 rpk_load_paged7(void)
{
    u8 err = 0;

    err = rpk_load_paged();     // paged7 is esentially a paged load and then we modify the paging below

    if (!err)
    {
        u8 *swapArea = (u8*)(MemCART + 0x10000);            // Just need 32K somewhere convienent

        memcpy(swapArea+0x0000, MemCART+0x0000, 0x1000);    // Build ROM0 + ROM0
        memcpy(swapArea+0x1000, MemCART+0x0000, 0x1000);

        memcpy(swapArea+0x2000, MemCART+0x0000, 0x1000);    // Build ROM0 + ROM1
        memcpy(swapArea+0x3000, MemCART+0x1000, 0x1000);

        memcpy(swapArea+0x4000, MemCART+0x0000, 0x1000);    // Build ROM0 + ROM2
        memcpy(swapArea+0x5000, MemCART+0x2000, 0x1000);

        memcpy(swapArea+0x6000, MemCART+0x0000, 0x1000);    // Build ROM0 + ROM3
        memcpy(swapArea+0x7000, MemCART+0x3000, 0x1000);

        memcpy(MemCPU+0x6000,  swapArea, 0x2000);           // Bank 0 + Bank 0
        memcpy(MemCART+0x0000, swapArea, 0x8000);           // The new 32K ROM with all the banks in place

        tms9900.bankMask = 0x0003;                          // We have 4 banks.
    }

    return err;
}

// ------------------------------------------------------------------------------
// This is the only public interface - the caller should pass the filename.rpk
// and this will unpack it and extract the layout.xml and figure out what
// individual roms get loaded where in the memory map... It returns 0 if there
// were no errors or non-zero if an error was encoutered.
// ------------------------------------------------------------------------------
u8 rpk_load(const char* filename)
{
    lowzip_file *fileinfo;
    FILE *input = NULL;
    u8 errors = 0;

    // Everything is zero to start
    memset((void *) &st, 0, sizeof(st));
    memset((void *) &read_st, 0, sizeof(read_st));

    // --------------------------------------------------
    // Open file, seek to end, get length, setup zip
    // --------------------------------------------------
    input = fopen(filename, "rb");
    fseek(input, 0, SEEK_END);
	read_st.input = input;
	read_st.input_length = (unsigned int) ftell(input);

    st.udata = (void *) &read_st;
    st.read_callback = rpk_read_file;
    st.zip_length = read_st.input_length;

    // Initialize the lowzip library
    lowzip_init_archive(&st);
    // --------------------------------------------------
    // Try to read the layout XML file in the archive.
    // This must exist so we know how to load the ROMs
    // --------------------------------------------------
    fileinfo = lowzip_locate_file(&st, 0, "layout.xml");
    if (fileinfo)
    {
        memset(fileBuf, 0x00, sizeof(fileBuf));
        if (rpk_extract_located_file(&st, fileinfo, fileBuf, 4096) == 0)
        {
            // --------------------------------------------------------------------------------------
            // Parse the XML and find the PCB type, file/socket type and associated binary files...
            // --------------------------------------------------------------------------------------
            rpk_parse_xml((char *)fileBuf);

            switch (cart_layout.pcb)
            {
                case PCB_STANDARD:
                    errors = rpk_load_standard();    // Load standard ROM ('C') and usually also a GROM ('G') - most first party TI carts use this
                    break;
                case PCB_PAGED:
                    errors = rpk_load_paged();       // Load standard ROM ('C') with 8K rom2 ('D') and possibly GROM ('G') - some third party carts use this
                    break;
                case PCB_GROMEMU:
                    errors = rpk_load_standard();    // No distinction here... MESS/MAME forces 6K GROM vs 8K GROM but DS994a doesn't care
                    break;
                case PCB_PAGED377:                   // Same as 378 for our purposes (the only real distinction is the max size of the load and possible GROM use)
                case PCB_PAGED378:
                    errors = rpk_load_paged378();    // Load flat ROM with non-inverted paging up to 512K (8MB on the DSi)
                    break;
                case PCB_PAGED379i:
                    errors = rpk_load_paged379i();   // Load flat ROM with inversed paging up to 512K
                    break;
                case PCB_MBX:
                    errors = rpk_load_mbx();         // Load special MBX cart with 1K of RAM and special bank handling
                    break;
                case PCB_MINIMEM:
                    errors = rpk_load_minimem();     // Load special mini-memory with 4K of RAM at >7000
                    break;
                case PCB_PAGEDCRU:
                    errors = rpk_load_pagedcru();    // Load special paged CRU cart (DataBiotics mostly)
                    break;
                case PCB_SUPER:
                    errors = rpk_load_super();       // Load special Super Cart II with 32K of RAM that is paged via CRU
                    break;
                case PCB_PAGED7:
                    errors = rpk_load_paged7();      // Load special paged7 ROM (TI-Calc is the only one I know of)
                    break;
                default:
                    errors = 1;                      // Unsupported type - in theory, this should never happen
                    break;
            }
        } else errors = 1;
    } else errors = 1;

    fclose(input);

    if (errors)
    {
        memset(&MemCPU[0x6000],  0xFF, 0x2000);   // Failed to load - clear main memory CART area
        memset(&MemGROM[0x6000], 0xFF, 0xA000);   // Failed to load - clear the user GROM area
        memset(MemCART,          0xFF, 0x10000);  // And clear out a big chunk of the Cart Buffer just to be safe
    }
    else
    {
        // -------------------------------------------------------------------------------------------
        // Here we look at the listname to try and make some sensible mappings for controllers, etc.
        // -------------------------------------------------------------------------------------------
        if (strcasecmp(cart_layout.listname, "qbert")    == 0)  SetDiagonals();             // Q-Bert wants to play using diagnoal movement
        if (strcasecmp(cart_layout.listname, "frogger")  == 0)  MapPlayer2();               // Frogger uses the P2 controller port
        if (strcasecmp(cart_layout.listname, "congobng") == 0)  myConfig.RAMMirrors = 1;    // TI-99/4a Congo Bongo requires RAM mirrors to run properly
        if (strcasecmp(cart_layout.listname, "buckrog")  == 0)  myConfig.RAMMirrors = 1;    // TI-99/4a Buck Rogers requires RAM mirrors to run properly
    }

    return errors;
}

// -----------------------------------------------------------------------
// For debugging use only - we return the name of the current PCB layout
// -----------------------------------------------------------------------
char *rpk_get_pcb_name(void)
{
    switch (cart_layout.pcb)
    {
        case PCB_STANDARD:      return "STANDARD";
        case PCB_PAGED:         return "PAGED";
        case PCB_GROMEMU:       return "GROMEMU";
        case PCB_PAGED377:      return "PAGED377";
        case PCB_PAGED378:      return "PAGED378";
        case PCB_PAGED379i:     return "PAGED379i";
        case PCB_MBX:           return "MBX";
        case PCB_MINIMEM:       return "MINIMEM";
        case PCB_PAGEDCRU:      return "PAGEDCRU";
        case PCB_SUPER:         return "SUPER";
        case PCB_PAGED7:        return "PAGED7";
        default:
            break;
    }
    return "UNKNOWN";
}

// End of file
