/**

MIT License

Copyright (c) 2018 David G. Starkweather 

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

**/

#include <inttypes.h>
#include <algorithm>
#include <climits>
#include <iostream>
#include "VideoCapture.hpp"

extern "C" {
#include <libavformat/avio.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/rational.h>	
#include "circ_buf.h"
};

using namespace ph;
using namespace std;

void VideoCapture::RegisterInit(bool warn){
	if (warn)
		av_log_set_level(AV_LOG_WARNING);
	else
		av_log_set_level(AV_LOG_ERROR);
	avcodec_register_all();
	av_register_all();
	avfilter_register_all();
	avformat_network_init();
}

void VideoCapture::OpenFile(const string &file){
	char msg[32];
	int rc;
	fmt_ctx = NULL;
	if ((rc = avformat_open_input(&fmt_ctx, file.c_str(), 0, 0)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
	if ((rc = avformat_find_stream_info(fmt_ctx, 0)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
}

void VideoCapture::InitMetaData(){
	if (fmt_ctx == NULL) return;
	if (fmt_ctx->metadata == NULL) return;
	AVDictionaryEntry *tag = NULL;

	tag = av_dict_get(fmt_ctx->metadata, "title", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag) metadata.title_str = string(tag->value);

	tag = av_dict_get(fmt_ctx->metadata, "artist", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag) metadata.artist_str = string(tag->value);

	tag = av_dict_get(fmt_ctx->metadata, "album", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag) metadata.album_str = string(tag->value);

	tag = av_dict_get(fmt_ctx->metadata, "genre", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag) metadata.genre_str = string(tag->value);

	tag = av_dict_get(fmt_ctx->metadata, "composer", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag) metadata.composer_str = string(tag->value);

	tag = av_dict_get(fmt_ctx->metadata, "performer", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag) metadata.performer_str = string(tag->value);

	tag = av_dict_get(fmt_ctx->metadata, "album_artist", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag) metadata.album_artist_str = string(tag->value);

	tag = av_dict_get(fmt_ctx->metadata, "copyright", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag) metadata.copyright_str = string(tag->value);

	tag = av_dict_get(fmt_ctx->metadata, "date", NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag) metadata.date_str = string(tag->value);
}

void VideoCapture::InitVideoCodec(){
	char msg[32];
	int rc;

	AVCodec *pCodec = NULL;
	video_stream  = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1,&pCodec, 0);
	if (video_stream < 0)
		return;

	dec_ctx = fmt_ctx->streams[video_stream]->codec;
	av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);

	if ((rc = avcodec_open2(dec_ctx, pCodec, 0)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
	
	pframe_decoded = av_frame_alloc();
	pframe_filtered = av_frame_alloc();
	if (pframe_decoded == NULL || pframe_filtered == NULL)
		throw VideoCaptureException("unable to allocate frames");
}

void VideoCapture::InitAudioCodec(){
	char msg[32];
	int rc;
	AVCodec *pCodec = NULL;
	audio_stream = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &pCodec, 0);
	if (audio_stream < 0) return;

	adec_ctx = fmt_ctx->streams[audio_stream]->codec;
	av_opt_set_int(adec_ctx, "refcounted_frames", 1, 0);
	
	if ((rc = avcodec_open2(adec_ctx, pCodec, 0)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}

	pframeAu = av_frame_alloc();
	pframeAufiltered = av_frame_alloc();
	if (pframeAu == NULL || pframeAufiltered == NULL)
		throw VideoCaptureException("unable to alloc audio frames");

}

void VideoCapture::InitSubtitleCodec(){
	char msg[256];
	int rc;
	AVCodec *pCodec = NULL;
	subtitle_stream = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_SUBTITLE, -1, -1, &pCodec, 0);
	if (subtitle_stream < 0) return;

	subdec_ctx = fmt_ctx->streams[subtitle_stream]->codec;
	av_opt_set_int(subdec_ctx, "refcounted_frames", 1, 0);

	if ((rc = avcodec_open2(subdec_ctx, pCodec, 0)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
}

void VideoCapture::InitVideoFilters(int tm, int bm, int lm, int rm, int width, int dst_fps){
	if (dec_ctx == NULL) return;
	char msg[32];
	int rc;
	AVFilter *bufferSrc = avfilter_get_by_name("buffer");
	AVFilter *bufferSink = avfilter_get_by_name("buffersink");
	AVFilterInOut *outputs = avfilter_inout_alloc();
	AVFilterInOut *inputs  = avfilter_inout_alloc();
	AVRational time_base = fmt_ctx->streams[video_stream]->time_base;
	enum PixelFormat pix_fmts[] = { PIX_FMT_YUV420P,
									PIX_FMT_YUV422P,
									PIX_FMT_YUV444P,
									PIX_FMT_GRAY8,
									PIX_FMT_NONE };
  
	filter_graph = avfilter_graph_alloc();
	if (outputs == NULL || inputs == NULL || filter_graph == NULL)
		throw VideoCaptureException("error no mem");

	// filter args for bufferSrc filter
    char filter_args[128];
	snprintf(filter_args, sizeof(filter_args),
			 "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:sar=%d/%d",
			 dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
			 time_base.num, time_base.den,
			 dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

	//create buffer source in filter graph
	if ((rc = avfilter_graph_create_filter(&buffersrc_ctx, bufferSrc, "in", filter_args,
										   NULL, filter_graph)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
	if ((rc = av_opt_set_int(buffersrc_ctx, "thread_type",
							AVFILTER_THREAD_SLICE, AV_OPT_SEARCH_CHILDREN)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
	
	//create buffer sink in filter graph
	if ((rc = avfilter_graph_create_filter(&buffersink_ctx, bufferSink, "out", NULL,
										   NULL, filter_graph)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}

	if ((rc = av_opt_set_int(buffersink_ctx, "thread_type",
							 AVFILTER_THREAD_SLICE, AV_OPT_SEARCH_CHILDREN))< 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
				   
	//set pix_fmts option on buffer sink object
	if ((rc = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
								  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
    
	// endpoints of filter graph 
	outputs->name       = av_strdup("in");
	outputs->filter_ctx = buffersrc_ctx;
	outputs->pad_idx    = 0;
	outputs->next       = NULL;
  
	inputs->name        = av_strdup("out");
	inputs->filter_ctx  = buffersink_ctx;
	inputs->pad_idx     = 0;
	inputs->next        = NULL;

	int widthsc = width;
	int heightsc = -1;  //keeps original aspect ratio intact
	int crop_width = dec_ctx->width - lm - rm;
	int crop_height = dec_ctx->height - tm - bm;

	int src_fps = (int)(av_q2d(fmt_ctx->streams[video_stream]->avg_frame_rate) + 0.5);
	int fps = (dst_fps > 0) ? dst_fps : src_fps;
	// filter description
	char filter_descr[128];
	snprintf(filter_descr, sizeof(filter_descr),
			 "fps=fps=%d:round=near,format=yuv444p,yadif=0:-1:1,crop=%d:%d:%d:%d,scale=w=%d:h=%d",
  			 fps, crop_width, crop_height, lm, tm, widthsc, heightsc);
	if ((rc = avfilter_graph_parse_ptr(filter_graph, filter_descr, &inputs, &outputs, NULL)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}

	av_log(NULL, AV_LOG_INFO, "video filter: %s", filter_descr);
	if ((rc = av_opt_set_int(filter_graph, "thread_type", AVFILTER_THREAD_SLICE, AV_OPT_SEARCH_CHILDREN)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
    
	// check filter graph configuration 
	if ((rc = avfilter_graph_config(filter_graph, NULL)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}

	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);
}

void VideoCapture::InitAudioFilters(const int sr, const int flt_fmt){
	if (adec_ctx == NULL) return;
	int rc;
	AVFilter *abuffersrc = avfilter_get_by_name("abuffer");
	AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
	AVFilterInOut *outputs = avfilter_inout_alloc();
	AVFilterInOut *inputs = avfilter_inout_alloc();
	int sample_fmts[] = {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT, -1};
	int64_t channel_layouts[] = {AV_CH_LAYOUT_MONO, -1 };
	int sample_rates[] = {44100, 22050, 11025, 8000, 6000, -1};
	AVRational time_base = fmt_ctx->streams[audio_stream]->time_base;

	afilter_graph = avfilter_graph_alloc();
	if (!outputs || !inputs || !afilter_graph)
		throw AudioCaptureException("mem alloc errro");

	if (adec_ctx->channel_layout == 0)
		adec_ctx->channel_layout = av_get_default_channel_layout(adec_ctx->channel_layout);
	int nbchannels = av_get_channel_layout_nb_channels(adec_ctx->channel_layout);
	char layoutstr[32];
	av_get_channel_layout_string(layoutstr, sizeof(layoutstr), nbchannels,
								 adec_ctx->channel_layout);
	char args[512];
	snprintf(args, sizeof(args),
			 "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
		     time_base.num, time_base.den, adec_ctx->sample_rate,
		     av_get_sample_fmt_name(adec_ctx->sample_fmt), layoutstr);

	// buffer source
	rc = avfilter_graph_create_filter(&abuffersrc_ctx, abuffersrc, "in", args, NULL, afilter_graph);
	if (rc < 0) throw AudioCaptureException("unable to create buffer source");

	// buffer sink 
    rc = avfilter_graph_create_filter(&abuffersink_ctx, abuffersink, "out", NULL, NULL, afilter_graph);
    if (rc < 0) throw AudioCaptureException("unable to create buffer sink");
    rc = av_opt_set_int_list(abuffersink_ctx, "sample_fmts", sample_fmts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (rc < 0) throw AudioCaptureException("unable to set sample formats");
    rc = av_opt_set_int_list(abuffersink_ctx, "channel_layouts", channel_layouts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (rc < 0) throw AudioCaptureException("unable to set channel layouts");
    rc = av_opt_set_int_list(abuffersink_ctx, "sample_rates", sample_rates, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (rc < 0) throw AudioCaptureException("unable to set sample rates");

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = abuffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = abuffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

	char filter_descr[128];
	if (flt_fmt){
		snprintf(filter_descr, sizeof(filter_descr), 
				 "aresample=%d,aformat=sample_fmts=flt:channel_layouts=mono", sr);
	} else {
		snprintf(filter_descr, sizeof(filter_descr),
				 "aresample=%d,aformat=sample_fmts=s16:channel_layouts=mono", sr);
	}
	
	av_log(NULL, AV_LOG_INFO, "audio filter: %s" , filter_descr);
    if ((rc = avfilter_graph_parse_ptr(afilter_graph, filter_descr, &inputs, &outputs, NULL)) < 0)
		throw AudioCaptureException("unable to parse filter");

    if ((rc = avfilter_graph_config(afilter_graph, NULL)) < 0)
		throw AudioCaptureException("filter graph not configured correctly");

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
}

void VideoCapture::InitMsgQueues(){
	char msg[32];
	int rc;
	if (dec_ctx != NULL){ 
		if ((rc = av_thread_message_queue_alloc(&video_frames_queue,
													  QueueCapacity, sizeof(AVFrame*))) < 0){
			av_strerror(rc, msg, sizeof(msg));
			throw AudioCaptureException(string(msg));
		}
		av_thread_message_queue_set_err_recv(video_frames_queue, AVERROR(EAGAIN));
	}
	
	if (adec_ctx != NULL){
		circ_buf.head = 0;
		circ_buf.tail = 0;
		if (flt_fmt == 0){
			circ_buf.s16samples = new int16_t[CircBufferSize];
		} else {
			circ_buf.fltsamples = new float[CircBufferSize];
		}
	}
	
	if (subdec_ctx != NULL){
		if ((rc = av_thread_message_queue_alloc(&subtitle_queue,
												QueueCapacity, sizeof(AVSubtitle*)))){
			av_strerror(rc, msg, sizeof(msg));
			throw VideoCaptureException(string(msg));
		}
		av_thread_message_queue_set_err_recv(subtitle_queue, AVERROR(EAGAIN));
	}
}

void VideoCapture::FlushFrames(){
	if (buffersrc_ctx != NULL) // EOF marker to filter graph
		av_buffersrc_add_frame_flags(buffersrc_ctx, NULL, 0);
	if (abuffersrc_ctx != NULL)
		av_buffersrc_add_frame_flags(abuffersrc_ctx, NULL, 0);

	if (dec_ctx != NULL)   //flush frames from filters
		PushVideoFrames();
	if (adec_ctx != NULL)
		PushAudioFrames();

	if (video_frames_queue != NULL)
		av_thread_message_queue_set_err_recv(video_frames_queue, AVERROR_EOF);
	if (subtitle_queue != NULL)
		av_thread_message_queue_set_err_recv(subtitle_queue , AVERROR_EOF);
}

void VideoCapture::PushVideoFrames(){
	char msg[64];
	char msg2[32];
	while (true){
		int rc = av_buffersink_get_frame(buffersink_ctx, pframe_filtered);
		if (rc == AVERROR(EAGAIN)) break;
		if (rc == AVERROR_EOF) break;
		if (rc < 0){
			av_strerror(rc, msg2, sizeof(msg2));
			snprintf(msg, sizeof(msg), "unable to get frame from filter: %s", msg2);
			throw VideoCaptureException(string(msg));
		}
		AVFrame *frame = av_frame_clone(pframe_filtered);
		if ((rc = av_thread_message_queue_send(video_frames_queue, (void*)&frame, 0)) < 0){
			if (rc == AVERROR(EAGAIN)){
				av_log(NULL, AV_LOG_ERROR, "video queue overrun");
				av_frame_free(&frame);
				break;
			}
			if (rc < 0){
				av_strerror(rc, msg2, sizeof(msg2));
				snprintf(msg, sizeof(msg), "unable to push video frame onto queue: %s", msg2);
				throw VideoCaptureException(string(msg));
			}
		}
		av_frame_unref(pframe_filtered);
	}
}

void VideoCapture::HandleVideoPacket(AVPacket &pkt){
	char msg[64];
	int rc, done = 0;
	if ((rc = avcodec_decode_video2(dec_ctx, pframe_decoded, &done, &pkt)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
	pkt.size -= rc;
	pkt.data += rc;
	if (done) {
		pframe_decoded->pts = av_frame_get_best_effort_timestamp(pframe_decoded);
		if ((rc = av_buffersrc_add_frame_flags(buffersrc_ctx, pframe_decoded,
											   AV_BUFFERSRC_FLAG_KEEP_REF)) < 0){
			av_strerror(rc, msg, sizeof(msg));
			printf("error adding frame to buffer: %s %x\n", msg, rc);
			throw VideoCaptureException(string(msg));
		}
		av_frame_unref(pframe_decoded);
		PushVideoFrames();
	}
}

void VideoCapture::PushAudioFrames_flt(){
	char msg[64];
	char msg2[32];
	while (true){
		int rc = av_buffersink_get_frame(abuffersink_ctx, pframeAufiltered);
		if (rc == AVERROR(EAGAIN)) break;
		if (rc == AVERROR_EOF){
			stop_flag.store(true, memory_order_release);
			break;
		}
		if (rc < 0) {
			av_strerror(rc, msg2, sizeof(msg2));
			snprintf(msg, sizeof(msg), "unable to get frame from filter: %s" , msg2);
			throw AudioCaptureException(string(msg));
		}
		bool more = true;
		float *buffer = circ_buf.fltsamples;
		float *sample_data = (float*)(pframeAufiltered->data[0]);
		unsigned long nbsamples = pframeAufiltered->nb_samples;
		while (more){
			while (audio_producer_flag.test_and_set(memory_order_acquire));
			unsigned long head = circ_buf.head.load(memory_order_relaxed);
			unsigned long tail = circ_buf.tail.load(memory_order_acquire);
			if (CIRC_SPACE(head, tail, CircBufferSize) >= nbsamples){
				for (int i=0;i<(int)nbsamples;i++){
					buffer[head] = sample_data[i];
					head = (head+1) & (CircBufferSize - 1);
				}
				circ_buf.head.store(head, memory_order_release);
				more = false;
			}
			audio_producer_flag.clear(memory_order_release);
		}
		av_frame_unref(pframeAufiltered);
	}
}

void VideoCapture::PushAudioFrames_s16(){
	char msg[64];
	char msg2[32];
	while (true){
		int rc = av_buffersink_get_frame(abuffersink_ctx, pframeAufiltered);
		if (rc == AVERROR(EAGAIN)) break;
		if (rc == AVERROR_EOF){
			stop_flag.store(true, memory_order_release);
			break;
		}
		if (rc < 0) {
			av_strerror(rc, msg2, sizeof(msg2));
			snprintf(msg, sizeof(msg), "unable to get frame from filter: %s" , msg2);
			throw AudioCaptureException(string(msg));
		}
		int16_t *buffer = circ_buf.s16samples;
		int16_t *sample_data = (int16_t*)(pframeAufiltered->data[0]);
		unsigned long nbsamples = pframeAufiltered->nb_samples;
		bool more = true;
		while (more){
			while (audio_producer_flag.test_and_set(memory_order_acquire));
			unsigned long head = circ_buf.head.load(memory_order_relaxed);
			unsigned long tail = circ_buf.tail.load(memory_order_acquire);
			if (CIRC_SPACE(head, tail, CircBufferSize) >= nbsamples){
				for (int i=0;i<(int)nbsamples;i++){
					buffer[head] = sample_data[i];
					head = (head+1) & (CircBufferSize - 1);
				}
				circ_buf.head.store(head, memory_order_release);
				more = false;
			}
			audio_producer_flag.clear(memory_order_release);
		}
		av_frame_unref(pframeAufiltered);
	}
}

void VideoCapture::PushAudioFrames(){
	if (flt_fmt) PushAudioFrames_flt();
	else PushAudioFrames_s16();
}

void VideoCapture::HandleAudioPacket(AVPacket &pkt){
	char msg[64];
	char msg2[32];
	int rc, done = 0;
	if ((rc = avcodec_decode_audio4(adec_ctx, pframeAu, &done, &pkt)) < 0){
		av_strerror(rc, msg2, sizeof(msg2));
		snprintf(msg, sizeof(msg), "unable to decode audio frame: %s", msg2);
		throw VideoCaptureException(string(msg));
	}
	pkt.size -= rc;
	pkt.data += rc;
	if (done) {
		pframeAu->pts = av_frame_get_best_effort_timestamp(pframeAu);
		if ((rc = av_buffersrc_add_frame_flags(abuffersrc_ctx, pframeAu,
											   AV_BUFFERSRC_FLAG_KEEP_REF)) < 0){
			av_strerror(rc, msg2, sizeof(msg2));
			snprintf(msg, sizeof(msg), "Unable to add frame to audio filter: %s" , msg2);
			throw AudioCaptureException(string(msg));
		}
		av_frame_unref(pframeAu);
		PushAudioFrames();
	}
	
}										  

void VideoCapture::HandleSubtitlePacket(AVPacket &pkt){
	static AVSubtitle *subtitle = NULL;
	if (subtitle == NULL) subtitle = new AVSubtitle();

	char msg[64];
	char msg2[32];
	int rc, done = 0;
	if ((rc = avcodec_decode_subtitle2(subdec_ctx, subtitle, &done, &pkt)) < 0){
		av_strerror(rc, msg, sizeof(msg));
		throw VideoCaptureException(string(msg));
	}
	pkt.size -= rc;
	pkt.data += rc;
	if (done){
		while (true){
			if ((rc = av_thread_message_queue_send(subtitle_queue, (void*)&subtitle, 0)) < 0){
				if (rc == AVERROR(EAGAIN)){
					av_log(NULL, AV_LOG_ERROR, "subtitle queue overrun");
					break;
				}
				if (rc < 0){
					av_strerror(AVERROR(rc), msg2, sizeof(msg2));
					snprintf(msg, sizeof(msg), "error adding audio frame to queue: %s", msg2);
					throw AudioCaptureException(string(msg));
				}
			}
		    subtitle = NULL;
			break;
		}
	}
}

VideoCapture::VideoCapture(){
	video_stream = -1;
	dec_ctx = NULL;
	video_frames_queue = NULL;
	audio_stream = -1;
	adec_ctx = NULL;
	subtitle_stream = -1;
	subdec_ctx = NULL;
	subtitle_queue = NULL;
	circ_buf.fltsamples = NULL;
}

VideoCapture::VideoCapture(const string &filename,
						   int top_m, int bottom_m,
						   int left_m, int right_m,
						   int sr, int width,
						   int flag, int flt_fmt, int fps, bool warn){
	VideoCapture();
	this->flt_fmt = flt_fmt;
	this->sr = sr;
	RegisterInit(warn);
	OpenFile(filename);
	InitMetaData();
	if (flag & PHCAPTURE_VIDEO_FLAG){
		InitVideoCodec();
		InitVideoFilters(top_m, bottom_m, left_m, right_m, width, fps);
	}
	if (flag & PHCAPTURE_AUDIO_FLAG){
		InitAudioCodec();
		InitAudioFilters(sr, flt_fmt);
	}
	if (flag & PHCAPTURE_SUBTITLE_FLAG){
		InitSubtitleCodec();
	}
	if (flag) InitMsgQueues();
}

VideoCapture::~VideoCapture(){
	Close();
}

int VideoCapture::NextPacket(AVPacket &pkt){
	int rc = 0;
	av_init_packet(&pkt);
	while (true){
		rc = av_read_frame(fmt_ctx, &pkt);
		if (rc == AVERROR(EAGAIN)) continue;
		if (rc < 0) break;
		if (pkt.stream_index == video_stream) break;
	}
	return rc;
}

uint32_t VideoCapture::CountVideoPackets(){
	int rc;
	char msg[64];
	char submsg[32];
	uint32_t count = 0;
	AVPacket pkt;
	while (true){
		rc = av_read_frame(fmt_ctx, &pkt);
		if (rc == AVERROR(EAGAIN)) continue;
		if (rc == AVERROR_EOF) break;
		if (rc < 0){
			av_strerror(AVERROR(rc), submsg, sizeof(submsg));
			snprintf(msg, sizeof(msg), "unable to read packet: %s", submsg);
			throw VideoCaptureException(string(msg));
		}
		if (pkt.stream_index == video_stream) 
			count++;
		av_free_packet(&pkt);
	}

	avio_flush(fmt_ctx->pb);
	if ((rc = avformat_flush(fmt_ctx)) < 0){
		av_strerror(AVERROR(rc), submsg, sizeof(submsg));
		snprintf(msg, sizeof(msg), "unable to flush format context: %s", submsg);
		throw VideoCaptureException(string(msg));
	}
	if ((rc = av_seek_frame(fmt_ctx, video_stream, 0, AVSEEK_FLAG_BACKWARD)) < 0){
		av_strerror(AVERROR(rc), submsg, sizeof(submsg));
		snprintf(msg, sizeof(msg), "unable to seek to start of file: %s", submsg);
		throw VideoCaptureException(string(msg));
	}
	return count;
}

void VideoCapture::Process(int64_t secs){
	AVRational fr = fmt_ctx->streams[video_stream]->avg_frame_rate;
	int64_t frame_count = 0;
	int64_t total_frames = av_rescale(secs, fr.num, fr.den);
	
	AVPacket pkt, pkt0;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	av_init_packet(&pkt0);
	pkt0.data = NULL;
	pkt0.size = 0;
	int rc;
	bool done = false;
	while (!done){
		if (pkt0.data == NULL){
			if ((rc = av_read_frame(fmt_ctx, &pkt)) < 0){
				if (rc == AVERROR(EAGAIN))continue;
				if (rc == AVERROR_EOF){
					FlushFrames();
					break;
				}
				throw VideoCaptureException("unable to read packet");
			}
			pkt0 = pkt;
		}
		if (pkt.stream_index == video_stream){
			HandleVideoPacket(pkt);
			frame_count++;
		} else if (pkt.stream_index == audio_stream){
			HandleAudioPacket(pkt);
		} else if (pkt.stream_index == subtitle_stream){
			HandleSubtitlePacket(pkt);
		} else {
			av_free_packet(&pkt0);
		}
		if (pkt.size <= 0){
			av_free_packet(&pkt0);
		}
		if (secs > 0 && frame_count >= total_frames){
			FlushFrames();
			done = true;
			av_free_packet(&pkt0);
		}
	}
}

AVFrame* VideoCapture::PullVideoFrame(){
	if (video_frames_queue == NULL) return NULL;
	char msg[64];
	AVFrame *frame = NULL;
	while (true){
		int rc = av_thread_message_queue_recv(video_frames_queue, &frame,
											  AV_THREAD_MESSAGE_NONBLOCK);
		if (AVERROR(rc) == EAGAIN) continue;
		if (rc == AVERROR_EOF) break;
		if (rc < 0) {
			av_strerror(rc, msg, sizeof(msg));
			throw VideoCaptureException(string(msg));
		}
		break;
	}
	return frame;
}


AVFrame* VideoCapture::PullVideoKeyFrame(){
	AVFrame *frame = NULL;
	while ((frame = PullVideoFrame()) != NULL){
		if (frame->key_frame) break;
		av_frame_free(&frame);
	}
	return frame;
}

int VideoCapture::PullAudioSamples(int16_t buf[], int buffer_length){
	if (adec_ctx == NULL || flt_fmt) return -1;
	int pos = 0;
	int16_t *samples = circ_buf.s16samples;
	bool again = true;
	while (again){
		while (audio_consumer_flag.test_and_set(memory_order_acquire));
		unsigned long head = circ_buf.head.load(memory_order_acquire);
		unsigned long tail = circ_buf.tail.load(memory_order_acquire);
		unsigned long nelems = CIRC_CNT(head, tail, CircBufferSize);
		if (nelems > 0){
		    int limit = ((int)nelems < buffer_length) ? (int)nelems : buffer_length;
			while (pos < limit){
				buf[pos] = samples[tail];
				tail = (tail) & (CircBufferSize - 1);
				pos++;
				tail++;
			}
			circ_buf.tail.store(tail, memory_order_release);
		}
		audio_consumer_flag.clear(memory_order_release);
		if (pos >= buffer_length || stop_flag.load(memory_order_acquire)){
			again = false;
		}
	}
	return pos;
}

int VideoCapture::PullAudioSamples(float buf[], int buffer_length){
	if (adec_ctx == NULL || !flt_fmt) return -1;
	int pos = 0;
	float *samples = circ_buf.fltsamples;
	bool again = true;
	while (again){
		while (audio_consumer_flag.test_and_set(memory_order_acquire));;
		unsigned long head = circ_buf.head.load(memory_order_acquire);
		unsigned long tail = circ_buf.tail.load(memory_order_relaxed);
		unsigned long nelems = CIRC_CNT(head, tail, CircBufferSize);
		if ((int)nelems > 0){
			int limit = ((int)nelems < buffer_length) ? (int)nelems : buffer_length;
			while (pos < limit){
				buf[pos] = samples[tail];
				tail = (tail) & (CircBufferSize - 1);
				pos++;
				tail++;
			}
			circ_buf.tail.store(tail, memory_order_release);
		}
		audio_consumer_flag.clear(memory_order_release);
		if (pos >= buffer_length || stop_flag.load(memory_order_acquire)){
			again = false;
		}
	}
	return pos;
}

AVSubtitle* VideoCapture::PullSubtitle(){
	if (subtitle_queue == NULL) return NULL;
	char msg[64];
	AVSubtitle *sub = NULL;
	while (true){
		int rc = av_thread_message_queue_recv(subtitle_queue, &sub,
											  AV_THREAD_MESSAGE_NONBLOCK);
		if (AVERROR(rc) == EAGAIN) continue;
		if (rc == AVERROR_EOF)break;
		if (rc < 0) {
			av_strerror(rc, msg, sizeof(msg));
			throw VideoCaptureException(string(msg));
		}
		break;
	}
	return sub;
}

AVRational VideoCapture::GetVideoTimebase(){
	AVRational result;
	result.num = 0;
	result.den = 0;
	if (video_stream >= 0)
		result = buffersink_ctx->inputs[0]->time_base;
	return result;
}

AVRational VideoCapture::GetAudioTimebase(){
	AVRational result;
	result.num = 0;
	result.den = 0;
	if (audio_stream >= 0)
		result = abuffersink_ctx->inputs[0]->time_base;
	return result;
}

AVRational VideoCapture::GetAvgFrameRate(){
	if (video_stream >= 0)
		return av_buffersink_get_frame_rate(buffersink_ctx);
	return av_make_q(0,0);
}

double VideoCapture::GetAvgFrameRate_d(){
	if (video_stream >= 0)
		return av_q2d(av_buffersink_get_frame_rate(buffersink_ctx));
	return 0;
}

int VideoCapture::GetAudioSampleRate(){
	return sr;
}

int VideoCapture::GetNumberStreams(){
	return fmt_ctx->nb_streams;
}

int VideoCapture::GetNumberPrograms(){
	return fmt_ctx->nb_programs;
}

MetaData& VideoCapture::GetMetaData(){
	return metadata;
}

void VideoCapture::Close(){
	if (adec_ctx != NULL){
		delete[] circ_buf.fltsamples;
	}
    avcodec_close(dec_ctx);
	avcodec_close(adec_ctx);
	avcodec_close(subdec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&pframe_decoded);
	av_frame_free(&pframe_filtered);
	av_frame_free(&pframeAu);
	av_frame_free(&pframeAufiltered);
    avfilter_graph_free(&filter_graph);
	avfilter_graph_free(&afilter_graph);
	av_thread_message_queue_free(&video_frames_queue);
	av_thread_message_queue_free(&subtitle_queue);
}

