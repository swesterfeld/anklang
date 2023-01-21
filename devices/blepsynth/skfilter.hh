// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

//#define PANDA_RESAMPLER_HEADER_ONLY
//#include "pandaresampler.hh"
#include <algorithm>

using PandaResampler::Resampler2;

class SKFilter
{
  float pre_scale_ = 1;
  float post_scale_ = 1;
  int mode_ = 0;
  float reso_ = 0;
  float drive_ = 0;
  int over_ = 1;
  float freq_warp_factor_ = 0;

  static constexpr int MAX_STAGES = 4;
  static constexpr uint MAX_BLOCK_SIZE = 1024;

  struct Channel
  {
    std::unique_ptr<Resampler2> res_up;
    std::unique_ptr<Resampler2> res_down;

    std::array<float, MAX_STAGES> s1;
    std::array<float, MAX_STAGES> s2;
  };
  std::array<float, MAX_STAGES> k_;

  static constexpr int
  mode2stages (int mode)
  {
    if (mode > 10)
      return 4;
    if (mode > 7)
      return 3;
    else if (mode > 4)
      return 2;
    else
      return 1;
  }

  std::array<Channel, 2> channels_;

  class RTable {
    std::vector<float> res2_k;
    std::vector<float> res3_k;
    std::vector<float> res4_k;
    static constexpr int TSIZE = 16;
    RTable()
    {
      for (int order = 4; order <= 8; order += 2)
        {
          for (int t = 0; t <= TSIZE + 1; t++)
            {
              double res = std::clamp (double (t) / TSIZE, 0.0, 1.0);

              // R must be in interval [0:1]
              const double R = 1 - res;
              const double r_alpha = std::acos (R) / (order / 2);

              std::vector<double> Rn;
              for (int i = 0; i < order / 2; i++)
                {
                  /* butterworth roots in s, left semi plane */
                  const double bw_s_alpha = M_PI * (4 * i + order + 2) / (2 * order);
                  /* add resonance */
                  Rn.push_back (-cos (bw_s_alpha + r_alpha));
                }

              std::sort (Rn.begin(), Rn.end(), std::greater<double>());

              for (auto xr : Rn)
                {
                  if (order == 4)
                    res2_k.push_back ((1 - xr) * 2);
                  if (order == 6)
                    res3_k.push_back ((1 - xr) * 2);
                  if (order == 8)
                    res4_k.push_back ((1 - xr) * 2);
                }
            }
        }
    }
  public:
    static const RTable&
    the()
    {
      static RTable rtable;
      return rtable;
    }
    void
    interpolate_resonance (float res, int stages, float *k, const std::vector<float>& res_k) const
    {
      auto lerp = [] (float a, float b, float frac) {
        return a + frac * (b - a);
      };

      float fidx = std::clamp (res, 0.f, 1.f) * TSIZE;
      int idx = fidx;
      float frac = fidx - idx;

      for (int s = 0; s < stages; s++)
        {
          k[s] = lerp (res_k[idx * stages + s], res_k[idx * stages + stages + s], frac);
        }
    }
    void
    lookup_resonance (float res, int stages, float *k) const
    {
      if (stages == 2)
        interpolate_resonance (res, stages, k, res2_k);

      if (stages == 3)
        interpolate_resonance (res, stages, k, res3_k);

      if (stages == 4)
        interpolate_resonance (res, stages, k, res4_k);
    }
  };
  const RTable& rtable_;
public:
  SKFilter (int over) :
    over_ (over),
    rtable_ (RTable::the())
  {
    for (auto& channel : channels_)
      {
        channel.res_up   = std::make_unique<Resampler2> (Resampler2::UP, over_, Resampler2::PREC_72DB, true, Resampler2::FILTER_FIR);
        channel.res_down = std::make_unique<Resampler2> (Resampler2::DOWN, over_, Resampler2::PREC_72DB, true, Resampler2::FILTER_FIR);
      }
    set_rate (48000);
    reset();
  }
  void
  set_scale (float pre, float post)
  {
    pre_scale_ = pre;
    post_scale_ = post;
  }
  void
  apply_reso_drive (float reso, float drive)
  {
    const float db_x2_factor = 0.166096404744368; // 1/(20*log(2)/log(10))
    const float sqrt2 = M_SQRT2;

    // drive resonance boost
    if (drive > 0)
      reso += drive * 0.015f;

    float vol = exp2f ((drive + -18 * reso) * db_x2_factor);

    if (reso < 0.9)
      {
        reso = 1 - (1-reso)*(1-reso)*(1-sqrt2/4);
      }
    else
      {
        reso = 1 - (1-0.9f)*(1-0.9f)*(1-sqrt2/4) + (reso-0.9f)*0.1f;
      }

    set_scale (vol, std::max (1 / vol, 1.0f));
    setup_k (reso);
  }
  void
  setup_k (float res)
  {
    if (mode2stages (mode_) == 1)
      {
        // just one stage
        k_[0] = res * 2;
      }
    else
      {
        rtable_.lookup_resonance (res, mode2stages (mode_), &k_[0]);
      }
  }
  void
  set_params (int i, float res)
  {
    mode_ = i;
    setup_k (res);
  }
  void
  set_mode (int m)
  {
    mode_ = m;
  }
  void
  set_reso (float reso)
  {
    reso_ = reso;
  }
  void
  set_drive (float drive)
  {
    drive_ = drive;
  }
  void
  reset ()
  {
    for (auto& channel : channels_)
      {
        channel.res_up->reset();
        channel.res_down->reset();
        std::fill (channel.s1.begin(), channel.s1.end(), 0.0);
        std::fill (channel.s2.begin(), channel.s2.end(), 0.0);
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

    for (int stage = 0; stage < mode2stages (MODE); stage++)
      {
        const float k = k_[stage];

        float xnorm = 1.f / (1 - k * G + k * G * G);
        float s1feedback = -xnorm * k * (G - 1) / (1 + g);
        float s2feedback = -xnorm * k / (1 + g);

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
                case 5: return y2;
                case 6: return y2hp;
                case 7: return (y1hp - y2hp);
                case 8: return y2;
                case 9: return y2hp;
                case 10: return (y1hp - y2hp);
                case 11: return y2;
                case 12: return y2hp;
                case 13: return (y1hp - y2hp);
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

        s1l = channels_[0].s1[stage];
        s2l = channels_[0].s2[stage];

        if (STEREO)
          {
            s1r = channels_[1].s1[stage];
            s2r = channels_[1].s2[stage];
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

        channels_[0].s1[stage] = s1l;
        channels_[0].s2[stage] = s2l;

        if (STEREO)
          {
            channels_[1].s1[stage] = s1r;
            channels_[1].s2[stage] = s2r;
          }
      }
  }
  template<int MODE>
  void
  process_block_mode (uint n_samples, float *left, float *right, const float *freq_in, const float *reso_in, const float *drive_in)
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
        apply_reso_drive (reso_in ? reso_in[j] : reso_, drive_in ? drive_in[j] : drive_);

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

  using ProcessBlockFunc = decltype (&SKFilter::process_block_mode<0>);
  static constexpr size_t LAST_MODE = 13;

  template<size_t... INDICES>
  static constexpr std::array<ProcessBlockFunc, LAST_MODE + 1>
  make_jump_table (std::integer_sequence<size_t, INDICES...>)
  {
    auto mk_func = [] (auto I) { return &SKFilter::process_block_mode<I.value>; };

    return { mk_func (std::integral_constant<int, INDICES>{})... };
  }
public:
  void
  process_block (uint n_samples, float *left, float *right, const float *freq_in, const float *reso_in = nullptr, const float *drive_in = nullptr)
  {
    static constexpr auto jump_table { make_jump_table (std::make_index_sequence<LAST_MODE + 1>()) };

    while (n_samples)
      {
        const uint todo = std::min (n_samples, MAX_BLOCK_SIZE);

        (this->*jump_table[mode_]) (todo, left, right, freq_in, reso_in, drive_in);

        if (left)
          left += todo;
        if (right)
          right += todo;
        if (freq_in)
          freq_in += todo;
        if (reso_in)
          reso_in += todo;

        n_samples -= todo;
      }
  }
};
