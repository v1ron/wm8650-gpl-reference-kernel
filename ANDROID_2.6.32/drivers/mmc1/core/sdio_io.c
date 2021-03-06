/*
 *  linux/drivers/mmc/core/sdio_io.c
 *
 *  Copyright 2007-2008 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/mmc1/host.h>
#include <linux/mmc1/card.h>
#include <linux/mmc1/sdio.h>
#include <linux/mmc1/sdio_func.h>

#include "sdio_ops.h"

#if 0
#define DBG(x...)	printk(KERN_ALERT x)
#else
#define DBG(x...)	do { } while (0)
#endif

/**
 *	sdio_claim_host - exclusively claim a bus for a certain SDIO function
 *	@func: SDIO function that will be accessed
 *
 *	Claim a bus for a set of operations. The SDIO function given
 *	is used to figure out which bus is relevant.
 */
void sdio1_claim_host(struct sdio_func *func)
{
	DBG("[%s] s\n",__func__);
	BUG_ON(!func);
	BUG_ON(!func->card);

	mmc1_claim_host(func->card->host);
	DBG("[%s] e\n",__func__);
}
EXPORT_SYMBOL_GPL(sdio1_claim_host);

/**
 *	sdio_release_host - release a bus for a certain SDIO function
 *	@func: SDIO function that was accessed
 *
 *	Release a bus, allowing others to claim the bus for their
 *	operations.
 */
void sdio1_release_host(struct sdio_func *func)
{
	DBG("[%s] s\n",__func__);
	BUG_ON(!func);
	BUG_ON(!func->card);

	mmc1_release_host(func->card->host);
	DBG("[%s] e\n",__func__);
}
EXPORT_SYMBOL_GPL(sdio1_release_host);

/**
 *	sdio_enable_func - enables a SDIO function for usage
 *	@func: SDIO function to enable
 *
 *	Powers up and activates a SDIO function so that register
 *	access is possible.
 */
int sdio1_enable_func(struct sdio_func *func)
{
	int ret;
	unsigned char reg;
	unsigned long timeout;

	DBG("[%s] s\n",__func__);
	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Enabling device %s...\n", sdio1_func_id(func));

	ret = mmc1_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IOEx, 0, &reg);
	if (ret)
		goto err;

	reg |= 1 << func->num;

	ret = mmc1_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IOEx, reg, NULL);
	if (ret)
		goto err;

	timeout = jiffies + msecs_to_jiffies(func->enable_timeout);

	while (1) {
		ret = mmc1_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IORx, 0, &reg);
		if (ret)
			goto err;
		if (reg & (1 << func->num))
			break;
		ret = -ETIME;
		if (time_after(jiffies, timeout))
			goto err;
	}

	pr_debug("SDIO: Enabled device %s\n", sdio1_func_id(func));

	DBG("[%s] e1\n",__func__);
	return 0;

err:
	pr_debug("SDIO: Failed to enable device %s\n", sdio1_func_id(func));
	DBG("[%s] e2\n",__func__);
	return ret;
}
EXPORT_SYMBOL_GPL(sdio1_enable_func);

/**
 *	sdio_disable_func - disable a SDIO function
 *	@func: SDIO function to disable
 *
 *	Powers down and deactivates a SDIO function. Register access
 *	to this function will fail until the function is reenabled.
 */
int sdio1_disable_func(struct sdio_func *func)
{
	int ret;
	unsigned char reg;

	DBG("[%s] s\n",__func__);
	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Disabling device %s...\n", sdio1_func_id(func));

	ret = mmc1_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IOEx, 0, &reg);
	if (ret)
		goto err;

	reg &= ~(1 << func->num);

	ret = mmc1_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IOEx, reg, NULL);
	if (ret)
		goto err;

	pr_debug("SDIO: Disabled device %s\n", sdio1_func_id(func));

	DBG("[%s] e1\n",__func__);
	return 0;

err:
	pr_debug("SDIO: Failed to disable device %s\n", sdio1_func_id(func));
	DBG("[%s] e2\n",__func__);
	return -EIO;
}
EXPORT_SYMBOL_GPL(sdio1_disable_func);

/**
 *	sdio_set_block_size - set the block size of an SDIO function
 *	@func: SDIO function to change
 *	@blksz: new block size or 0 to use the default.
 *
 *	The default block size is the largest supported by both the function
 *	and the host, with a maximum of 512 to ensure that arbitrarily sized
 *	data transfer use the optimal (least) number of commands.
 *
 *	A driver may call this to override the default block size set by the
 *	core. This can be used to set a block size greater than the maximum
 *	that reported by the card; it is the driver's responsibility to ensure
 *	it uses a value that the card supports.
 *
 *	Returns 0 on success, -EINVAL if the host does not support the
 *	requested block size, or -EIO (etc.) if one of the resultant FBR block
 *	size register writes failed.
 *
 */
int sdio1_set_block_size(struct sdio_func *func, unsigned blksz)
{
	int ret;

	DBG("[%s] s\n",__func__);
	if (blksz > func->card->host->max_blk_size) {
		DBG("[%s] e1\n",__func__);
		return -EINVAL;
	}

	if (blksz == 0) {
		blksz = min(func->max_blksize, func->card->host->max_blk_size);
		blksz = min(blksz, 512u);
	}

	ret = mmc1_io_rw_direct(func->card, 1, 0,
		SDIO_FBR_BASE(func->num) + SDIO_FBR_BLKSIZE,
		blksz & 0xff, NULL);
	if (ret) {
		DBG("[%s] e2\n",__func__);
		return ret;
	}
	ret = mmc1_io_rw_direct(func->card, 1, 0,
		SDIO_FBR_BASE(func->num) + SDIO_FBR_BLKSIZE + 1,
		(blksz >> 8) & 0xff, NULL);
	if (ret) {
		DBG("[%s] e3\n",__func__);
		return ret;
	}
	func->cur_blksize = blksz;
	DBG("[%s] e4\n",__func__);
	return 0;
}
EXPORT_SYMBOL_GPL(sdio1_set_block_size);

/*
 * Calculate the maximum byte mode transfer size
 */
static inline unsigned int sdio1_max_byte_size(struct sdio_func *func)
{
	unsigned mval =	min(func->card->host->max_seg_size,
			    func->card->host->max_blk_size);
	DBG("[%s] s\n",__func__);
	mval = min(mval, func->max_blksize);
	DBG("[%s] e\n",__func__);
	return min(mval, 512u); /* maximum size for byte mode */
}

/**
 *	sdio_align_size - pads a transfer size to a more optimal value
 *	@func: SDIO function
 *	@sz: original transfer size
 *
 *	Pads the original data size with a number of extra bytes in
 *	order to avoid controller bugs and/or performance hits
 *	(e.g. some controllers revert to PIO for certain sizes).
 *
 *	If possible, it will also adjust the size so that it can be
 *	handled in just a single request.
 *
 *	Returns the improved size, which might be unmodified.
 */
unsigned int sdio1_align_size(struct sdio_func *func, unsigned int sz)
{
	unsigned int orig_sz;
	unsigned int blk_sz, byte_sz;
	unsigned chunk_sz;

	DBG("[%s] s\n",__func__);
	orig_sz = sz;

	/*
	 * Do a first check with the controller, in case it
	 * wants to increase the size up to a point where it
	 * might need more than one block.
	 */
	sz = mmc1_align_data_size(func->card, sz);

	/*
	 * If we can still do this with just a byte transfer, then
	 * we're done.
	 */
	if (sz <= sdio1_max_byte_size(func)) {
		DBG("[%s] e1\n",__func__);
		return sz;
	}

	if (func->card->cccr.multi_block) {
		/*
		 * Check if the transfer is already block aligned
		 */
		if ((sz % func->cur_blksize) == 0) {
			DBG("[%s] e2\n",__func__);
			return sz;
		}

		/*
		 * Realign it so that it can be done with one request,
		 * and recheck if the controller still likes it.
		 */
		blk_sz = ((sz + func->cur_blksize - 1) /
			func->cur_blksize) * func->cur_blksize;
		blk_sz = mmc1_align_data_size(func->card, blk_sz);

		/*
		 * This value is only good if it is still just
		 * one request.
		 */
		if ((blk_sz % func->cur_blksize) == 0) {
			DBG("[%s] e3\n",__func__);
			return blk_sz;
		}

		/*
		 * We failed to do one request, but at least try to
		 * pad the remainder properly.
		 */
		byte_sz = mmc1_align_data_size(func->card,
				sz % func->cur_blksize);
		if (byte_sz <= sdio1_max_byte_size(func)) {
			blk_sz = sz / func->cur_blksize;
			DBG("[%s] e4\n",__func__);
			return blk_sz * func->cur_blksize + byte_sz;
		}
	} else {
		/*
		 * We need multiple requests, so first check that the
		 * controller can handle the chunk size;
		 */
		chunk_sz = mmc1_align_data_size(func->card,
				sdio1_max_byte_size(func));
		if (chunk_sz == sdio1_max_byte_size(func)) {
			/*
			 * Fix up the size of the remainder (if any)
			 */
			byte_sz = orig_sz % chunk_sz;
			if (byte_sz) {
				byte_sz = mmc1_align_data_size(func->card,
						byte_sz);
			}

			DBG("[%s] e5\n",__func__);
			return (orig_sz / chunk_sz) * chunk_sz + byte_sz;
		}
	}

	/*
	 * The controller is simply incapable of transferring the size
	 * we want in decent manner, so just return the original size.
	 */
	DBG("[%s] e6\n",__func__);
	return orig_sz;
}
EXPORT_SYMBOL_GPL(sdio1_align_size);

/* Split an arbitrarily sized data transfer into several
 * IO_RW_EXTENDED commands. */
static int sdio1_io_rw_ext_helper(struct sdio_func *func, int write,
	unsigned addr, int incr_addr, u8 *buf, unsigned size)
{
	unsigned remainder = size;
	unsigned max_blocks;
	int ret;

	DBG("[%s] s\n",__func__);
	/* Do the bulk of the transfer using block mode (if supported). */
	if (func->card->cccr.multi_block && (size > sdio1_max_byte_size(func))) {
		/* Blocks per command is limited by host count, host transfer
		 * size (we only use a single sg entry) and the maximum for
		 * IO_RW_EXTENDED of 511 blocks. */
		max_blocks = min(func->card->host->max_blk_count,
			func->card->host->max_seg_size / func->cur_blksize);
		max_blocks = min(max_blocks, 511u);

		while (remainder > func->cur_blksize) {
			unsigned blocks;

			blocks = remainder / func->cur_blksize;
			if (blocks > max_blocks)
				blocks = max_blocks;
			size = blocks * func->cur_blksize;

			ret = mmc1_io_rw_extended(func->card, write,
				func->num, addr, incr_addr, buf,
				blocks, func->cur_blksize);
			if (ret) {
				DBG("[%s] e1\n",__func__);
				return ret;
			}

			remainder -= size;
			buf += size;
			if (incr_addr)
				addr += size;
		}
	}

	/* Write the remainder using byte mode. */
	while (remainder > 0) {
		size = min(remainder, sdio1_max_byte_size(func));

		ret = mmc1_io_rw_extended(func->card, write, func->num, addr,
			 incr_addr, buf, 1, size);
		if (ret) {
			DBG("[%s] e2\n",__func__);
			return ret;
		}

		remainder -= size;
		buf += size;
		if (incr_addr)
			addr += size;
	}
	DBG("[%s] e3\n",__func__);
	return 0;
}

/**
 *	sdio_readb - read a single byte from a SDIO function
 *	@func: SDIO function to access
 *	@addr: address to read
 *	@err_ret: optional status value from transfer
 *
 *	Reads a single byte from the address space of a given SDIO
 *	function. If there is a problem reading the address, 0xff
 *	is returned and @err_ret will contain the error code.
 */
u8 sdio1_readb(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;
	u8 val;

	DBG("[%s] s\n",__func__);
	BUG_ON(!func);

	if (err_ret)
		*err_ret = 0;

	ret = mmc1_io_rw_direct(func->card, 0, func->num, addr, 0, &val);
	if (ret) {
		if (err_ret)
			*err_ret = ret;
		DBG("[%s] e1\n",__func__);
		return 0xFF;
	}

	DBG("[%s] e2\n",__func__);
	return val;
}
EXPORT_SYMBOL_GPL(sdio1_readb);

/**
 *	sdio_readb_ext - read a single byte from a SDIO function
 *	@func: SDIO function to access
 *	@addr: address to read
 *	@err_ret: optional status value from transfer
 *	@in: value to add to argument
 *
 *	Reads a single byte from the address space of a given SDIO
 *	function. If there is a problem reading the address, 0xff
 *	is returned and @err_ret will contain the error code.
 */
unsigned char sdio1_readb_ext(struct sdio_func *func, unsigned int addr,
	int *err_ret, unsigned in)
{
	int ret;
	unsigned char val;

	DBG("[%s] s\n",__func__);
	BUG_ON(!func);

	if (err_ret)
		*err_ret = 0;

	ret = mmc1_io_rw_direct(func->card, 0, func->num, addr, (u8)in, &val);
	if (ret) {
		if (err_ret)
			*err_ret = ret;
		DBG("[%s] e1\n",__func__);
		return 0xFF;
	}

	DBG("[%s] e2\n",__func__);
	return val;
}
EXPORT_SYMBOL_GPL(sdio1_readb_ext);

/**
 *	sdio_writeb - write a single byte to a SDIO function
 *	@func: SDIO function to access
 *	@b: byte to write
 *	@addr: address to write to
 *	@err_ret: optional status value from transfer
 *
 *	Writes a single byte to the address space of a given SDIO
 *	function. @err_ret will contain the status of the actual
 *	transfer.
 */
void sdio1_writeb(struct sdio_func *func, u8 b, unsigned int addr, int *err_ret)
{
	int ret;
	DBG("[%s] s\n",__func__);
	BUG_ON(!func);

	ret = mmc1_io_rw_direct(func->card, 1, func->num, addr, b, NULL);
	if (err_ret)
		*err_ret = ret;
	DBG("[%s] e\n",__func__);
}
EXPORT_SYMBOL_GPL(sdio1_writeb);

/**
 *	sdio_memcpy_fromio - read a chunk of memory from a SDIO function
 *	@func: SDIO function to access
 *	@dst: buffer to store the data
 *	@addr: address to begin reading from
 *	@count: number of bytes to read
 *
 *	Reads from the address space of a given SDIO function. Return
 *	value indicates if the transfer succeeded or not.
 */
int sdio1_memcpy_fromio(struct sdio_func *func, void *dst,
	unsigned int addr, int count)
{
	int ret;

	DBG("[%s] s\n",__func__);
	ret = sdio1_io_rw_ext_helper(func, 0, addr, 1, dst, count);
	DBG("[%s] e\n",__func__);

	return ret;
}
EXPORT_SYMBOL_GPL(sdio1_memcpy_fromio);

/**
 *	sdio_memcpy_toio - write a chunk of memory to a SDIO function
 *	@func: SDIO function to access
 *	@addr: address to start writing to
 *	@src: buffer that contains the data to write
 *	@count: number of bytes to write
 *
 *	Writes to the address space of a given SDIO function. Return
 *	value indicates if the transfer succeeded or not.
 */
int sdio1_memcpy_toio(struct sdio_func *func, unsigned int addr,
	void *src, int count)
{
	int ret;

	DBG("[%s] s\n",__func__);
	ret = sdio1_io_rw_ext_helper(func, 1, addr, 1, src, count);
	DBG("[%s] e\n",__func__);
	return ret;
}
EXPORT_SYMBOL_GPL(sdio1_memcpy_toio);

/**
 *	sdio_readsb - read from a FIFO on a SDIO function
 *	@func: SDIO function to access
 *	@dst: buffer to store the data
 *	@addr: address of (single byte) FIFO
 *	@count: number of bytes to read
 *
 *	Reads from the specified FIFO of a given SDIO function. Return
 *	value indicates if the transfer succeeded or not.
 */
int sdio1_readsb(struct sdio_func *func, void *dst, unsigned int addr,
	int count)
{
	int ret;
	
	DBG("[%s] s\n",__func__);
	ret = sdio1_io_rw_ext_helper(func, 0, addr, 0, dst, count);
	DBG("[%s] e\n",__func__);
	
	return ret;
}
EXPORT_SYMBOL_GPL(sdio1_readsb);

/**
 *	sdio_writesb - write to a FIFO of a SDIO function
 *	@func: SDIO function to access
 *	@addr: address of (single byte) FIFO
 *	@src: buffer that contains the data to write
 *	@count: number of bytes to write
 *
 *	Writes to the specified FIFO of a given SDIO function. Return
 *	value indicates if the transfer succeeded or not.
 */
int sdio1_writesb(struct sdio_func *func, unsigned int addr, void *src,
	int count)
{
	int ret;

	DBG("[%s] s\n",__func__);
	ret = sdio1_io_rw_ext_helper(func, 1, addr, 0, src, count);
	DBG("[%s] e\n",__func__);
	
	return ret;
}
EXPORT_SYMBOL_GPL(sdio1_writesb);

/**
 *	sdio_readw - read a 16 bit integer from a SDIO function
 *	@func: SDIO function to access
 *	@addr: address to read
 *	@err_ret: optional status value from transfer
 *
 *	Reads a 16 bit integer from the address space of a given SDIO
 *	function. If there is a problem reading the address, 0xffff
 *	is returned and @err_ret will contain the error code.
 */
u16 sdio1_readw(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;

	DBG("[%s] s\n",__func__);
	if (err_ret)
		*err_ret = 0;

	ret = sdio1_memcpy_fromio(func, func->tmpbuf, addr, 2);
	if (ret) {
		if (err_ret)
			*err_ret = ret;
		DBG("[%s] e1\n",__func__);
		return 0xFFFF;
	}
	
	DBG("[%s] e2\n",__func__);
	return le16_to_cpup((__le16 *)func->tmpbuf);
}
EXPORT_SYMBOL_GPL(sdio1_readw);

/**
 *	sdio_writew - write a 16 bit integer to a SDIO function
 *	@func: SDIO function to access
 *	@b: integer to write
 *	@addr: address to write to
 *	@err_ret: optional status value from transfer
 *
 *	Writes a 16 bit integer to the address space of a given SDIO
 *	function. @err_ret will contain the status of the actual
 *	transfer.
 */
void sdio1_writew(struct sdio_func *func, u16 b, unsigned int addr, int *err_ret)
{
	int ret;
	DBG("[%s] s\n",__func__);
	*(__le16 *)func->tmpbuf = cpu_to_le16(b);

	ret = sdio1_memcpy_toio(func, addr, func->tmpbuf, 2);
	if (err_ret)
		*err_ret = ret;
	DBG("[%s] e\n",__func__);
}
EXPORT_SYMBOL_GPL(sdio1_writew);

/**
 *	sdio_readl - read a 32 bit integer from a SDIO function
 *	@func: SDIO function to access
 *	@addr: address to read
 *	@err_ret: optional status value from transfer
 *
 *	Reads a 32 bit integer from the address space of a given SDIO
 *	function. If there is a problem reading the address,
 *	0xffffffff is returned and @err_ret will contain the error
 *	code.
 */
u32 sdio1_readl(struct sdio_func *func, unsigned int addr, int *err_ret)
{
	int ret;
	DBG("[%s] s\n",__func__);
	if (err_ret)
		*err_ret = 0;

	ret = sdio1_memcpy_fromio(func, func->tmpbuf, addr, 4);
	if (ret) {
		if (err_ret)
			*err_ret = ret;
		DBG("[%s] e1\n",__func__);
		return 0xFFFFFFFF;
	}
	
	DBG("[%s] e2\n",__func__);
	return le32_to_cpup((__le32 *)func->tmpbuf);
}
EXPORT_SYMBOL_GPL(sdio1_readl);

/**
 *	sdio_writel - write a 32 bit integer to a SDIO function
 *	@func: SDIO function to access
 *	@b: integer to write
 *	@addr: address to write to
 *	@err_ret: optional status value from transfer
 *
 *	Writes a 32 bit integer to the address space of a given SDIO
 *	function. @err_ret will contain the status of the actual
 *	transfer.
 */
void sdio1_writel(struct sdio_func *func, u32 b, unsigned int addr, int *err_ret)
{
	int ret;
	DBG("[%s] s\n",__func__);
	*(__le32 *)func->tmpbuf = cpu_to_le32(b);

	ret = sdio1_memcpy_toio(func, addr, func->tmpbuf, 4);
	if (err_ret)
		*err_ret = ret;
	DBG("[%s] e\n",__func__);
}
EXPORT_SYMBOL_GPL(sdio1_writel);

/**
 *	sdio_f0_readb - read a single byte from SDIO function 0
 *	@func: an SDIO function of the card
 *	@addr: address to read
 *	@err_ret: optional status value from transfer
 *
 *	Reads a single byte from the address space of SDIO function 0.
 *	If there is a problem reading the address, 0xff is returned
 *	and @err_ret will contain the error code.
 */
unsigned char sdio1_f0_readb(struct sdio_func *func, unsigned int addr,
	int *err_ret)
{
	int ret;
	unsigned char val;
	DBG("[%s] s\n",__func__);
	BUG_ON(!func);

	if (err_ret)
		*err_ret = 0;

	ret = mmc1_io_rw_direct(func->card, 0, 0, addr, 0, &val);
	if (ret) {
		if (err_ret)
			*err_ret = ret;
		DBG("[%s] e1\n",__func__);
		return 0xFF;
	}

	DBG("[%s] e2\n",__func__);
	return val;
}
EXPORT_SYMBOL_GPL(sdio1_f0_readb);

/**
 *	sdio_f0_writeb - write a single byte to SDIO function 0
 *	@func: an SDIO function of the card
 *	@b: byte to write
 *	@addr: address to write to
 *	@err_ret: optional status value from transfer
 *
 *	Writes a single byte to the address space of SDIO function 0.
 *	@err_ret will contain the status of the actual transfer.
 *
 *	Only writes to the vendor specific CCCR registers (0xF0 -
 *	0xFF) are permiited; @err_ret will be set to -EINVAL for *
 *	writes outside this range.
 */
void sdio1_f0_writeb(struct sdio_func *func, unsigned char b, unsigned int addr,
	int *err_ret)
{
	int ret;
	DBG("[%s] s\n",__func__);
	BUG_ON(!func);

	if ((addr < 0xF0 || addr > 0xFF) && (!mmc1_card_lenient_fn0(func->card))) {
		if (err_ret)
			*err_ret = -EINVAL;
		DBG("[%s] e1\n",__func__);
		return;
	}

	ret = mmc1_io_rw_direct(func->card, 1, 0, addr, b, NULL);
	if (err_ret)
		*err_ret = ret;
	DBG("[%s] e2\n",__func__);
}
EXPORT_SYMBOL_GPL(sdio1_f0_writeb);
