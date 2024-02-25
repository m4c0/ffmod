#pragma leco app
#pragma leco add_shader "poc.frag"
#pragma leco add_shader "poc.vert"
import casein;
import ffmod;
import siaudio;
import silog;
import sith;
import vee;
import voo;

static constexpr const auto filename = "movie.mov";

class threaded_poc : public sith::thread {
  ffmod::fmt_ctx m_ctx;

public:
  explicit threaded_poc(const char *filename)
      : m_ctx{ffmod::avformat_open_input(filename)} {}

  void run() override {
    auto decs = ffmod::avcodec_open_all(m_ctx);
    silog::assert((*decs[0])->codec_type == AVMEDIA_TYPE_VIDEO,
                  "invalid movie format");
    silog::assert((*decs[1])->codec_type == AVMEDIA_TYPE_AUDIO,
                  "invalid movie format");

    auto pkt = ffmod::av_packet_alloc();
    auto frm = ffmod::av_frame_alloc();
    while (!interrupted()) {
      auto pkt_ref = ffmod::av_read_frame(m_ctx, pkt);
      if (!*pkt_ref)
        break;

      auto idx = (*pkt)->stream_index;
      auto &dec = decs[idx];

      ffmod::avcodec_send_packet(dec, pkt);
      while (!interrupted()) {
        auto frm_ref = ffmod::avcodec_receive_frame(dec, frm);
        if (!*frm_ref)
          break;

        if ((*dec)->codec_type == AVMEDIA_TYPE_VIDEO) {
          // ffmod::copy_frame_yuv(frm, y, u, v);
        } else if ((*dec)->codec_type == AVMEDIA_TYPE_AUDIO) {
          // auto num_samples = (*frm)->nb_samples;
          // auto sample_rate = (*frm)->sample_rate;
          // auto *data = reinterpret_cast<float *>((*frm)->extended_data[0]);
        }
      }
    }
  }
};

class thread : public voo::casein_thread {
  // TODO: proper sample rate detection
  siaudio::ring_buffered_stream m_audio{48000};

  void copy_frame(voo::h2l_yuv_image *img, ffmod::frame &frm) {
    voo::mapmem y{img->host_memory_y()};
    voo::mapmem u{img->host_memory_u()};
    voo::mapmem v{img->host_memory_v()};

    ffmod::copy_frame_yuv(frm, static_cast<unsigned char *>(*y),
                          static_cast<unsigned char *>(*u),
                          static_cast<unsigned char *>(*v));
  }

public:
  void run() override {
    voo::device_and_queue dq{"ffmod", native_ptr()};
    voo::one_quad quad{dq};

    auto fmt_ctx = ffmod::avformat_open_input(filename);

    // TODO: detect stream type instead of guessing
    // This allows usage of files with multiple streams (ex: OBS with two audio
    // tracks)

    auto vidx = ffmod::av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO);
    auto aidx = ffmod::av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO);

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

      // TODO: recreate movie texture after resize
      extent_loop([&] {
        // From FFMPEG docs: "For video, the packet contains exactly one frame.
        // For audio, it contains an integer number of frames if each frame has
        // a known fixed size (e.g. PCM or ADPCM data). If the audio frames have
        // a variable size (e.g. MPEG audio), then it contains one frame."
        while (!interrupted()) {
          auto pkt_ref = ffmod::av_read_frame(fmt_ctx, pkt);
          if (!*pkt_ref)
            break;

          if ((*pkt)->stream_index == aidx) {
              ffmod::avcodec_send_packet(actx, pkt);
              while (!interrupted()) {
                auto frm_ref = ffmod::avcodec_receive_frame(actx, frm);
                if (!*frm_ref)
                  break;

                auto n = (*frm)->nb_samples;
                auto *data =
                    reinterpret_cast<float *>((*frm)->extended_data[0]);
                m_audio.push_frame(data, n);
              }
            continue;
          } else if ((*pkt)->stream_index == vidx) {
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
