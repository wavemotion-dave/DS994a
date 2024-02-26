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
	unsigned char input_chunk[0x200];   // 512 byte buffer
	unsigned int  input_chunk_start;
	unsigned int  input_chunk_end;
} read_state;

lowzip_state st         __attribute__((section(".dtcm")));
read_state read_st      __attribute__((section(".dtcm")));
yxml_t xml              __attribute__((section(".dtcm")));
char xml_value[64]      __attribute__((section(".dtcm")));
Layout_t cart_layout    __attribute__((section(".dtcm")));

/* Read callback which uses a single cached chunk to minimize file I/O. */
unsigned int my_read(void *udata, unsigned int offset)
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

static int extract_located_file(lowzip_state *st, lowzip_file *fileinfo, u8 *buf)
{
	int retcode = 1;

	st->output_start = buf;
	st->output_end = buf + fileinfo->uncompressed_size;
	st->output_next = st->output_start;

	lowzip_get_data(st);

	if (st->have_error) {
			retcode = 0;
	} else {
		fwrite((void *) st->output_start, 1, (size_t) (st->output_next - st->output_start), stdout);
		fflush(stdout);
		retcode = 0;
	}

	return retcode;
}

u8 match_rom_to_socket(u8 rom_idx)
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
                break;
            case YXML_ATTRVAL:   xml_value[val_idx++] = xml.data[0]; xml_value[val_idx] = 0; break;
            case YXML_EEOF:
            case YXML_EREF:
            case YXML_ECLOSE:
            case YXML_ESTACK:
            case YXML_ESYN:
            case YXML_CONTENT:
            case YXML_PISTART:
            case YXML_PICONTENT:
            case YXML_PIEND:
            case YXML_OK:        break;
        }
    }

    return 0;
}

u8 rpk_load_standard(void)
{
    u8 err = 0;

    tms9900.bankMask = 0x0000;

    // ------------------------------------------------------------------------------------
    // For each ROM in our layout we load it in to the appopriate CPU/GROM memory area...
    // ------------------------------------------------------------------------------------
    for (u8 i=0; i<cart_layout.num_roms; i++)
    {
        // Check for load into the standard ROM socket at >6000
        if (strcasecmp(cart_layout.sockets[match_rom_to_socket(i)].socket_id, "rom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                u16 numCartBanks = (fileinfo->uncompressed_size / 0x2000) + ((fileinfo->uncompressed_size % 0x2000) ? 1:0);
                if (extract_located_file(&st, fileinfo, MemCART) == 0)
                {
                    memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // This cart gets loaded directly into main memory

                    if (numCartBanks) // If more than 8K we need to bank
                    {
                        tms9900.bankMask = BankMasks[numCartBanks-1];
                    }
                }
                else
                {
                    memset(&MemCPU[0x6000], 0xFF, 0x2000);   // Failed to load
                    err = 1;
                }
            }
        }

        // Check for load into the GROM socket at GROM offset >6000
        if (strcasecmp(cart_layout.sockets[match_rom_to_socket(i)].socket_id, "grom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                if (extract_located_file(&st, fileinfo, &MemGROM[0x6000]) != 0)
                {
                    memset(&MemGROM[0x6000], 0xFF, 0x2000);   // Failed to load
                    err = 1;
                }
            }
        }
    }
    return err;
}

u8 rpk_load_paged()
{
    u8 err = 0;

    tms9900.bankMask = 0x0001;

    // ------------------------------------------------------------------------------------
    // For each ROM in our layout we load it in to the appopriate CPU/GROM memory area...
    // ------------------------------------------------------------------------------------
    for (u8 i=0; i<cart_layout.num_roms; i++)
    {
        // Check for load into the standard ROM socket at >6000
        if (strcasecmp(cart_layout.sockets[match_rom_to_socket(i)].socket_id, "rom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                if (extract_located_file(&st, fileinfo, MemCART) == 0)
                {
                    memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // This cart gets loaded directly into main memory
                }
                else
                {
                    memset(&MemCPU[0x6000], 0xFF, 0x2000);   // Failed to load
                    err = 1;
                }
            }
        }

         // Check for the paged ROM load
        if (strcasecmp(cart_layout.sockets[match_rom_to_socket(i)].socket_id, "rom2_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                if (extract_located_file(&st, fileinfo, MemCART+0x2000) != 0)
                {
                    memset(&MemCPU[0x6000], 0xFF, 0x2000);   // Failed to load
                    err = 1;
                }
            }
        }

        // Check for load into the GROM socket at GROM offset >6000
        if (strcasecmp(cart_layout.sockets[match_rom_to_socket(i)].socket_id, "grom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                if (extract_located_file(&st, fileinfo, &MemGROM[0x6000]) != 0)
                {
                    memset(&MemGROM[0x6000], 0xFF, 0x2000);   // Failed to load
                    err = 1;
                }
            }
        }
    }

    return err;
}

u8 rpk_load_paged378(void)
{
    u8 err = 0;

    // ------------------------------------------------------------------------------------
    // For each ROM in our layout we load it in to the appopriate CPU/GROM memory area...
    // ------------------------------------------------------------------------------------
    for (u8 i=0; i<cart_layout.num_roms; i++)
    {
        // Find the main ROM which will likely be banked...
        if (strcasecmp(cart_layout.sockets[match_rom_to_socket(i)].socket_id, "rom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                u16 numCartBanks = (fileinfo->uncompressed_size / 0x2000) + ((fileinfo->uncompressed_size % 0x2000) ? 1:0);
                if (extract_located_file(&st, fileinfo, MemCART) == 0)
                {
                    // Full load cart
                    tms9900.bankMask = BankMasks[numCartBanks-1];
                    memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // First bank loaded into main memory
                }
                else
                {
                    err = 1;
                }
            }
            
            // Check for load into the GROM socket at GROM offset >6000
            if (strcasecmp(cart_layout.sockets[match_rom_to_socket(i)].socket_id, "grom_socket") == 0)
            {
                lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
                if (fileinfo)
                {
                    if (extract_located_file(&st, fileinfo, &MemGROM[0x6000]) != 0)
                    {
                        err = 1;
                    }
                }
            }
        }
    }
    
    if (err)
    {
        memset(&MemCPU[0x6000], 0xFF, 0x2000);    // Failed to load
        memset(&MemGROM[0x6000], 0xFF, 0x2000);   // Failed to load
    }
    
    return err;
}

u8 rpk_load_paged379i(void)
{
    u8 err = 0;

    // There should be just one ROM here - and we load it up into banked memory. GROMs are not supported for this type.
    lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[0].rom_file);
    if (fileinfo)
    {
        u16 numCartBanks = (fileinfo->uncompressed_size / 0x2000) + ((fileinfo->uncompressed_size % 0x2000) ? 1:0);
        if (extract_located_file(&st, fileinfo, MemCART) == 0)
        {
            // Full load cart
            tms9900.bankMask = BankMasks[numCartBanks-1];

            // Swap all 8k banks... this cart is inverted and this will make the page selecting work
            for (u16 i=0; i<numCartBanks/2; i++)
            {
                memcpy(SwapCartBuffer, MemCART + (i*0x2000), 0x2000);
                memcpy(MemCART+(i*0x2000), MemCART + ((numCartBanks-i-1)*0x2000), 0x2000);
                memcpy(MemCART + ((numCartBanks-i-1)*0x2000), SwapCartBuffer, 0x2000);
            }

            memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // First bank loaded into main memory
        }
        else
        {
            memset(&MemCPU[0x6000], 0xFF, 0x2000);   // Failed to load
            err = 1;
        }
    }
    else
    {
        memset(&MemCPU[0x6000], 0xFF, 0x2000);   // Failed to load
        err = 1;
    }
    return err;
}

u8 rpk_load_pagedcru(void)
{
    u8 err = 0;

    tms9900.bankMask = 0x0000;

    // ------------------------------------------------------------------------------------
    // For each ROM in our layout we load it in to the appopriate CPU/GROM memory area...
    // ------------------------------------------------------------------------------------
    for (u8 i=0; i<cart_layout.num_roms; i++)
    {
        // Check for load into the standard ROM socket at >6000
        if (strcasecmp(cart_layout.sockets[match_rom_to_socket(i)].socket_id, "rom_socket") == 0)
        {
            lowzip_file *fileinfo = lowzip_locate_file(&st, 0, cart_layout.roms[i].rom_file);
            if (fileinfo)
            {
                if (extract_located_file(&st, fileinfo, MemCART) == 0)
                {
                    memcpy(&MemCPU[0x6000], MemCART, 0x2000);   // This cart gets loaded directly into main memory
                    myConfig.cartType = CART_TYPE_PAGEDCRU;
                }
                else
                {
                    memset(&MemCPU[0x6000], 0xFF, 0x2000);   // Failed to load
                    err = 1;
                }
            }
        }
    }

    return err;
}

// MBX is a standard load and then we mark the cart as 'MBX with RAM' which
// will map in 1K of RAM and set the appopriate banking hotspots.
u8 rpk_load_mbx(void)
{
    u8 err = 0;

    err = rpk_load_standard();
    myConfig.cartType = CART_TYPE_MBX_WITH_RAM;

    return err;
}

// MINIMEM is a standard load and then we mark the cart as 'MINIMEM' which
// will map in 4K of RAM at >7000. This is not yet persisted.
u8 rpk_load_minimem(void)
{
    u8 err = 0;

    err = rpk_load_standard();
    myConfig.cartType = CART_TYPE_MINIMEM;

    return err;
}

// Super Cart is not yet supported. We could use the Super 8K version here... maybe.
u8 rpk_load_super(void)
{
    u8 err = 1;
    return err;
}

// Paged7 is not yet supported. Only used for TI-Calc anyway.
u8 rpk_load_paged7(void)
{
    u8 err = 1;
    return err;
}

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
    st.read_callback = my_read;
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
        if (extract_located_file(&st, fileinfo, fileBuf) == 0)
        {
            // --------------------------------------------------------------------------------------
            // Parse the XML and find the PCB type, file/socket type and associated binary files...
            // --------------------------------------------------------------------------------------
            rpk_parse_xml((char *)fileBuf);

            switch (cart_layout.pcb)
            {
                case PCB_STANDARD:
                    errors = rpk_load_standard();    // Load standard ROM ('C') with possibly GROM ('G')
                    break;
                case PCB_PAGED:
                    errors = rpk_load_paged();       // Load standard ROM ('C') with 8K rom2 ('D') and possibly GROM ('G')
                    break;
                case PCB_GROMEMU:
                    errors = rpk_load_standard();    // No distinction here... MESS/MAME forces 6K GROM vs 8K GROM but DS994a doesn't care
                    break;
                case PCB_PAGED377:                   // Same as 378 for our purposes (the only real distinction is the max size of the load)
                case PCB_PAGED378:
                    errors = rpk_load_paged378();    // Load flat ROM with paging up to 512K (8MB on the DSi)
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
                    errors = rpk_load_pagedcru();    // Load special paged CRU cart (DataBiotics, SuperCart)
                    break;
                case PCB_SUPER:
                    errors = rpk_load_super();       // Load special Super Cart II with 32K of RAM
                    break;
                case PCB_PAGED7:
                    errors = rpk_load_paged7();      // Load special paged7 ROM (TI-Calc is the only one I know of)
                    break;
                default:
                    errors = 1;
                    break;
            }
        } else errors = 1;
    } else errors = 1;

    fclose(input);

    return errors;
}
