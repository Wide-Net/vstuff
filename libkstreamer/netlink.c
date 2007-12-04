/*
 * Userland Kstreamer interface
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <sys/socket.h>

#include <linux/types.h>
#include <linux/netlink.h>

#include <linux/kstreamer/netlink.h>

#include <libskb.h>

#include <libkstreamer/libkstreamer.h>
#include <libkstreamer/netlink.h>
#include <libkstreamer/feature.h>
#include <libkstreamer/node.h>
#include <libkstreamer/channel.h>
#include <libkstreamer/pipeline.h>
#include <libkstreamer/xact.h>
#include <libkstreamer/req.h>

static const char *ks_netlink_message_type_to_string(
		enum ks_netlink_message_type message_type)
{
	switch((int)message_type) {
	case NLMSG_NOOP:
		return "NOOP";
	case NLMSG_ERROR:
		return "ERROR";
	case NLMSG_DONE:
		return "DONE";
	case NLMSG_OVERRUN:
		return "OVERRUN";
	case KS_NETLINK_BEGIN:
		return "BEGIN";
	case KS_NETLINK_COMMIT:
		return "COMMIT";
	case KS_NETLINK_ABORT:
		return "ABORT";
	case KS_NETLINK_VERSION:
		return "VERSION";
	case KS_NETLINK_FEATURE_GET:
		return "FEATURE_GET";
	case KS_NETLINK_FEATURE_NEW:
		return "FEATURE_NEW";
	case KS_NETLINK_FEATURE_DEL:
		return "FEATURE_DEL";
	case KS_NETLINK_FEATURE_SET:
		return "FEATURE_SET";
	case KS_NETLINK_NODE_GET:
		return "NODE_GET";
	case KS_NETLINK_NODE_NEW:
		return "NODE_NEW";
	case KS_NETLINK_NODE_DEL:
		return "NODE_DEL";
	case KS_NETLINK_NODE_SET:
		return "NODE_SET";
	case KS_NETLINK_CHAN_GET:
		return "CHAN_GET";
	case KS_NETLINK_CHAN_NEW:
		return "CHAN_NEW";
	case KS_NETLINK_CHAN_DEL:
		return "CHAN_DEL";
	case KS_NETLINK_CHAN_SET:
		return "CHAN_SET";
	case KS_NETLINK_PIPELINE_GET:
		return "PIPELINE_GET";
	case KS_NETLINK_PIPELINE_NEW:
		return "PIPELINE_NEW";
	case KS_NETLINK_PIPELINE_DEL:
		return "PIPELINE_DEL";
	case KS_NETLINK_PIPELINE_SET:
		return "PIPELINE_SET";
	}

	return "*INVALID*";
}


struct nlmsghdr *ks_nlmsg_put(
	struct sk_buff *skb, __u32 pid, __u32 seq,
	__u16 message_type, __u16 flags, int payload_size)
{
	int size = NLMSG_LENGTH(payload_size);
	struct nlmsghdr *nlh = skb_put(skb, NLMSG_ALIGN(size));

	if (!nlh)
		return NULL;

	nlh->nlmsg_len = size;
	nlh->nlmsg_type = message_type;
	nlh->nlmsg_pid = pid;
	nlh->nlmsg_seq = seq;
	nlh->nlmsg_flags = flags;

	return nlh;
}

int ks_netlink_put_attr(
	struct sk_buff *skb,
	int type,
	void *data,
	int data_len)
{
	struct ks_attr *attr;

	if (skb_tailroom(skb) < KS_ATTR_SPACE(data_len))
		return -ENOBUFS;

	attr = (struct ks_attr *)skb_put(skb, KS_ATTR_SPACE(data_len));
	attr->type = type;
	attr->len = KS_ATTR_LENGTH(data_len);

	if (data)
		memcpy(KS_ATTR_DATA(attr), data, data_len);

	return 0;
}

static void ks_dump_nlh(
	struct ks_conn *conn,
	struct nlmsghdr *nlh,
	const char *prefix)
{
	char flags[256];

	snprintf(flags, sizeof(flags),
		"%s%s%s%s%s%s%s",
		nlh->nlmsg_flags & NLM_F_REQUEST ? "NLM_F_REQUEST " : "",
		nlh->nlmsg_flags & NLM_F_MULTI ? "NLM_F_MULTI " : "",
		nlh->nlmsg_flags & NLM_F_ACK ? "NLM_F_ACK " : "",
		nlh->nlmsg_flags & NLM_F_ECHO ? "NLM_F_ECHO " : "",
		nlh->nlmsg_flags & NLM_F_ROOT ? "NLM_F_ROOT " : "",
		nlh->nlmsg_flags & NLM_F_MATCH ? "NLM_F_MATCH " : "",
		nlh->nlmsg_flags & NLM_F_ATOMIC ? "NLM_F_ATOMIC " : "");

	report_conn(conn, LOG_DEBUG,
		"%s  Message type: %s (%d)\n"
		"%s  PID: %d\n"
		"%s  Sequence number: %d\n"
		"%s  Flags: %s\n",
		prefix,
		ks_netlink_message_type_to_string(nlh->nlmsg_type),
		nlh->nlmsg_type,
		prefix,
		nlh->nlmsg_pid,
		prefix,
		nlh->nlmsg_seq,
		prefix,
		flags);
#if 0
	__u8 *payload = KS_DATA(nlh);
	int payload_len = KS_PAYLOAD(nlh);
	char *payload_text = alloca(payload_len * 3 + 1);
	int i;

	for(i=0; i<payload_len; i++)
		sprintf(pl + i * 3, "%02x ", payload[i]);

	report_conn(conn, LOG_DEBUG,
		"  Payload: %s\n",
		payload_text);
#endif

}

int ks_netlink_sendmsg(struct ks_conn *conn, struct sk_buff *skb)
{
	struct sockaddr_nl dest_sa;
	memset (&dest_sa, 0, sizeof(dest_sa));
	dest_sa.nl_family = AF_NETLINK;
	dest_sa.nl_pid = 0;
	dest_sa.nl_groups = 0;

	struct iovec iov;
	iov.iov_base = skb->data;
	iov.iov_len = skb->len;

	struct msghdr msg;
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = &dest_sa;
	msg.msg_namelen = sizeof(dest_sa);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	if (conn->debug_netlink)
		report_conn(conn, LOG_DEBUG,
			"TX --------- Sending packet len = %d --------\n",
			skb->len);

	struct nlmsghdr *nlh;
	int len_left = skb->len;

	if (conn->debug_netlink) {
		for (nlh = skb->data;
		     NLMSG_OK(nlh, len_left);
		     nlh = NLMSG_NEXT(nlh, len_left))
			ks_dump_nlh(conn, nlh, "TX");
	}

	if (conn->debug_netlink)
		report_conn(conn, LOG_DEBUG,
			"TX ------------------------------------------\n");

	int len = sendmsg(conn->sock, &msg, 0);
	if(len < 0) {
		perror("sendmsg()");
		return -errno;
	}

	kfree_skb(skb);

	return 0;
}

static void ks_netlink_request_complete(
	struct ks_conn *conn,
	int err)
{
	conn->cur_req->err = err;

	pthread_mutex_lock(&conn->cur_req->completed_lock);
	conn->cur_req->completed = TRUE;
	pthread_mutex_unlock(&conn->cur_req->completed_lock);
	pthread_cond_broadcast(&conn->cur_req->completed_cond);

	ks_req_put(conn->cur_req);
	conn->cur_req = NULL;

	if (!list_empty(&conn->cur_xact->requests_sent)) {
		ks_conn_set_state(conn,
			KS_CONN_STATE_WAITING_ACK);
	} else {
		ks_conn_set_state(conn, KS_CONN_STATE_IDLE);
	}
}

static void ks_netlink_receive_unicast(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	if (conn->cur_xact && nlh->nlmsg_type == KS_NETLINK_BEGIN)
		conn->cur_xact->autocommit = FALSE;

	switch(conn->state) {
	case KS_CONN_STATE_NULL:
		report_conn(conn, LOG_ERR,
			"Unexpected message in state NULL\n");
	break;

	case KS_CONN_STATE_IDLE:
		report_conn(conn, LOG_ERR,
			"Unexpected message in state IDLE\n");
	break;

	case KS_CONN_STATE_WAITING_ACK: {

		assert(conn->cur_xact);
		assert(!conn->cur_req);

		assert(!list_empty(&conn->cur_xact->requests_sent));

		pthread_mutex_lock(&conn->cur_xact->requests_lock);
		conn->cur_req = list_entry(conn->cur_xact->requests_sent.next,
						struct ks_req, node);
		list_del(&conn->cur_req->node);
		pthread_mutex_unlock(&conn->cur_xact->requests_lock);

		if (conn->cur_req->response_callback) {
			conn->cur_req->response_callback(conn->cur_req, nlh);
		} else {
			conn->cur_req->response_payload =
				malloc(KS_PAYLOAD(nlh));

			if (!conn->cur_req->response_payload) {
				//FIXME
				abort();
			}

			memcpy(conn->cur_req->response_payload,
					NLMSG_DATA(nlh),
					NLMSG_PAYLOAD(nlh, 0));
		}

		if (nlh->nlmsg_type == NLMSG_ERROR) {
			ks_netlink_request_complete(conn,
				-*((__u32 *)NLMSG_DATA(nlh)));

			break;
		}

		if (nlh->nlmsg_type != conn->cur_req->type) {
			report_conn(conn, LOG_ERR,
				"Message type different: %d %d\n",
				nlh->nlmsg_type, conn->cur_req->type);

			break;
		}

		if (nlh->nlmsg_flags & NLM_F_MULTI) {
			ks_conn_set_state(conn,
				KS_CONN_STATE_WAITING_DONE);
		} else {
			ks_netlink_request_complete(conn, 0);
		}

	}
	break;

	case KS_CONN_STATE_WAITING_DONE:

		assert(conn->cur_xact);
		assert(conn->cur_req);

		if (conn->cur_req->response_callback)
			conn->cur_req->response_callback(conn->cur_req, nlh);

		if (nlh->nlmsg_type == NLMSG_DONE ||
		    nlh->nlmsg_type == NLMSG_ERROR) {

			int err;
			if (nlh->nlmsg_type == NLMSG_ERROR)
				err = -*((__u32 *)NLMSG_DATA(nlh));
			else
				err = 0;

			ks_netlink_request_complete(conn, err);
		}
	break;
	}

	if (conn->cur_xact &&
	    (conn->cur_xact->autocommit ||
	    nlh->nlmsg_type == KS_NETLINK_COMMIT)) {

		pthread_mutex_lock(&conn->cur_xact->state_lock);
		conn->cur_xact->state = KS_XACT_STATE_COMPLETED;
		pthread_mutex_unlock(&conn->cur_xact->state_lock);
		pthread_cond_broadcast(&conn->cur_xact->state_cond);
		ks_xact_put(conn->cur_xact);
		conn->cur_xact = NULL;
	}
}

static void ks_netlink_receive_multicast(
	struct ks_conn *conn,
	struct nlmsghdr *nlh)
{
	if (conn->topology_state == KS_TOPOLOGY_STATE_SYNCHED)
		ks_topology_update(conn, nlh);
}

static void ks_netlink_receive_msg(
	struct ks_conn *conn,
	struct nlmsghdr *nlh,
	struct sockaddr_nl *src_sa)
{
#if 0
	__u8 *data = NLMSG_DATA(nlh);
	__u8 *text = alloca(NLMSG_PAYLOAD(nlh, 0) * 3 + 1);
	int i;
	for(i=0; i<NLMSG_PAYLOAD(nlh, 0); i++)
		sprintf(text + i * 3, "%02x ", *(data + i));

	report_conn(conn, LOG_DEBUG, "%s\n", text);
#endif

	if (conn->debug_netlink)
		ks_dump_nlh(conn, nlh, "RX");

	if (src_sa->nl_groups)
		ks_netlink_receive_multicast(conn, nlh);
	else
		ks_netlink_receive_unicast(conn, nlh);

}

int ks_netlink_receive(struct ks_conn *conn)
{
	int buf_size = NLMSG_SPACE(8192);
	void *buf = malloc(buf_size);
	memset(buf, 0, buf_size);

	struct sockaddr_nl src_sa;

	struct iovec iov;
	iov.iov_base = buf;
	iov.iov_len = buf_size;

	struct msghdr msg = {};
	msg.msg_name = &src_sa;
	msg.msg_namelen = sizeof(src_sa);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	int len = recvmsg(conn->sock, &msg, 0);
	if(len < 0) {
		perror("recvmsg()");
		free(buf);
		return -errno;
	}

	if (conn->debug_netlink)
		report_conn(conn, LOG_DEBUG,
			"RX --------- Received packet len = %d groups = %d"
			"--------\n"
			,len, src_sa.nl_groups);

	struct nlmsghdr *nlh;
	int len_left = len;

	for (nlh = buf;
	     NLMSG_OK(nlh, len_left);
	     nlh = NLMSG_NEXT(nlh, len_left))
		ks_netlink_receive_msg(conn, nlh, &src_sa);

	free(buf);

	if (conn->debug_netlink)
		report_conn(conn, LOG_DEBUG,
			"RX -------------------------------------------\n");

	return 0;
}
