/*
 * vISDN - Controlling program
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDNCTL_H
#define _VISDNCTL_H

#include <list.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE !TRUE
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

//#define CHANNELS_DIR "/sys/visdn_channels"
//#define TDM_DIR "/sys/visdn_tdm"
#define CXC_CONTROL_DEV "/dev/visdn/router-control"

struct module
{
	struct list_head node;

	const char *cmd;

	int (*do_it)(int argc, char *argv[], int optind);
	void (*usage)(int argc, char *argv[]);
};

#define verbose(format, arg...)						\
	if (verbosity)							\
		fprintf(stderr,						\
			format,						\
			## arg)

extern int verbosity;

extern void print_usage(const char *fmt, ...);
//extern int decode_endpoint_id(const char *chan_str);

#endif
