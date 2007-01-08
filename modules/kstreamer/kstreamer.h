/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef __KSTREAMER_H
#define __KSTREAMER_H

#ifdef __KERNEL__

extern struct class ks_system_class;

extern struct subsystem kstreamer_subsys;

extern struct device ks_system_device;

#endif

#endif
