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
import hai;
import silog;

namespace ffmod {
struct deleter {
  // void operator()(AVCodecContext *c) { avcodec_free_context(&c); }
  void operator()(AVFormatContext *c) { avformat_close_input(&c); }
  // void operator()(AVFrame *c) { av_frame_free(&c); }
  // void operator()(AVPacket *c) { av_packet_free(&c); }
};
struct unref {
  // void operator()(AVFrame *c) { av_frame_unref(c); }
  // void operator()(AVPacket *c) { av_packet_unref(c); }
};

inline void assert(bool cond, const char *msg) {
  silog::assert(cond, msg);
  if (!cond)
    throw 0;
}
inline int assert_p(int i, const char *msg) {
  assert(i >= 0, msg);
  return i;
}

using fmt_ctx = hai::holder<AVFormatContext, deleter>;
export auto avformat_open_input(const char *filename) {
  fmt_ctx res{};
  assert_p(avformat_open_input(&*res, filename, nullptr, nullptr),
           "Failed to read input file");
  return res;
}
export void avformat_find_stream_info(fmt_ctx &ctx) {
  assert_p(avformat_find_stream_info(*ctx, nullptr),
           "Could not find stream info");
}
} // namespace ffmod
