/*
 * Copyright (c) 2016  Intel Corporation.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef COMMON_H_
#define COMMON_H_

void
sig_handle(int signum);

void
netfe_stream_dump(const struct netfe_stream *fes, struct sockaddr_storage *la,
	struct sockaddr_storage *ra);

uint32_t
netfe_get_streams(struct netfe_stream_list *list, struct netfe_stream *rs[],
	uint32_t num);

struct netfe_stream *
netfe_get_stream(struct netfe_stream_list *list);

void
netfe_put_streams(struct netfe_lcore *fe, struct netfe_stream_list *list,
	struct netfe_stream *fs[], uint32_t num);

void
netfe_put_stream(struct netfe_lcore *fe, struct netfe_stream_list *list,
	struct netfe_stream *s);

void
netfe_rem_stream(struct netfe_stream_list *list, struct netfe_stream *s);

void
netfe_stream_close(struct netfe_lcore *fe, struct netfe_stream *fes);

/*
 * Helper functions, verify the queue for corresponding UDP port.
 */
uint8_t
verify_queue_for_port(const struct netbe_dev *prtq, const uint16_t lport);

size_t
pkt_buf_empty(struct pkt_buf *pb);

void
pkt_buf_fill(uint32_t lcore, struct pkt_buf *pb, uint32_t dlen);

int
netbe_lcore_setup(struct netbe_lcore *lc);

void
netbe_lcore_clear(void);

int
netbe_add_ipv4_route(struct netbe_lcore *lc, const struct netbe_dest *dst,
	uint8_t idx);

int
netbe_add_ipv6_route(struct netbe_lcore *lc, const struct netbe_dest *dst,
	uint8_t idx);

void
fill_dst(struct tle_dest *dst, struct netbe_dev *bed,
	const struct netbe_dest *bdp, uint16_t l3_type, int32_t sid,
	uint8_t proto_id);

int
netbe_add_dest(struct netbe_lcore *lc, uint32_t dev_idx, uint16_t family,
	const struct netbe_dest *dst, uint32_t dnum);

void
fill_arp_reply(struct netbe_dev *dev, struct rte_mbuf *m);

/* this is a semi ARP response implementation of RFC 826
 * in RFC, it algo is as below
 *
 * ?Do I have the hardware type in ar$hrd?
 * Yes: (almost definitely)
 * [optionally check the hardware length ar$hln]
 * ?Do I speak the protocol in ar$pro?
 * Yes:
 *  [optionally check the protocol length ar$pln]
 *  Merge_flag := false
 *  If the pair <protocol type, sender protocol address> is
 *      already in my translation table, update the sender
 *      hardware address field of the entry with the new
 *      information in the packet and set Merge_flag to true.
 *  ?Am I the target protocol address?
 *  Yes:
 *    If Merge_flag is false, add the triplet <protocol type,
 *        sender protocol address, sender hardware address> to
 *        the translation table.
 *    ?Is the opcode ares_op$REQUEST?  (NOW look at the opcode!!)
 *    Yes:
 *      Swap hardware and protocol fields, putting the local
 *          hardware and protocol addresses in the sender fields.
 *      Set the ar$op field to ares_op$REPLY
 *      Send the packet to the (new) target hardware address on
 *          the same hardware on which the request was received.
 *
 * So, in our implementation we skip updating the local cache,
 * we assume that local cache is ok, so we just reply the packet.
 */

void
send_arp_reply(struct netbe_dev *dev, struct pkt_buf *pb);

void
netbe_rx(struct netbe_lcore *lc, uint32_t pidx);

void
netbe_tx(struct netbe_lcore *lc, uint32_t pidx);

void
netbe_lcore(void);

int
netfe_rxtx_get_mss(struct netfe_stream *fes);

int
netfe_rxtx_dispatch_reply(uint32_t lcore, struct netfe_stream *fes);

int
netfe_rx_process(uint32_t lcore, struct netfe_stream *fes);

#endif /* COMMON_H_ */
