/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_TIMER_H
#define _VISDN_TIMER_H

#ifdef __KERNEL__

struct visdn_timer;
struct visdn_timer_ops
{
	unsigned int (*poll)(struct visdn_timer *timer, poll_table *wait);
};

struct visdn_timer
{
	char name[64]; // FIXME

	void *priv;

	struct class_device class_dev;

	struct visdn_timer_ops *ops;

	struct file *file;
};
int visdn_timer_modinit(void);
void visdn_timer_modexit(void);

#define to_visdn_timer(class) container_of(class, struct visdn_timer, class_dev)

extern void visdn_timer_init(
	struct visdn_timer *visdn_timer,
	struct visdn_timer_ops *ops);

extern struct visdn_timer *visdn_timer_alloc(void);

extern int visdn_timer_register(
	struct visdn_timer *visdn_timer,
	const char *name);

extern void visdn_timer_unregister(
	struct visdn_timer *visdn_timer);

#endif

#endif
