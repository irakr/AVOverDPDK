#include <libavutil/opt.h>
#include <libavutil/avstring.h>
#include <libavdevice/avdevice.h>
#include <libavutil/avassert.h>
#include <nspk_avio.h>
#include <nspk_av_rtp.h>

static int nspk_url_alloc_for_protocol(URLContext **puc, const URLProtocol *up,
                                const char *filename, int flags,
                                const AVIOInterruptCB *int_cb)
{
    URLContext *uc;
    int err;
#if CONFIG_NETWORK
    if (up->flags & URL_PROTOCOL_FLAG_NETWORK && !ff_network_init())
        return AVERROR(EIO);
#endif
    if ((flags & AVIO_FLAG_READ) && !up->url_read) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to open the '%s' protocol for reading\n", up->name);
        return AVERROR(EIO);
    }
    if ((flags & AVIO_FLAG_WRITE) && !up->url_write) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to open the '%s' protocol for writing\n", up->name);
        return AVERROR(EIO);
    }
    uc = av_mallocz(sizeof(URLContext) + strlen(filename) + 1);
    if (!uc) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    uc->av_class = &ffurl_context_class;
    uc->filename = (char *)&uc[1];
    strcpy(uc->filename, filename);
    uc->prot            = up;
    uc->flags           = flags;
    uc->is_streamed     = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    if (up->priv_data_size) {
        uc->priv_data = av_mallocz(up->priv_data_size);
        if (!uc->priv_data) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        if (up->priv_data_class) {
            char *start;
            *(const AVClass **)uc->priv_data = up->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
            if (av_strstart(uc->filename, up->name, (const char**)&start) && *start == ',') {
                int ret= 0;
                char *p= start;
                char sep= *++p;
                char *key, *val;
                p++;

                if (strcmp(up->name, "subfile"))
                    ret = AVERROR(EINVAL);

                while(ret >= 0 && (key= strchr(p, sep)) && p<key && (val = strchr(key+1, sep))){
                    *val= *key= 0;
                    if (strcmp(p, "start") && strcmp(p, "end")) {
                        ret = AVERROR_OPTION_NOT_FOUND;
                    } else
                        ret= av_opt_set(uc->priv_data, p, key+1, 0);
                    if (ret == AVERROR_OPTION_NOT_FOUND)
                        av_log(uc, AV_LOG_ERROR, "Key '%s' not found.\n", p);
                    *val= *key= sep;
                    p= val+1;
                }
                if(ret<0 || p!=key){
                    av_log(uc, AV_LOG_ERROR, "Error parsing options string %s\n", start);
                    av_freep(&uc->priv_data);
                    av_freep(&uc);
                    err = AVERROR(EINVAL);
                    goto fail;
                }
                memmove(start, key+1, strlen(key));
            }
        }
    }
    if (int_cb)
        uc->interrupt_callback = *int_cb;
    *puc = uc;
    return 0;
fail:
    *puc = NULL;
    if (uc)
        av_freep(&uc->priv_data);
    av_freep(&uc);
#if CONFIG_NETWORK
    if (up->flags & URL_PROTOCOL_FLAG_NETWORK)
        ff_network_close();
#endif
    return err;
}

int nspk_ffurl_open_whitelist(URLContext **puc, const char *filename, int flags,
                             const AVIOInterruptCB *int_cb, AVDictionary **options,
                             const char *whitelist, const char* blacklist,
                             URLContext *parent, const URLProtocol *tldk_protocol)
{
    AVDictionary *tmp_opts = NULL;
    AVDictionaryEntry *e;
    parent->priv_data;
    int ret = nspk_url_alloc_for_protocol(puc, tldk_protocol,
                                      filename, AVIO_FLAG_WRITE,
                                      NULL);
    if (ret != 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to allocate URLProtocol\n");
        return ret;
    }
    if (parent) {
        ret = av_opt_copy(*puc, parent);
        if (ret < 0)
            goto fail;
    }
    if (options &&
        (ret = av_opt_set_dict(*puc, options)) < 0)
        goto fail;
    if (options && (*puc)->prot->priv_data_class &&
        (ret = av_opt_set_dict((*puc)->priv_data, options)) < 0)
        goto fail;

    if (!options)
        options = &tmp_opts;

    av_assert0(!whitelist ||
               !(e=av_dict_get(*options, "protocol_whitelist", NULL, 0)) ||
               !strcmp(whitelist, e->value));
    av_assert0(!blacklist ||
               !(e=av_dict_get(*options, "protocol_blacklist", NULL, 0)) ||
               !strcmp(blacklist, e->value));

    if ((ret = av_dict_set(options, "protocol_whitelist", whitelist, 0)) < 0)
        goto fail;

    if ((ret = av_dict_set(options, "protocol_blacklist", blacklist, 0)) < 0)
        goto fail;

    if ((ret = av_opt_set_dict(*puc, options)) < 0)
        goto fail;
    ret = ffurl_connect(*puc, NULL);
    if (!ret)
        return 0;
fail:
    ffurl_closep(puc);
    return ret;
}

// TEST:
// This is almost what avio_open() does but with TLDK APIs for 
// opening streams.
// TODO: We must rename this function to something relevant. It is not really avio_.
int nspk_avio_open(struct nspk_rtp_session_ctx_t *rtp_sess, AVIOContext **s, const char *filename, int flags)
{
    int ret;
    URLContext *tldk_url_ctx;
    ret = nspk_ffurl_open_whitelist(&tldk_url_ctx, filename, flags,
          NULL, NULL, NULL, NULL, NULL, &tldk_rtp_protocol);
    if (ret != 0) {
        av_log(NULL, AV_LOG_FATAL, "nspk_ffurl_open_whitelist failed ret=%d\n", ret);
        return ret;
    }

    ret = ffio_fdopen(s, tldk_url_ctx);
    if (ret != 0) {
        av_log(NULL, AV_LOG_FATAL, "ffio_fdopen failed ret=%d\n", ret);
        goto fail;
    }
    return 0;
fail:
    ffurl_close(tldk_url_ctx);
    return ret;
}
