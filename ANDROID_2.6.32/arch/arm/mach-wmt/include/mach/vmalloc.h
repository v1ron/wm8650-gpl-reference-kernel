/*++
	linux/include/asm-arm/arch-wmt/vmalloc.h

	Some descriptions of such software. Copyright (c) 2008  WonderMedia Technologies, Inc.

	This program is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software Foundation,
	either version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful, but WITHOUT
	ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
	PARTICULAR PURPOSE.  See the GNU General Public License for more details.
	You should have received a copy of the GNU General Public License along with
	this program.  If not, see <http://www.gnu.org/licenses/>.

	WonderMedia Technologies, Inc.
	10F, 529, Chung-Cheng Road, Hsin-Tien, Taipei 231, R.O.C.
--*/


/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET	  (8*1024*1024)
#define VMALLOC_START	  (((unsigned long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_END       (0xd8000000)

/* #define MODULE_START	(PAGE_OFFSET - 16*1048576) */
/* #define MODULE_END	(PAGE_OFFSET) */

/*
 * VMALLOC_START
 * VMALLOC_END
 *      Virtual addresses bounding the vmalloc() area.  There must not be
 *      any static mappings in this area; vmalloc will overwrite them.
 *      The addresses must also be in the kernel segment (see above).
 *      Normally, the vmalloc() area starts VMALLOC_OFFSET bytes above the
 *      last virtual RAM address (found using variable high_memory).
 *
 * VMALLOC_OFFSET
 *      Offset normally incorporated into VMALLOC_START to provide a hole
 *      between virtual RAM and the vmalloc area.  We do this to allow
 *      out of bounds memory accesses (eg, something writing off the end
 *      of the mapped memory map) to be caught.  Normally set to 8MB.
 */
