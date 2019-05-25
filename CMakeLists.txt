cmake_minimum_required(VERSION 3.0)

project(phvideocapture)

set(phvideocapture_VERSION_MAJOR 0)
set(phvideocapture_VERSION_MINOR 0)
set(phvideocapture_VERSION_PATCH 1)

option(WITH_ASNDLIB "compile with alsa libasound lib support" ON)

set(FFMPEG_DIR "/usr/local"  CACHE STRING "ffmpeg libav* library location")
set(OPENCV_DIR "/usr/local/share/OpenCV"  CACHE STRING "opencv libs")

set(ffmpeglibs avformat avcodec avutillib avfilter swscale)

if (FFMPEG_DIR)
  find_library(avformatlib avformat PATHS ${FFMPEG_DIR}/lib NO_DEFAULT_PATH)
else()
  find_library(avformatlib avformat)
endif()

if (${avformatlib} STREQUAL avformatlib-NOTFOUND)
  message(FATAL_ERROR "libavformat not found. Get FFMPEG libavformat >= v56.25.101")
else()
  message(STATUS "Found ${avformatlib}")
endif()

if (FFMPEG_DIR)
  find_library(avcodeclib avcodec PATHS ${FFMPEG_DIR}/lib NO_DEFAULT_PATH)
else()
find_library(avcodeclib avcodec)
endif()

if (${avcodeclib} STREQUAL avcodeclib-NOTFOUND)
  message(FATAL_ERROR "libavcodec not found. Get FFMPEG libavcodec >= v56.26.100")
else()
  message(STATUS "Found ${avcodeclib}")
endif()

if (FFMPEG_DIR)
  find_library(avutillib avutil PATHS ${FFMPEG_DIR}/lib NO_DEFAULT_PATH)
else()
  find_library(avutillib avutil)
endif()

if (${avutillib} STREQUAL avutillib-NOTFOUND)
  message(FATAL_ERROR "libavutil not found. Get FFMPEG libutil  >= v54.20.100")
else()
  message(STATUS "Found ${avutillib}")
endif()

if(FFMPEG_DIR)
  find_library(avfilterlib avfilter PATHS ${FFMPEG_DIR}/lib NO_DEFAULT_PATH)
else()
find_library(avfilterlib avfilter)
endif()

if (${avfilterlib} STREQUAL avfilterlib-NOTFOUND)
  message(FATAL_ERROR "libavfilter not found. Get FFMPEG libavfilter >= v5.11.102")
else()
  message(STATUS "Found ${avfilterlib}")
endif()

if (FFMPEG_DIR)
  find_library(swscalelib swscale PATHS ${FFMPEG_DIR}/lib NO_DEFAULT_PATH)
else()
  find_library(swscalelib swscale)
endif()

if (${swscalelib} STREQUAL swscalelib-NOTFOUND)
  message(FATAL_ERROR "libswscale not found. Get FFMPEG libswscale >= 3.1.101")
else()
  message(STATUS "Found ${swscalelib}")
endif()

if (WITH_ASNDLIB)
  find_library(asoundlib asound)
  if (${asoundlib} STREQUAL asoundlib-NOTFOUND)
	message(FATAL_ERROR "ALSA asound library not found. (Optional). libasound >= v1.0.28")
  else()
	message(STATUS "Found ${asoundlib}")
  endif()
endif()

if (OPENCV_DIR)
  find_package(OpenCV 3.1.0 COMPONENTS core highgui imgproc PATHS ${OPENCV_DIR} NO_DEFAULT_PATH)
else()
  find_package(OpenCV 3.1.0 COMPONENTS core highgui imgproc)
endif()
set(OpenCV_3RDPARTY_LIB_DIR "${OpenCV_INSTALL_PATH}/share/OpenCV/3rdparty/lib")

if (NOT OpenCV_FOUND)
  message(FATAL_ERROR "OpenCV not found. Get OpenCV version >= 3.1.0")
else()
  message(STATUS "Found OpenCV ${OpenCV_INSTALL_PATH}")
  message(STATUS "      3rd party libs ${OpenCV_3RDPARTY_LIB_DIR}")
endif()

find_package(GTK2 2.24)
if (NOT GTK2_FOUND)
  message(FATAL_ERROR "GTK2 >=2.24 not found.")
else()
  message(STATUS "Found GTK2 at ${GTK2_LIBRARIES}")
endif()
  
configure_file (
  "${PROJECT_SOURCE_DIR}/VideoCaptureConfig.hpp.in"
  "${PROJECT_BINARY_DIR}/VideoCaptureConfig.hpp"
  )


add_library(phvideocapture SHARED VideoCapture.cpp)
set_property(TARGET phvideocapture APPEND PROPERTY COMPILE_FLAGS "-std=c++11")
set_property(TARGET phvideocapture PROPERTY PUBLIC_HEADER VideoCapture.hpp)
set_property(TARGET phvideocapture APPEND PROPERTY INCLUDE_DIRECTORIES ${FFMPEG_DIR}/include)
target_link_libraries(phvideocapture ${avformatlib} ${avcodeclib} ${avutillib} ${avfilterlib} ${swscalelib})

add_library(phvideocapture-static STATIC VideoCapture.cpp)
set_property(TARGET phvideocapture-static APPEND PROPERTY COMPILE_FLAGS "-std=c++11")
set_property(TARGET phvideocapture-static PROPERTY PUBLIC_HEADER VideoCapture.hpp)
set_property(TARGET phvideocapture-static APPEND PROPERTY INCLUDE_DIRECTORIES ${FFMPEG_DIR}/include)
set_property(TARGET phvideocapture-static PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(phvideocapture-static ${avformatlib} ${avcodeclib} ${avutillib} ${avfilterlib} ${swscalelib})

#tests
if (WITH_ASNDLIB)
  set(TestVC_SOURCES TestVC.cpp playaudio.cpp)
else()
  set(TestVC_SOURCES TestVC.cpp)
endif()

add_executable(testvc ${TestVC_SOURCES})
set_property(TARGET testvc APPEND PROPERTY COMPILE_FLAGS "-std=c++11")
set_property(TARGET testvc APPEND PROPERTY INCLUDE_DIRECTORIES ${OPENCV_DIR})

target_link_libraries(testvc phvideocapture-static pthread)
target_link_libraries(testvc ${avformatlib} ${avcodeclib}
  ${avfilterlib} ${swscalelib} ${avutillib} ${asoundlib})
target_link_libraries(testvc ${OpenCV_LIBS})


add_executable(testvc2 TestVC2.cpp)
set_property(TARGET testvc2 APPEND PROPERTY COMPILE_FLAGS "-g -O0 -Wall -std=c++11")
target_link_libraries(testvc2 phvideocapture-static)
target_link_libraries(testvc2 ${avformatlib} ${avcodeclib} ${avutillib} ${avfilterlib} ${swscalelib})
add_executable(testcircbuf testcircbuf.cpp)
set_property(TARGET testcircbuf APPEND PROPERTY COMPILE_FLAGS "-g -Wall -std=c++11")
target_link_libraries(testcircbuf pthread)

install(TARGETS phvideocapture LIBRARY DESTINATION lib PUBLIC_HEADER DESTINATION include)
install(TARGETS testvc DESTINATION bin)
install(TARGETS phvideocapture-static ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include)

#build cpack installer package
include (InstallRequiredSystemLibraries)
set(CPACK_GENERATOR "TGZ")
set (CPACK_RESOURCE_FILE_LICENSE
        "${CMAKE_CURRENT_SOURCE_DIR}/License.txt")
set (CPACK_PACKAGE_VERSION_MAJOR "${phvideocapture_VERSION_MAJOR}")
set (CPACK_PACKAGE_VERSION_MINOR "${phvideocapture_VERSION_MINOR}")
set (CPACK_PACKAGE_VERSION_PATCH "${phvideocapture_VERSION_PATCH}")
include (CPack)
