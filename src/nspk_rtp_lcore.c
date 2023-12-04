/**
 * NSPK DPDK based RTP core lib.
 */

#include <nspk.h>
#include <tldk_utils/udp.h>
#include <tldk_utils/parse.h>
#include <libavdevice/alsa.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavformat/internal.h>
#include <libavcodec/internal.h>
#include <libavcodec/packet_internal.h>
#include <libavutil/opt.h>
#include <libavutil/dict.h>
#include <libavutil/pixdesc.h>
#include <libavutil/timestamp.h>
#include <libavutil/avassert.h>
#include <libavutil/avstring.h>
#include <libavutil/internal.h>
#include <libavutil/mathematics.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavformat/url.h>

// FIXME
// TEST PURPOSE ONLY
// The stream to be sent over RTP port 5000
#define TARGET_INPUT_STREAM     0

#define AV_PKT_FLAG_UNCODED_FRAME 0x2000

static AVFormatContext *ifmt_ctx;
static AVFormatContext *ofmt_ctx;
static struct filtering_ctx_t *filter_ctx = NULL;
static struct stream_ctx_t *stream_ctx = NULL;

static int open_input_file(struct nspk_rtp_session_ctx_t *rtp_sess)
{
    int ret;
    unsigned int i;
    char *filename = rtp_sess->src_url; 

    ifmt_ctx = NULL;
    if ((ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    stream_ctx = av_mallocz_array(ifmt_ctx->nb_streams, sizeof(*stream_ctx));
    if (!stream_ctx)
        return AVERROR(ENOMEM);

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *stream = ifmt_ctx->streams[i];
        AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *codec_ctx;
        if (!dec) {
            av_log(NULL, AV_LOG_ERROR, "Failed to find decoder for stream #%u\n", i);
            return AVERROR_DECODER_NOT_FOUND;
        }
        codec_ctx = avcodec_alloc_context3(dec);
        if (!codec_ctx) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream #%u\n", i);
            return AVERROR(ENOMEM);
        }
        ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to copy decoder parameters to input decoder context "
                   "for stream #%u\n", i);
            return ret;
        }
        /* Reencode video & audio and remux subtitles etc. */
        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
                || codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
                codec_ctx->framerate = av_guess_frame_rate(ifmt_ctx, stream, NULL);
            /* Open decoder */
            ret = avcodec_open2(codec_ctx, dec, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return ret;
            }
        }
        stream_ctx[i].dec_ctx = codec_ctx;

        stream_ctx[i].dec_frame = av_frame_alloc();
        if (!stream_ctx[i].dec_frame)
            return AVERROR(ENOMEM);
    }

    av_dump_format(ifmt_ctx, 0, filename, 0);
    return 0;
}

static void print_sdp(void)
{
//     char sdp[16384];
//     int i;
//     int j;
//     int nb_output_files = 1;
//     AVIOContext *sdp_pb;
//     AVFormatContext **avc;

//     avc = av_malloc_array(1, sizeof(*avc));
//     if (!avc)
//         return;
//     for (i = 0, j = 0; i < nb_output_files; i++) {
//         if (!strcmp(ofmt_ctx->oformat->name, "rtp")) {
//             avc[j] = ofmt_ctx;
//             j++;
//         }
//     }

//     if (!j)
//         goto fail;

//     av_sdp_create(avc, j, sdp, sizeof(sdp));

//     av_log(NULL, AV_LOG_DEBUG, "SDP:\n%s\n", sdp);
//     fflush(stdout);

// fail:
//     av_freepp(&avc);
}

static int open_output_file(struct nspk_rtp_session_ctx_t *rtp_sess, int target_input_stream)
{
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    AVCodec *encoder;
    int ret;
    unsigned int i = target_input_stream;
    char *filename = rtp_sess->dst_url;

    ofmt_ctx = NULL;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "rtp", filename);
    if (!ofmt_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return AVERROR_UNKNOWN;
    }

    // TODO: Use NSPK's RTP codec.
    av_log(NULL, AV_LOG_DEBUG, "Guessing codec for %s\n", filename);
    enum AVCodecID out_codec = av_guess_codec(ofmt_ctx->oformat, NULL, filename, NULL, AVMEDIA_TYPE_VIDEO);
    if (out_codec == AV_CODEC_ID_NONE) {
        av_log(NULL, AV_LOG_ERROR, "Could not guess codec\n");
        return AVERROR_UNKNOWN;
    }
    av_log(NULL, AV_LOG_DEBUG, "Input codec: %d, Output codec: %d\n", stream_ctx[target_input_stream].dec_ctx->codec_id, out_codec);

    out_stream = avformat_new_stream(ofmt_ctx, NULL);
    if (!out_stream) {
        av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
        return AVERROR_UNKNOWN;
    }
    in_stream = ifmt_ctx->streams[target_input_stream];
    dec_ctx = stream_ctx[target_input_stream].dec_ctx;
    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
            || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        /* in this example, we choose transcoding to same codec */
        encoder = avcodec_find_encoder(out_codec);
        if (!encoder) {
            av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
            return AVERROR_INVALIDDATA;
        }
        enc_ctx = avcodec_alloc_context3(encoder);
        if (!enc_ctx) {
            av_log(NULL, AV_LOG_FATAL, "Failed to allocate the encoder context\n");
            return AVERROR(ENOMEM);
        }
        /* In this example, we transcode to same properties (picture size,
         * sample rate etc.). These properties can be changed for output
         * streams easily using filters */
        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            av_log(NULL, AV_LOG_DEBUG, "AVMEDIA_TYPE_VIDEO\n");
            enc_ctx->height = dec_ctx->height;
            enc_ctx->width = dec_ctx->width;
            enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
            /* take first format from list of supported formats */
            if (encoder->pix_fmts)
                enc_ctx->pix_fmt = encoder->pix_fmts[0];
            else
                enc_ctx->pix_fmt = dec_ctx->pix_fmt;
            /* video time_base can be set to whatever is handy and supported by encoder */
            // av_log(NULL, AV_LOG_DEBUG, "framerate=%d/%d\n", enc_ctx->framerate.num, enc_ctx->framerate.den);
            // enc_ctx->time_base = (AVRational){1, 25};
            enc_ctx->time_base = av_inv_q(dec_ctx->framerate);
            av_log(NULL, AV_LOG_DEBUG, "time_base=%d/%d\n", enc_ctx->time_base.num, enc_ctx->time_base.den);
        } else {
            av_log(NULL, AV_LOG_DEBUG, "AVMEDIA_TYPE_AUDIO\n");
            enc_ctx->sample_rate = dec_ctx->sample_rate;
            enc_ctx->channel_layout = dec_ctx->channel_layout;
            enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
            /* take first format from list of supported formats */
            enc_ctx->sample_fmt = encoder->sample_fmts[0];
            enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
        }
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        /* Third parameter can be used to pass settings to encoder */
        ret = avcodec_open2(enc_ctx, encoder, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
            return ret;
        }
        ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to copy encoder parameters to output stream #%u\n", i);
            return ret;
        }
        out_stream->time_base = enc_ctx->time_base;
        stream_ctx[i].enc_ctx = enc_ctx;
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
        av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
        return AVERROR_INVALIDDATA;
    } else {
        /* if this stream must be remuxed */
        ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Copying parameters for stream #%u failed\n", i);
            return ret;
        }
        out_stream->time_base = in_stream->time_base;
    }

    av_dump_format(ofmt_ctx, 0, filename, 1);

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        // ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        ret = nspk_avio_open(rtp_sess, &ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "nspk_avio_open: Could not open output file '%s'", filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }
    print_sdp();

    return 0;
}

static int init_filter(struct filtering_ctx_t* fctx, AVCodecContext *dec_ctx,
        AVCodecContext *enc_ctx, const char *filter_spec)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = NULL;
    const AVFilter *buffersink = NULL;
    AVFilterContext *buffersrc_ctx = NULL;
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVFilterGraph *filter_graph = avfilter_graph_alloc();

    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                dec_ctx->time_base.num, dec_ctx->time_base.den,
                dec_ctx->sample_aspect_ratio.num,
                dec_ctx->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
                (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (!dec_ctx->channel_layout)
            dec_ctx->channel_layout =
                av_get_default_channel_layout(dec_ctx->channels);
        snprintf(args, sizeof(args),
                "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
                dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
                av_get_sample_fmt_name(dec_ctx->sample_fmt),
                dec_ctx->channel_layout);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
                (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "channel_layouts",
                (uint8_t*)&enc_ctx->channel_layout,
                sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
                (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    } else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec,
                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

    /* Fill struct filtering_ctx_t */
    fctx->buffersrc_ctx = buffersrc_ctx;
    fctx->buffersink_ctx = buffersink_ctx;
    fctx->filter_graph = filter_graph;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static int init_filters(int target_input_stream)
{
    const char *filter_spec;
    unsigned int i = target_input_stream;
    int ret;
    filter_ctx = av_malloc_array(ifmt_ctx->nb_streams, sizeof(*filter_ctx));
    if (!filter_ctx)
        return AVERROR(ENOMEM);

    i = target_input_stream;
    filter_ctx[i].buffersrc_ctx  = NULL;
    filter_ctx[i].buffersink_ctx = NULL;
    filter_ctx[i].filter_graph   = NULL;
    if (!(ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
            || ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
        return AVERROR(EINVAL);
    if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        filter_spec = "null"; /* passthrough (dummy) filter for video */
    else
        filter_spec = "anull"; /* passthrough (dummy) filter for audio */
    ret = init_filter(&filter_ctx[i], stream_ctx[i].dec_ctx,
            stream_ctx[i].enc_ctx, filter_spec);
    if (ret)
        return ret;
    filter_ctx[i].enc_pkt = av_packet_alloc();
    if (!filter_ctx[i].enc_pkt)
        return AVERROR(ENOMEM);
    filter_ctx[i].filtered_frame = av_frame_alloc();
    if (!filter_ctx[i].filtered_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int encode_write_frame(unsigned int stream_index, int flush)
{
    struct stream_ctx_t *stream = &stream_ctx[stream_index];
    struct filtering_ctx_t *filter = &filter_ctx[stream_index];
    AVFrame *filt_frame = flush ? NULL : filter->filtered_frame;
    AVPacket *enc_pkt = filter->enc_pkt;
    int ret;

    av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
    /* encode filtered frame */
    av_packet_unref(enc_pkt);

    ret = avcodec_send_frame(stream->enc_ctx, filt_frame);

    if (ret < 0)
        return ret;

    while (ret >= 0) {
        ret = avcodec_receive_packet(stream->enc_ctx, enc_pkt);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;

        /* prepare packet for muxing */
        enc_pkt->stream_index = stream_index;
        av_packet_rescale_ts(enc_pkt,
                             stream->enc_ctx->time_base,
                             ofmt_ctx->streams[stream_index]->time_base);

        av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
        /* mux encoded frame */
        // ret = av_interleaved_write_frame(ofmt_ctx, enc_pkt);
        
        // TODO... Create my own RTP muxer like function which
        // sends the RTP payloads through the TLDK UDP streams.
        ret = av_interleaved_write_frame(ofmt_ctx, enc_pkt);
    }

    return ret;
}

static int filter_encode_write_frame(AVFrame *frame, unsigned int stream_index)
{
    struct filtering_ctx_t *filter = &filter_ctx[stream_index];
    int ret;

    av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(filter->buffersrc_ctx,
            frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(filter->buffersink_ctx,
                                      filter->filtered_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            break;
        }

        filter->filtered_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(stream_index, 0);
        av_frame_unref(filter->filtered_frame);
        if (ret < 0)
            break;
    }

    return ret;
}

static int flush_encoder(unsigned int stream_index)
{
    if (!(stream_ctx[stream_index].enc_ctx->codec->capabilities &
                AV_CODEC_CAP_DELAY))
        return 0;

    av_log(NULL, AV_LOG_INFO, "Flushing stream #%u encoder\n", stream_index);
    return encode_write_frame(stream_index, 1);
}

static void nspk_av_cleanup()
{
	int i;
	for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        avcodec_free_context(&stream_ctx[i].dec_ctx);
        if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] && stream_ctx[i].enc_ctx)
            avcodec_free_context(&stream_ctx[i].enc_ctx);
        if (filter_ctx && filter_ctx[i].filter_graph) {
            avfilter_graph_free(&filter_ctx[i].filter_graph);
            av_packet_free(&filter_ctx[i].enc_pkt);
            av_frame_free(&filter_ctx[i].filtered_frame);
        }

        av_frame_free(&stream_ctx[i].dec_frame);
    }
    if (filter_ctx)
        av_freep(filter_ctx);
    if (stream_ctx)
        av_freep(stream_ctx);
    avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
}

int nspk_media_init(struct nspk_rtp_session_ctx_t *rtp_sess)
{
	int ret;

	av_log_set_level(AV_LOG_DEBUG);

#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avformat_network_init();

    av_log(NULL, AV_LOG_INFO, "*** Opening input file %s ***\n", rtp_sess->src_url);
    if ((ret = open_input_file(rtp_sess)) < 0)
        goto error;
    av_log(NULL, AV_LOG_INFO, "*** Opened input file ***\n");

    av_log(NULL, AV_LOG_INFO, "*** Opening output file %s ***\n", rtp_sess->dst_url);
    if ((ret = open_output_file(rtp_sess, TARGET_INPUT_STREAM)) < 0)
        goto error;
    av_log(NULL, AV_LOG_INFO, "*** Opened output file ***\n");

    av_log(NULL, AV_LOG_INFO, "*** Intializing filters ***\n");
    if ((ret = init_filters(TARGET_INPUT_STREAM)) < 0)
        goto error;
    av_log(NULL, AV_LOG_INFO, "*** Initialized filters ***\n");

	return 0;
error:
	nspk_av_cleanup();
	return EINVAL;
}

int nspk_media_start(struct nspk_rtp_session_ctx_t *rtp_sess)
{
	AVPacket *packet = NULL;
	unsigned int stream_index;
	unsigned int i;
	int ret;

	if (!(packet = av_packet_alloc()))
        goto end;
    av_log(NULL, AV_LOG_INFO, "Allocated AV packet.\n");

	/* read all packets */
    while (!force_quit) {
        if ((ret = av_read_frame(ifmt_ctx, packet)) < 0)
            break;
        stream_index = packet->stream_index;
        av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame of stream_index %u%s\n",
                stream_index, (stream_index != TARGET_INPUT_STREAM) ? "(Ignoring)" : "");
        if (stream_index != TARGET_INPUT_STREAM)
            continue;

        if (filter_ctx[stream_index].filter_graph) {
            struct stream_ctx_t *stream = &stream_ctx[stream_index];

            av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");

            av_packet_rescale_ts(packet,
                                 ifmt_ctx->streams[stream_index]->time_base,
                                 stream->dec_ctx->time_base);
            ret = avcodec_send_packet(stream->dec_ctx, packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(stream->dec_ctx, stream->dec_frame);
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                    break;
                else if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "avcodec_receive_frame(): ret=%d\n", ret);
                    goto end;
                }

                stream->dec_frame->pts = stream->dec_frame->best_effort_timestamp;
				// TODO:
				// This function should perform TLDK UDP send.
                ret = filter_encode_write_frame(stream->dec_frame, stream_index);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "filter_encode_write_frame(): ret=%d\n", ret);
                    goto end;
                }
            }
        } else {
            /* remux this frame without reencoding */
            av_packet_rescale_ts(packet,
                                 ifmt_ctx->streams[stream_index]->time_base,
                                 ofmt_ctx->streams[stream_index]->time_base);

            ret = av_interleaved_write_frame(ofmt_ctx, packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "av_write_frame(): ret=%d\n", ret);
                goto end;
            }
        }
        av_packet_unref(packet);
        av_log(NULL, AV_LOG_INFO, "Transcoded a packet.\n");
    } // while(1)

    av_log(NULL, AV_LOG_DEBUG, "%s: Stopping stream.\n", __FUNCTION__);

	/* flush filters and encoders */
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        /* flush filter */
        if (!filter_ctx[i].filter_graph)
            continue;
        ret = filter_encode_write_frame(NULL, i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
            goto end;
        }

        /* flush encoder */
        ret = flush_encoder(i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
            goto end;
        }
    }

    av_write_trailer(ofmt_ctx);

end:
	nspk_av_cleanup();

    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
	return ret ? 1 : 0;
}

static int
nspk_rtp_generate_rtp(struct nspk_rtp_session_ctx_t *rtp_sess)
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

struct nspk_rtp_session_ctx_t *g_rtp_sess = NULL;
// TODO
// This thread is run by the master lcore and is currently dedicated to sending RTP stream only.
// Later we will change the design such that this thread will dedicate to the session control thread,
// that is when this function will be moved elsewhere.
int
nspk_lcore_main_rtp(void *arg)
{
    int i;
	int rc = 0;
	uint32_t lcore;
	struct nspk_rtp_session_ctx_t *rtp_sess = (struct nspk_rtp_session_ctx_t*)arg;
    g_rtp_sess = rtp_sess;
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

    // Init per lcore FE.
	if (!RTE_PER_LCORE(_fe)) {
		netfe_init_per_lcore_fe(&prm->fe);
	}
    fe = RTE_PER_LCORE(_fe);
    if (fe == NULL)
		return EINVAL;

    rc = nspk_media_init(rtp_sess);
	if (rc != 0) {
		return rc;
	}

	// /* lcore FE init. */
	// if (prm->fe.max_streams != 0) {
	// 	fs = netfe_lcore_init_udp(&prm->fe);
	// 	if (fs == NULL) {
	// 		fprintf(stderr, "Error: failed to init UDP stream.\n");
	// 		return EFAULT;
	// 	}
	// 	av_log(NULL, AV_LOG_DEBUG, "FE stream laddr=%s, raddr=%s\n",
    //         format_addr((struct sockaddr_storage*)&fs->laddr, ip_addr, sizeof(fs->laddr)),
	// 		format_addr((struct sockaddr_storage*)&fs->raddr, ip_addr, sizeof(fs->raddr)));
	// }

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
    //         av_log(NULL, AV_LOG_DEBUG, "Matched network address %s\n",
    //             format_addr((struct sockaddr_storage*)&remote_addr, remote_ip, sizeof(remote_ip)));
    //         break;
    //     }
	// 	// netfe_stream_close(fe, fs);
	// }

	RTE_LOG(NOTICE, USER1, "%s (lcore=%u) Starting RTP session %d\n",
		__func__, lcore, rtp_sess->session_id);
	rc = nspk_media_start(rtp_sess);

	// while (force_quit == 0) {
	// 	// TODO: Understand the event API.
    //     // tle_event_raise(fs->txev);
	// 	// fs->stat.txev[TLE_SEV_UP]++;

    //     // TODO:
    //     // Build a burst of RTP packets from the opened media file.
    //     // Write a test RTP packet into the UDP TX queue.
	// 	int num_pkt;
	// 	if ((num_pkt = nspk_rtp_generate_rtp(rtp_sess)) <= 0) {
	// 		fprintf(stderr, "Error: No RTP packets generated to mbuf.\n");
	// 		continue;
	// 	}
	// 	av_log(NULL, AV_LOG_DEBUG, "Generated %d RTP packets to mbuf.\n", num_pkt);

	// 	// FE send
	// 	netfe_tx_process_udp(lcore, fs);

	// 	// BE send
	// 	netbe_lcore();

	// 	rte_delay_ms(1000);
	// }
end:
	RTE_LOG(NOTICE, USER1, "%s(lcore=%u) finish\n",
		__func__, lcore);

	netfe_lcore_fini_udp();
	netbe_lcore_clear();

	return rc;
}
