#pragma once

#include <libavutil/fifo.h>
#include <libavformat/url.h>
#include <libavformat/os_support.h>
#include <libavformat/ip.h>

#define UDP_TX_BUF_SIZE 32768
#define UDP_RX_BUF_SIZE 393216
#define UDP_MAX_PKT_SIZE 65536
#define UDP_HEADER_SIZE 8

typedef struct UDPTldkContext {
    const AVClass *class;
    int udp_fd;
    int ttl;
    int buffer_size;
    int pkt_size;
    int is_multicast;
    int is_broadcast;
    int local_port;
    int reuse_socket;
    int overrun_nonfatal;
    struct sockaddr_storage dest_addr;
    int dest_addr_len;
    int is_connected;

    /* TLDK FE stream */
    struct netfe_stream *tldk_udp_stream;
    struct netfe_sprm *tldk_stream_prm;

    /* Circular Buffer variables for use in UDP receive code */
    int circular_buffer_size;
    AVFifoBuffer *fifo;
    int circular_buffer_error;
    int64_t bitrate; /* number of bits to send per second */
    int64_t burst_bits;
    int close_req;
#if HAVE_PTHREAD_CANCEL
    pthread_t circular_buffer_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int thread_started;
#endif
    uint8_t tmp[UDP_MAX_PKT_SIZE+4];
    int remaining_in_dg;
    char *localaddr;
    int timeout;
    struct sockaddr_storage local_addr_storage;
    char *sources;
    char *block;
    IPSourceFilters filters;
} UDPTldkContext;

extern const URLProtocol tldk_udp_protocol;
