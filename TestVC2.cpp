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
#include "VideoCapture.hpp"

using namespace std;

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

int main(int argc, char **argv){
 	if (argc < 2){
		cout << "not enough args." << endl;
 		cout << "usage: prog filename" << endl;
 		return 0;
 	}
 	const string filename = argv[1];
 	cout << "file: " << filename << endl;

	try {
		ph::VideoCapture vc(filename);
		ph::MetaData mdata = vc.GetMetaData();
		print_metadata(mdata);
		
		int rc, count = 0;;
		AVPacket pkt;
		while ((rc = vc.NextPacket(pkt)) >= 0){
			cout << "(video pkt " << count++ << ") pkt size: " << pkt.size << endl; 
			av_packet_unref(&pkt);
		}
		cout << "no. packets: " << count << endl;
	} catch (ph::VideoCaptureException &ex){
		cout << "vc error: " << ex.what() << endl;
	} catch (ph::AudioCaptureException &ex){
		cout << "vc audio error: " << ex.what() << endl;
	}

	cout << "done." << endl;
	return 0;
}
