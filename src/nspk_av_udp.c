#define _DEFAULT_SOURCE
#define _BSD_SOURCE     /* Needed for using struct ip_mreq with recent glibc */

#include <libavformat/avformat.h>
#include <libavformat/avio_internal.h>
#include <libavutil/avassert.h>
#include <libavutil/parseutils.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/avstring.h>
#include <libavutil/opt.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#include <libavformat/internal.h>
#include <libavformat/network.h>
#include <arpa/inet.h>

#if HAVE_PTHREAD_CANCEL
#include <libavutil/thread.h>
#endif

#include <nspk.h>
#include <tldk_utils/udp.h>

#ifndef IPV6_ADD_MEMBERSHIP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif

#define OFFSET(x) offsetof(UDPTldkContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "buffer_size",    "System data size (in bytes)",                     OFFSET(buffer_size),    AV_OPT_TYPE_INT,    { .i64 = -1 },    -1, INT_MAX, .flags = D|E },
    { "bitrate",        "Bits to send per second",                         OFFSET(bitrate),        AV_OPT_TYPE_INT64,  { .i64 = 0  },     0, INT64_MAX, .flags = E },
    { "burst_bits",     "Max length of bursts in bits (when using bitrate)", OFFSET(burst_bits),   AV_OPT_TYPE_INT64,  { .i64 = 0  },     0, INT64_MAX, .flags = E },
    { "localport",      "Local port",                                      OFFSET(local_port),     AV_OPT_TYPE_INT,    { .i64 = -1 },    -1, INT_MAX, D|E },
    { "local_port",     "Local port",                                      OFFSET(local_port),     AV_OPT_TYPE_INT,    { .i64 = -1 },    -1, INT_MAX, .flags = D|E },
    { "localaddr",      "Local address",                                   OFFSET(localaddr),      AV_OPT_TYPE_STRING, { .str = NULL },               .flags = D|E },
    { "pkt_size",       "Maximum UDP packet size",                         OFFSET(pkt_size),       AV_OPT_TYPE_INT,    { .i64 = 1472 },  -1, INT_MAX, .flags = D|E },
    { "reuse",          "explicitly allow reusing UDP sockets",            OFFSET(reuse_socket),   AV_OPT_TYPE_BOOL,   { .i64 = -1 },    -1, 1,       D|E },
    { "reuse_socket",   "explicitly allow reusing UDP sockets",            OFFSET(reuse_socket),   AV_OPT_TYPE_BOOL,   { .i64 = -1 },    -1, 1,       .flags = D|E },
    { "broadcast", "explicitly allow or disallow broadcast destination",   OFFSET(is_broadcast),   AV_OPT_TYPE_BOOL,   { .i64 = 0  },     0, 1,       E },
    { "ttl",            "Time to live (multicast only)",                   OFFSET(ttl),            AV_OPT_TYPE_INT,    { .i64 = 16 },     0, INT_MAX, E },
    { "connect",        "set if connect() should be called on socket",     OFFSET(is_connected),   AV_OPT_TYPE_BOOL,   { .i64 =  0 },     0, 1,       .flags = D|E },
    { "fifo_size",      "set the UDP receiving circular buffer size, expressed as a number of packets with size of 188 bytes", OFFSET(circular_buffer_size), AV_OPT_TYPE_INT, {.i64 = 7*4096}, 0, INT_MAX, D },
    { "overrun_nonfatal", "survive in case of UDP receiving circular buffer overrun", OFFSET(overrun_nonfatal), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1,    D },
    { "timeout",        "set raise error timeout, in microseconds (only in read mode)",OFFSET(timeout),         AV_OPT_TYPE_INT,  {.i64 = 0}, 0, INT_MAX, D },
    { "sources",        "Source list",                                     OFFSET(sources),        AV_OPT_TYPE_STRING, { .str = NULL },               .flags = D|E },
    { "block",          "Block list",                                      OFFSET(block),          AV_OPT_TYPE_STRING, { .str = NULL },               .flags = D|E },
    { NULL }
};

static const AVClass udp_class = {
    .class_name = "tldk_udp",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int udp_set_multicast_ttl(int sockfd, int mcastTTL,
                                 struct sockaddr *addr)
{
#ifdef IP_MULTICAST_TTL
    if (addr->sa_family == AF_INET) {
        if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &mcastTTL, sizeof(mcastTTL)) < 0) {
            ff_log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IP_MULTICAST_TTL)");
            return ff_neterrno();
        }
    }
#endif
#if defined(IPPROTO_IPV6) && defined(IPV6_MULTICAST_HOPS)
    if (addr->sa_family == AF_INET6) {
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &mcastTTL, sizeof(mcastTTL)) < 0) {
            ff_log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IPV6_MULTICAST_HOPS)");
            return ff_neterrno();
        }
    }
#endif
    return 0;
}

static int udp_join_multicast_group(int sockfd, struct sockaddr *addr,struct sockaddr *local_addr)
{
#ifdef IP_ADD_MEMBERSHIP
    if (addr->sa_family == AF_INET) {
        struct ip_mreq mreq;

        mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        if (local_addr)
            mreq.imr_interface= ((struct sockaddr_in *)local_addr)->sin_addr;
        else
            mreq.imr_interface.s_addr = INADDR_ANY;
        if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
            ff_log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IP_ADD_MEMBERSHIP)");
            return ff_neterrno();
        }
    }
#endif
#if HAVE_STRUCT_IPV6_MREQ && defined(IPPROTO_IPV6)
    if (addr->sa_family == AF_INET6) {
        struct ipv6_mreq mreq6;

        memcpy(&mreq6.ipv6mr_multiaddr, &(((struct sockaddr_in6 *)addr)->sin6_addr), sizeof(struct in6_addr));
        //TODO: Interface index should be looked up from local_addr
        mreq6.ipv6mr_interface = 0;
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
            ff_log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IPV6_ADD_MEMBERSHIP)");
            return ff_neterrno();
        }
    }
#endif
    return 0;
}

static int udp_leave_multicast_group(int sockfd, struct sockaddr *addr,struct sockaddr *local_addr)
{
#ifdef IP_DROP_MEMBERSHIP
    if (addr->sa_family == AF_INET) {
        struct ip_mreq mreq;

        mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        if (local_addr)
            mreq.imr_interface = ((struct sockaddr_in *)local_addr)->sin_addr;
        else
            mreq.imr_interface.s_addr = INADDR_ANY;
        if (setsockopt(sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
            ff_log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IP_DROP_MEMBERSHIP)");
            return -1;
        }
    }
#endif
#if HAVE_STRUCT_IPV6_MREQ && defined(IPPROTO_IPV6)
    if (addr->sa_family == AF_INET6) {
        struct ipv6_mreq mreq6;

        memcpy(&mreq6.ipv6mr_multiaddr, &(((struct sockaddr_in6 *)addr)->sin6_addr), sizeof(struct in6_addr));
        //TODO: Interface index should be looked up from local_addr
        mreq6.ipv6mr_interface = 0;
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
            ff_log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IPV6_DROP_MEMBERSHIP)");
            return -1;
        }
    }
#endif
    return 0;
}

static int udp_set_multicast_sources(URLContext *h,
                                     int sockfd, struct sockaddr *addr,
                                     int addr_len, struct sockaddr_storage *local_addr,
                                     struct sockaddr_storage *sources,
                                     int nb_sources, int include)
{
    int i;
    if (addr->sa_family != AF_INET) {
#if HAVE_STRUCT_GROUP_SOURCE_REQ && defined(MCAST_BLOCK_SOURCE)
        /* For IPv4 prefer the old approach, as that alone works reliably on
         * Windows and it also supports supplying the interface based on its
         * address. */
        int i;
        for (i = 0; i < nb_sources; i++) {
            struct group_source_req mreqs;
            int level = addr->sa_family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6;

            //TODO: Interface index should be looked up from local_addr
            mreqs.gsr_interface = 0;
            memcpy(&mreqs.gsr_group, addr, addr_len);
            memcpy(&mreqs.gsr_source, &sources[i], sizeof(*sources));

            if (setsockopt(sockfd, level,
                           include ? MCAST_JOIN_SOURCE_GROUP : MCAST_BLOCK_SOURCE,
                           (const void *)&mreqs, sizeof(mreqs)) < 0) {
                if (include)
                    ff_log_net_error(NULL, AV_LOG_ERROR, "setsockopt(MCAST_JOIN_SOURCE_GROUP)");
                else
                    ff_log_net_error(NULL, AV_LOG_ERROR, "setsockopt(MCAST_BLOCK_SOURCE)");
                return ff_neterrno();
            }
        }
        return 0;
#else
        av_log(h, AV_LOG_ERROR,
               "Setting multicast sources only supported for IPv4\n");
        return AVERROR(EINVAL);
#endif
    }
#if HAVE_STRUCT_IP_MREQ_SOURCE && defined(IP_BLOCK_SOURCE)
    for (i = 0; i < nb_sources; i++) {
        struct ip_mreq_source mreqs;
        if (sources[i].ss_family != AF_INET) {
            av_log(h, AV_LOG_ERROR, "Source/block address %d is of incorrect protocol family\n", i + 1);
            return AVERROR(EINVAL);
        }

        mreqs.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        if (local_addr)
            mreqs.imr_interface = ((struct sockaddr_in *)local_addr)->sin_addr;
        else
            mreqs.imr_interface.s_addr = INADDR_ANY;
        mreqs.imr_sourceaddr.s_addr = ((struct sockaddr_in *)&sources[i])->sin_addr.s_addr;

        if (setsockopt(sockfd, IPPROTO_IP,
                       include ? IP_ADD_SOURCE_MEMBERSHIP : IP_BLOCK_SOURCE,
                       (const void *)&mreqs, sizeof(mreqs)) < 0) {
            if (include)
                ff_log_net_error(h, AV_LOG_ERROR, "setsockopt(IP_ADD_SOURCE_MEMBERSHIP)");
            else
                ff_log_net_error(h, AV_LOG_ERROR, "setsockopt(IP_BLOCK_SOURCE)");
            return ff_neterrno();
        }
    }
#else
    return AVERROR(ENOSYS);
#endif
    return 0;
}
static int udp_set_url(URLContext *h,
                       struct sockaddr_storage *addr,
                       const char *hostname, int port)
{
    struct addrinfo *res0;
    int addr_len;

    res0 = ff_ip_resolve_host(h, hostname, port, SOCK_DGRAM, AF_UNSPEC, 0);
    if (!res0) return AVERROR(EIO);
    memcpy(addr, res0->ai_addr, res0->ai_addrlen);
    addr_len = res0->ai_addrlen;
    freeaddrinfo(res0);
    av_log(NULL, AV_LOG_DEBUG, "%s: successful.\n", __func__);
    return addr_len;
}

static int udp_socket_create(URLContext *h, struct sockaddr_storage *addr,
                             socklen_t *addr_len, const char *localaddr)
{
    UDPTldkContext *s = h->priv_data;
    int udp_fd = -1;
    struct addrinfo *res0, *res;
    int family = AF_UNSPEC;

    if (((struct sockaddr *) &s->dest_addr)->sa_family)
        family = ((struct sockaddr *) &s->dest_addr)->sa_family;
    res0 = ff_ip_resolve_host(h, (localaddr && localaddr[0]) ? localaddr : NULL,
                            s->local_port,
                            SOCK_DGRAM, family, AI_PASSIVE);
    if (!res0)
        goto fail;
    for (res = res0; res; res=res->ai_next) {
        udp_fd = ff_socket(res->ai_family, SOCK_DGRAM, 0);
        if (udp_fd != -1) break;
        ff_log_net_error(NULL, AV_LOG_ERROR, "socket");
    }

    if (udp_fd < 0)
        goto fail;

    memcpy(addr, res->ai_addr, res->ai_addrlen);
    *addr_len = res->ai_addrlen;

    freeaddrinfo(res0);

    return udp_fd;

 fail:
    if (udp_fd >= 0)
        closesocket(udp_fd);
    if(res0)
        freeaddrinfo(res0);
    return -1;
}

static int udp_port(struct sockaddr_storage *addr, int addr_len)
{
    char sbuf[sizeof(int)*3+1];
    int error;

    if ((error = getnameinfo((struct sockaddr *)addr, addr_len, NULL, 0,  sbuf, sizeof(sbuf), NI_NUMERICSERV)) != 0) {
        av_log(NULL, AV_LOG_ERROR, "getnameinfo: %s\n", gai_strerror(error));
        return -1;
    }

    return strtol(sbuf, NULL, 10);
}


/**
 * If no filename is given to av_open_input_file because you want to
 * get the local port first, then you must call this function to set
 * the remote server address.
 *
 * url syntax: udp://host:port[?option=val...]
 * option: 'ttl=n'       : set the ttl value (for multicast only)
 *         'localport=n' : set the local port
 *         'pkt_size=n'  : set max packet size
 *         'reuse=1'     : enable reusing the socket
 *         'overrun_nonfatal=1': survive in case of circular buffer overrun
 *
 * @param h media file context
 * @param uri of the remote server
 * @return zero if no error.
 */
int ff_udp_set_remote_url(URLContext *h, const char *uri)
{
    UDPTldkContext *s = h->priv_data;
    char hostname[256], buf[10];
    int port;
    const char *p;

    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port, NULL, 0, uri);

    /* set the destination address */
    s->dest_addr_len = udp_set_url(h, &s->dest_addr, hostname, port);
    if (s->dest_addr_len < 0) {
        return AVERROR(EIO);
    }
    char addr_buf[INET6_ADDRSTRLEN];
    struct sockaddr_in *raddr = (struct sockaddr_in*)&s->dest_addr;
    av_log(h, AV_LOG_DEBUG, "%s: dest_addr=%u,%u\n", __func__, ntohl(raddr->sin_addr.s_addr), ntohl(raddr->sin_port));
    s->is_multicast = ff_is_multicast_address((struct sockaddr*) &s->dest_addr);
    p = strchr(uri, '?');
    if (p) {
        if (av_find_info_tag(buf, sizeof(buf), "connect", p)) {
            int was_connected = s->is_connected;
            s->is_connected = strtol(buf, NULL, 10);
            if (s->is_connected && !was_connected) {
                if (connect(s->udp_fd, (struct sockaddr *) &s->dest_addr,
                            s->dest_addr_len)) {
                    s->is_connected = 0;
                    ff_log_net_error(h, AV_LOG_ERROR, "connect");
                    return AVERROR(EIO);
                }
            }
        }
    }

    return 0;
}

/**
 * Return the local port used by the UDP connection
 * @param h media file context
 * @return the local port number
 */
int ff_udp_get_local_port(URLContext *h)
{
    UDPTldkContext *s = h->priv_data;
    return s->local_port;
}

/**
 * Return the udp file handle for select() usage to wait for several RTP
 * streams at the same time.
 * @param h media file context
 */
static int udp_get_file_handle(URLContext *h)
{
    UDPTldkContext *s = h->priv_data;
    return s->udp_fd;
}

#if HAVE_PTHREAD_CANCEL
static void *circular_buffer_task_rx( void *_URLContext)
{
    URLContext *h = _URLContext;
    UDPTldkContext *s = h->priv_data;
    int old_cancelstate;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancelstate);
    pthread_mutex_lock(&s->mutex);
    if (ff_socket_nonblock(s->udp_fd, 0) < 0) {
        av_log(h, AV_LOG_ERROR, "Failed to set blocking mode");
        s->circular_buffer_error = AVERROR(EIO);
        goto end;
    }
    while(1) {
        int len;
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        pthread_mutex_unlock(&s->mutex);
        /* Blocking operations are always cancellation points;
           see "General Information" / "Thread Cancelation Overview"
           in Single Unix. */
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_cancelstate);
        len = recvfrom(s->udp_fd, s->tmp+4, sizeof(s->tmp)-4, 0, (struct sockaddr *)&addr, &addr_len);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancelstate);
        pthread_mutex_lock(&s->mutex);
        if (len < 0) {
            if (ff_neterrno() != AVERROR(EAGAIN) && ff_neterrno() != AVERROR(EINTR)) {
                s->circular_buffer_error = ff_neterrno();
                goto end;
            }
            continue;
        }
        if (ff_ip_check_source_lists(&addr, &s->filters))
            continue;
        AV_WL32(s->tmp, len);

        if(av_fifo_space(s->fifo) < len + 4) {
            /* No Space left */
            if (s->overrun_nonfatal) {
                av_log(h, AV_LOG_WARNING, "Circular buffer overrun. "
                        "Surviving due to overrun_nonfatal option\n");
                continue;
            } else {
                av_log(h, AV_LOG_ERROR, "Circular buffer overrun. "
                        "To avoid, increase fifo_size URL option. "
                        "To survive in such case, use overrun_nonfatal option\n");
                s->circular_buffer_error = AVERROR(EIO);
                goto end;
            }
        }
        av_fifo_generic_write(s->fifo, s->tmp, len+4, NULL);
        pthread_cond_signal(&s->cond);
    }

end:
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    return NULL;
}

static void *circular_buffer_task_tx( void *_URLContext)
{
    URLContext *h = _URLContext;
    UDPTldkContext *s = h->priv_data;
    int64_t target_timestamp = av_gettime_relative();
    int64_t start_timestamp = av_gettime_relative();
    int64_t sent_bits = 0;
    int64_t burst_interval = s->bitrate ? (s->burst_bits * 1000000 / s->bitrate) : 0;
    int64_t max_delay = s->bitrate ?  ((int64_t)h->max_packet_size * 8 * 1000000 / s->bitrate + 1) : 0;

    pthread_mutex_lock(&s->mutex);

    if (ff_socket_nonblock(s->udp_fd, 0) < 0) {
        av_log(h, AV_LOG_ERROR, "Failed to set blocking mode");
        s->circular_buffer_error = AVERROR(EIO);
        goto end;
    }

    for(;;) {
        int len;
        const uint8_t *p;
        uint8_t tmp[4];
        int64_t timestamp;

        len = av_fifo_size(s->fifo);

        while (len<4) {
            if (s->close_req)
                goto end;
            if (pthread_cond_wait(&s->cond, &s->mutex) < 0) {
                goto end;
            }
            len = av_fifo_size(s->fifo);
        }

        av_fifo_generic_read(s->fifo, tmp, 4, NULL);
        len = AV_RL32(tmp);

        av_assert0(len >= 0);
        av_assert0(len <= sizeof(s->tmp));

        av_fifo_generic_read(s->fifo, s->tmp, len, NULL);

        pthread_mutex_unlock(&s->mutex);

        if (s->bitrate) {
            timestamp = av_gettime_relative();
            if (timestamp < target_timestamp) {
                int64_t delay = target_timestamp - timestamp;
                if (delay > max_delay) {
                    delay = max_delay;
                    start_timestamp = timestamp + delay;
                    sent_bits = 0;
                }
                av_usleep(delay);
            } else {
                if (timestamp - burst_interval > target_timestamp) {
                    start_timestamp = timestamp - burst_interval;
                    sent_bits = 0;
                }
            }
            sent_bits += len * 8;
            target_timestamp = start_timestamp + sent_bits * 1000000 / s->bitrate;
        }

        p = s->tmp;
        while (len) {
            int ret;
            av_assert0(len > 0);
            if (!s->is_connected) {
                ret = sendto (s->udp_fd, p, len, 0,
                            (struct sockaddr *) &s->dest_addr,
                            s->dest_addr_len);
            } else
                ret = send(s->udp_fd, p, len, 0);
            if (ret >= 0) {
                len -= ret;
                p   += ret;
            } else {
                ret = ff_neterrno();
                if (ret != AVERROR(EAGAIN) && ret != AVERROR(EINTR)) {
                    pthread_mutex_lock(&s->mutex);
                    s->circular_buffer_error = ret;
                    pthread_mutex_unlock(&s->mutex);
                    return NULL;
                }
            }
        }

        pthread_mutex_lock(&s->mutex);
    }

end:
    pthread_mutex_unlock(&s->mutex);
    return NULL;
}


#endif

/* put it in UDP context */
/* return non zero if error */
static int udp_open(URLContext *h, const char *uri, int flags)
{
    char hostname[1024], localaddr[1024] = "";
    int port, udp_fd = -1, tmp, bind_ret = -1, dscp = -1;
    UDPTldkContext *s = h->priv_data;
    int is_output;
    const char *p;
    char buf[256];
    struct sockaddr_storage my_addr;
    socklen_t len;
    int ret;

    av_log(NULL, AV_LOG_DEBUG, "%s: URL=%s\n", __FUNCTION__, uri);
    h->is_streamed = 1;

    is_output = !(flags & AVIO_FLAG_READ);
    if (s->buffer_size < 0)
        s->buffer_size = is_output ? UDP_TX_BUF_SIZE : UDP_RX_BUF_SIZE;

    if (s->sources) {
        if ((ret = ff_ip_parse_sources(h, s->sources, &s->filters)) < 0)
            goto fail;
    }

    if (s->block) {
        if ((ret = ff_ip_parse_blocks(h, s->block, &s->filters)) < 0)
            goto fail;
    }

    p = strchr(uri, '?');
    if (p) {
        if (av_find_info_tag(buf, sizeof(buf), "reuse", p)) {
            char *endptr = NULL;
            s->reuse_socket = strtol(buf, &endptr, 10);
            /* assume if no digits were found it is a request to enable it */
            if (buf == endptr)
                s->reuse_socket = 1;
        }
        if (av_find_info_tag(buf, sizeof(buf), "overrun_nonfatal", p)) {
            char *endptr = NULL;
            s->overrun_nonfatal = strtol(buf, &endptr, 10);
            /* assume if no digits were found it is a request to enable it */
            if (buf == endptr)
                s->overrun_nonfatal = 1;
            if (!HAVE_PTHREAD_CANCEL)
                av_log(h, AV_LOG_WARNING,
                       "'overrun_nonfatal' option was set but it is not supported "
                       "on this build (pthread support is required)\n");
        }
        if (av_find_info_tag(buf, sizeof(buf), "ttl", p)) {
            s->ttl = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "localport", p)) {
            s->local_port = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "pkt_size", p)) {
            s->pkt_size = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "buffer_size", p)) {
            s->buffer_size = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "connect", p)) {
            s->is_connected = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "dscp", p)) {
            dscp = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "fifo_size", p)) {
            s->circular_buffer_size = strtol(buf, NULL, 10);
            if (!HAVE_PTHREAD_CANCEL)
                av_log(h, AV_LOG_WARNING,
                       "'circular_buffer_size' option was set but it is not supported "
                       "on this build (pthread support is required)\n");
        }
        if (av_find_info_tag(buf, sizeof(buf), "bitrate", p)) {
            s->bitrate = strtoll(buf, NULL, 10);
            if (!HAVE_PTHREAD_CANCEL)
                av_log(h, AV_LOG_WARNING,
                       "'bitrate' option was set but it is not supported "
                       "on this build (pthread support is required)\n");
        }
        if (av_find_info_tag(buf, sizeof(buf), "burst_bits", p)) {
            s->burst_bits = strtoll(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "localaddr", p)) {
            av_strlcpy(localaddr, buf, sizeof(localaddr));
        }
        if (av_find_info_tag(buf, sizeof(buf), "sources", p)) {
            if ((ret = ff_ip_parse_sources(h, buf, &s->filters)) < 0)
                goto fail;
        }
        if (av_find_info_tag(buf, sizeof(buf), "block", p)) {
            if ((ret = ff_ip_parse_blocks(h, buf, &s->filters)) < 0)
                goto fail;
        }
        if (!is_output && av_find_info_tag(buf, sizeof(buf), "timeout", p))
            s->timeout = strtol(buf, NULL, 10);
        if (is_output && av_find_info_tag(buf, sizeof(buf), "broadcast", p))
            s->is_broadcast = strtol(buf, NULL, 10);
    }
    /* handling needed to support options picking from both AVOption and URL */
    s->circular_buffer_size *= 188;
    if (flags & AVIO_FLAG_WRITE) {
        h->max_packet_size = s->pkt_size;
    } else {
        h->max_packet_size = UDP_MAX_PKT_SIZE;
    }
    h->rw_timeout = s->timeout;

    /* fill the dest addr */
    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port, NULL, 0, uri);

    /* XXX: fix av_url_split */
    if (hostname[0] == '\0' || hostname[0] == '?') {
        /* only accepts null hostname if input */
        if (!(flags & AVIO_FLAG_READ)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    } else {
        av_log(NULL, AV_LOG_DEBUG, "%s: Setting remote URL\n", __func__);
        if ((ret = ff_udp_set_remote_url(h, uri)) < 0)
            goto fail;
    }

    if ((s->is_multicast || s->local_port <= 0) && (h->flags & AVIO_FLAG_READ))
        s->local_port = port;


    memset((void*)&s->local_addr_storage, 0, sizeof(s->local_addr_storage));
    struct sockaddr_in *_addr = (struct sockaddr_in*)&s->local_addr_storage;
    _addr->sin_family = AF_INET;
    _addr->sin_addr.s_addr = INADDR_ANY;
    _addr->sin_port = 0;

    // TODO:
    ret = nspk_tldk_udp_stream_new(s);
    if (ret != 0) {
        av_log(NULL, AV_LOG_ERROR, "%s: Failed to create new TLDK stream.\n", __func__);
        goto fail;
    }

    s->local_port = 0; // TODO: Try to get it from TLDK APIs

    av_log(h, AV_LOG_DEBUG, "%s: TLDK UDP stream opened.\n", __func__);

    /* Follow the requested reuse option, unless it's multicast in which
     * case enable reuse unless explicitly disabled.
     */
    // if (s->reuse_socket > 0 || (s->is_multicast && s->reuse_socket < 0)) {
    //     s->reuse_socket = 1;
    //     av_log(h, AV_LOG_WARNING, "%s: Socket reuse not implemented for TLDK.\n", __func__);
    //     goto fail;
    // }

//     if (s->is_broadcast) {
// #ifdef SO_BROADCAST
//         av_log(h, AV_LOG_WARNING, "%s: Broadcast not implemented for TLDK.\n", __func__);
//         goto fail;
// #else
//         ret = AVERROR(ENOSYS);
//         goto fail;
// #endif
//     }

//     if (dscp >= 0) {
//         dscp <<= 2;
//         av_log(h, AV_LOG_WARNING, "%s: DSCP not implemented for TLDK.\n", __func__);
//         goto fail;
//     }

    // if (s->is_connected) {
    //     av_log(h, AV_LOG_WARNING, "%s: Connected stream not implemented for TLDK.\n", __func__);
    // }

    s->udp_fd = -2;

#if HAVE_PTHREAD_CANCEL
    /*
      Create thread in case of:
      1. Input and circular_buffer_size is set
      2. Output and bitrate and circular_buffer_size is set
    */

    // if (is_output && s->bitrate && !s->circular_buffer_size) {
    //     /* Warn user in case of 'circular_buffer_size' is not set */
    //     av_log(h, AV_LOG_WARNING,"'bitrate' option was set but 'circular_buffer_size' is not, but required\n");
    // }

    if ((!is_output && s->circular_buffer_size) || (is_output && s->bitrate && s->circular_buffer_size)) {
        /* start the task going */
        s->fifo = av_fifo_alloc(s->circular_buffer_size);
        if (!s->fifo) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ret = pthread_mutex_init(&s->mutex, NULL);
        if (ret != 0) {
            av_log(h, AV_LOG_ERROR, "pthread_mutex_init failed : %s\n", strerror(ret));
            ret = AVERROR(ret);
            goto fail;
        }
        ret = pthread_cond_init(&s->cond, NULL);
        if (ret != 0) {
            av_log(h, AV_LOG_ERROR, "pthread_cond_init failed : %s\n", strerror(ret));
            ret = AVERROR(ret);
            goto cond_fail;
        }
        ret = pthread_create(&s->circular_buffer_thread, NULL, is_output?circular_buffer_task_tx:circular_buffer_task_rx, h);
        if (ret != 0) {
            av_log(h, AV_LOG_ERROR, "pthread_create failed : %s\n", strerror(ret));
            ret = AVERROR(ret);
            goto thread_fail;
        }
        s->thread_started = 1;
    }
#endif

    return 0;
#if HAVE_PTHREAD_CANCEL
 thread_fail:
    pthread_cond_destroy(&s->cond);
 cond_fail:
    pthread_mutex_destroy(&s->mutex);
#endif
 fail:
    av_fifo_freep(&s->fifo);
    ff_ip_reset_filters(&s->filters);
    return ret;
}

static int udp_read(URLContext *h, uint8_t *buf, int size)
{
    return 0;
    UDPTldkContext *s = h->priv_data;
    int ret;
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
#if HAVE_PTHREAD_CANCEL
    int avail, nonblock = h->flags & AVIO_FLAG_NONBLOCK;

    if (s->fifo) {
        pthread_mutex_lock(&s->mutex);
        do {
            avail = av_fifo_size(s->fifo);
            if (avail) { // >=size) {
                uint8_t tmp[4];

                av_fifo_generic_read(s->fifo, tmp, 4, NULL);
                avail = AV_RL32(tmp);
                if(avail > size){
                    av_log(h, AV_LOG_WARNING, "Part of datagram lost due to insufficient buffer size\n");
                    avail = size;
                }

                av_fifo_generic_read(s->fifo, buf, avail, NULL);
                av_fifo_drain(s->fifo, AV_RL32(tmp) - avail);
                pthread_mutex_unlock(&s->mutex);
                return avail;
            } else if(s->circular_buffer_error){
                int err = s->circular_buffer_error;
                pthread_mutex_unlock(&s->mutex);
                return err;
            } else if(nonblock) {
                pthread_mutex_unlock(&s->mutex);
                return AVERROR(EAGAIN);
            } else {
                /* FIXME: using the monotonic clock would be better,
                   but it does not exist on all supported platforms. */
                int64_t t = av_gettime() + 100000;
                struct timespec tv = { .tv_sec  =  t / 1000000,
                                       .tv_nsec = (t % 1000000) * 1000 };
                int err = pthread_cond_timedwait(&s->cond, &s->mutex, &tv);
                if (err) {
                    pthread_mutex_unlock(&s->mutex);
                    return AVERROR(err == ETIMEDOUT ? EAGAIN : err);
                }
                nonblock = 1;
            }
        } while(1);
    }
#endif

    // if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
    //     ret = ff_network_wait_fd(s->udp_fd, 0);
    //     if (ret < 0)
    //         return ret;
    // }
    // ret = recvfrom(s->udp_fd, buf, size, 0, (struct sockaddr *)&addr, &addr_len);
    // if (ret < 0)
    //     return ff_neterrno();

    // TLDK receive
    netbe_lcore();
    int n = s->tldk_udp_stream->pbuf.num;
    int k = RTE_DIM(s->tldk_udp_stream->pbuf.pkt) - n;
    // n = netfe_rx_process(rte_lcore_id(), s->tldk_udp_stream);
	n = tle_stream_recv(s->tldk_udp_stream->s, s->tldk_udp_stream->pbuf.pkt + n, k);
    av_log(NULL, AV_LOG_DEBUG, "%s: Received %u bytes\n", __func__, n);
	if (n == 0)
        return AVERROR(ENODATA);
    for (int i = 0; i < n; i++) {
        struct rte_mbuf *pkt_data = s->tldk_udp_stream->pbuf.pkt[i];
        memcpy(buf, rte_pktmbuf_mtod(pkt_data, void*), rte_pktmbuf_data_len(pkt_data));
    }
    if (ff_ip_check_source_lists(&addr, &s->filters))
        return AVERROR(EINTR);
    return ret;
}

static int udp_write(URLContext *h, const uint8_t *buf, int size)
{
    UDPTldkContext *s = h->priv_data;
    int ret;

    av_log(NULL, AV_LOG_DEBUG, "%s: filename=%s\n", __FUNCTION__, h->filename);

#if HAVE_PTHREAD_CANCEL
    if (s->fifo) {
        uint8_t tmp[4];

        pthread_mutex_lock(&s->mutex);

        /*
          Return error if last tx failed.
          Here we can't know on which packet error was, but it needs to know that error exists.
        */
        if (s->circular_buffer_error<0) {
            int err = s->circular_buffer_error;
            pthread_mutex_unlock(&s->mutex);
            return err;
        }

        if(av_fifo_space(s->fifo) < size + 4) {
            /* What about a partial packet tx ? */
            pthread_mutex_unlock(&s->mutex);
            return AVERROR(ENOMEM);
        }
        AV_WL32(tmp, size);
        av_fifo_generic_write(s->fifo, tmp, 4, NULL); /* size of packet */
        av_fifo_generic_write(s->fifo, (uint8_t *)buf, size, NULL); /* the data */
        pthread_cond_signal(&s->cond);
        pthread_mutex_unlock(&s->mutex);
        return size;
    }
#endif
    // if (!(h->flags & AVIO_FLAG_NONBLOCK)) {
    //     av_log(h, AV_LOG_WARNING, "%s: Non-blocking socket not implemented for TLDK.\n", __func__);
    // }

    if (!s->is_connected) {
        // ret = sendto (s->udp_fd, buf, size, 0,
        //               (struct sockaddr *) &s->dest_addr,
        //               s->dest_addr_len);

        ret = nspk_tldk_udp_stream_send(s, (void*)buf, size);
        // rte_delay_ms(100);
    } else {
        ;
        // ret = send(s->udp_fd, buf, size, 0);
    }
    av_log(NULL, AV_LOG_DEBUG, "%s: ret=%d\n", __func__, ret);

    return ret < 0 ? ff_neterrno() : ret;
}

static int udp_close(URLContext *h)
{

    UDPTldkContext *s = h->priv_data;

#if HAVE_PTHREAD_CANCEL
    // Request close once writing is finished
    if (s->thread_started && !(h->flags & AVIO_FLAG_READ)) {
        pthread_mutex_lock(&s->mutex);
        s->close_req = 1;
        pthread_cond_signal(&s->cond);
        pthread_mutex_unlock(&s->mutex);
    }
#endif

    if (s->is_multicast && (h->flags & AVIO_FLAG_READ))
        udp_leave_multicast_group(s->udp_fd, (struct sockaddr *)&s->dest_addr,(struct sockaddr *)&s->local_addr_storage);
#if HAVE_PTHREAD_CANCEL
    if (s->thread_started) {
        int ret;
        // Cancel only read, as write has been signaled as success to the user
        if (h->flags & AVIO_FLAG_READ) {
#ifdef _WIN32
            /* recvfrom() is not a cancellation point for win32, so we shutdown
             * the socket and abort pending IO, subsequent recvfrom() calls
             * will fail with WSAESHUTDOWN causing the thread to exit. */
            shutdown(s->udp_fd, SD_RECEIVE);
            CancelIoEx((HANDLE)(SOCKET)s->udp_fd, NULL);
#else
            pthread_cancel(s->circular_buffer_thread);
#endif
        }
        ret = pthread_join(s->circular_buffer_thread, NULL);
        if (ret != 0)
            av_log(h, AV_LOG_ERROR, "pthread_join(): %s\n", strerror(ret));
        pthread_mutex_destroy(&s->mutex);
        pthread_cond_destroy(&s->cond);
    }
#endif

    // Flush pkts.
    netfe_tx_process_udp(rte_lcore_id(), s->tldk_udp_stream);
    netbe_lcore();

    // closesocket(s->udp_fd);
    av_fifo_freep(&s->fifo);
    ff_ip_reset_filters(&s->filters);
    return 0;
}

const URLProtocol tldk_udp_protocol = {
    .name                = "tldk_udp",
    .url_open            = udp_open,
    .url_read            = udp_read,
    .url_write           = udp_write,
    .url_close           = udp_close,
    .url_get_file_handle = udp_get_file_handle,
    .priv_data_size      = sizeof(UDPTldkContext),
    .priv_data_class     = &udp_class,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
