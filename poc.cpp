#pragma leco tool
import casein;
import ffmod;
import voo;

class thread : public voo::casein_thread {
public:
  void run() override {
    voo::device_and_queue dq{"ffmod", native_ptr()};

    while (!interrupted()) {
      voo::swapchain_and_stuff sw{dq};

      extent_loop(dq, sw, [&] {
        sw.queue_one_time_submit(dq, [&](auto pcb) {
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
