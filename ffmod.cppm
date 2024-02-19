module;
#pragma leco add_include_dir "ffmpeg/include"
#pragma leco add_library_dir "ffmpeg/lib"
#pragma leco add_library "avcodec"
#pragma leco add_library "avformat"
#pragma leco add_library "avutil"

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
  void operator()(AVCodecContext *c) { avcodec_free_context(&c); }
  void operator()(AVFormatContext *c) { avformat_close_input(&c); }
  void operator()(AVFrame *c) { av_frame_free(&c); }
  void operator()(AVPacket *c) { av_packet_free(&c); }
};
struct unref {
  void operator()(AVFrame *c) { av_frame_unref(c); }
  void operator()(AVPacket *c) { av_packet_unref(c); }
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

export using codec_ctx = hai::holder<AVCodecContext, deleter>;
export using fmt_ctx = hai::holder<AVFormatContext, deleter>;
export using frame = hai::holder<AVFrame, deleter>;
export using packet = hai::holder<AVPacket, deleter>;

export using frame_ref = hai::holder<AVFrame, unref>;
export using packet_ref = hai::holder<AVPacket, unref>;

export auto avformat_open_input(const char *filename) {
  fmt_ctx res{};
  assert_p(avformat_open_input(&*res, filename, nullptr, nullptr),
           "Failed to read input file");

  av_dump_format(*res, 0, filename, 0);
  return res;
}

export void avformat_find_stream_info(fmt_ctx &ctx) {
  assert_p(avformat_find_stream_info(*ctx, nullptr),
           "Could not find stream info");
}

export auto av_find_best_stream(fmt_ctx &ctx, AVMediaType mt) {
  auto idx = av_find_best_stream(*ctx, mt, -1, -1, nullptr, 0);
  assert_p(idx, "Could not find stream");
  return idx;
}

export auto avcodec_open_best(fmt_ctx &ctx, AVMediaType mt) {
  auto idx = av_find_best_stream(ctx, mt);
  auto st = (*ctx)->streams[idx];
  assert(st, "Missing stream");
  auto dec = avcodec_find_decoder(st->codecpar->codec_id);
  assert(dec, "Could not find codec");

  codec_ctx res{avcodec_alloc_context3(dec)};
  assert(*res, "Could not allocate codec context");
  assert_p(avcodec_parameters_to_context(*res, st->codecpar),
           "Could not copy codec parameters");
  assert_p(avcodec_open2(*res, dec, nullptr), "Could not open codec");

  return res;
}

export auto av_packet_alloc() { return packet{::av_packet_alloc()}; }
export auto av_frame_alloc() { return frame{::av_frame_alloc()}; }

export auto av_read_frame(fmt_ctx &ctx, packet &pkt) {
  if (av_read_frame(*ctx, *pkt) < 0) {
    return packet_ref{};
  }
  return packet_ref{*pkt};
}

export void avcodec_send_packet(codec_ctx &ctx, packet &pkt) {
  assert_p(avcodec_send_packet(*ctx, *pkt), "Error sending packet to decode");
}

export auto avcodec_receive_frame(codec_ctx &ctx, frame &frm) {
  auto err = avcodec_receive_frame(*ctx, *frm);
  if (err >= 0)
    return frame_ref{*frm};
  if (err == AVERROR_EOF || AVERROR(EAGAIN))
    return frame_ref{};
  assert_p(err, "Error decoding frame");
}

void log_callback(void *avc, int lvl, const char *fmt, va_list args) {
  // TODO: support level and args
  silog::log(silog::debug, "%s", fmt);
}

struct init {
  init() { av_log_set_callback(log_callback); }
} i{};
} // namespace ffmod
