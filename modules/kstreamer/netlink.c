/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/netlink.h>

#include <kernel_config.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "netlink.h"
#include "feature.h"
#include "node.h"
#include "channel.h"
#include "pipeline.h"

struct sock *ksnl;

static struct workqueue_struct *ks_netlink_rcv_wq;
static struct sk_buff_head ks_backlog;

struct ks_netlink_state ks_netlink_state = { };

int ks_kobj_make_path(struct kobject *kobj, void *buf)
{
	struct kobject *cur;
	int length = 0;
	int pos;

	for (cur = kobj; cur; cur = cur->parent)
		length += strlen(kobject_name(cur)) + 1;

	if (!buf)
		return length;

	pos = length;

	for (cur = kobj; cur; cur = cur->parent) {

		int len = strlen(kobject_name(cur));

		pos -= len;

		memcpy(buf + pos, kobject_name(cur), len);
		*((char *)buf + --pos) = '/';
	}

	return length;
}

int ks_netlink_put_attr_path(
	struct sk_buff *skb,
	int type,
	struct kobject *kobj)
{
	int len;
	struct ks_attr *attr;

	len = ks_kobj_make_path(kobj, NULL);

	if (unlikely(skb_tailroom(skb) < KS_ATTR_SPACE(len)))
		return -ENOBUFS;

	attr = (struct ks_attr *)skb_put(skb, KS_ATTR_SPACE(len));
	attr->type = type;
	attr->len = KS_ATTR_LENGTH(len);

	ks_kobj_make_path(kobj, KS_ATTR_DATA(attr));

	return 0;
}

int ks_netlink_put_attr(
	struct sk_buff *skb,
	int type,
	void *data,
	int data_len)
{
	struct ks_attr *attr;

	if (unlikely(skb_tailroom(skb) < KS_ATTR_SPACE(data_len)))
		return -ENOBUFS;

	attr = (struct ks_attr *)skb_put(skb, KS_ATTR_SPACE(data_len));
	attr->type = type;
	attr->len = KS_ATTR_LENGTH(data_len);

	if (data)
		memcpy(KS_ATTR_DATA(attr), data, data_len);

	return 0;
}

int ks_netlink_need_skb(struct ks_netlink_state *state)
{
	if (!state->out_skb) {
		state->out_skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	}

	if (!state->out_skb) {
		ksnl->sk_err = ENOBUFS;
		ksnl->sk_error_report(ksnl);
		return -ENOMEM;
	}

	return 0;
}

int ks_netlink_mcast_need_skb(struct ks_netlink_state *state)
{
	if (!state->mcast_skb) {
		state->mcast_skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
		NETLINK_CB(state->mcast_skb).dst_pid = 0;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
		NETLINK_CB(state->mcast_skb).dst_groups =
					(1 << KS_NETLINK_GROUP_TOPOLOGY);
#else
		NETLINK_CB(state->mcast_skb).dst_group =
					KS_NETLINK_GROUP_TOPOLOGY;
#endif
	}

	if (!state->mcast_skb) {
		ksnl->sk_err = ENOBUFS;
		ksnl->sk_error_report(ksnl);
		return -ENOMEM;
	}

	return 0;
}

void ks_netlink_flush(struct ks_netlink_state *state)
{
	if (state->out_skb) {
		if (state->out_skb->len) {
			int err;

			err = netlink_unicast(ksnl, state->out_skb,
							state->cur_pid, 0);
			if (err < 0) {
				ksnl->sk_err = ENOBUFS;
				ksnl->sk_error_report(ksnl);
			}
		} else
			kfree_skb(state->out_skb);

		state->out_skb = NULL;
	}
}

static void ks_netlink_mcast_flush_nolock(struct ks_netlink_state *state)
{
	struct sk_buff *skb;
	int err;

	while ((skb = skb_dequeue(&state->mcast_queue))) {
		err = netlink_broadcast(ksnl, skb,
				0, KS_NETLINK_GROUP_TOPOLOGY,
				GFP_KERNEL);
		if (err < 0) {
			ksnl->sk_err = ENOBUFS;
			ksnl->sk_error_report(ksnl);
		}
	}

	if (state->mcast_skb) {
		if (state->mcast_skb->len) {

			err = netlink_broadcast(ksnl, state->mcast_skb,
					0, KS_NETLINK_GROUP_TOPOLOGY,
					GFP_KERNEL);
			if (err < 0) {
				ksnl->sk_err = ENOBUFS;
				ksnl->sk_error_report(ksnl);
			}

		} else
			kfree_skb(state->mcast_skb);

		state->mcast_skb = NULL;
	}
}

void ks_netlink_mcast_flush(struct ks_netlink_state *state)
{
	if (!down_write_trylock(&ks_topology_lock)) {
		if (state->mcast_skb) {
			skb_queue_tail(&state->mcast_queue,
					state->mcast_skb);
			state->mcast_skb = NULL;
		}

		return;
	}

	ks_netlink_mcast_flush_nolock(state);

	up_write(&ks_topology_lock);
}

int ks_cmd_done(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	return 0;
}

int ks_cmd_topology_lock(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	state->lock_owner = nlh->nlmsg_pid;

	ks_netlink_mcast_flush_nolock(state);

	state->lock_timer.expires = jiffies + 5 * HZ;
	add_timer(&state->lock_timer);

	ks_netlink_send_ack(state, nlh, 0);

	return 0;
}

int ks_cmd_topology_trylock(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	if (!down_write_trylock(&ks_topology_lock))
		return -EBUSY;

	ks_netlink_mcast_flush_nolock(state);

	state->lock_timer.expires = jiffies + 5 * HZ;
	add_timer(&state->lock_timer);

	ks_netlink_send_ack(state, nlh, 0);

	return 0;
}

int ks_cmd_topology_unlock(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	if (!state->lock_owner)
		return -ENOENT;

	if (state->lock_owner != nlh->nlmsg_pid)
		return -EINVAL;

	state->lock_owner = 0;

	del_timer(&state->lock_timer);

	ks_netlink_send_ack(state, nlh, 0);

	ks_netlink_mcast_flush_nolock(state);

	return 0;
}

int ks_cmd_noop(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	ks_netlink_send_ack(state, nlh, 0);

	return 0;
}

int ks_cmd_version_request(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *req_nlh)
{
	struct ks_netlink_version_response *vr;
	struct nlmsghdr *nlh;

retry:

	ks_netlink_need_skb(state);
	if (!state->out_skb)
		return -ENOMEM;

	nlh = NLMSG_PUT(state->out_skb, state->cur_pid, state->cur_seq,
			KS_NETLINK_VERSION,
			sizeof(*vr));
	nlh->nlmsg_flags = NLM_F_ACK;

	vr = (struct ks_netlink_version_response *)NLMSG_DATA(nlh);
	vr->reserved = 0;
	vr->major = 1;
	vr->minor = 0;
	vr->service = 0;

	return 0;

nlmsg_failure:
	ks_netlink_flush(state);
	goto retry;
}

int ks_cmd_not_implemented(
	struct ks_netlink_state *state,
	struct ks_command *cmd,
	struct nlmsghdr *nlh)
{
	ks_msg(KERN_INFO, "command %d not implemented\n",
		cmd->message_type);

	return 0;
};

struct ks_command ks_commands[] =
{
	{ NLMSG_DONE, ks_cmd_done, 0 },
	{ NLMSG_NOOP, ks_cmd_noop, 0 },

	{ KS_NETLINK_VERSION, ks_cmd_version_request, 0 },

	{ KS_NETLINK_TOPOLOGY_LOCK, ks_cmd_topology_lock, KS_CMD_WR },
	{ KS_NETLINK_TOPOLOGY_TRYLOCK, ks_cmd_topology_trylock, 0 },
	{ KS_NETLINK_TOPOLOGY_UNLOCK, ks_cmd_topology_unlock, 0 },

	{ KS_NETLINK_FEATURE_NEW, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_FEATURE_DEL, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_FEATURE_GET, ks_feature_cmd_get, KS_CMD_RD },
	{ KS_NETLINK_FEATURE_SET, ks_cmd_not_implemented, KS_CMD_WR },

	{ KS_NETLINK_NODE_NEW, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_NODE_DEL, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_NODE_GET, ks_node_cmd_get, KS_CMD_RD },
	{ KS_NETLINK_NODE_SET, ks_cmd_not_implemented, KS_CMD_WR },

	{ KS_NETLINK_CHAN_NEW, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_CHAN_DEL, ks_cmd_not_implemented, KS_CMD_WR },
	{ KS_NETLINK_CHAN_GET, ks_chan_cmd_get, KS_CMD_RD },
	{ KS_NETLINK_CHAN_SET, ks_chan_cmd_set, KS_CMD_WR },

	{ KS_NETLINK_PIPELINE_NEW, ks_pipeline_cmd_new, KS_CMD_WR },
	{ KS_NETLINK_PIPELINE_DEL, ks_pipeline_cmd_del, KS_CMD_WR },
	{ KS_NETLINK_PIPELINE_GET, ks_pipeline_cmd_get, KS_CMD_RD },
	{ KS_NETLINK_PIPELINE_SET, ks_pipeline_cmd_set, KS_CMD_WR },
};

int ks_netlink_send_done(
	struct ks_netlink_state *state,
	struct nlmsghdr *req_nlh,
	u16 seq)
{
	struct nlmsghdr *nlh;

retry:
	ks_netlink_need_skb(state);
	if (!state->out_skb)
		return -ENOMEM;

	nlh = NLMSG_PUT(state->out_skb,
			req_nlh->nlmsg_pid,
			req_nlh->nlmsg_seq + seq,
			NLMSG_DONE, 0);
	nlh->nlmsg_flags = NLM_F_MULTI;

	return 0;

nlmsg_failure:
	ks_netlink_flush(state);
	goto retry;
}

int ks_netlink_send_ack(
	struct ks_netlink_state *state,
	struct nlmsghdr *req_nlh,
	int flags)
{
	struct nlmsghdr *nlh;

retry:
	ks_netlink_need_skb(state);
	if (!state->out_skb)
		return -ENOMEM;

	nlh = NLMSG_PUT(state->out_skb,
			req_nlh->nlmsg_pid,
			req_nlh->nlmsg_seq,
			req_nlh->nlmsg_type, 0);
	nlh->nlmsg_flags = flags | NLM_F_ACK;

	return 0;

nlmsg_failure:
	ks_netlink_flush(state);
	goto retry;
}

int ks_netlink_send_error(
	struct ks_netlink_state *state,
	struct nlmsghdr *req_nlh,
	int error)
{
	struct nlmsghdr *nlh;

retry:
	ks_netlink_need_skb(state);
	if (!state->out_skb)
		return -ENOMEM;

	nlh = NLMSG_PUT(state->out_skb, req_nlh->nlmsg_pid, req_nlh->nlmsg_seq,
						NLMSG_ERROR, sizeof(int));
	nlh->nlmsg_flags = NLM_F_ACK;

	*((int *)NLMSG_DATA(nlh)) = -error;

	return 0;

nlmsg_failure:
	ks_netlink_flush(state);
	goto retry;
}

static int ks_netlink_rcv_msg(
	struct ks_netlink_state *state,
	struct sk_buff *skb,
	struct nlmsghdr *nlh)
{
	struct ks_command *cmd = NULL;
	int i;
	int err;

	/* Only requests are handled */
	if (!(nlh->nlmsg_flags & NLM_F_REQUEST)) {
		err = -EINVAL;
		goto err_not_request;
	}

	/* Do this with a hash TODO */
	for(i=0; i<ARRAY_SIZE(ks_commands); i++) {
		if (ks_commands[i].message_type == nlh->nlmsg_type) {
			cmd = &ks_commands[i];
			break;
		}
	}

	if (!cmd) {
		err = -EINVAL;
		goto err_invalid_command;
	}

	if (!down_write_trylock(&ks_topology_lock)) {
		if (state->lock_owner != nlh->nlmsg_pid) {
			err = -EAGAIN;
			goto err_lock_failed;
		}
	}

	state->cur_pid = nlh->nlmsg_pid;
	state->cur_seq = nlh->nlmsg_seq;

	err = cmd->handler(state, cmd, nlh);
	if (err < 0)
		ks_netlink_send_error(state, nlh, err);

	ks_netlink_flush(state);

	/* We have the lock but there is no real owner, release it */
	if (!state->lock_owner)
		up_write(&ks_topology_lock);

	return 0;

	if (!state->lock_owner)
		up_write(&ks_topology_lock);
err_lock_failed:
err_not_request:
err_invalid_command:

	return err;
}

static int ks_netlink_rcv_skb(
	struct ks_netlink_state *state,
	struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	int err;

	nlh = (struct nlmsghdr *)skb->data;

	while (skb->len >= NLMSG_SPACE(0)) {
		u32 rlen;

		nlh = (struct nlmsghdr *)skb->data;
		if (nlh->nlmsg_len < sizeof(*nlh) ||
		    skb->len < nlh->nlmsg_len)
			return 0;

		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;

		err = ks_netlink_rcv_msg(state, skb, nlh);
		if (err == -EAGAIN) {
			return err;
		} else
			netlink_ack(skb, nlh, err);

		skb_pull(skb, rlen);
	}

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void ks_netlink_rcv_work_func(void *data)
#else
static void ks_netlink_rcv_work_func(struct work_struct *work)
#endif
{
	struct sk_buff *skb;
	struct sk_buff *tail;
	int err;
	int processed;

	while ((skb = skb_dequeue(&ksnl->sk_receive_queue))) {

		err = ks_netlink_rcv_skb(&ks_netlink_state, skb);
		if (err == -EAGAIN)
			skb_queue_tail(&ks_backlog, skb);
		else
			kfree_skb(skb);
	}

redo_backlog:
	tail = skb_peek_tail(&ks_backlog);
	processed = FALSE;
	while ((skb = skb_dequeue(&ks_backlog))) {
		err = ks_netlink_rcv_skb(&ks_netlink_state, skb);
		if (err == -EAGAIN)
			skb_queue_tail(&ks_backlog, skb);
		else {
			kfree_skb(skb);
			processed = TRUE;
		}

		if (skb == tail)
			break;
	}

	if (processed)
		goto redo_backlog;

	ks_netlink_mcast_flush(&ks_netlink_state);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static DECLARE_WORK(ks_netlink_rcv_work, ks_netlink_rcv_work_func, NULL);
#else
static DECLARE_WORK(ks_netlink_rcv_work, ks_netlink_rcv_work_func);
#endif

void ks_lock_timeout(unsigned long data)
{
//	struct ks_netlink_state *state = (void *)data;

	ks_msg(KERN_INFO, "topology lock timer expired!\n");

	up_write(&ks_topology_lock);

	queue_work(ks_netlink_rcv_wq, &ks_netlink_rcv_work);
}

static void ks_netlink_rcv(struct sock *sk, int len)
{
	/* Packets can only be coming in from ksnl */

	queue_work(ks_netlink_rcv_wq, &ks_netlink_rcv_work);
}

int ks_netlink_modinit(void)
{
	int err;

	skb_queue_head_init(&ks_backlog);

	ks_netlink_rcv_wq = create_singlethread_workqueue("ksnl");
	if (!ks_netlink_rcv_wq) {
		err = -ENOMEM;
		goto err_create_workqueue;
	}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
	ksnl = netlink_kernel_create(NETLINK_KSTREAMER, ks_netlink_rcv);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	ksnl = netlink_kernel_create(NETLINK_KSTREAMER, 0,
					ks_netlink_rcv, THIS_MODULE);
#else
	ksnl = netlink_kernel_create(NETLINK_KSTREAMER, 0, ks_netlink_rcv,
							NULL, THIS_MODULE);
#endif
	if (!ksnl) {
		err = -ENOMEM;
		goto err_netlink_kernel_create;
	}

	netlink_set_nonroot(NETLINK_KSTREAMER, NL_NONROOT_RECV);

	ks_netlink_state.mcast_seqnum = 0xBEEF;

	init_timer(&ks_netlink_state.lock_timer);
	ks_netlink_state.lock_timer.function = ks_lock_timeout;
	ks_netlink_state.lock_timer.data = (unsigned long)&ks_netlink_state;

	skb_queue_head_init(&ks_netlink_state.mcast_queue);

	return 0;

	destroy_workqueue(ks_netlink_rcv_wq);
err_create_workqueue:
	sock_release(ksnl->sk_socket);
err_netlink_kernel_create:

	return err;
}

void ks_netlink_modexit(void)
{
	destroy_workqueue(ks_netlink_rcv_wq);

	sock_release(ksnl->sk_socket);
}

