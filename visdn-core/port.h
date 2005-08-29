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

#ifndef _VISDN_PORT_H
#define _VISDN_PORT_H

#ifdef __KERNEL__

#define VISDN_PORT_HASHBITS 8

extern struct hlist_head visdn_port_index_hash[];

#define to_visdn_port(class) container_of(class, struct visdn_port, device)

struct visdn_port_ops
{
	int (*enable)(struct visdn_port *port);
	int (*disable)(struct visdn_port *port);
};

struct visdn_port
{
	void *priv;

	struct device device;

	struct hlist_node index_hlist;
	int index;

	char port_name[BUS_ID_SIZE];

	struct visdn_port_ops *ops;

	int enabled;
};

int visdn_port_modinit(void);
void visdn_port_modexit(void);

extern void visdn_port_init(
	struct visdn_port *visdn_port,
	struct visdn_port_ops *ops);

extern struct visdn_port *visdn_port_alloc(void);

extern int visdn_port_register(
	struct visdn_port *visdn_port,
	const char *global_name,
	const char *local_name,
	struct device *parent_device);

extern void visdn_port_unregister(
	struct visdn_port *visdn_port);

#endif

#endif
