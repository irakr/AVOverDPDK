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

#ifndef UDP_H_
#define UDP_H_

#include <nspk.h>

/*
 * helper function: opens IPv4 and IPv6 streams for selected port.
 */
struct netfe_stream *
netfe_stream_open_udp(struct netfe_lcore *fe, struct netfe_sprm *sprm,
	uint32_t lcore, uint16_t op, uint32_t bidx);

struct netfe_stream *
netfe_lcore_init_udp(const struct netfe_lcore_prm *prm);

// int
// netfe_lcore_init_udp(const struct netfe_lcore_prm *prm);

struct netfe_stream *
find_fwd_dst_udp(uint32_t lcore, struct netfe_stream *fes,
	const struct sockaddr *sa);

int
netfe_addr_eq(struct sockaddr_storage *l, struct sockaddr_storage *r,
	uint16_t family);

void
netfe_pkt_addr(const struct rte_mbuf *m, struct sockaddr_storage *ps,
	uint16_t family);

uint32_t
pkt_eq_addr(struct rte_mbuf *pkt[], uint32_t num, uint16_t family,
	struct sockaddr_storage *cur, struct sockaddr_storage *nxt);

void
netfe_fwd_udp(uint32_t lcore, struct netfe_stream *fes);

void
netfe_rxtx_process_udp(__rte_unused uint32_t lcore, struct netfe_stream *fes);

void
netfe_tx_process_udp(uint32_t lcore, struct netfe_stream *fes);

void
netfe_lcore_fini_udp(void);

#endif /* UDP_H_ */
