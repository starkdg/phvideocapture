# PHVideoCapture 

  A simple api to read video/audio/subtitle frames from a video stream
  and extract available metadata.  

## Install

'''
cmake .
make
make install
'''

## Dependencies

- FFMPEG
    libavformat ( >= v56.25.101)
	libavcodec  ( >= v56.26.100)
	libavutil   ( >= v54.20.100)
	libavfilter ( >= v5.11.102)
	libswscale  ( >= v3.1.101)

- alsa libasound >= v1.0.28 (for test programs)
- OpenCV >= v3.1.0  (compiled with GTK2.0 support)
                    (for display feature used in test programs)


