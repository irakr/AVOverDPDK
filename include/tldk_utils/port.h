#ifndef PORT_H_
#define PORT_H_

#include <nspk.h>

void
prepare_hash_key(struct netbe_port *uprt, uint8_t key_size, uint16_t family);

int
update_rss_conf(struct netbe_port *uprt,
	const struct rte_eth_dev_info *dev_info,
	struct rte_eth_conf *port_conf, uint32_t proto);

uint32_t
qidx_from_hash_index(uint32_t hash, uint32_t align_nb_q);

int
update_rss_reta(struct netbe_port *uprt,
	const struct rte_eth_dev_info *dev_info);

/*
 * Initilise DPDK port.
 * In current version, multi-queue per port is used.
 */
int
port_init(struct netbe_port *uprt, uint32_t proto);

int
queue_init(struct netbe_port *uprt, struct rte_mempool *mp);

/*
 * Check that lcore is enabled, not master, and not in use already.
 */
int
check_lcore(uint32_t lc);

void
log_netbe_prt(const struct netbe_port *uprt);

void
log_netbe_cfg(const struct netbe_cfg *ucfg);

int
pool_init(uint32_t sid, uint32_t mpool_buf_num);

int
frag_pool_init(uint32_t sid, uint32_t mpool_buf_num);

struct netbe_lcore *
find_initilized_lcore(struct netbe_cfg *cfg, uint32_t lc_num);

/*
 * Setup all enabled ports.
 */
int
netbe_port_init(struct netbe_cfg *cfg);

#endif /* PORT_H_ */
