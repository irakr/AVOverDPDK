#pragma once

#include <time.h>
#include <sched.h>
#include <rte_os.h>
#include <rte_arp.h>
#include <rte_log.h>
#include <rte_per_lcore.h>
#include <tldk_utils/netbe.h>
#include <tldk_utils/common.h>
#include <nspk_audio.h>
#include <nspk_control_lcore.h>
#include <nspk_rtp_lcore.h>
#include <nspk_avio.h>
#include <nspk_tldk.h>

#define	MAX_RULES	0x100
#define	MAX_TBL8	0x800

#define	RX_RING_SIZE	0x400
#define	TX_RING_SIZE	0x800

#define	MPOOL_CACHE_SIZE	0x100
#define	MPOOL_NB_BUF		0x20000

#define FRAG_MBUF_BUF_SIZE	(RTE_PKTMBUF_HEADROOM + TLE_DST_MAX_HDR)
#define FRAG_TTL		MS_PER_S
#define	FRAG_TBL_BUCKET_ENTRIES	16

#define	FIRST_PORT	0x8000

#define RX_CSUM_OFFLOAD	(DEV_RX_OFFLOAD_IPV4_CKSUM | DEV_RX_OFFLOAD_UDP_CKSUM)
#define TX_CSUM_OFFLOAD	(DEV_TX_OFFLOAD_IPV4_CKSUM | DEV_TX_OFFLOAD_UDP_CKSUM)

RTE_DECLARE_PER_LCORE(struct netbe_lcore *, _be);
RTE_DECLARE_PER_LCORE(struct netfe_lcore *, _fe);

extern volatile int force_quit;

extern struct netbe_cfg becfg;
extern struct rte_mempool *mpool[RTE_MAX_NUMA_NODES + 1];
extern struct rte_mempool *frag_mpool[RTE_MAX_NUMA_NODES + 1];
extern char proto_name[3][10];

extern const struct rte_eth_conf port_conf_default;

extern struct tx_content tx_content;

/* function pointers */
extern TLE_RX_BULK_FUNCTYPE tle_rx_bulk;
extern TLE_TX_BULK_FUNCTYPE tle_tx_bulk;
extern TLE_STREAM_RECV_FUNCTYPE tle_stream_recv;
extern TLE_STREAM_CLOSE_FUNCTYPE tle_stream_close;

extern LCORE_MAIN_FUNCTYPE lcore_main;

extern struct nspk_rtp_session_ctx_t *g_rtp_sess;

/**
 * Location to be modified to create the IPv4 hash key which helps
 * to distribute packets based on the destination TCP/UDP port.
 */
#define RSS_HASH_KEY_DEST_PORT_LOC_IPV4 15

/**
 * Location to be modified to create the IPv6 hash key which helps
 * to distribute packets based on the destination TCP/UDP port.
 */
#define RSS_HASH_KEY_DEST_PORT_LOC_IPV6 39

/**
 * Size of the rte_eth_rss_reta_entry64 array to update through
 * rte_eth_dev_rss_reta_update.
 */
#define RSS_RETA_CONF_ARRAY_SIZE (ETH_RSS_RETA_SIZE_512/RTE_RETA_GROUP_SIZE)
