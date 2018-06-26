# PHVideoCapture 

  A simple api to process video, audio or subtitle frames
  from a video stream.

  ## Features
  - Use of multiple threads to process frames asynchronously
    through message queues
  - Extraction of metadata info, such as title, artist, date, etc.
  - Ability to designate custom function to process frames

## Install

```
cmake .
make
make install
```

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


