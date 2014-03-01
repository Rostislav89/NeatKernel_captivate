/* linux/arch/arm/plat-s5p/bootmem.c
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Bootmem helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/err.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/dma-contiguous.h>
#include <asm/setup.h>
#include <linux/io.h>
#include <mach/memory.h>
#include <plat/media.h>
#include <mach/media.h>

static struct s5p_media_device *media_devs;
static int nr_media_devs;

static dma_addr_t media_base[NR_BANKS];

static struct s5p_media_device *s5p_get_media_device(int dev_id, int bank)
{
	struct s5p_media_device *mdev = NULL;
	int i = 0, found = 0;

	if (dev_id < 0)
		return NULL;

	while (!found && (i < nr_media_devs)) {
		mdev = &media_devs[i];
		if (mdev->id == dev_id && mdev->bank == bank)
			found = 1;
		else
			i++;
	}

	if (!found)
		mdev = NULL;

	return mdev;
}

dma_addr_t s5p_get_media_memory_bank(int dev_id, int bank)
{
	struct s5p_media_device *mdev;

	mdev = s5p_get_media_device(dev_id, bank);
	if (!mdev) {
		printk(KERN_ERR "invalid media device\n");
		return 0;
	}

	if (!mdev->paddr) {
		printk(KERN_ERR "no memory for %s\n", mdev->name);
		return 0;
	}

	return mdev->paddr;
}
EXPORT_SYMBOL(s5p_get_media_memory_bank);

size_t s5p_get_media_memsize_bank(int dev_id, int bank)
{
	struct s5p_media_device *mdev;

	mdev = s5p_get_media_device(dev_id, bank);
	if (!mdev) {
		printk(KERN_ERR "invalid media device\n");
		return 0;
	}

	return mdev->memsize;
}
EXPORT_SYMBOL(s5p_get_media_memsize_bank);

int s5p_alloc_media_memory_bank(int dev_id, int bank)

{
	struct s5p_media_device *mdev;

	mdev = s5p_get_media_device(dev_id, bank);
	if (!mdev) {
		printk(KERN_ERR "invalid media device\n");
		return -EINVAL;
	}

	if (!mdev->cmadev) {
		// Not a CMA allocation
		return 0;
	}

	if (mdev->cmapages) {
		printk(KERN_ERR "s5p-cma: attempt to double allocate pages for %s\n", mdev->name);
		return -EBUSY;
	}

	mdev->cmapages = dma_alloc_from_contiguous(
			mdev->cmadev, mdev->memsize / PAGE_SIZE, 1);
	if (!mdev->cmapages) {
		printk(KERN_ERR "s5p-cma: unable to alloc pages for %s\n", mdev->name);
		return -ENOMEM;
	}

	if (page_to_phys(mdev->cmapages) != mdev->paddr) {
		// XXX: would work in normal cases... but we sometimes set paddr at init
		printk(KERN_ERR "s5p-cma: allocated page does not match paddr for %s\n", mdev->name);
		//mdev->paddr = page_to_phys(mdev->cmapages);
		s5p_release_media_memory_bank(dev_id, bank);
		return -ENOMEM;
	}

	printk(KERN_INFO "s5p-cma: allocated pages for %s\n", mdev->name);
	return 0;
}
EXPORT_SYMBOL(s5p_alloc_media_memory_bank);

int s5p_release_media_memory_bank(int dev_id, int bank)
{
	struct s5p_media_device *mdev;

	mdev = s5p_get_media_device(dev_id, bank);
	if (!mdev) {
		printk(KERN_ERR "invalid media device\n");
		return -EINVAL;
	}

	if (!mdev->cmadev) {
		// Not a CMA allocation
		return 0;
	}

	if (!dma_release_from_contiguous(mdev->cmadev,
			mdev->cmapages, mdev->memsize / PAGE_SIZE)) {
		printk(KERN_ERR "s5p-cma: unable to release pages for %s\n", mdev->name);
		return -EINVAL;
	}
	mdev->cmapages = 0;

	printk(KERN_INFO "s5p-cma: released pages for %s\n", mdev->name);
	return 0;
}
EXPORT_SYMBOL(s5p_release_media_memory_bank);

dma_addr_t s5p_get_media_membase_bank(int bank)
{
	if (bank > meminfo.nr_banks) {
		printk(KERN_ERR "invalid bank.\n");
		return -EINVAL;
	}

	return media_base[bank];
}
EXPORT_SYMBOL(s5p_get_media_membase_bank);

void s5p_reserve_bootmem(struct s5p_media_device *mdevs,
			 int nr_mdevs, size_t boundary)
{
	struct s5p_media_device *mdev;
	u64 start, end;
	int i, ret, cma_align;
        dma_addr_t mfc_paddr;

	media_devs = mdevs;
	nr_media_devs = nr_mdevs;

        // From drivers/base/dma-contiguous.c
	cma_align = PAGE_SIZE << max(MAX_ORDER, pageblock_order);

	for (i = 0; i < meminfo.nr_banks; i++)
		media_base[i] = meminfo.bank[i].start + meminfo.bank[i].size;

	for (i = 0; i < nr_media_devs; i++) {
		mdev = &media_devs[i];
		if (mdev->memsize <= 0)
			continue;

		if (!strcmp(mdev->name, "jpeg"))
			mdev->paddr = mfc_paddr;
		else

		if (!mdev->paddr) {
			start = meminfo.bank[mdev->bank].start;
			end = start + meminfo.bank[mdev->bank].size;

			if (boundary && (boundary < end - start))
				start = end - boundary;

			mdev->paddr = memblock_find_in_range(start, end,
						mdev->memsize, mdev->cmadev ? cma_align : PAGE_SIZE);
		}

		if (mdev->cmadev) {
			if (dma_declare_contiguous(mdev->cmadev, mdev->memsize, mdev->paddr, 0))
				pr_err("dma_declare_contiguous for %s failed\n", mdev->name);
		} else {
			ret = memblock_remove(mdev->paddr, mdev->memsize);
			if (ret < 0)
				pr_err("memblock_reserve(%x, %x) failed\n",
					mdev->paddr, mdev->memsize);
		}

		if (media_base[mdev->bank] > mdev->paddr)
			media_base[mdev->bank] = mdev->paddr;

		if (!strcmp(mdev->name, "mfc") && mdev->bank == 0)
			mfc_paddr = mdev->paddr;

		printk(KERN_INFO "s5p: %lu bytes system memory reserved "
			"for %s at 0x%08x, %d-bank base(0x%08x) cmadev=%p\n",
			(unsigned long) mdev->memsize, mdev->name, mdev->paddr,
			mdev->bank, media_base[mdev->bank], mdev->cmadev);
	}
}

/* FIXME: temporary implementation to avoid compile error */
int dma_needs_bounce(struct device *dev, dma_addr_t addr, size_t size)
{
	return 0;
}


