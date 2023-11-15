/**
 * NSPK DPDK based RTP core lib.
 */

#include <nspk.h>
#include <tldk_utils/udp.h>
#include <tldk_utils/parse.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/avcodec.h>

#define INBUF_SIZE 4096

static int
nspk_rtp_generate_rtp(struct nspk_rtp_session_t *rtp_sess)
{
	int num_pkts = 0;

	// TODO: Implement this function:
	// - Read media file at rtp_sess->src through ffmpeg lib.
	// - Parse the file and generate H264 RTP payload.
	// - Append each payload in the mbuf at rtp_sess->fe_stream->pbuf.
	pkt_buf_fill(rtp_sess->lcore_prm->be.lc->id, &rtp_sess->fe_stream->pbuf, 64);
	num_pkts = 64;
	return num_pkts;
}

// TODO
// This thread is run by the master lcore and is currently dedicated to sending RTP stream only.
// Later we will change the design such that this thread will dedicate to the session control thread,
// that is when this function will be moved elsewhere.
int
nspk_lcore_main_rtp(void *arg)
{
    int i;
	int32_t rc = 0;
	uint32_t lcore;
	struct nspk_rtp_session_t *rtp_sess = (struct nspk_rtp_session_t*)arg;
	struct lcore_prm *prm;
    struct netfe_stream *fs;
    struct netfe_sprm *sprm;
    struct tle_udp_stream_param uprm;
    struct sockaddr_in remote_addr;
    char ip_addr[INET_ADDRSTRLEN];
    struct netfe_lcore *fe;

	prm = rtp_sess->lcore_prm;
	lcore = rte_lcore_id();

	RTE_LOG(NOTICE, USER1, "%s(lcore=%u) start\n",
		__func__, lcore);

	/* lcore FE init. */
	if (prm->fe.max_streams != 0) {
		fs = netfe_lcore_init_udp(&prm->fe);
		if (fs == NULL) {
			fprintf(stderr, "Error: failed to init UDP stream.\n");
			return EFAULT;
		}
		printf("FE stream laddr=%s, raddr=%s\n",
            format_addr((struct sockaddr_storage*)&fs->laddr, ip_addr, sizeof(fs->laddr)),
			format_addr((struct sockaddr_storage*)&fs->raddr, ip_addr, sizeof(fs->raddr)));
		rtp_sess->fe_stream = fs;
	}

	/* lcore BE init. */
	if (prm->be.lc != NULL)
		rc = netbe_lcore_setup(prm->be.lc);

	if (rc != 0)
		sig_handle(SIGQUIT);

    // We will use only one FE stream for now.
    // sprm = &prm->fe.stream[0].sprm;
    // fs = netfe_stream_open_udp(fe, sprm, lcore, prm->fe.stream[0].op,
	// 	sprm->bidx);
	// if (fs == NULL) {
	// 	rc = -rte_errno;
	// 	return rc;
	// }

    // TODO: Find a stream using its remote address.
    // inet_pton(AF_INET, "10.0.0.10", &(remote_addr.sin_addr));
    // for (i = 0; i != fe->use.num; i++) {
	// 	fs = netfe_get_stream(&fe->use);
	// 	tle_udp_stream_get_param(fs->s, &uprm);
	// 	netfe_stream_dump(fs, &uprm.local_addr, &uprm.remote_addr);
    //     if (netfe_addr_eq(&uprm.remote_addr, &remote_addr, AF_INET)) {
    //         printf("Matched network address %s\n",
    //             format_addr((struct sockaddr_storage*)&remote_addr, remote_ip, sizeof(remote_ip)));
    //         break;
    //     }
	// 	// netfe_stream_close(fe, fs);
	// }

	// RTE_PER_LCORE(_fe):
	// Generates the global variable name "per_lcore__fe" which is initialized by netfe_lcore_init_udp().
	fe = RTE_PER_LCORE(_fe);
	if (fe == NULL)
		return EINVAL;

	RTE_LOG(NOTICE, USER1, "%s (lcore=%u) Starting RTP session...\n",
		__func__, lcore);
	printf("Session ID: %d, RTP source %s: %s\n",
		rtp_sess->session_id, (rtp_sess->src.src_type == FILE_SRC) ? "file" : "device", rtp_sess->src.src_name);
	while (force_quit == 0) {
		// TODO: Understand the event API.
        // tle_event_raise(fs->txev);
		// fs->stat.txev[TLE_SEV_UP]++;

        // TODO:
        // Build a burst of RTP packets from the opened media file.
        // Write a test RTP packet into the UDP TX queue.
		int num_pkt;
		if ((num_pkt = nspk_rtp_generate_rtp(rtp_sess)) <= 0) {
			fprintf(stderr, "Error: No RTP packets generated to mbuf.\n");
			continue;
		}
		printf("Generated %d RTP packets to mbuf.\n", num_pkt);

		// FE send
		netfe_tx_process_udp(lcore, fs);

		// BE send
		netbe_lcore();

		rte_delay_ms(1000);
	}

	RTE_LOG(NOTICE, USER1, "%s(lcore=%u) finish\n",
		__func__, lcore);

	netfe_lcore_fini_udp();
	netbe_lcore_clear();

	return rc;
}
