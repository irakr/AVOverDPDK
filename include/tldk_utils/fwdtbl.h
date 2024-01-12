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
