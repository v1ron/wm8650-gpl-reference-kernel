/*
 * linux/drivers/mmc/core/sdio_irq.c
 *
 * Author:      Nicolas Pitre
 * Created:     June 18, 2007
 * Copyright:   MontaVista Software Inc.
 *
 * Copyright 2008 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <linux/mmc1/core.h>
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

static int process_sdio1_pending_irqs(struct mmc_card *card)
{
	int i, ret, count;
	unsigned char pending;
	DBG("[%s] s\n",__func__);
	ret = mmc1_io_rw_direct(card, 0, 0, SDIO_CCCR_INTx, 0, &pending);
	if (ret) {
		printk(KERN_DEBUG "%s: error %d reading SDIO_CCCR_INTx\n",
		       mmc1_card_id(card), ret);
		DBG("[%s] e1\n",__func__);
		return ret;
	}

	count = 0;
	for (i = 1; i <= 7; i++) {
		if (pending & (1 << i)) {
			struct sdio_func *func = card->sdio_func[i - 1];
			if (!func) {
				printk(KERN_WARNING "%s: pending IRQ for "
					"non-existant function\n",
					mmc1_card_id(card));
				ret = -EINVAL;
			} else if (func->irq_handler) {
				func->irq_handler(func);
				count++;
			} else {
				printk(KERN_WARNING "%s: pending IRQ with no handler\n",
				       sdio1_func_id(func));
				ret = -EINVAL;
			}
		}
	}

	if (count) {
		DBG("[%s] e2\n",__func__);
		return count;
	}

	DBG("[%s] e3\n",__func__);
	return ret;
}

static int sdio1_irq_thread(void *_host)
{
	struct mmc_host *host = _host;
	struct sched_param param = { .sched_priority = 1 };
	unsigned long period, idle_period;
	int ret;

	DBG("[%s] s\n",__func__);
	
	sched_setscheduler(current, SCHED_FIFO, &param);

	/*
	 * We want to allow for SDIO cards to work even on non SDIO
	 * aware hosts.  One thing that non SDIO host cannot do is
	 * asynchronous notification of pending SDIO card interrupts
	 * hence we poll for them in that case.
	 */
	idle_period = msecs_to_jiffies(10);
	period = (host->caps & MMC_CAP_SDIO_IRQ) ?
		MAX_SCHEDULE_TIMEOUT : idle_period;

	pr_debug("%s: IRQ thread started (poll period = %lu jiffies)\n",
		 mmc1_hostname(host), period);

	do {
		/*
		 * We claim the host here on drivers behalf for a couple
		 * reasons:
		 *
		 * 1) it is already needed to retrieve the CCCR_INTx;
		 * 2) we want the driver(s) to clear the IRQ condition ASAP;
		 * 3) we need to control the abort condition locally.
		 *
		 * Just like traditional hard IRQ handlers, we expect SDIO
		 * IRQ handlers to be quick and to the point, so that the
		 * holding of the host lock does not cover too much work
		 * that doesn't require that lock to be held.
		 */
		ret = __mmc1_claim_host(host, &host->sdio_irq_thread_abort);
		if (ret)
			break;
		ret = process_sdio1_pending_irqs(host->card);
		mmc1_release_host(host);

		/*
		 * Give other threads a chance to run in the presence of
		 * errors.
		 */
		if (ret < 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop())
				schedule_timeout(HZ);
			set_current_state(TASK_RUNNING);
		}

		/*
		 * Adaptive polling frequency based on the assumption
		 * that an interrupt will be closely followed by more.
		 * This has a substantial benefit for network devices.
		 */
		if (!(host->caps & MMC_CAP_SDIO_IRQ)) {
			if (ret > 0)
				period /= 2;
			else {
				period++;
				if (period > idle_period)
					period = idle_period;
			}
		}

		set_current_state(TASK_INTERRUPTIBLE);
		if (host->caps & MMC_CAP_SDIO_IRQ)
			host->ops->enable_sdio_irq(host, 1);
		if (!kthread_should_stop())
			schedule_timeout(period);
		set_current_state(TASK_RUNNING);
	} while (!kthread_should_stop());

	if (host->caps & MMC_CAP_SDIO_IRQ)
		host->ops->enable_sdio_irq(host, 0);

	pr_debug("%s: IRQ thread exiting with code %d\n",
		 mmc1_hostname(host), ret);
	DBG("[%s] e\n",__func__);
	return ret;
}

static int sdio1_card_irq_get(struct mmc_card *card)
{
	struct mmc_host *host = card->host;
	DBG("[%s] s\n",__func__);
	WARN_ON(!host->claimed);

	if (!host->sdio_irqs++) {
		atomic_set(&host->sdio_irq_thread_abort, 0);
		host->sdio_irq_thread =
			kthread_run(sdio1_irq_thread, host, "ksdioirqd/%s",
				mmc1_hostname(host));
		if (IS_ERR(host->sdio_irq_thread)) {
			int err = PTR_ERR(host->sdio_irq_thread);
			host->sdio_irqs--;
			DBG("[%s] e1\n",__func__);
			return err;
		}
	}
	DBG("[%s] e2\n",__func__);
	return 0;
}

static int sdio1_card_irq_put(struct mmc_card *card)
{
	struct mmc_host *host = card->host;
	DBG("[%s] s\n",__func__);
	
	WARN_ON(!host->claimed);
	BUG_ON(host->sdio_irqs < 1);

	if (!--host->sdio_irqs) {
		atomic_set(&host->sdio_irq_thread_abort, 1);
		kthread_stop(host->sdio_irq_thread);
	}
	
	DBG("[%s] e\n",__func__);
	return 0;
}

/**
 *	sdio_claim_irq - claim the IRQ for a SDIO function
 *	@func: SDIO function
 *	@handler: IRQ handler callback
 *
 *	Claim and activate the IRQ for the given SDIO function. The provided
 *	handler will be called when that IRQ is asserted.  The host is always
 *	claimed already when the handler is called so the handler must not
 *	call sdio_claim_host() nor sdio_release_host().
 */
int sdio1_claim_irq(struct sdio_func *func, sdio_irq_handler_t *handler)
{
	int ret;
	unsigned char reg;
	DBG("[%s] s\n",__func__);
	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Enabling IRQ for %s...\n", sdio1_func_id(func));

	if (func->irq_handler) {
		pr_debug("SDIO: IRQ for %s already in use.\n", sdio1_func_id(func));
		DBG("[%s] e1\n",__func__);
		return -EBUSY;
	}

	ret = mmc1_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
	if (ret) {
		DBG("[%s] e2\n",__func__);
		return ret;
	}

	reg |= 1 << func->num;

	reg |= 1; /* Master interrupt enable */

	ret = mmc1_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
	if (ret) {
		DBG("[%s] e3\n",__func__);
		return ret;
	}

	func->irq_handler = handler;
	ret = sdio1_card_irq_get(func->card);
	if (ret)
		func->irq_handler = NULL;

	DBG("[%s] e4\n",__func__);
	return ret;
}
EXPORT_SYMBOL_GPL(sdio1_claim_irq);

/**
 *	sdio_release_irq - release the IRQ for a SDIO function
 *	@func: SDIO function
 *
 *	Disable and release the IRQ for the given SDIO function.
 */
int sdio1_release_irq(struct sdio_func *func)
{
	int ret;
	unsigned char reg;

	DBG("[%s] s\n",__func__);
	BUG_ON(!func);
	BUG_ON(!func->card);

	pr_debug("SDIO: Disabling IRQ for %s...\n", sdio1_func_id(func));

	if (func->irq_handler) {
		func->irq_handler = NULL;
		sdio1_card_irq_put(func->card);
	}

	ret = mmc1_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
	if (ret) {
		DBG("[%s] e1\n",__func__);
		return ret;
	}

	reg &= ~(1 << func->num);

	/* Disable master interrupt with the last function interrupt */
	if (!(reg & 0xFE))
		reg = 0;

	ret = mmc1_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
	if (ret) {
		DBG("[%s] e2\n",__func__);
		return ret;
	}
	
	DBG("[%s] e3\n",__func__);
	return 0;
}
EXPORT_SYMBOL_GPL(sdio1_release_irq);

