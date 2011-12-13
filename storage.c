/* This file is part of 34S.
 * 
 * 34S is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * 34S is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with 34S.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This module handles all load/save operations in the real build or emulator
 * Module written by MvC
 */
#ifdef REALBUILD
#define PERSISTENT_RAM __attribute__((section(".persistent_ram")))
#define SLCDCMEM       __attribute__((section(".slcdcmem")))
#define VOLATILE_RAM   __attribute__((section(".volatile_ram")))
#define BACKUP_FLASH   __attribute__((section(".backup_flash")))
#ifndef NULL
#define NULL 0
#endif
#else
// Emulator definitions
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define PERSISTENT_RAM
#define SLCDCMEM
#define VOLATILE_RAM
#define BACKUP_FLASH
#define STATE_FILE "wp34s.dat"
#define BACKUP_FILE "wp34s-backup.dat"
#define LIBRARY_FILE "wp34s-lib.dat"

#endif

#include "xeq.h"
#include "storage.h"
#include "display.h"
#include "stats.h"

#define PAGE_SIZE	 256

/*
 *  Setup the persistent RAM
 */
PERSISTENT_RAM TPersistentRam PersistentRam;

/*
 *  Data that is saved in the SLCD controller during deep sleep
 */
SLCDCMEM TStateWhileOn StateWhileOn;

/*
 *  A private register area for XROM code in volatile RAM
 *  It replaces the local registers and flags if active.
 */
VOLATILE_RAM TXromLocal XromLocal;

/*
 *  The backup flash area:
 *  2 KB for storage of programs and registers
 *  Same data as in persistent RAM but in flash memory
 */
BACKUP_FLASH TPersistentRam BackupFlash;

#ifndef REALBUILD
/*
 *  We need to define the Library space here.
 *  On the device the linker takes care of this.
 */
FLASH_REGION UserFlash;
#endif

/*
 *  The CCITT 16 bit CRC algorithm (X^16 + X^12 + X^5 + 1)
 */
unsigned short int crc16( const void *base, unsigned int length )
{
	unsigned short int crc = 0x5aa5;
	unsigned char *d = (unsigned char *) base;
	unsigned int i;

	for ( i = 0; i < length; ++i ) {
		crc  = ( (unsigned char)( crc >> 8 ) ) | ( crc << 8 );
		crc ^= *d++;
		crc ^= ( (unsigned char)( crc & 0xff ) ) >> 4;
		crc ^= crc << 12;
		crc ^= ( crc & 0xff ) << 5;
	}
	return crc;
}


/*
 *  Compute a checksum and compare it against the stored sum
 *  Returns non zero value if failure
 */
static int test_checksum( const void *data, unsigned int length, unsigned short oldcrc, unsigned short *pcrc )
{
	unsigned short crc;
	crc = crc16( data, length );
	if ( pcrc != NULL ) {
		*pcrc = crc;
	}
	return crc != oldcrc && oldcrc != MAGIC_MARKER;
}


/*
 *  Checksum the program area.
 *  This always computes and sets the checksum of the program in RAM.
 *  The checksum of any program in flash can simply be read out.
 *  Returns non zero value if failure
 */
int checksum_code( void )
{
	return LastProg < 1 || LastProg > NUMPROG 
		|| test_checksum( Prog, ( LastProg - 1 ) * sizeof( s_opcode ), CrcProg, &CrcProg );
}


/*
 *  Checksum the persistent RAM area (registers and state only)
 *  The magic marker is always valid. This eases manipulating state files.
 *  Returns non zero value if failure
 */
int checksum_data( void )
{
	const char *r = (char *) get_reg_n(0);
	return test_checksum( r, (char *) &Crc - r, Crc, &Crc );
}


/*
 *  Checksum all RAM
 *  Returns non zero value if failure
 */
int checksum_all( void )
{
	return checksum_data() + checksum_code();
}


/*
 *  Checksum the backup flash region (registers and state only)
 *  Returns non zero value if failure
 */
int checksum_backup( void )
{
	const char *r = (char *) get_flash_reg_n(0);
	return test_checksum( r, (char *) &( BackupFlash._crc ) - r,
		              BackupFlash._crc, NULL );
}


/*
 *  Checksum a flash region
 *  Returns non zero value if failure
 */
static int checksum_region( FLASH_REGION *fr, FLASH_REGION *header )
{
	int l = ( header->last_prog - 1 ) * sizeof( s_opcode );
	return l < 0 || l > sizeof( fr->prog ) || test_checksum( fr->prog, l, fr->crc, &(header->crc) );
}

#ifdef REALBUILD
/*
 *  We do not copy any static data from flash to RAM at startup and
 *  thus can't use code in RAM. In order to program flash use the
 *  IAP feature in ROM instead
 */
#define IAP_FUNC ((int (*)(unsigned int)) (*(int *)0x400008))

/*
 *  Issue a command to the flash controller. Must be done from ROM.
 *  Returns zero if OK or non zero on error.
 */
static int flash_command( unsigned int cmd )
{
	SUPC_SetVoltageOutput( SUPC_VDD_180 );
	return IAP_FUNC( cmd ) >> 1;
}

/*
 *  Program the flash starting at destination.
 *  Returns 0 if OK or non zero on error.
 *  count is in pages, destination % PAGE_SIZE needs to be 0.
 */
static int program_flash( void *destination, void *source, int count )
{
	int *flash = (int *) destination;
	int *ip = (int *) source;

	lock();  // No interrupts, please!

	while ( count-- > 0 ) {
		/*
		 *  Setup the command for the controller by computing the page from the address
		 */
		const unsigned int cmd = 0x5A000003 | ( (unsigned int) flash & 0x1ff00 );
		int i;

		/*
		 *  Copy the source to the flash write buffer
		 */
		for ( i = 0; i < PAGE_SIZE / 4; ++i ) {
			*flash++ = *ip++;
		}

		/*
		 *  Command the controller to erase and write the page.
		 */
		if ( flash_command( cmd ) ) {
			err( ERR_IO );
			break;
		}
	}
	unlock();
	return Error != 0;
}


/*
 *  Set the boot bit to ROM and turn off the device.
 *  Next power ON goes into SAM-BA mode.
 */
void sam_ba_boot(void)
{
	/*
	 *  Command the controller to clear GPNVM1
	 */
	lock();
	flash_command( 0x5A00010C );
	SUPC_Shutdown();
}


#else

/*
 *  Emulate the flash in a file wp34s-lib.dat or wp34s-backup.dat
 *  Page numbers are relative to the start of the user flash
 *  count is in pages, destination % PAGE_SIZE needs to be 0.
 */
#ifdef QTGUI
extern char* get_region_path(int region);
#else
static char* get_region_path(int region)
{
	return region == REGION_BACKUP ? BACKUP_FILE : LIBRARY_FILE;
}
#endif

static int program_flash( void *destination, void *source, int count )
{
	char *name;
	char *dest = (char *) destination;
	FILE *f = NULL;
	int offset;

	/*
	 *  Copy the source to the destination memory
	 */
	memcpy( dest, source, count * PAGE_SIZE );

	/*
	 *  Update the correct region file
	 */
	if ( dest >= (char *) &BackupFlash && dest < (char *) &BackupFlash + sizeof( BackupFlash ) ) {
		name = get_region_path( REGION_BACKUP );
		offset = dest - (char *) &BackupFlash;
	}
	else if ( dest >= (char *) &UserFlash && dest < (char *) &UserFlash + sizeof( UserFlash ) ) {
		name = get_region_path( REGION_LIBRARY );
		offset = dest - (char *) &UserFlash;
	}
	else {
		// Bad address
		err( ERR_ILLEGAL );
		return 1;
	}
	f = fopen( name, "rb+" );
	if ( f == NULL ) {
		f = fopen( name, "wb+" );
	}
	if ( f == NULL ) {
		err( ERR_IO );
		return 1;
	}
	fseek( f, offset, SEEK_SET );
	if ( count != fwrite( dest, PAGE_SIZE, count, f ) ) {
		fclose( f );
		err( ERR_IO );
		return 1;
	}
	fclose( f );
	return 0;
}
#endif


/*
 *  Initialize the library to an empty state if it's not valid
 */
void init_library( void )
{
	if ( checksum_region( &UserFlash, &UserFlash ) ) {
		struct {
			unsigned short crc;
			unsigned short last_prog;
			s_opcode prog[ 126 ];
		} lib;
		lib.last_prog = 1;
		lib.crc = MAGIC_MARKER;
		xset( lib.prog, 0xff, sizeof( lib.prog ) );
		program_flash( &UserFlash, &lib, 1 );
	}
}


/*
 *  Add data at the end of user flash memory.
 *  Update crc and counter when done.
 *  All sizes are given in steps.
 */
static int flash_append( int destination_step, s_opcode *source, int count, int last_prog )
{
	char *dest = (char *) ( UserFlash.prog + offsetLIB( destination_step ) );
	char *src = (char *) source;
#ifdef REALBUILD
	int offset_in_page = (int) dest & 0xff;
#else
	int offset_in_page = ( dest - (char *) &UserFlash ) & 0xff;
#endif
	char buffer[ PAGE_SIZE ];
	FLASH_REGION *fr = (FLASH_REGION *) buffer;
	count <<= 1;

	if ( offset_in_page != 0 ) {
		/*
		 *  We are not on a page boundary
		 *  Assemble a buffer from existing and new data
		 */
		const int bytes = PAGE_SIZE - offset_in_page;
		xcopy( buffer, dest - offset_in_page, offset_in_page );
		xcopy( buffer + offset_in_page, src, bytes );
		if ( program_flash( dest - offset_in_page, buffer, 1 ) ) {
			return 1;
		}
		src += bytes;
		dest += bytes;
		count -= bytes;
	}

	if ( count > 0 ) {
		/*
		 *  Move multiples of complete pages
		 */
		count = ( count + ( PAGE_SIZE - 1 ) ) >> 8;
		if ( program_flash( dest, src, count ) ) {
			return 1;
		}
	}

	/*
	 *  Update the library header to fix the crc and last_prog fields.
	 */
	xcopy( fr, &UserFlash, PAGE_SIZE );
	fr->last_prog = last_prog;
	checksum_region( &UserFlash, fr );
	return program_flash( &UserFlash, fr, 1 );
}


/*
 *  Remove steps from user flash memory.
 */
int flash_remove( int step_no, int count )
{
	const int last_prog = UserFlash.last_prog - count;
	step_no = offsetLIB( step_no );
	return flash_append( step_no, UserFlash.prog + step_no + count,
			     last_prog - step_no, last_prog );
}


/*
 *  Simple backup / restore
 *  Started with ON+STO or ON+RCL or the SAVE/LOAD commands
 *  The backup area is the last 2KB of flash (pages 504 to 511)
 */
void flash_backup( decimal64 *nul1, decimal64 *nul2, enum nilop op )
{
	if ( not_running() ) {
		process_cmdline_set_lift();
		init_state();
		checksum_all();

		if ( program_flash( &BackupFlash, &PersistentRam, sizeof( BackupFlash ) / PAGE_SIZE ) ) {
			err( ERR_IO );
			DispMsg = "Error";
		}
		else {
			DispMsg = "Saved";
		}
	}
}


void flash_restore(decimal64 *nul1, decimal64 *nul2, enum nilop op)
{
	if ( not_running() ) {
		if ( checksum_backup() ) {
			err( ERR_INVALID );
		}
		else {
			xcopy( &PersistentRam, &BackupFlash, sizeof( PersistentRam ) );
			init_state();
			DispMsg = "Restored";
		}
	}
}


/*
 *  Load the user program area from the backup.
 *  Called by PLOAD.
 */
void load_program(decimal64 *nul1, decimal64 *nul2, enum nilop op)
{
	if ( not_running() ) {
		FLASH_REGION *fr = (FLASH_REGION *) &BackupFlash;

		if ( checksum_region( fr, fr ) || fr->last_prog > NUMPROG + 1 ) {
			/*
			 *  Not a valid program region
			 */
			err( ERR_INVALID );
			return;
		}
		clpall();
		xcopy( &CrcProg, fr, sizeof( s_opcode ) * ( fr->last_prog + 1 ) );
		if (RetStk < Prog + LastProg ) {
			sigmaDeallocate();
		}
		clrretstk_pc();
	}
}


/*
 *  Load registers from backup
 */
void load_registers(decimal64 *nul1, decimal64 *nul2, enum nilop op)
{
	if ( checksum_backup() ) {
		/*
		 *  Not a valid backup region
		 */
		err( ERR_INVALID );
		return;
	}
	clrretstk();
	NumRegs = BackupFlash._numregs;
	sigmaDeallocate();
	xcopy( Regs, BackupFlash._regs, sizeof( Regs ) );
}


void load_state(decimal64 *nul1, decimal64 *nul2, enum nilop op)
{
	if ( not_running() ) {
		if ( checksum_backup() ) {
			/*
			 *  Not a valid backup region
			 */
			err( ERR_INVALID );
			return;
		}
		xcopy( &RandS1, &BackupFlash._rand_s1, (char *) &Crc - (char *) &RandS1 );
		init_state();
		clrretstk_pc();
	}
}


/*
 *  Save a user program to the library region. Called by PSTO.
 */
void store_program(decimal64 *nul1, decimal64 *nul2, enum nilop op)
{
	if ( not_running() ) {
		update_program_bounds( 1 );
	}
}


/*
 *  Load a user program from the library region. Called by PRCL.
 */
void recall_program(decimal64 *nul1, decimal64 *nul2, enum nilop op)
{
	if ( not_running() ) {

	}
}



#if !defined(REALBUILD) && !defined(QTGUI)
/*
 *  Save/Load state to a file (only for emulator(s))
 */
void save_statefile( void )
{
	FILE *f = fopen( STATE_FILE, "wb" );
	if ( f == NULL ) return;
	process_cmdline_set_lift();
	init_state();
	checksum_all();
	fwrite( &PersistentRam, sizeof( PersistentRam ), 1, f );
	fclose( f );
#ifdef DEBUG
	printf( "sizeof struct _state = %d\n", (int)sizeof( struct _state ) );
	printf( "sizeof struct _ustate = %d\n", (int)sizeof( struct _ustate ) );
	printf( "sizeof RAM = %d (%d free)\n", (int)sizeof(PersistentRam), 2048 - (int)sizeof(PersistentRam));
	printf( "sizeof struct _state2 = %d\n", (int)sizeof( struct _state2 ) );
	printf( "sizeof while on = %d (%d free)\n", (int)sizeof(TStateWhileOn), 55 - (int)sizeof(TStateWhileOn));
	printf( "sizeof decNumber = %d\n", (int)sizeof(decNumber));
	printf( "sizeof decContext = %d\n", (int)sizeof(decContext));
#endif
}


/*
 *  Load both the RAM file and the flash emulation images
 */
void load_statefile( void )
{
	FILE *f = fopen( STATE_FILE, "rb" );
	if ( f != NULL ) {
		fread( &PersistentRam, sizeof( PersistentRam ), 1, f );
		fclose( f );
		init_34s();
	}
	f = fopen( BACKUP_FILE, "rb" );
	if ( f != NULL ) {
		fread( &BackupFlash, sizeof( BackupFlash ), 1, f );
		fclose( f );
	}
	else {
		// Emulate a backup
		BackupFlash = PersistentRam;
	}
	f = fopen( LIBRARY_FILE, "rb" );
	if ( f != NULL ) {
		fread( &UserFlash, 1, sizeof( UserFlash ), f );
		fclose( f );
	}
	init_library();
}
#endif


