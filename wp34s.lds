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

/*------------------------------------------------------------------------------
 *      Linker script for running in internal Flash on the AT91SAM7L128.
 *
 *      Modified by Marcus von Cube for wp34s
 *----------------------------------------------------------------------------*/

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(entry)

MEMORY
{
    volatileram (W!RX) :      	ORIGIN = 0x200000, LENGTH = 4K
    persistentram (W!RX) : 	ORIGIN = 0x300000, LENGTH = 2K
    
    programflash (RX) :		ORIGIN = 0x100000, LENGTH = 0x20000 - 2K
    backupflash (R) :		ORIGIN = 0x120000 - 2K, LENGTH = 2K

    cmdtab (R) :		ORIGIN = 0x0f0000, LENGTH = 0x10000
}

SECTIONS
{
    .cmdtab :
    {
    	*(.cmdtab)
    	*(.implicit_consts)
    } >cmdtab 
      
    .fixed : 
    {
        . = ALIGN(4);
        _sfixed = .;
        *(.text*)
        *(.rodata*)
        *(.xrom)          /* remove for fixed XROM */
        . = ALIGN(4);
        _efixed = .;
    } >programflash

/*
    .relocate : AT (_efixed)
    {
        . = ALIGN(4);
        _srelocate = .;
        *(.vectors)
        *(.ramfunc)
        *(.data)
        . = ALIGN(4);
        _erelocate = .;
    } >persistentram
*/

    .revision : 
    {
	*(.revision)
        . = ALIGN(256);
	UserFlash = .;
    } >programflash
    
    .backupflash (NOLOAD) :
    {
    	UserFlashSize = . - UserFlash;
	*(.backup_flash)
    } >backupflash

    .bss (NOLOAD) : 
    {
        . = ALIGN(4);
        _szero = .;
        *(.bss)
    } >volatileram
    
    .slcdcmem (NOLOAD) :
    {
        . = ALIGN(4);
    	*(.slcdcmem)
        . = ALIGN(4);
        _ezero = .;
    } >volatileram

    .volatileram (NOLOAD) :
    {
        . = ALIGN(4);
    	*(.volatileram)
        . = ALIGN(4);
    } >volatileram

    .persistentram (NOLOAD) :
    {
        . = ALIGN(4);
        _spersistentram = .;
        *(.persistentram)
        . = ALIGN(4);
        _epersistentram = .;
    } >persistentram

    _sstack = 0x201000;

    /* The following gets rid of any exception handler code */
    /DISCARD/ :
    {
        *(.ARM.extab*)
        *(.ARM.exidx*)
    } 
}
end = .;
