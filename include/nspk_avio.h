#pragma once

#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavformat/url.h>
#include <libavformat/avio_internal.h>
#include <libavutil/avutil.h>
#include <nspk_rtp_lcore.h>
#include <nspk_av_udp.h>
#include <nspk_av_rtp.h>

int nspk_ffurl_open_whitelist(URLContext **puc, const char *filename, int flags,
                             const AVIOInterruptCB *int_cb, AVDictionary **options,
                             const char *whitelist, const char* blacklist,
                             URLContext *parent, const URLProtocol *tldk_protocol);

int nspk_avio_open(struct nspk_rtp_session_ctx_t *rtp_sess, AVIOContext **s, const char *filename, int flags);
