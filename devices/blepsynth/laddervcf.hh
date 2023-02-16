// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#ifndef __ASE_DEVICES_LADDER_VCF_HH__
#define __ASE_DEVICES_LADDER_VCF_HH__

#define PANDA_RESAMPLER_HEADER_ONLY
#include "pandaresampler.hh"

#include <array>

namespace Ase {

using PandaResampler::Resampler2;

enum class LadderVCFMode { LP1, LP2, LP3, LP4 };

class LadderVCF
{
  struct Channel {
    float x1, x2, x3, x4;
    float y1, y2, y3, y4;

    std::unique_ptr<Resampler2> res_up;
    std::unique_ptr<Resampler2> res_down;
  };
  std::array<Channel, 2> channels;
  LadderVCFMode mode;
  float rate;
  float freq_ = 440;
  float reso_ = 0;
  float drive_ = 0;
  uint over_ = 0;
  bool test_linear_ = false;

  struct FParams
  {
    float reso = 0;
    float pre_scale = 1;
    float post_scale = 1;
  };
  FParams fparams_;
  bool    fparams_valid_ = false;
public:
  LadderVCF (int over) :
    over_ (over)
  {
    for (auto& channel : channels)
      {
        channel.res_up   = std::make_unique<Resampler2> (Resampler2::UP, over_, Resampler2::PREC_72DB);
        channel.res_down = std::make_unique<Resampler2> (Resampler2::DOWN, over_, Resampler2::PREC_72DB);
      }
    reset();
    set_mode (LadderVCFMode::LP4);
    set_rate (48000);
  }
  void
  set_mode (LadderVCFMode new_mode)
  {
    mode = new_mode;
  }
  void
  set_freq (float freq)
  {
    freq_ = freq;
  }
  void
  set_reso (float reso)
  {
    reso_ = reso;
    fparams_valid_ = false;
  }
  void
  set_drive (float drive)
  {
    drive_ = drive;
    fparams_valid_ = false;
  }
  void
  set_test_linear (bool test_linear)
  {
    test_linear_ = test_linear;
    fparams_valid_ = false;
  }
  void
  set_rate (float r)
  {
    rate = r;
  }
  void
  reset()
  {
    for (auto& c : channels)
      {
        c.x1 = c.x2 = c.x3 = c.x4 = 0;
        c.y1 = c.y2 = c.y3 = c.y4 = 0;

        c.res_up->reset();
        c.res_down->reset();
      }
    fparams_valid_ = false;
  }
  double
  delay()
  {
    return channels[0].res_up->delay() / over_ + channels[0].res_down->delay();
  }
private:
  float
  distort (float x)
  {
    /* shaped somewhat similar to tanh() and others, but faster */
    x = std::clamp (x, -1.0f, 1.0f);

    return x - x * x * x * (1.0f / 3);
  }
  void
  setup_reso_drive (FParams& fparams, float reso, float drive)
  {
    if (test_linear_) // test filter as linear filter; don't do any resonance correction
      {
        const float scale = 1e-5;
        fparams.pre_scale = scale;
        fparams.post_scale = 1 / scale;
        fparams.reso = reso;

        return;
      }
    const float db_x2_factor = 0.166096404744368; // 1/(20*log(2)/log(10))

    // scale signal down (without normalization on output) for negative drive
    float negative_drive_vol = 1;
    if (drive < 0)
      {
        negative_drive_vol = exp2f (drive * db_x2_factor);
        drive = 0;
      }
    float vol = exp2f ((drive + -12 * reso) * db_x2_factor);
    fparams.pre_scale = negative_drive_vol * vol;
    fparams.post_scale = std::max (1 / vol, 1.0f);
    fparams.reso = reso;
  }
  /*
   * This ladder filter implementation is mainly based on
   *
   * Välimäki, Vesa & Huovilainen, Antti. (2006).
   * Oscillator and Filter Algorithms for Virtual Analog Synthesis.
   * Computer Music Journal. 30. 19-31. 10.1162/comj.2006.30.2.19.
   */
  template<LadderVCFMode MODE, bool STEREO> inline void
  run (float *left, float *right, float fc)
  {
    const float pi = M_PI;
    fc = pi * fc;
    const float g = 0.9892f * fc - 0.4342f * fc * fc + 0.1381f * fc * fc * fc - 0.0202f * fc * fc * fc * fc;
    const float b0 = g * (1 / 1.3f);
    const float b1 = g * (0.3f / 1.3f);
    const float a1 = g - 1;

    float res = fparams_.reso;
    res *= 1.0029f + 0.0526f * fc - 0.0926f * fc * fc + 0.0218f * fc * fc * fc;

    for (uint os = 0; os < over_; os++)
      {
        for (uint i = 0; i < (STEREO ? 2 : 1); i++)
          {
            float &value = i == 0 ? left[os] : right[os];

            Channel& c = channels[i];
            const float x = value * fparams_.pre_scale;
            const float g_comp = 0.5f; // passband gain correction
            const float x0 = distort (x - (c.y4 - g_comp * x) * res * 4);

            c.y1 = b0 * x0 + b1 * c.x1 - a1 * c.y1;
            c.x1 = x0;

            c.y2 = b0 * c.y1 + b1 * c.x2 - a1 * c.y2;
            c.x2 = c.y1;

            c.y3 = b0 * c.y2 + b1 * c.x3 - a1 * c.y3;
            c.x3 = c.y2;

            c.y4 = b0 * c.y3 + b1 * c.x4 - a1 * c.y4;
            c.x4 = c.y3;

            switch (MODE)
              {
                case LadderVCFMode::LP1:
                  value = c.y1 * fparams_.post_scale;
                  break;
                case LadderVCFMode::LP2:
                  value = c.y2 * fparams_.post_scale;
                  break;
                case LadderVCFMode::LP3:
                  value = c.y3 * fparams_.post_scale;
                  break;
                case LadderVCFMode::LP4:
                  value = c.y4 * fparams_.post_scale;
                  break;
                default:
                  assert (false);
              }
          }
      }
  }
  template<LadderVCFMode MODE, bool STEREO> inline void
  do_run_block (uint          n_samples,
                float        *left,
                float        *right,
                const float  *freq_in,
                const float  *reso_in,
                const float  *drive_in)
  {
    float over_samples_left[over_ * n_samples];
    float over_samples_right[over_ * n_samples];
    float freq_scale = 1.0f / over_;
    float nyquist    = rate * 0.5;

    channels[0].res_up->process_block (left, n_samples, over_samples_left);
    if (STEREO)
      channels[1].res_up->process_block (right, n_samples, over_samples_right);

    float fc = freq_ * freq_scale / nyquist;

    if (!fparams_valid_)
      {
        setup_reso_drive (fparams_, reso_in ? reso_in[0] : reso_, drive_in ? drive_in[0] : drive_);
        fparams_valid_ = true;
      }

    if (reso_in || drive_in)
      {
        /* for reso or drive modulation, we split the input it into small blocks
         * and interpolate the pre_scale / post_scale / reso parameters
         */
        float *left_blk = over_samples_left;
        float *right_blk = over_samples_right;

        uint n_remaining_samples = n_samples;
        while (n_remaining_samples)
          {
            const uint todo = std::min<uint> (n_remaining_samples, 64);

            FParams fparams_end;
            setup_reso_drive (fparams_end, reso_in ? reso_in[todo - 1] : reso_, drive_in ? drive_in[todo - 1] : drive_);

            float todo_inv = 1.f / todo;
            float delta_pre_scale = (fparams_end.pre_scale - fparams_.pre_scale) * todo_inv;
            float delta_post_scale = (fparams_end.post_scale - fparams_.post_scale) * todo_inv;
            float delta_reso = (fparams_end.reso - fparams_.reso) * todo_inv;

            uint j = 0;
            for (uint i = 0; i < todo * over_; i += over_)
              {
                fparams_.pre_scale += delta_pre_scale;
                fparams_.post_scale += delta_post_scale;
                fparams_.reso += delta_reso;

                float mod_fc = fc;

                if (freq_in)
                  mod_fc = freq_in[j++] * freq_scale / nyquist;

                mod_fc  = std::clamp (mod_fc, 0.0f, 1.0f);

                run<MODE, STEREO> (left_blk + i, right_blk + i, mod_fc);
              }

            n_remaining_samples -= todo;
            left_blk += todo * over_;
            right_blk += todo * over_;

            if (freq_in)
              freq_in += todo;
            if (reso_in)
              reso_in += todo;
            if (drive_in)
              drive_in += todo;
          }
      }
    else
      {
        for (uint i = 0; i < n_samples; i++)
          {
            float mod_fc = fc;

            if (freq_in)
              mod_fc = freq_in[i] * freq_scale / nyquist;

            mod_fc  = std::clamp (mod_fc, 0.0f, 1.0f);

            const uint over_pos = i * over_;
            run<MODE, STEREO> (over_samples_left + over_pos, over_samples_right + over_pos, mod_fc);
          }
      }
    channels[0].res_down->process_block (over_samples_left, over_ * n_samples, left);
    if (STEREO)
      channels[1].res_down->process_block (over_samples_right, over_ * n_samples, right);
  }
  template<LadderVCFMode MODE> inline void
  run_block_mode (uint          n_samples,
                  float        *left,
                  float        *right,
                  const float  *freq_in,
                  const float  *reso_in,
                  const float  *drive_in)
  {
    if (right) // stereo?
      do_run_block<MODE, true> (n_samples, left, right, freq_in, reso_in, drive_in);
    else
      do_run_block<MODE, false> (n_samples, left, right, freq_in, reso_in, drive_in);
  }
public:
  void
  run_block (uint         n_samples,
             float       *left,
             float       *right,
             const float *freq_in = nullptr,
             const float *reso_in = nullptr,
             const float *drive_in = nullptr)
  {
    switch (mode)
    {
      case LadderVCFMode::LP4: run_block_mode<LadderVCFMode::LP4> (n_samples, left, right, freq_in, reso_in, drive_in);
                               break;
      case LadderVCFMode::LP3: run_block_mode<LadderVCFMode::LP3> (n_samples, left, right, freq_in, reso_in, drive_in);
                               break;
      case LadderVCFMode::LP2: run_block_mode<LadderVCFMode::LP2> (n_samples, left, right, freq_in, reso_in, drive_in);
                               break;
      case LadderVCFMode::LP1: run_block_mode<LadderVCFMode::LP1> (n_samples, left, right, freq_in, reso_in, drive_in);
                               break;
    }
  }
};

} // Ase

#endif // __ASE_DEVICES_LADDER_VCF_HH__
