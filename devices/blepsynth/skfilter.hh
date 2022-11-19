// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

//#define PANDA_RESAMPLER_HEADER_ONLY
//#include "pandaresampler.hh"
#include <algorithm>

using PandaResampler::Resampler2;

class SKFilter
{
  float k_ = 1;
  float pre_scale_ = 1;
  float post_scale_ = 1;
  int mode_ = 0;
  int over_ = 1;
  float freq_warp_factor_ = 0;

  struct Channel
  {
    std::unique_ptr<Resampler2> res_up;
    std::unique_ptr<Resampler2> res_down;

    float s1 = 0;
    float s2 = 0;
  };
  std::array<Channel, 2> channels_;

public:
  SKFilter (int over) :
    over_ (over)
  {
    for (auto& channel : channels_)
      {
        channel.res_up   = std::make_unique<Resampler2> (Resampler2::UP, over_, Resampler2::PREC_72DB, true, Resampler2::FILTER_FIR);
        channel.res_down = std::make_unique<Resampler2> (Resampler2::DOWN, over_, Resampler2::PREC_72DB, true, Resampler2::FILTER_FIR);
      }
    set_rate (48000);
  }
  void
  set_scale (float pre, float post)
  {
    pre_scale_ = pre;
    post_scale_ = post;
  }
  void
  set_params (int i, float res)
  {
    k_ = res * 2;
    mode_ = i;
  }
  void
  reset ()
  {
    for (auto& channel : channels_)
      {
        channel.res_up->reset();
        channel.res_down->reset();
        channel.s1 = 0;
        channel.s2 = 0;
      }
  }
  void
  set_rate (float rate)
  {
    freq_warp_factor_ = 4 / (rate * over_);
  }
private:
  float
  cutoff_warp (float freq)
  {
    float x = freq * freq_warp_factor_;

    /* approximate tan (pi*x/4) for cutoff warping */
    const float c1 = -3.16783027;
    const float c2 =  0.134516124;
    const float c3 = -4.033321984;

    float x2 = x * x;

    return x * (c1 + c2 * x2) / (c3 + x2);
  }
  template<int MODE, bool STEREO>
  void
  process (float *left, float *right, float freq)
  {
    float g = cutoff_warp (freq); // FIXME: clamp freq
    float G = g / (1 + g);

    float xnorm = 1.f / (1 - k_ * G + k_ * G * G);
    float s1feedback = -xnorm * k_ * (G - 1) / (1 + g);
    float s2feedback = -xnorm * k_ / (1 + g);

    auto lowpass = [G] (float in, float& state)
      {
        float v = G * (in - state);
        float y = v + state;
        state = y + v;
        return y;
      };

    auto mode_out = [] (float y0, float y1, float y2) -> float
      {
        float y1hp = y0 - y1;
        float y2hp = y1 - y2;

        switch (MODE)
          {
            case 0: return y2;
            case 1: return y2hp;
            case 2: return (y1hp - y2hp);
            case 3: return y1;
            case 4: return y1hp;
            default: return 0;
          }
      };

    auto distort = [] (float x)
      {
        /* shaped somewhat similar to tanh() and others, but faster */
        x = std::clamp (x, -1.0f, 1.0f);

        return x - x * x * x * (1.0f / 3);
      };

    float s1l, s1r, s2l, s2r, xl, xr, y0l, y0r, y1l, y1r, y2l, y2r;

    s1l = channels_[0].s1;
    s2l = channels_[0].s2;

    if (STEREO)
      {
        s1r = channels_[1].s1;
        s2r = channels_[1].s2;
      }

    for (int i = 0; i < over_; i++)
      {
        /*
         * interleaving processing of both channels performs better than
         * processing left and right channel seperately (measured on Ryzen7)
         */

                    { xl = left[i]  * pre_scale_; }
        if (STEREO) { xr = right[i] * pre_scale_; }

                    { y0l = distort (xl * xnorm + s1l * s1feedback + s2l * s2feedback); }
        if (STEREO) { y0r = distort (xr * xnorm + s1r * s1feedback + s2r * s2feedback); }

                    { y1l = lowpass (y0l, s1l); }
        if (STEREO) { y1r = lowpass (y0r, s1r); }

                    { y2l = lowpass (y1l, s2l); }
        if (STEREO) { y2r = lowpass (y1r, s2r); }

                    { left[i]  = mode_out (y0l, y1l, y2l) * post_scale_; }
        if (STEREO) { right[i] = mode_out (y0r, y1r, y2r) * post_scale_; }
      }

    channels_[0].s1 = s1l;
    channels_[0].s2 = s2l;

    if (STEREO)
      {
        channels_[1].s1 = s1r;
        channels_[1].s2 = s2r;
      }
  }
  template<int MODE>
  void
  process_block_mode (uint n_samples, float *left, float *right, const float *freq_in)
  {
    float over_samples_left[n_samples * over_];
    float over_samples_right[n_samples * over_];

    if (left)
      channels_[0].res_up->process_block (left, n_samples, over_samples_left);

    if (right)
      channels_[1].res_up->process_block (right, n_samples, over_samples_right);

    uint j = 0;
    for (uint i = 0; i < n_samples * over_; i += over_)
      {
        /* we only support stereo (left != 0, right != 0) and mono (left != 0, right == 0) */

        if (left && right)
          {
            process<MODE, true>  (over_samples_left + i, over_samples_right + i, freq_in[j++]);
          }
        else
          {
            process<MODE, false> (over_samples_left + i, nullptr, freq_in[j++]);
          }
      }

    if (left)
      channels_[0].res_down->process_block (over_samples_left, n_samples * over_, left);

    if (right)
      channels_[1].res_down->process_block (over_samples_right, n_samples * over_, right);
  }
public:
  void
  process_block (uint n_samples, float *left, float *right, float *freq_in)
  {
    switch (mode_)
      {
        case 0: process_block_mode<0> (n_samples, left, right, freq_in);
                break;
        case 1: process_block_mode<1> (n_samples, left, right, freq_in);
                break;
        case 2: process_block_mode<2> (n_samples, left, right, freq_in);
                break;
        case 3: process_block_mode<3> (n_samples, left, right, freq_in);
                break;
        case 4: process_block_mode<4> (n_samples, left, right, freq_in);
                break;
      }
  }
};
