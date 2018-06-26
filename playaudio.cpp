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

#include "playaudio.hpp"

snd_pcm_t* audio_init(const string hwdevice, int &sr, int &bufsize){
	snd_pcm_t *pcm_handle = NULL;
	snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
	snd_pcm_hw_params_t *hwparams;
	char *pcm_name = strdup(hwdevice.c_str());

	if (snd_pcm_hw_params_malloc(&hwparams) < 0)
		return NULL;
	
	if (snd_pcm_open(&pcm_handle, pcm_name, stream, 0) < 0)
		return NULL;

	if (snd_pcm_hw_params_any(pcm_handle, hwparams) < 0)
		return NULL;
	
	if (snd_pcm_hw_params_set_access(pcm_handle, hwparams, SND_PCM_ACCESS_RW_NONINTERLEAVED) < 0)
		return NULL;

	if (snd_pcm_hw_params_set_format(pcm_handle, hwparams, SND_PCM_FORMAT_S16_LE) < 0)
		return NULL;

	unsigned int exact_sr = sr;
	int dir;
	if (snd_pcm_hw_params_set_rate_near(pcm_handle, hwparams, &exact_sr, &dir) < 0)
		return NULL;
	sr = exact_sr;

	if (snd_pcm_hw_params_set_channels(pcm_handle, hwparams, 1) < 0)
		return NULL;

	snd_pcm_uframes_t period_size = bufsize;
	if (snd_pcm_hw_params_set_period_size_near(pcm_handle, hwparams, &period_size, &dir) < 0)
		exit(EXIT_FAILURE);
	
	int rc;
	if ((rc = snd_pcm_hw_params(pcm_handle, hwparams)) < 0)
		return NULL;

	snd_pcm_hw_params_get_period_size(hwparams, &period_size, &dir);
	bufsize = period_size;
	
	snd_pcm_hw_params_free(hwparams);

	return pcm_handle;
}

int play_samples(snd_pcm_t *pcm_handle, int16_t *buf, int length){
	if (pcm_handle == NULL) return -1;
	snd_pcm_sframes_t nframes = 0;
	do {
		nframes = snd_pcm_writen(pcm_handle, (void**)&buf, length);
		if (nframes < 0) snd_pcm_prepare(pcm_handle);
	} while (nframes < 0);
	return nframes;
}


void close_audio(snd_pcm_t *pcm_handle){
	if (pcm_handle)	snd_pcm_close(pcm_handle);
}
