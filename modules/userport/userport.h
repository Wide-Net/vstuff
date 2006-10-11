/*
 * vISDN gateway between vISDN's softswitch and userland for stream access
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _KS_USERPORT_H
#define _KS_USERPORT_H

/* See core.h for IOC allocation */
#define KS_UP_GET_NODEID	_IOR(0xd0, 0x20, unsigned int)
#define KS_UP_GET_PRESSURE	_IOR(0xd0, 0x21, unsigned int)

struct vup_ctl
{
	__u32 node_id;
};

#ifdef __KERNEL__

#include <linux/kfifo.h>

#include <linux/kstreamer/node.h>
#include <linux/kstreamer/link.h>

#define vup_MODULE_NAME "visdn-userport"
#define vup_MODULE_PREFIX vup_MODULE_NAME ": "
#define vup_MODULE_DESCR "vISDN userport module"

#define SB_CHAN_HASHBITS 8
#define SB_CHAN_HASHSIZE (1 << SB_CHAN_HASHBITS)

#define to_vup_chan(vchan) container_of((vchan), struct vup_chan, visdn_chan)

enum vup_h223_rx_state
{
	VUP_H223_STATE_HUNTING1,
	VUP_H223_STATE_HUNTING2,
	VUP_H223_STATE_READING_FRAME,
	VUP_H223_STATE_CLOSING,
	VUP_H223_STATE_DROPPING,
};

struct vup_chan
{
	struct list_head node;

	struct ks_node ks_node;
	struct ks_link *ks_link_rx;
	struct ks_link *ks_link_tx;

	int id;

	int framed;

	struct timer_list stimulus_timer;
	int stimulus_frequency;

	struct kfifo *read_fifo;
	spinlock_t read_fifo_lock;
	wait_queue_head_t read_wait_queue;
	struct sk_buff_head read_queue;

	enum vup_h223_rx_state h223_rx_state;
};

#if defined(DEBUG_CODE) && defined(DEBUG_DEFAULTS)
#define vup_debug(dbglevel, format, arg...)			\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG vup_MODULE_PREFIX		\
			format,					\
			## arg)
#else
#define vup_debug(format, arg...) do {} while (0)
#endif

#define vup_msg(level, format, arg...)				\
	printk(level vup_MODULE_PREFIX				\
		format,						\
		## arg)

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#endif

#endif
