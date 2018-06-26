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

#include <cstdlib>
#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include "VideoCapture.hpp"

#ifdef USE_ASOUNDLIB
#include "playaudio.hpp"
#endif

#include <opencv2/core/core_c.h>
#include <opencv2/highgui/highgui_c.h>

using namespace std;

void pause(int64_t current_pts, int64_t &last_pts, AVRational time_base){
	if (current_pts != AV_NOPTS_VALUE){
		if (last_pts != AV_NOPTS_VALUE){
			int64_t lag = current_pts - last_pts;
			int64_t delay = av_rescale_q(lag, time_base, AV_TIME_BASE_Q);
			if (delay > 0){
				usleep(delay);
			}
		}
		last_pts = current_pts;
	}
}

void print_metadata(ph::MetaData &mdata){
	cout << "artist: " << mdata.artist_str << endl;
	cout << "title: " << mdata.title_str << endl;
	cout << "album: " << mdata.album_str << endl;
	cout << "genre: " << mdata.genre_str << endl;
	cout << "composer: " << mdata.composer_str << endl;
	cout << "performer: " << mdata.performer_str << endl;
	cout << "album artist: " << mdata.album_artist_str << endl;
	cout << "copyright: " << mdata.copyright_str << endl;
	cout << "date: " << mdata.date_str << endl;
	cout << "track: " << mdata.track_str << endl;
	cout << "disc: " << mdata.disc_str << endl;
}

void process_timestamp(int64_t ts, AVRational tb, int &hrs, int &mins, int &secs){
	int64_t seconds = av_rescale(ts, tb.num, tb.den);
	hrs  =  seconds/3600;
	int64_t remaining_secs = seconds%3600;
	mins = remaining_secs/60;
	secs = remaining_secs%60;
}


void process_video(ph::VideoCapture *vc){
	int count = 0;
	int64_t last_pts = AV_NOPTS_VALUE;
	AVRational time_base = vc->GetVideoTimebase();
	AVFrame *frame = vc->PullVideoFrame();
	if (frame == NULL) return;

	cvNamedWindow("main", CV_WINDOW_AUTOSIZE);

	CvSize sz;
	sz.width = frame->width;
	sz.height = frame->height;
	IplImage *img = cvCreateImageHeader(sz, IPL_DEPTH_8U, 1);
	assert(img);
	int64_t start_ts = frame->pts, ts;
	while (frame != NULL){
		ts = frame->pts;
		cvSetData(img, frame->data[0], frame->linesize[0]);
		pause(frame->pts, last_pts, time_base);
		cvShowImage("main", img);
		cvWaitKey(10);
		count++;
		av_frame_free(&frame);
		frame = vc->PullVideoFrame();
	}
	int hrs, mins, secs;
	process_timestamp(ts - start_ts, time_base, hrs, mins, secs);
	cout << "video frames processed " << count << " in " << hrs << ":" << mins << ":" << secs << endl;
	cvDestroyAllWindows();
}

void process_audio(ph::VideoCapture *vc){
	const string hwdev = "plughw:0,0";
	int nbsamples;
	int total = 0;
	int count = 0;
	int buffer_size = 1024;

#ifdef USE_ASOUNDLIB
	int sr = vc->GetAudioSampleRate();
	snd_pcm_t *pcm_handle = audio_init(hwdev, sr, buffer_size);
	cout << "buffer size " << buffer_size << endl;
#endif
#ifdef USE_ASOUNDLIB
	assert(pcm_handle);
#endif
	int N = buffer_size;
	int16_t *buf = new int16_t[N];
	while ((nbsamples = vc->PullAudioSamples(buf, N)) > 0){
		count++;
		total += nbsamples;
#ifdef USE_ASOUNDLIB
		play_samples(pcm_handle, buf, nbsamples);
#endif
	}
	cout << "no. audio buffers: " << count << endl;
	cout << "no. audio samples: " << total << endl;
	
#ifdef USE_ASOUNDLIB
	close_audio(pcm_handle);
#endif
	delete[] buf;
}

void process_subs(ph::VideoCapture *vc){
	AVSubtitle *sub = NULL;
	int count = 0;
	while ((sub = vc->PullSubtitle()) != NULL){
		for (unsigned int i=0;i<sub->num_rects;i++){
			cout << "(" << sub->pts << ") " << string(sub->rects[i]->ass) << endl;
		}
		count++;
		avsubtitle_free(sub);
		free(sub);
	}
	cout << "no. subtitles: " << count << endl;
}

void process_main(ph::VideoCapture *vc, int64_t secs){
	assert(vc != NULL);
	try {
		vc->Process(secs);
	} catch (ph::VideoCaptureException &ex){
		cout << "VC Exception: unable to process video: " << ex.what() << endl;
	} catch (ph::AudioCaptureException &ex){
		cout << "AC Exception: unable to process audio: " << ex.what() << endl;
	}
}


int main(int argc, char **argv){
 	if (argc < 4){
		cout << "not enough args." << endl;
 		cout << "usage: prog filename secs fps" << endl;
 		return 0;
 	}
 	const string filename = argv[1];
 	const int margin = 0;
 	const int sr = 8000;
	const int64_t secs = atoi(argv[2]);
	const int fps = atoi(argv[3]);
 	const int width = 400;
	const bool warn = true;
	
 	cout << "file: " << filename << endl;
	cout << "margin: " << margin << endl;
	cout << "no. seconds to play: " << secs << endl;
	cout << "width: " << width << endl;
	cout << "fps: " << fps << endl;
	
	thread video_thr;
	thread audio_thr;
	thread subtitle_thr;
	thread main_thr;

	int flag = PHCAPTURE_ALL_FLAG;
	int audio_fmt = PHAUDIO_S16_FMT; /** PHAUDIO_FLT_FMT, PHAUDIO_S16_FMT **/
	try {
		cout << "initialize video capture with flag: " << flag<< endl;
		ph::VideoCapture *vc = new ph::VideoCapture(filename, margin, margin,
													margin, margin, sr, width, flag, audio_fmt, fps, warn);

		cout << "no. streams: " << vc->GetNumberStreams() << endl;
		cout << "no. programs: " << vc->GetNumberPrograms() << endl;

		ph::MetaData mdata = vc->GetMetaData();
		print_metadata(mdata);

		uint32_t nbframes = vc->CountVideoPackets();
		double fr = vc->GetAvgFrameRate_d();
		cout << "no. video frames: " << nbframes << endl;
		cout << "avg frame rate: " << fr << endl;
		
		main_thr = thread(process_main, vc, secs);
		video_thr = thread(process_video, vc);
		audio_thr = thread(process_audio, vc);
		subtitle_thr = thread(process_subs, vc);
		main_thr.join();
		video_thr.join();
		audio_thr.join();
		subtitle_thr.join();
		delete vc;
	} catch (ph::VideoCaptureException &ex){
		cout << "vc error: " << ex.what() << endl;
	} catch (ph::AudioCaptureException &ex){
		cout << "vc audio error: " << ex.what() << endl;
	}
	cout << "done." << endl;
	return 0;
}
