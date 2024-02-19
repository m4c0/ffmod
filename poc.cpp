#pragma leco tool
import casein;
import ffmod;
import vee;
import voo;

static constexpr const auto filename = "movie.mov";

class thread : public voo::casein_thread {
public:
  void run() override {
    voo::device_and_queue dq{"ffmod", native_ptr()};

    auto fmt_ctx = ffmod::avformat_open_input(filename);
    ffmod::avformat_find_stream_info(fmt_ctx);

    auto vctx = ffmod::avcodec_open_best(fmt_ctx, AVMEDIA_TYPE_VIDEO);
    auto actx = ffmod::avcodec_open_best(fmt_ctx, AVMEDIA_TYPE_AUDIO);

    auto pkt = ffmod::av_packet_alloc();

    voo::h2l_image frm;

    while (!interrupted()) {
      voo::swapchain_and_stuff sw{dq};

      extent_loop(dq, sw, [&] {
        // From FFMPEG docs: "For video, the packet contains exactly one frame.
        // For audio, it contains an integer number of frames if each frame has
        // a known fixed size (e.g. PCM or ADPCM data). If the audio frames have
        // a variable size (e.g. MPEG audio), then it contains one frame."

        sw.queue_one_time_submit(dq, [&](auto pcb) {
          // frm.setup_copy(*pcb);

          auto scb = sw.cmd_render_pass(pcb);
          ;
        });
      });
    }
  }
};

extern "C" void casein_handle(const casein::event &e) {
  static thread t{};
  t.handle(e);
}
