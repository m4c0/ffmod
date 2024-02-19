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

    voo::h2l_image frm;

    while (!interrupted()) {
      voo::swapchain_and_stuff sw{dq};

      extent_loop(dq, sw, [&] {
        sw.queue_one_time_submit(dq, [&](auto pcb) {
          frm.setup_copy(*pcb);

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
