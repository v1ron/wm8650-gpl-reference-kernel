/*
 *  linux/drivers/mmc/core/bus.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright (C) 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMC card bus driver model
 */

#include <linux/device.h>
#include <linux/err.h>

#include <linux/mmc1/card.h>
#include <linux/mmc1/host.h>

#include "core.h"
#include "sdio_cis.h"
#include "bus.h"

#define dev_to_mmc_card(d)	container_of(d, struct mmc_card, dev)
#define to_mmc_driver(d)	container_of(d, struct mmc_driver, drv)

#if 0
#define DBG(x...)	printk(KERN_ALERT x)
#else
#define DBG(x...)	do { } while (0)
#endif

static ssize_t mmc1_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mmc_card *card = dev_to_mmc_card(dev);
	DBG("[%s] s\n",__func__);
	
	switch (card->type) {
	case MMC_TYPE_MMC:
		return sprintf(buf, "MMC\n");
	case MMC_TYPE_SD:
		return sprintf(buf, "SD\n");
	case MMC_TYPE_SDIO:
		return sprintf(buf, "SDIO\n");
	default:
		DBG("[%s] e1\n",__func__);
		return -EFAULT;
	}
	DBG("[%s] e2\n",__func__);
}

static struct device_attribute mmc_dev_attrs[] = {
	__ATTR(type, S_IRUGO, mmc1_type_show, NULL),
	__ATTR_NULL,
};

/*
 * This currently matches any MMC driver to any MMC card - drivers
 * themselves make the decision whether to drive this card in their
 * probe method.
 */
static int mmc1_bus_match(struct device *dev, struct device_driver *drv)
{
	DBG("[%s] \n",__func__);
	return 1;
}

static int
mmc1_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct mmc_card *card = dev_to_mmc_card(dev);
	const char *type;
	int retval = 0;
	DBG("[%s] s\n",__func__);
	
	switch (card->type) {
	case MMC_TYPE_MMC:
		type = "MMC";
		break;
	case MMC_TYPE_SD:
		type = "SD";
		break;
	case MMC_TYPE_SDIO:
		type = "SDIO";
		break;
	default:
		type = NULL;
	}

	if (type) {
		retval = add_uevent_var(env, "MMC_TYPE=%s", type);
		if (retval) {
			DBG("[%s] e1\n",__func__);
			return retval;
		}
	}

	retval = add_uevent_var(env, "MMC_NAME=%s", mmc1_card_name(card));
	if (retval) {
		DBG("[%s] e2\n",__func__);
		return retval;
	}

	/*
	 * Request the mmc_block device.  Note: that this is a direct request
	 * for the module it carries no information as to what is inserted.
	 */
	retval = add_uevent_var(env, "MODALIAS=mmc:block");
	DBG("[%s] e3\n",__func__);
	return retval;
}

static int mmc1_bus_probe(struct device *dev)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);
	DBG("[%s] \n",__func__);
	return drv->probe(card);
}

static int mmc1_bus_remove(struct device *dev)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);
	DBG("[%s] s\n",__func__);
	drv->remove(card);
	DBG("[%s] e\n",__func__);
	return 0;
}

static int mmc1_bus_suspend(struct device *dev, pm_message_t state)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);
	int ret = 0;
	DBG("[%s] s\n",__func__);
	if (dev->driver && drv->suspend)
		ret = drv->suspend(card, state);
	DBG("[%s] e\n",__func__);
	return ret;
}

static int mmc1_bus_resume(struct device *dev)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);
	int ret = 0;
	DBG("[%s] s\n",__func__);
	if (dev->driver && drv->resume)
		ret = drv->resume(card);
	DBG("[%s] e\n",__func__);
	return ret;
}

static struct bus_type mmc_bus_type = {
	.name		= "mmc1",
	.dev_attrs	= mmc_dev_attrs,
	.match		= mmc1_bus_match,
	.uevent		= mmc1_bus_uevent,
	.probe		= mmc1_bus_probe,
	.remove		= mmc1_bus_remove,
	.suspend	= mmc1_bus_suspend,
	.resume		= mmc1_bus_resume,
};

int mmc1_register_bus(void)
{
	DBG("[%s] \n",__func__);
	return bus_register(&mmc_bus_type);
}

void mmc1_unregister_bus(void)
{
	DBG("[%s] \n",__func__);
	bus_unregister(&mmc_bus_type);
}

/**
 *	mmc_register_driver - register a media driver
 *	@drv: MMC media driver
 */
int mmc1_register_driver(struct mmc_driver *drv)
{
	DBG("[%s] s\n",__func__);
	drv->drv.bus = &mmc_bus_type;
	DBG("[%s] e\n",__func__);
	return driver_register(&drv->drv);
}

EXPORT_SYMBOL(mmc1_register_driver);

/**
 *	mmc_unregister_driver - unregister a media driver
 *	@drv: MMC media driver
 */
void mmc1_unregister_driver(struct mmc_driver *drv)
{
	DBG("[%s] s\n",__func__);
	drv->drv.bus = &mmc_bus_type;
	driver_unregister(&drv->drv);
	DBG("[%s] e\n",__func__);
}

EXPORT_SYMBOL(mmc1_unregister_driver);

static void mmc1_release_card(struct device *dev)
{
	struct mmc_card *card = dev_to_mmc_card(dev);
	DBG("[%s] s\n",__func__);
	sdio1_free_common_cis(card);

	if (card->info)
		kfree(card->info);

	kfree(card);
	DBG("[%s] e\n",__func__);
}

/*
 * Allocate and initialise a new MMC card structure.
 */
struct mmc_card *mmc1_alloc_card(struct mmc_host *host, struct device_type *type)
{
	struct mmc_card *card;
	DBG("[%s] s\n",__func__);

	card = kzalloc(sizeof(struct mmc_card), GFP_KERNEL);
	if (!card) {
		DBG("[%s] e1\n",__func__);
		return ERR_PTR(-ENOMEM);
	}
	
	card->host = host;

	device_initialize(&card->dev);

	card->dev.parent = mmc1_classdev(host);
	card->dev.bus = &mmc_bus_type;
	card->dev.release = mmc1_release_card;
	card->dev.type = type;
	
	DBG("[%s] e2\n",__func__);
	return card;
}

/*
 * Register a new MMC card with the driver model.
 */
int mmc1_add_card(struct mmc_card *card)
{
	int ret;
	const char *type;
	DBG("[%s] s\n",__func__);
	dev_set_name(&card->dev, "%s:%04x", mmc1_hostname(card->host), card->rca);

	switch (card->type) {
	case MMC_TYPE_MMC:
		type = "MMC";
		break;
	case MMC_TYPE_SD:
		type = "SD";
		if (mmc1_card_blockaddr(card))
			type = "SDHC";
		break;
	case MMC_TYPE_SDIO:
		type = "SDIO";
		break;
	default:
		type = "?";
		break;
	}

	if (mmc1_host_is_spi(card->host)) {
		printk(KERN_INFO "%s: new %s%s card on SPI\n",
			mmc1_hostname(card->host),
			mmc1_card_highspeed(card) ? "high speed " : "",
			type);
	} else {
		printk(KERN_INFO "%s: new %s%s card at address %04x\n",
			mmc1_hostname(card->host),
			mmc1_card_highspeed(card) ? "high speed " : "",
			type, card->rca);
	}

	ret = device_add(&card->dev);
	if (ret) {
		DBG("[%s] e1\n",__func__);
		return ret;
	}

#ifdef CONFIG_DEBUG_FS
	mmc1_add_card_debugfs(card);
#endif

	mmc1_card_set_present(card);

	DBG("[%s] e2\n",__func__);
	return 0;
}

/*
 * Unregister a new MMC card with the driver model, and
 * (eventually) free it.
 */
void mmc1_remove_card(struct mmc_card *card)
{
	DBG("[%s] s\n",__func__);
#ifdef CONFIG_DEBUG_FS
	mmc1_remove_card_debugfs(card);
#endif

	if (mmc1_card_present(card)) {
		if (mmc1_host_is_spi(card->host)) {
			printk(KERN_INFO "%s: SPI card removed\n",
				mmc1_hostname(card->host));
		} else {
			printk(KERN_INFO "%s: card %04x removed\n",
				mmc1_hostname(card->host), card->rca);
		}
		device_del(&card->dev);
	}

	put_device(&card->dev);
	DBG("[%s] e\n",__func__);
}

