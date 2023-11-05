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

#ifndef __FWDTBL_H__
#define __FWDTBL_H__

#include <nspk.h>

struct fwd4_key {
	uint32_t port;
	struct in_addr addr;
} __attribute__((__packed__));

struct fwd6_key {
	uint32_t port;
	struct in6_addr addr;
} __attribute__((__packed__));

union fwd_key {
	struct fwd4_key k4;
	struct fwd6_key k6;
};

struct rte_hash *
fwd_tbl_key_prep(const struct netfe_lcore *fe, uint16_t family,
	const struct sockaddr *sa, union fwd_key *key);

int
fwd_tbl_add(struct netfe_lcore *fe, uint16_t family, const struct sockaddr *sa,
	struct netfe_stream *data);

struct netfe_stream *
fwd_tbl_lkp(struct netfe_lcore *fe, uint16_t family, const struct sockaddr *sa);

int
fwd_tbl_init(struct netfe_lcore *fe, uint16_t family, uint32_t lcore);

#endif /* __FWDTBL_H__ */
