#include <nspk.h>
#include <tldk_utils/udp.h>

void print_stream_addresses(struct netfe_sprm *sprm)
{
    struct sockaddr_in *laddr = (struct sockaddr_in*)&sprm->local_addr;
    struct sockaddr_in *raddr = (struct sockaddr_in*)&sprm->remote_addr;
    char ip_addr[INET6_ADDRSTRLEN];

    if (!sprm)
        return;

    av_log(NULL, AV_LOG_DEBUG, "%s: (AF=%d)Local address: %s:%u\n", __func__,
           laddr->sin_family,
           inet_ntop(AF_INET, (void*)&laddr->sin_addr, ip_addr, INET_ADDRSTRLEN) ? ip_addr : "",
           ntohs(laddr->sin_port));
    av_log(NULL, AV_LOG_DEBUG, "%s: (AF=%d)Remote address: %s:%u\n", __func__,
           raddr->sin_family,
           inet_ntop(AF_INET, (void*)&raddr->sin_addr, ip_addr, INET_ADDRSTRLEN) ? ip_addr : "",
           ntohs(raddr->sin_port));
}

static int nspk_udp_av_to_tldk(UDPTldkContext *udp_ctx, struct netfe_sprm *sprm)
{
    if (!udp_ctx || !sprm)
        return -EINVAL;

    sprm->local_addr = udp_ctx->local_addr_storage;
    sprm->remote_addr = udp_ctx->dest_addr;

    print_stream_addresses(sprm);

    return 0;
}

// TODO:
// This function should create a new TLDK stream and add it to
// stream list at `g_stream_list`.
int nspk_tldk_udp_stream_new(UDPTldkContext *udp_ctx)
{
    struct netfe_lcore *fe = RTE_PER_LCORE(_fe);
    struct lcore_prm *lcore_prm = g_rtp_sess->lcore_prm;

    if (!udp_ctx)
        return -EINVAL;

    // Copy UDP connection info from UDPTldkContext to TLDK FE stream.
    if (fe->use.num >= lcore_prm->fe.max_streams) {
        av_log(NULL, AV_LOG_ERROR, "%s: Number of streams has reached its max: %u/%u\n", __func__,
               fe->use.num, lcore_prm->fe.max_streams);
        return -ENOMEM;
    }
    udp_ctx->tldk_stream_prm = &lcore_prm->fe.stream[fe->use.num].sprm;
    nspk_udp_av_to_tldk(udp_ctx, udp_ctx->tldk_stream_prm);

    av_log(NULL, AV_LOG_DEBUG, "%s: Calling netfe_stream_open_udp\n", __func__);
    udp_ctx->tldk_udp_stream = netfe_lcore_init_udp(&lcore_prm->fe);
    if (udp_ctx->tldk_udp_stream == NULL) {
        av_log(NULL, AV_LOG_FATAL, "%s: netfe_lcore_init_udp failed\n", __func__);
        return -rte_errno;
    }

    return 0;
}

int nspk_tldk_udp_stream_send(UDPTldkContext *udp_ctx, void *data, int dlen)
{
    static const uint32_t FLUSH_THRESHOLD = 128;
    int ret = 0;

    ret = pkt_buf_fill_data(rte_lcore_id(), &udp_ctx->tldk_udp_stream->pbuf, data, dlen);
    if (ret < 0) {
        av_log(NULL, AV_LOG_DEBUG, "%s: pkt_buf_fill_data failed, ret=%d\n", __func__, ret);
        return ret;
    }

    // Flush
    if (udp_ctx->tldk_udp_stream->pbuf.num >= FLUSH_THRESHOLD) {
        // TODO: Implement return values for these function.
        netfe_tx_process_udp(rte_lcore_id(), udp_ctx->tldk_udp_stream);
	    netbe_lcore();
    }

    return ret;
}

int nspk_tldk_udp_stream_recv(UDPTldkContext *udp_ctx, void *data, int *dlen)
{
    return 0;    
}

int nspk_tldk_udp_stream_delete(UDPTldkContext *udp_ctx)
{
    return 0;
}