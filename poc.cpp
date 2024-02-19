#pragma leco tool
import casein;
import ffmod;
import vee;
import voo;

static constexpr const auto filename = "movie.mov";

class thread : public voo::casein_thread {
  void copy_frame(voo::h2l_yuv_image *img, ffmod::frame &frm, unsigned w,
                  unsigned h) {
    voo::mapmem y{img->host_memory_y()};
    auto *yy = static_cast<unsigned char *>(*y);
    for (auto y = 0; y < h; y++) {
      for (auto x = 0; x < w; x++) {
        *yy++ = (*frm)->data[0][y * (*frm)->linesize[0] + x];
      }
    }

    voo::mapmem u{img->host_memory_u()};
    auto *uu = static_cast<unsigned char *>(*u);
    voo::mapmem v{img->host_memory_v()};
    auto *vv = static_cast<unsigned char *>(*v);
    for (auto y = 0; y < h / 2; y++) {
      for (auto x = 0; x < w / 2; x++) {
        *uu++ = (*frm)->data[1][y * (*frm)->linesize[1] + x];
        *vv++ = (*frm)->data[2][y * (*frm)->linesize[2] + x];
      }
    }
  }

public:
  void run() override {
    voo::device_and_queue dq{"ffmod", native_ptr()};

    auto fmt_ctx = ffmod::avformat_open_input(filename);
    ffmod::avformat_find_stream_info(fmt_ctx);

    // TODO: detect stream type instead of guessing
    // This allows usage of files with multiple streams (ex: OBS with two audio
    // tracks)

    auto vidx = ffmod::av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO);

    auto vctx = ffmod::avcodec_open_best(fmt_ctx, AVMEDIA_TYPE_VIDEO);
    auto actx = ffmod::avcodec_open_best(fmt_ctx, AVMEDIA_TYPE_AUDIO);

    auto pkt = ffmod::av_packet_alloc();
    auto frm = ffmod::av_frame_alloc();

    auto w = static_cast<unsigned>((*vctx)->width);
    auto h = static_cast<unsigned>((*vctx)->height);
    voo::h2l_yuv_image frm_buf{dq, w, h};

    while (!interrupted()) {
      voo::swapchain_and_stuff sw{dq};

      extent_loop([&] {
        // From FFMPEG docs: "For video, the packet contains exactly one frame.
        // For audio, it contains an integer number of frames if each frame has
        // a known fixed size (e.g. PCM or ADPCM data). If the audio frames have
        // a variable size (e.g. MPEG audio), then it contains one frame."
        ffmod::packet_ref pkt_ref{};
        while (!interrupted() && *(pkt_ref = ffmod::av_read_frame(fmt_ctx, pkt))) {
          // skip audio deocding
          if ((*pkt)->stream_index != vidx) {
            continue;
          }

          ffmod::avcodec_send_packet(vctx, pkt);
          ffmod::frame_ref frm_ref{};
          while (!interrupted() &&
                 *(frm_ref = ffmod::avcodec_receive_frame(vctx, frm))) {
            copy_frame(&frm_buf, frm, w, h);

            sw.acquire_next_image();
            sw.queue_one_time_submit(dq, [&](auto pcb) {
              frm_buf.setup_copy(*pcb);

              auto scb = sw.cmd_render_pass(pcb);
              ;
            });
            sw.queue_present(dq);
          }
        }
      });
      dq.device_wait_idle();
    }
  }
};

extern "C" void casein_handle(const casein::event &e) {
  static thread t{};
  t.handle(e);
}
