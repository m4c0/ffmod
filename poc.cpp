#pragma leco app
#pragma leco add_shader "poc.frag"
#pragma leco add_shader "poc.vert"
import casein;
import ffmod;
import vee;
import voo;

static constexpr const auto filename = "movie.mov";

class thread : public voo::casein_thread {
  void copy_frame(voo::h2l_yuv_image *img, ffmod::frame &frm) {
    auto w = (*frm)->width;
    auto h = (*frm)->height;

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
    voo::one_quad quad{dq};

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

    auto yuv_smp = vee::create_yuv_sampler(vee::linear_sampler, frm_buf.conv());
    auto dsl = vee::create_descriptor_set_layout(
        {vee::dsl_fragment_samplers({*yuv_smp})});

    // TODO: find out why we need two samplers here
    auto dp = vee::create_descriptor_pool(1, {vee::combined_image_sampler(2)});
    auto dset = vee::allocate_descriptor_set(*dp, *dsl);
    vee::update_descriptor_set(dset, 0, frm_buf.iv());

    auto pl = vee::create_pipeline_layout({*dsl});
    auto gp = vee::create_graphics_pipeline({
        .pipeline_layout = *pl,
        .render_pass = dq.render_pass(),
        .shaders{
            voo::shader("poc.vert.spv").pipeline_vert_stage(),
            voo::shader("poc.frag.spv").pipeline_frag_stage(),
        },
        .bindings{
            quad.vertex_input_bind(),
        },
        .attributes{
            quad.vertex_attribute(0),
        },
    });

    while (!interrupted()) {
      voo::swapchain_and_stuff sw{dq};

      extent_loop([&] {
        // From FFMPEG docs: "For video, the packet contains exactly one frame.
        // For audio, it contains an integer number of frames if each frame has
        // a known fixed size (e.g. PCM or ADPCM data). If the audio frames have
        // a variable size (e.g. MPEG audio), then it contains one frame."
        while (!interrupted()) {
          auto pkt_ref = ffmod::av_read_frame(fmt_ctx, pkt);
          if (!*pkt_ref)
            break;

          // skip audio deocding
          if ((*pkt)->stream_index != vidx) {
            continue;
          }

          ffmod::avcodec_send_packet(vctx, pkt);
          while (!interrupted()) {
            auto frm_ref = ffmod::avcodec_receive_frame(vctx, frm);
            if (!*frm_ref)
              break;

            copy_frame(&frm_buf, frm);

            sw.acquire_next_image();
            sw.queue_one_time_submit(dq, [&](auto pcb) {
              frm_buf.setup_copy(*pcb);

              auto scb = sw.cmd_render_pass(pcb);
              vee::cmd_bind_gr_pipeline(*scb, *gp);
              vee::cmd_bind_descriptor_set(*scb, *pl, 0, dset);
              quad.run(scb, 0);
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
