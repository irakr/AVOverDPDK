/*
 * Copyright (c) 2016-2017  Intel Corporation.
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

#include <nspk.h>
#include <tldk_utils/parse.h>
#include <tldk_utils/port.h>
#include <tldk_utils/lcore.h>
#include <tldk_utils/tcp.h>
#include <tldk_utils/udp.h>

volatile int force_quit;

RTE_DEFINE_PER_LCORE(struct netbe_lcore *, _be) = NULL;
RTE_DEFINE_PER_LCORE(struct netfe_lcore *, _fe) = NULL;

struct netbe_cfg becfg = {.mpool_buf_num=MPOOL_NB_BUF};
struct rte_mempool *mpool[RTE_MAX_NUMA_NODES + 1];
struct rte_mempool *frag_mpool[RTE_MAX_NUMA_NODES + 1];
char proto_name[3][10] = {"udp", "tcp", ""};

const struct rte_eth_conf port_conf_default;

struct tx_content tx_content = {
	.sz = 0,
	.data = NULL,
};

/* function pointers */
TLE_RX_BULK_FUNCTYPE tle_rx_bulk;
TLE_TX_BULK_FUNCTYPE tle_tx_bulk;
TLE_STREAM_RECV_FUNCTYPE tle_stream_recv;
TLE_STREAM_CLOSE_FUNCTYPE tle_stream_close;

LCORE_MAIN_FUNCTYPE lcore_main;

int verbose = VERBOSE_NONE;

static void
netbe_lcore_fini(struct netbe_cfg *cfg)
{
	uint32_t i;

	for (i = 0; i != cfg->cpu_num; i++) {
		tle_ctx_destroy(cfg->cpu[i].ctx);
		rte_ip_frag_table_destroy(cfg->cpu[i].ftbl);
		rte_lpm_free(cfg->cpu[i].lpm4);
		rte_lpm6_free(cfg->cpu[i].lpm6);

		rte_free(cfg->cpu[i].prtq);
		cfg->cpu[i].prtq_num = 0;
	}

	rte_free(cfg->cpu);
	cfg->cpu_num = 0;
	for (i = 0; i != cfg->prt_num; i++) {
		rte_free(cfg->prt[i].lcore_id);
		cfg->prt[i].nb_lcore = 0;
	}
	rte_free(cfg->prt);
	cfg->prt_num = 0;
}

static int
netbe_dest_init(const char *fname, struct netbe_cfg *cfg)
{
	int32_t rc;
	uint32_t f, i, p;
	uint32_t k, l, cnt;
	struct netbe_lcore *lc;
	struct netbe_dest_prm prm;

	rc = netbe_parse_dest(fname, &prm);
	if (rc != 0)
		return rc;

	rc = 0;
	for (i = 0; i != prm.nb_dest; i++) {

		p = prm.dest[i].port;
		f = prm.dest[i].family;

		cnt = 0;
		for (k = 0; k != cfg->cpu_num; k++) {
			lc = cfg->cpu + k;
			for (l = 0; l != lc->prtq_num; l++)
				if (lc->prtq[l].port.id == p) {
					rc = netbe_add_dest(lc, l, f,
							prm.dest + i, 1);
					if (rc != 0) {
						RTE_LOG(ERR, USER1,
							"%s(lc=%u, family=%u) "
							"could not add "
							"destinations(%u)\n",
							__func__, lc->id, f, i);
						return -ENOSPC;
					}
					cnt++;
				}
		}

		if (cnt == 0) {
			RTE_LOG(ERR, USER1, "%s(%s) error at line %u: "
				"port %u not managed by any lcore;\n",
				__func__, fname, prm.dest[i].line, p);
			break;
		}
	}

	free(prm.dest);
	return rc;
}

static void
func_ptrs_init(uint32_t proto) {
	if (proto == TLE_PROTO_TCP) {
		tle_rx_bulk = tle_tcp_rx_bulk;
		tle_tx_bulk = tle_tcp_tx_bulk;
		tle_stream_recv = tle_tcp_stream_recv;
		tle_stream_close = tle_tcp_stream_close;

		// lcore_main = lcore_main_tcp;

	} else {
		tle_rx_bulk = tle_udp_rx_bulk;
		tle_tx_bulk = tle_udp_tx_bulk;
		tle_stream_recv = tle_udp_stream_recv;
		tle_stream_close = tle_udp_stream_close;

		// lcore_main = lcore_main_udp;
	}
}

int
main(int argc, char *argv[])
{
	int32_t rc;
	uint32_t i;
	struct tle_ctx_param ctx_prm;
	struct netfe_lcore_prm feprm;
	struct rte_eth_stats stats;
	char fecfg_fname[PATH_MAX + 1];
	char becfg_fname[PATH_MAX + 1];
	struct lcore_prm prm[RTE_MAX_LCORE];
	struct rte_eth_dev_info dev_info;

	fecfg_fname[0] = 0;
	becfg_fname[0] = 0;
	memset(prm, 0, sizeof(prm));

	rc = rte_eal_init(argc, argv);
	if (rc < 0)
		rte_exit(EXIT_FAILURE,
			"%s: rte_eal_init failed with error code: %d\n",
			__func__, rc);

	memset(&ctx_prm, 0, sizeof(ctx_prm));
	ctx_prm.timewait = TLE_TCP_TIMEWAIT_DEFAULT;

	signal(SIGINT, sig_handle);

	argc -= rc;
	argv += rc;

	rc = parse_app_options(argc, argv, &becfg, &ctx_prm,
		fecfg_fname, becfg_fname);
	if (rc != 0)
		rte_exit(EXIT_FAILURE,
			"%s: parse_app_options failed with error code: %d\n",
			__func__, rc);

	/* init all the function pointer */
	func_ptrs_init(becfg.proto);

	rc = netbe_port_init(&becfg);
	if (rc != 0)
		rte_exit(EXIT_FAILURE,
			"%s: netbe_port_init failed with error code: %d\n",
			__func__, rc);

	rc = netbe_lcore_init(&becfg, &ctx_prm);
	if (rc != 0)
		sig_handle(SIGQUIT);

	rc = netbe_dest_init(becfg_fname, &becfg);
	if (rc != 0)
		sig_handle(SIGQUIT);

	for (i = 0; i != becfg.prt_num && rc == 0; i++) {
		RTE_LOG(NOTICE, USER1, "%s: starting port %u\n",
			__func__, becfg.prt[i].id);
		rc = rte_eth_dev_start(becfg.prt[i].id);
		if (rc != 0) {
			RTE_LOG(ERR, USER1,
				"%s: rte_eth_dev_start(%u) returned "
				"error code: %d\n",
				__func__, becfg.prt[i].id, rc);
			sig_handle(SIGQUIT);
		}
		rte_eth_dev_info_get(becfg.prt[i].id, &dev_info);
		rc = update_rss_reta(&becfg.prt[i], &dev_info);
		if (rc != 0)
			sig_handle(SIGQUIT);
	}

	feprm.max_streams = ctx_prm.max_streams * becfg.cpu_num;

	rc = (rc != 0) ? rc : netfe_parse_cfg(fecfg_fname, &feprm);
	if (rc != 0)
		sig_handle(SIGQUIT);

	for (i = 0; rc == 0 && i != becfg.cpu_num; i++)
		prm[becfg.cpu[i].id].be.lc = becfg.cpu + i;

	rc = (rc != 0) ? rc : netfe_lcore_fill(prm, &feprm);
	if (rc != 0)
		sig_handle(SIGQUIT);
	
	int rc1;
	/* launch all slave lcores. */
	RTE_LCORE_FOREACH_WORKER(i) {
		if (prm[i].be.lc != NULL || prm[i].fe.max_streams != 0) {
			struct nspk_rtp_session_ctx_t *my_sess = calloc(1, sizeof(*my_sess));
			my_sess->session_id = 0;
			strncpy(my_sess->src_url, "/home/procfser/Videos/Movie43.mp4",
					sizeof(my_sess->src_url));
			strncpy(my_sess->dst_url, "rtp://192.168.1.9:5000", sizeof(my_sess->dst_url));
			my_sess->lcore_prm = prm + i;
			rc1 = rte_eal_remote_launch(nspk_lcore_main_rtp, my_sess, i);
			if (rc1 == 0) {
				printf("RTP thread started at slave LCore %u\n", i);
				break;
			}
			printf("Failed to launch RTP thread at core %u. Trying next...", i);
		}
	}

	/* launch master lcore. */
	i = rte_get_main_lcore();
	if (prm[i].be.lc != NULL || prm[i].fe.max_streams != 0) {
		printf("Launching master lcore thread.\n");
		rc1 = lcore_main_control(prm + i);
	}
	printf("Master lcore initialized, rc1=%d.\n", rc1);

	rte_eal_mp_wait_lcore();

	for (i = 0; i != becfg.prt_num; i++) {
		RTE_LOG(NOTICE, USER1, "%s: stoping port %u\n",
			__func__, becfg.prt[i].id);
		rte_eth_stats_get(becfg.prt[i].id, &stats);
		RTE_LOG(NOTICE, USER1, "port %u stats={\n"
			"ipackets=%" PRIu64 ";"
			"ibytes=%" PRIu64 ";"
			"ierrors=%" PRIu64 ";"
			"imissed=%" PRIu64 ";\n"
			"opackets=%" PRIu64 ";"
			"obytes=%" PRIu64 ";"
			"oerrors=%" PRIu64 ";\n"
			"}\n",
			becfg.prt[i].id,
			stats.ipackets,
			stats.ibytes,
			stats.ierrors,
			stats.imissed,
			stats.opackets,
			stats.obytes,
			stats.oerrors);
		rte_eth_dev_stop(becfg.prt[i].id);
	}

	netbe_lcore_fini(&becfg);

	return 0;
}
