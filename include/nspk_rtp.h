#pragma once

/**
 * \brief Source file or device from which RTP session will extract media packets for RTP tranmission.
*/
struct nspk_rtp_source_t {
    enum {
        FILE_SRC, // media file
        DEV_SRC,  // mic
    } src_type;
	char src_name[FILENAME_MAX]; // Filename or device name
};

struct nspk_rtp_session_t {
    int session_id;
    struct nspk_rtp_source_t src;
    struct lcore_prm *lcore_prm;
    struct netfe_stream *fe_stream;
};

int
nspk_lcore_main_rtp(void *arg);
