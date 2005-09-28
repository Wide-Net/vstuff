/*
 * vISDN LAPD/q.931 protocol implementation
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LAPD_IN_H
#define _LAPD_IN_H

/*
void lapd_frame_reject(struct lapd_sock *lapd_sock, struct sk_buff *skb,
	enum lapd_format_errors error);
*/

int lapd_backlog_rcv(struct sock *sk, struct sk_buff *skb);

int lapd_rcv(struct sk_buff *skb, struct net_device *dev,
		     struct packet_type *pt);

#endif
