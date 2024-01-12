#ifndef LCORE_H_
#define LCORE_H_

#include <nspk.h>
#include <tldk_utils/dpdk_legacy.h>

/*
 * IPv4 destination lookup callback.
 */
int
lpm4_dst_lookup(void *data, __rte_unused uint64_t sdata,
	const struct in_addr *addr, struct tle_dest *res);

/*
 * IPv6 destination lookup callback.
 */
int
lpm6_dst_lookup(void *data, __rte_unused uint64_t sdata,
	const struct in6_addr *addr, struct tle_dest *res);

int
lcore_lpm_init(struct netbe_lcore *lc);

/*
 * Helper functions, finds BE by given local and remote addresses.
 */
int
netbe_find4(const struct in_addr *laddr, const uint16_t lport,
	const struct in_addr *raddr, const uint32_t belc);

int
netbe_find6(const struct in6_addr *laddr, uint16_t lport,
	const struct in6_addr *raddr, uint32_t belc);

int
create_context(struct netbe_lcore *lc, const struct tle_ctx_param *ctx_prm);

/*
 * BE lcore setup routine.
 */
int
lcore_init(struct netbe_lcore *lc, const struct tle_ctx_param *ctx_prm,
	const uint32_t prtqid, const uint16_t *bl_ports, uint32_t nb_bl_ports);

uint16_t
create_blocklist(const struct netbe_port *beprt, uint16_t *bl_ports,
	uint32_t q);

int
netbe_lcore_init(struct netbe_cfg *cfg, const struct tle_ctx_param *ctx_prm);

int
netfe_lcore_cmp(const void *s1, const void *s2);

int
netbe_find(const struct sockaddr_storage *la,
	const struct sockaddr_storage *ra,
	uint32_t belc);

int
netfe_sprm_flll_be(struct netfe_sprm *sp, uint32_t line, uint32_t belc);

/* start front-end processing. */
int
netfe_lcore_fill(struct lcore_prm prm[RTE_MAX_LCORE],
	struct netfe_lcore_prm *lprm);

#endif /* LCORE_H_ */
