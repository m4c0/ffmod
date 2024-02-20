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

export void avformat_find_stream_info(fmt_ctx &ctx) {
  assert_p(avformat_find_stream_info(*ctx, nullptr),
           "Could not find stream info");
}

export auto avformat_open_input(const char *filename) {
  fmt_ctx res{};
  assert_p(avformat_open_input(&*res, filename, nullptr, nullptr),
           "Failed to read input file");

  avformat_find_stream_info(res);
  av_dump_format(*res, 0, filename, 0);
  return res;
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

export void avcodec_flush_buffers(codec_ctx &ctx) {
  avcodec_flush_buffers(*ctx);
}

export void avformat_seek_file(fmt_ctx &ctx, double timestamp) {
  auto vtb = static_cast<int>(timestamp * static_cast<double>(AV_TIME_BASE));
  assert_p(avformat_seek_file(*ctx, -1, INT64_MIN, vtb, vtb, 0),
           "Failed to seek");
}

export auto frame_timestamp(const fmt_ctx &ctx, const frame &frm,
                            unsigned idx) {
  auto st = (*ctx)->streams[idx];
  auto t = static_cast<double>((*frm)->pts);
  auto tb = st->time_base;
  return t * av_q2d(tb);
}

export void copy_frame_yuv(const frame &frm, unsigned char *yy,
                           unsigned char *uu, unsigned char *vv) {
  auto w = (*frm)->width;
  auto h = (*frm)->height;

  for (auto y = 0; y < h; y++) {
    for (auto x = 0; x < w; x++) {
      *yy++ = (*frm)->data[0][y * (*frm)->linesize[0] + x];
    }
  }
  for (auto y = 0; y < h / 2; y++) {
    for (auto x = 0; x < w / 2; x++) {
      *uu++ = (*frm)->data[1][y * (*frm)->linesize[1] + x];
      *vv++ = (*frm)->data[2][y * (*frm)->linesize[2] + x];
    }
  }
}

// ffmpeg sends partial lines for each call. we need to track it and only submit
// when we reach a CRLF
char log_buf[10240]{};

inline auto log_level(int lvl) {
  if (lvl <= AV_LOG_ERROR)
    return silog::error;
  if (lvl <= AV_LOG_WARNING)
    return silog::warning;
  if (lvl <= AV_LOG_INFO)
    return silog::info;

  return silog::debug;
}
void log_callback(void *avc, int lvl, const char *fmt, va_list args) {
  // MS, in its infinite wisdom, expect us to pass a char[Sz] to vsnprintf_s
  // So, double-triple-quadruple copying it is
  char buf[10240];
  auto fsz = vsnprintf_s(buf, sizeof(buf), fmt, args);

  strncat_s(log_buf, sizeof(log_buf), buf, fsz);
  auto len = strlen(log_buf);

  if (buf[fsz - 1] == '\n') {
    log_buf[len - 1] = 0;

    auto l = log_level(lvl);
    // We can only do this check when printing. ffmpeg sends different levels
    // while it constructs the message
    if (l != silog::debug)
      silog::log(l, "[ffmpeg] %s", log_buf);

    log_buf[0] = 0;
  }
}

struct init {
  init() { av_log_set_callback(log_callback); }
} i{};
} // namespace ffmod
