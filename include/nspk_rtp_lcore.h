#pragma once

#include <libavfilter/avfilter.h>
#include <nspk_avio.h>

struct filtering_ctx_t
{
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
    AVPacket *enc_pkt;
    AVFrame *filtered_frame;
};

struct stream_ctx_t
{
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;
    AVFrame *dec_frame;
};

/**
 * \brief Context for libav elements.
 */
struct nspk_av_ctx_t
{
    AVFormatContext *ifmt_ctx;
    AVFormatContext *ofmt_ctx;
    struct filtering_ctx_t *filter_ctx;
    struct stream_ctx_t *stream_ctx;
};

/**
 * \brief Context for NSPK RTP session.
 */
struct nspk_rtp_session_ctx_t
{
    int session_id;
    struct lcore_prm *lcore_prm;
    struct netfe_stream *fe_stream;
    struct nspk_av_ctx_t *av_ctx;

    /**
     * Source and destination file/network URLs
     * These must be set and passed as input to the RTP lcore thread.
     */
    char src_url[FILENAME_MAX], dst_url[FILENAME_MAX];
};

/**
 * \brief Open input and output media files, filters, and initializes
 * the libav context.
 */
int nspk_media_init(struct nspk_rtp_session_ctx_t *rtp_sess);

/**
 * \brief Starts the transcoder loop.
 */
int nspk_media_start(struct nspk_rtp_session_ctx_t *rtp_sess);

/**
 * \brief DPDK LCore thread for processing RTP sessions.
 */
int nspk_lcore_main_rtp(void *arg);
