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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <libgen.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

#include <linux/visdn/router.h>

#include "visdnctl.h"
#include "pipeline_start.h"

static int do_pipeline_start(const char *pipeline_str)
{
	int fd = open(CXC_CONTROL_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n",
			strerror(errno));

		return 1;
	}

	struct visdn_connect connect;
	connect.pipeline_id = atoi(pipeline_str);
	strcpy(connect.from_endpoint, "");
	strcpy(connect.to_endpoint, "");
	connect.flags = 0;

	if (ioctl(fd, VISDN_IOC_PIPELINE_START, &connect) < 0) {
		fprintf(stderr, "ioctl(IOC_PIPELINE_START) failed: %s\n",
			strerror(errno));

		return 1;
	}

	close(fd);

	return 0;
}

static int handle_pipeline_start(int argc, char *argv[], int optind)
{
	if (argc <= optind + 1)
		print_usage("Missing first endpoint ID\n");

	return do_pipeline_start(argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  pipeline_start <endpoint>\n"
		"\n"
		"    Enable all the endpoints comprising a path to which\n"
		"    endpoint belongs.\n");
}

struct module module_pipeline_start =
{
	.cmd	= "pipeline_start",
	.do_it	= handle_pipeline_start,
	.usage	= usage,
};
