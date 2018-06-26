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

#ifndef _VIDEOCAPTURE_H
#define _VIDEOCAPTURE_H

#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/avfiltergraph.h>
#include <libavutil/threadmessage.h>
#include <libavutil/opt.h>
#include <libavutil/dict.h>
#include <libavutil/mathematics.h>
};

using namespace std;

#define PHCAPTURE_RAW_FLAG 0x0000
#define PHCAPTURE_VIDEO_FLAG 0x0001
#define PHCAPTURE_AUDIO_FLAG 0X0002
#define PHCAPTURE_SUBTITLE_FLAG 0x0004
#define PHCAPTURE_VIDEOAUDIO_FLAG 0x0003
#define PHCAPTURE_VIDEOSUBTITLE_FLAG 0x0005
#define PHCAPTURE_AUDIOSUBTITLE_FLAG 0x0006
#define PHCAPTURE_ALL_FLAG 0x0007

#define PHAUDIO_S16_FMT 0x0000
#define PHAUDIO_FLT_FMT 0x0001


namespace ph {

typedef struct Metadata {
	string title_str;
	string artist_str;
	string album_str;
	string genre_str;
	string composer_str;
	string performer_str;
	string album_artist_str;
	string copyright_str;
	string date_str;
	string track_str;
	string disc_str;
} MetaData;

typedef struct circ_buf{
	union {
		float *fltsamples;
		int16_t *s16samples;
	};
	atomic_ulong head;
	atomic_ulong tail;
} CircBuffer;

const int CircBufferSize = 0x0001 << 20;
	
/* VideoCapture class */
class VideoCapture {
protected: 
	AVFormatContext *fmt_ctx = NULL;
	AVCodecContext *dec_ctx = NULL;
	AVFrame *pframe_decoded = NULL;
	AVFrame *pframe_filtered = NULL;
	AVFilterContext *buffersink_ctx = NULL;
	AVFilterContext *buffersrc_ctx = NULL;
	AVFilterGraph *filter_graph = NULL;
	AVThreadMessageQueue *video_frames_queue = NULL;
	
	AVCodecContext *adec_ctx = NULL;
	AVFrame *pframeAu = NULL;
	AVFrame *pframeAufiltered = NULL;
	AVFilterContext *abuffersink_ctx = NULL;
	AVFilterContext *abuffersrc_ctx = NULL;
	AVFilterGraph *afilter_graph = NULL;

	CircBuffer circ_buf;
	
	AVCodecContext *subdec_ctx = NULL;
	AVThreadMessageQueue *subtitle_queue = NULL;
	
	atomic_flag audio_producer_flag = ATOMIC_FLAG_INIT;
	atomic_flag audio_consumer_flag = ATOMIC_FLAG_INIT;
	atomic_bool stop_flag;

	const int QueueCapacity = 64;
	int video_stream = -1;
	int audio_stream = -1;
	int subtitle_stream = -1;
	
	int sr = 44100;
	int flt_fmt = 0;       //audio samples format 0 for s16, 1 for float
	
	atomic_flag stop = ATOMIC_FLAG_INIT;

	MetaData metadata;

	/** init functions **/
	void RegisterInit(bool warn);
	void OpenFile(const string &file);
	void InitMetaData();
	void InitVideoCodec();
	void InitAudioCodec();
	void InitSubtitleCodec();
	void InitVideoFilters(const int tm, const int bm, const int lm, const int rm, const int width, const int dst_fps);
	void InitAudioFilters(const int sr, const int flt_fmt);
	void InitMsgQueues();

	/** aux functions **/
	void FlushFrames();
	void PushVideoFrames();               
	void HandleVideoPacket(AVPacket &pkt);
	void PushAudioFrames_flt();          
	void PushAudioFrames_s16();
	void PushAudioFrames();
	void HandleAudioPacket(AVPacket &pkt);
	void HandleSubtitlePacket(AVPacket &pkt); 
	
public:
	VideoCapture();
	/** ctor 
	 * @param file name of video file
	 * @param top_m    crop top margin
	 * @param bottom_m crop bottom margin
	 * @param left_m   crop left margin
	 * @param right_m  crop right margin
	 * @param sr       convert to sr sample rate
	 * @param width    convert to width while keeping aspect ratio
	 * @param flag     stream capture flag
	 * @param flt_fmt  audio format flag (0 for s16 integer, 1 for float - mono)
	 * @param fps      desired frame rate (0 for source framerate)
	 * @param warn     log warning errors 
	 **/
	VideoCapture(const string &filename, const int top_m=0, const int bottom_m=0,
				 const int left_m=0, const int right_m=0, const int sr=44100,
				 const int width=-1, 
				 int flag = PHCAPTURE_ALL_FLAG,
				 int flt_fmt = PHAUDIO_FLT_FMT, int fps = 0, bool warn = false);
	~VideoCapture();

	/** return raw video packet
	 *  cannot be used with any other process or pull* functions
	 *  This simply reads video packets without decoding anything
	 *  Must call av_free_packet(&pkt) when done with packet
	 *  @param pkt 
	 *  @return 0 on success, neg on eof or error
	 **/
	int NextPacket(AVPacket &pkt);

	/** count video frame packets
	 *  returns file position to beginning of file when done
	 *  @return  no. of frame video packets in file
	 *  @throws VideoCaptureException
	 **/
	uint32_t CountVideoPackets();

    /** process packets async **/
	/* @param dur - duration (in seconds) to stream - 0 for continuous*/
	/** returns at EOF                       **/
	void Process(int64_t secs = 0);

	/** pull frames from message queues**/
	/** use in another thread to successively retrieve video frames  */
	/** returns null at end of stream */
	AVFrame* PullVideoFrame();

	/** pull key frames from message queues **/
	/** use in anothe rthread to successivly retrieve video key frames */
	/** return null at end of stream **/
	AVFrame* PullVideoKeyFrame();

	/** pull audio samples from circular buffer **/
	/** use in separate thread to retrieve samples **/
	/** returns 0 at end of stream, -1 for no samples available  **/
	int PullAudioSamples(int16_t buf[], int buffer_length);
	int PullAudioSamples(float buf[], int buffer_length);


	/** pull subtitles from message queue **/
	/** use in separate thread  **/
	/** returns null at end of stream **/
	AVSubtitle* PullSubtitle();

	/** get time base for format **/
	/** AVRational.num **/
	/** AVRAtional.den **/
	AVRational GetVideoTimebase();
	AVRational GetAudioTimebase();
	AVRational GetAvgFrameRate();
	double GetAvgFrameRate_d();
	int GetAudioSampleRate();
	int GetNumberStreams();
	int GetNumberPrograms();
	MetaData& GetMetaData();

	void Close();
};


class VideoCaptureException : public runtime_error {
public:
	VideoCaptureException(const string &str) : runtime_error(str){};
	
};

class AudioCaptureException : public runtime_error {
public:
	AudioCaptureException(const string &str) : runtime_error(str){};
};

	
} //namespace ph

#endif
