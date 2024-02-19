module;
#pragma leco add_include_dir "ffmpeg/include"
#pragma leco add_library_dir "ffmpeg/lib"
#pragma leco add_library "avcodec"
#pragma leco add_library "avformat"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
}

export module ffmod;
