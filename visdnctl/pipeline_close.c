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
#include "pipeline_close.h"

static int do_pipeline_close(const char *pipeline_str)
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

	if (ioctl(fd, VISDN_IOC_PIPELINE_CLOSE, &connect) < 0) {
		fprintf(stderr, "ioctl(IOC_PIPELINE_CLOSE) failed: %s\n",
			strerror(errno));

		return 1;
	}

	close(fd);

	return 0;
}

static int handle_pipeline_close(int argc, char *argv[], int optind)
{
	if (argc <= optind + 1)
		print_usage("Missing first endpoint ID\n");

	return do_pipeline_close(argv[optind + 1]);
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  pipeline_close <endpoint>\n"
		"\n"
		"    Disabled all the endpoints comprising a path to which\n"
		"    endpoint belongs.\n");
}

struct module module_pipeline_close =
{
	.cmd	= "pipeline_close",
	.do_it	= handle_pipeline_close,
	.usage	= usage,
};
