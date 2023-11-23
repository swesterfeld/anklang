// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "ase/processor.hh"
#include "ase/midievent.hh"
#include "ase/internal.hh"

#include <fluidsynth.h>

namespace {

using namespace Ase;

class FluidSynthLoader
{
  enum { STATE_IDLE, STATE_LOAD };
  std::atomic<int>      state_ { STATE_IDLE };
  std::atomic<int>      quit_ { 0 };
  Ase::ScopedSemaphore  sem_;
  std::thread           thread_;

  fluid_settings_t     *fluid_settings_ = nullptr;
  fluid_synth_t        *fluid_synth_ = nullptr;
  int                   sfont_id_ = 0;
  String                have_sf2_;
  String                want_sf2_;
  uint                  have_sample_rate_ = 0;
  uint                  want_sample_rate_ = 0;

  void
  free_fluid_synth()
  {
    if (fluid_synth_)
      {
        delete_fluid_synth (fluid_synth_);
        fluid_synth_ = nullptr;
      }
    if (fluid_settings_)
      {
        delete_fluid_settings (fluid_settings_);
        fluid_settings_ = nullptr;
      }
  }
  void
  run()
  {
    while (!quit_.load())
      {
        sem_.wait();
        if (state_.load() == STATE_LOAD)
          {
            if (want_sf2_ != have_sf2_ || want_sample_rate_ != have_sample_rate_)
              {
                free_fluid_synth();

                fluid_settings_ = new_fluid_settings();

                fluid_settings_setnum (fluid_settings_, "synth.sample-rate", want_sample_rate_);
                /* soundfont instruments should be as loud as beast synthesis network instruments */
                fluid_settings_setnum (fluid_settings_, "synth.gain", 1.0);
                fluid_settings_setint (fluid_settings_, "synth.midi-channels", 16);
                fluid_settings_setint (fluid_settings_, "synth.audio-channels", 1);
                fluid_settings_setint (fluid_settings_, "synth.audio-groups", 1);
                fluid_settings_setint (fluid_settings_, "synth.reverb.active", 0);
                fluid_settings_setint (fluid_settings_, "synth.chorus.active", 0);
                /* we ensure that our fluid_synth instance is only used by one thread at a time
                 *  => we can disable automated locks that protect all fluid synth API calls
                 */
                fluid_settings_setint (fluid_settings_, "synth.threadsafe-api", 0);

                fluid_synth_ = new_fluid_synth (fluid_settings_);
                sfont_id_ = fluid_synth_sfload (fluid_synth_, want_sf2_.c_str(), 0);
                // TODO: error handling

                /*
                 * TODO: this is not the same value as in the PROGRAM / BANK properties below,
                 * but it probably would make sense to reset these parameters on load (how?)
                 */
                fluid_synth_program_select (fluid_synth_, 0, sfont_id_, /* bank */ 0, /* program */ 0);

                have_sf2_ = want_sf2_;
                have_sample_rate_ = want_sample_rate_;
              }
            state_.store (STATE_IDLE);
          }
      }
  }
public:
  FluidSynthLoader ()
  {
    thread_ = std::thread (&FluidSynthLoader::run, this);
    want_sf2_.reserve (4096); // avoid allocations in audio thread
  }
  ~FluidSynthLoader()
  {
    quit_.store (1);
    sem_.post();
    thread_.join();

    free_fluid_synth();
  }
  // called from audio thread
  bool
  idle()
  {
    if (state_.load() == STATE_IDLE)
      {
        if (want_sf2_ == have_sf2_ && want_sample_rate_ == have_sample_rate_)
          return true;
      }
    state_.store (STATE_LOAD);
    sem_.post();
    return false;
  }
  // called from audio thread
  void
  load (const String &sf2)
  {
    want_sf2_ = sf2;
  }
  // called from audio thread
  void
  set_sample_rate (uint sample_rate)
  {
    want_sample_rate_ = sample_rate;
  }
  // called from audio thread (only allowed to use result if in STATE_IDLE state)
  fluid_synth_t *
  fluid_synth()
  {
    return fluid_synth_;
  }
};

// == FluidSynth ==
// SF2 SoundFont support using fluidsynth library
class FluidSynth : public AudioProcessor {
  OBusId stereo_out_;
  ChoiceS hardcoded_instruments_;
  FluidSynthLoader loader_;

  enum Params { INSTRUMENT = 1, BANK = 2, PROGRAM = 3 };

  void
  initialize (SpeakerArrangement busses) override
  {
    install_params (build_parameter_map());

    loader_.set_sample_rate (sample_rate());
    prepare_event_input();
    stereo_out_ = add_output_bus ("Stereo Out", SpeakerArrangement::STEREO);
    assert_return (bus_info (stereo_out_).ident == "stereo_out");
  }
  ParameterMap
  build_parameter_map()
  {
    auto insts = hardcoded_instruments_;

    ParameterMap pmap;
    pmap[INSTRUMENT] = Param { "instrument", "Instrument", "Instrument", 0, "", std::move (insts), "", "Instrument (should have a file selector)" };

    /* should we expose program/bank or by preset (fluid_sfont_iteration_start...fluid_sfont_iteration_next)? */
    ChoiceS banks;
    for (int i = 0; i < 128; i++)
      banks += { string_format ("%d", i), string_format ("%03d Bank Name", i) }; // TODO: fill me after loading SF2

    pmap[BANK] = Param { "bank", "Bank", "Bank", 0, "", std::move (banks), "", "Banks (should be filled from SF2 info)" };

    ChoiceS programs;
    for (int i = 0; i < 128; i++)
      programs += { string_format ("%d", i), string_format ("%03d Program Name", i) }; // TODO: fill me after loading SF2

    pmap[PROGRAM] = Param { "program", "Program", "Program", 0, "", std::move (programs), "", "Program (should be filled from SF2 info)" };
    return pmap;
  }
  void
  reset (uint64 target_stamp) override
  {
    adjust_all_params();
  }
  void
  adjust_param (uint32_t tag) override
  {
    switch (tag)
      {
        case INSTRUMENT: loader_.load (hardcoded_instruments_[irintf (get_param (tag))].blurb);
                         break;
      }
  }
  void
  render_audio (fluid_synth_t *fluid_synth, float *left_out, float *right_out, uint n_frames)
  {
    if (!n_frames || !fluid_synth)
      return;

    float *output[2] = { left_out, right_out };
    fluid_synth_process (fluid_synth, n_frames,
                         0, nullptr, /* no effects */
                         2, output);
  }
  void
  render (uint n_frames) override
  {
    float *left_out = oblock (stereo_out_, 0);
    float *right_out = oblock (stereo_out_, 1);

    floatfill (left_out, 0.f, n_frames);
    floatfill (right_out, 0.f, n_frames);

    if (!loader_.idle())
      return;

    fluid_synth_t *fluid_synth = loader_.fluid_synth();

    uint offset = 0;
    MidiEventInput evinput = midi_event_input();
    for (const auto &ev : evinput)
      {
        const uint frame = std::max (ev.frame, 0); // TODO: should be unsigned anyway, issue #26

        // process any audio that is before the event
        render_audio (fluid_synth, left_out + offset, right_out + offset, frame - offset);
        offset = frame;

        switch (ev.message())
          {
          case MidiMessage::NOTE_OFF:
            if (fluid_synth)
              fluid_synth_noteoff (fluid_synth, ev.channel, ev.key);
            break;
          case MidiMessage::NOTE_ON:
            if (fluid_synth)
              fluid_synth_noteon (fluid_synth, ev.channel, ev.key, std::clamp (irintf (ev.velocity * 127), 0, 127));
            break;
          case MidiMessage::ALL_NOTES_OFF:
            if (fluid_synth)
              fluid_synth_all_notes_off (fluid_synth, ev.channel);
            break;
          case MidiMessage::ALL_SOUND_OFF:
            if (fluid_synth)
              fluid_synth_all_sounds_off (fluid_synth, ev.channel);
            break;
          case MidiMessage::PARAM_VALUE:
            apply_event (ev);
            adjust_param (ev.param);
            if (ev.param == PROGRAM)
              if (fluid_synth)
                fluid_synth_program_change (fluid_synth, /* channel */ 0, irintf (get_param (PROGRAM)));
            if (ev.param == BANK)
              if (fluid_synth)
                fluid_synth_bank_select (fluid_synth, /* channel */ 0, irintf (get_param (BANK)));
            break;
          default: ;
          }
      }
    // process frames after last event
    render_audio (fluid_synth, left_out + offset, right_out + offset, n_frames - offset);
  }
public:
  FluidSynth (const ProcessorSetup &psetup) :
    AudioProcessor (psetup)
  {
    hardcoded_instruments_ += { "FR3", "FluidR3", "/usr/share/sounds/sf2/FluidR3_GM.sf2" };
    hardcoded_instruments_ += { "AVL", "AVL Kit", "/usr/lib/lv2/avldrums.lv2/Black_Pearl_4_LV2.sf2" };
  }
  static void
  static_info (AudioProcessorInfo &info)
  {
    info.version      = "1";
    info.label        = "FluidSynth";
    info.category     = "Synth";
    info.creator_name = "Stefan Westerfeld";
    info.website_url  = "https://anklang.testbit.eu";
  }
};

static auto liquidsfz = register_audio_processor<FluidSynth> ("Ase::Devices::FluidSynth");

} // Bse
