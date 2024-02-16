// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "processor.hh"
#include "main.hh"
#include "internal.hh"
#include "strings.hh"
#include "loft.hh"
#include "serialize.hh"
#include "storage.hh"
#include "project.hh"
#include "path.hh"
#include "compress.hh"

// X11 wrapper
#include "clapplugin.hh"

#include "lv2/atom/atom.h"
#include "lv2/atom/forge.h"
#include "lv2/midi/midi.h"
#include "lv2/options/options.h"
#include "lv2/parameters/parameters.h"
#include "lv2/buf-size/buf-size.h"
#include "lv2/worker/worker.h"
#include "lv2/presets/presets.h"
#include "lv2/resize-port/resize-port.h"
#include "lv2/data-access/data-access.h"
#include "lv2/instance-access/instance-access.h"
#include "lv2/ui/ui.h"
#include "lv2/time/time.h"
#include "lv2/state/state.h"
#include "lv2/units/units.h"
#include "lv2/port-props/port-props.h"

#include "lv2externalui.hh"

#include "lv2evbuf.hh"
#include "lv2device.hh"

#include <lilv/lilv.h>

// #define DEBUG_MAP
// #define DEBUG_WORKER
// #define DEBUG_MISSING_FEATURES

// support building with older version of the lv2 includes that do not yet support free path
#ifndef LV2_STATE__freePath
#define LV2_STATE__freePath LV2_STATE_PREFIX "freePath"
extern "C" {
typedef void *LV2_State_Free_Path_Handle;
typedef struct {
  LV2_State_Free_Path_Handle handle;
  void (*free_path) (LV2_State_Free_Path_Handle handle, char *path);
} LV2_State_Free_Path;
}
#endif

namespace Ase {

class LV2Processor;

namespace
{

static Gtk2DlWrapEntry *x11wrapper = nullptr;

using std::vector;
using std::set;
using std::map;
using std::max;
using std::min;

class ControlEvent
{
  LoftPtr<ControlEvent>       loft_ptr_;    // keep this object alive
  uint32_t                    port_index_;
  uint32_t                    protocol_;
  size_t                      size_;
  LoftPtr<void>               data_;

public:
  std::atomic<ControlEvent *> next_ = nullptr;

  static ControlEvent *
  loft_new (uint32_t port_index, uint32_t protocol, size_t size, const void *data = nullptr)
  {
    LoftPtr<ControlEvent> loft_ptr = loft_make_unique<ControlEvent>();
    ControlEvent *new_event = loft_ptr.get();
    new_event->loft_ptr_ = std::move (loft_ptr);
    new_event->port_index_ = port_index;
    new_event->protocol_ = protocol;
    new_event->size_ = size;
    new_event->data_ = loft_alloc (size);
    if (data)
      memcpy (new_event->data_.get(), data, size);
    return new_event;
  }
  void
  loft_free()
  {
    loft_ptr_.reset(); // do not access this after this line
  }
  uint32_t port_index() const { return port_index_; }
  uint32_t protocol() const   { return protocol_; }
  size_t   size() const       { return size_; }
  uint8_t *data() const       { return reinterpret_cast<uint8_t *> (data_.get()); }
};

class ControlEventVector
{
  AtomicIntrusiveStack<ControlEvent> events_;
public:
  template<typename Func>
  void
  for_each (ControlEventVector &trash_events, Func func)
  {
    ControlEvent *const events = events_.pop_reversed(), *last = nullptr;
    for (ControlEvent *event = events; event; last = event, event = event->next_)
      {
        const ControlEvent *cevent = event;
        func (cevent);
      }
    if (last)
      trash_events.events_.push_chain (events, last);
  }
  void
  free_all()
  {
    ControlEvent *event = events_.pop_all();
    while (event)
      {
        ControlEvent *old = event;
        event = event->next_;
        old->loft_free();
      }
  }
  void
  push (ControlEvent *event)
  {
    events_.push (event);
  }
  ~ControlEventVector()
  {
    free_all();
  }
};

static inline std::atomic<ControlEvent*>&
atomic_next_ptrref (ControlEvent *event)
{
  return event->next_;
}

class Map
{
  std::mutex            map_mutex;
  LV2_URID              next_id;
  map<String, LV2_URID> m_urid_map;
  map<LV2_URID, String> m_urid_unmap;

  LV2_URID_Map       lv2_urid_map;
  const LV2_Feature  lv2_urid_map_feature;
  LV2_URID_Unmap     lv2_urid_unmap;
  const LV2_Feature  lv2_urid_unmap_feature;
public:
  Map() :
    next_id (1),
    lv2_urid_map { this, urid_map },
    lv2_urid_map_feature { LV2_URID_MAP_URI, &lv2_urid_map },
    lv2_urid_unmap { this, urid_unmap },
    lv2_urid_unmap_feature { LV2_URID_UNMAP_URI, &lv2_urid_unmap }
  {
  }

  static LV2_URID
  urid_map (LV2_URID_Map_Handle handle, const char *str)
  {
    return static_cast<Map *> (handle)->urid_map (str);
  }
  static const char *
  urid_unmap (LV2_URID_Unmap_Handle handle, LV2_URID id)
  {
    return static_cast<Map *> (handle)->urid_unmap (id);
  }

  LV2_URID
  urid_map (const char *str)
  {
    std::lock_guard lg (map_mutex);

    LV2_URID &id = m_urid_map[str];
    if (id == 0)
      id = next_id++;

    m_urid_unmap[id] = str;
#ifdef DEBUG_MAP
    printerr ("map %s -> %d\n", str, id);
#endif
    return id;
  }
  const char *
  urid_unmap (LV2_URID id)
  {
    std::lock_guard lg (map_mutex);

    auto it = m_urid_unmap.find (id);
    if (it != m_urid_unmap.end())
      return it->second.c_str();
    else
      return nullptr;
  }

  const LV2_Feature *
  map_feature() const
  {
    return &lv2_urid_map_feature;
  }
  const LV2_Feature *
  unmap_feature() const
  {
    return &lv2_urid_unmap_feature;
  }
  LV2_URID_Map *
  lv2_map()
  {
    return &lv2_urid_map;
  }
  LV2_URID_Unmap *
  lv2_unmap()
  {
    return &lv2_urid_unmap;
  }
};

class PluginHost;

class Options
{
  float       m_sample_rate_ = 0;
  uint32_t    m_min_block_length_ = 0;
  uint32_t    m_max_block_length_ = AUDIO_BLOCK_MAX_RENDER_SIZE;

  vector<LV2_Options_Option> const_opts_;

  LV2_Feature lv2_options_feature_;
public:
  Options (PluginHost &plugin_host, float sample_rate);
  const LV2_Feature *
  feature() const
  {
    return &lv2_options_feature_;
  }
};

class Worker
{
  LV2_Worker_Schedule         lv2_worker_sched_ {};
  const LV2_Feature           lv2_worker_feature_;

  const LV2_Worker_Interface *worker_interface_ = nullptr;
  LV2_Handle                  instance_ = nullptr;
  ControlEventVector          work_events_, response_events_, trash_events_;
  std::thread                 thread_;
  std::atomic<int>            quit_;
  ScopedSemaphore             sem_;

  void
  run()
  {
#ifdef DEBUG_WORKER
    printerr ("worker thread running\n");
#endif
    while (!quit_)
      {
        sem_.wait();
        work_events_.for_each (trash_events_,
          [this] (const ControlEvent *event)
            {
#ifdef DEBUG_WORKER
              printerr ("worker: got work %zd bytes\n", event->size());
#endif
              worker_interface_->work (instance_, respond, this, event->size(), event->data());
            });
        // free both: old worker events and old response events
        trash_events_.free_all();
      }
  }
  LV2_Worker_Status
  schedule (uint32_t size, const void *data)
  {
    if (!worker_interface_)
      return LV2_WORKER_ERR_UNKNOWN;

    work_events_.push (ControlEvent::loft_new (0, 0, size, data));
    sem_.post();
    return LV2_WORKER_SUCCESS;
  }
  LV2_Worker_Status
  respond (uint32_t size, const void *data)
  {
    if (!worker_interface_)
      return LV2_WORKER_ERR_UNKNOWN;

    response_events_.push (ControlEvent::loft_new (0, 0, size, data));
    return LV2_WORKER_SUCCESS;
  }
  static LV2_Worker_Status
  schedule (LV2_Worker_Schedule_Handle handle,
            uint32_t                   size,
            const void*                data)
  {
    Worker *worker = static_cast<Worker *> (handle);
    return worker->schedule (size, data);
  }
  static LV2_Worker_Status
  respond  (LV2_Worker_Respond_Handle handle,
            uint32_t                  size,
            const void*               data)
  {
    Worker *worker = static_cast<Worker *> (handle);
    return worker->respond (size, data);
  }
public:
  Worker() :
    lv2_worker_sched_ { this, schedule },
    lv2_worker_feature_ { LV2_WORKER__schedule, &lv2_worker_sched_ },
    quit_ (0)
  {
    thread_ = std::thread (&Worker::run, this);
  }
  ~Worker()
  {
    // need to call stop before destructor
    assert_return (!thread_.joinable());
  }
  void
  stop()
  {
    quit_ = 1;
    sem_.post();
    thread_.join();
#ifdef DEBUG_WORKER
    printerr ("worker thread joined\n");
#endif
  }
  void
  set_instance (LilvInstance *lilv_instance)
  {
    instance_ = lilv_instance_get_handle (lilv_instance);

    const LV2_Descriptor *descriptor = lilv_instance_get_descriptor (lilv_instance);
    if (descriptor && descriptor->extension_data)
       worker_interface_ = (const LV2_Worker_Interface *) (*descriptor->extension_data) (LV2_WORKER__interface);
  }
  void
  handle_responses()
  {
    response_events_.for_each (trash_events_,
      [this] (const ControlEvent *event)
        {
          worker_interface_->work_response (instance_, event->size(), event->data());
        });
  }
  void
  end_run()
  {
    /* to be called after each run cycle */
    if (worker_interface_ && worker_interface_->end_run)
      worker_interface_->end_run (instance_);
  }
  const LV2_Feature *
  feature() const
  {
    return &lv2_worker_feature_;
  }
};


class Features
{
  std::vector<LV2_Feature> features;
  std::vector<const LV2_Feature *> null_terminated_ptrs;
public:
  const LV2_Feature * const*
  get_features()
  {
    assert_return (null_terminated_ptrs.empty(), nullptr);
    for (const auto &f : features)
      null_terminated_ptrs.push_back (&f);
    null_terminated_ptrs.push_back (nullptr);

    return null_terminated_ptrs.data();
  }
  void
  add (const LV2_Feature *lv2_feature)
  {
    features.push_back (*lv2_feature);
  }
  void
  add (const char *uri, void *data)
  {
    features.emplace_back (uri, data);
  }
};

struct ScalePoint
{
  String label;
  float  value = 0;
};

struct Port
{
  LV2_Evbuf  *evbuf = nullptr; /* for atom ports */
  float       control = 0;     /* for control ports */
  float       min_value = 0;   /* min control */
  float       max_value = 0;   /* max control */
  int         control_in_idx = -1; /* for control input ports */
  int         index = -1;      /* lv2 index for this port */
  String      name;
  String      symbol;
  String      unit;
  vector<ScalePoint> scale_points; /* for enumerations */

  static constexpr uint NO_FLAGS    = 0;
  static constexpr uint LOGARITHMIC = 1 << 0;
  static constexpr uint INTEGER     = 1 << 1;
  static constexpr uint TOGGLED     = 1 << 2;
  static constexpr uint ENUMERATION = 1 << 3;
  static constexpr uint HIDDEN      = 1 << 4;  // not visible in generic UI (i.e. bpm port)

  // port direction
  static constexpr uint INPUT       = 1 << 5;
  static constexpr uint OUTPUT      = 1 << 6;

  // port type
  static constexpr uint CONTROL     = 1 << 7;
  static constexpr uint AUDIO       = 1 << 8;
  static constexpr uint ATOM        = 1 << 9;

  uint flags = NO_FLAGS;

  float
  param_to_lv2 (double value) const
  {
    if (flags & ENUMERATION)
      {
        int index = std::clamp (irintf (value), 0, int (scale_points.size()) - 1);
        return scale_points[index].value;
      }
    else if (flags & LOGARITHMIC)
      {
        float f = exp2 (log2 (min_value) + (log2 (max_value) - log2 (min_value)) * value);
        return std::clamp (f, min_value, max_value);
      }
    else if (flags & INTEGER)
      {
        // TODO: the knob at the UI should also only allow integer values
        float f = std::round (value);
        return std::clamp (f, min_value, max_value);
      }
    else
      {
        return value;
      }
  }
  double
  param_from_lv2 (double value) const
  {
    if (flags & ENUMERATION)
      {
        double best_diff = 1e10;
        size_t best_idx = 0;
        for (size_t idx = 0; idx < scale_points.size(); idx++)
          {
            double diff = std::abs (scale_points[idx].value - value);
            if (diff < best_diff)
              {
                best_idx = idx;
                best_diff = diff;
              }
          }
        return best_idx;
      }
    else if (flags & LOGARITHMIC)
      {
        double d = (log2 (value) - log2 (min_value)) / (log2 (max_value) - log2 (min_value));
        return std::clamp (d, 0.0, 1.0);
      }
    else if (flags & INTEGER)
      {
        // TODO: the knob at the UI should also only allow integer values
        float f = std::round (value);
        return std::clamp (f, min_value, max_value);
      }
    else
      {
        return value;
      }
  }
};

struct PresetInfo
{
  String    name;
  LilvNode *preset = nullptr;
};

struct PortRestoreHelper;

class PluginUI;

struct PathMap
{
  LV2_State_Map_Path map_path {
    .handle = this,
    .abstract_path = [] (LV2_State_Map_Path_Handle handle, const char *path)
      {
        PathMap *path_map = (PathMap *) handle;
        if (path_map->abstract_path)
          return strdup (path_map->abstract_path (path).c_str());
        else
          {
            printerr ("LV2: PathMap: abstract_path (%s) called, but no mapping function defined\n", path);
            return strdup (path);
          }
      },
    .absolute_path = [] (LV2_State_Map_Path_Handle handle, const char *path)
      {
        PathMap *path_map = (PathMap *) handle;
        if (path_map->absolute_path)
          return strdup (path_map->absolute_path (path).c_str());
        else
          {
            printerr ("LV2: PathMap: absolute_path (%s) called, but no mapping function defined\n", path);
            return strdup (path);
          }
      }
  };
  LV2_State_Free_Path free_path {
    .free_path = [] (LV2_State_Map_Path_Handle handle, char *path) { free (path); }
  };

  std::function<String(String)> abstract_path;
  std::function<String(String)> absolute_path;
};

class PluginInstance
{
  bool                       init_ok_ = false;
  LilvUIs                   *uis_ = nullptr;
  const LilvUI              *ui_ = nullptr;
  String                     ui_type_uri_;
  std::unique_ptr<Worker>    worker_;
  Options                    options_;

  std::array<uint8, 256>     last_position_buffer_ {};
  std::array<uint8, 256>     position_buffer_ {};

  std::vector<int>           atom_out_ports_;
  std::vector<int>           atom_in_ports_;
  std::vector<int>           audio_in_ports_;
  std::vector<int>           audio_out_ports_;
  std::vector<int>           control_in_ports_;
  std::vector<int>           midi_in_ports_;
  std::vector<int>           position_in_ports_;
  int                        bpm_port_index_ = -1;

  const LilvPlugin          *plugin_ = nullptr;
  LV2_Atom_Forge             forge_;
  Features                   features_;
  uint                       sample_rate_ = 0;
  bool                       active_ = false;
  uint32_t                   ui_update_frame_count_ = 0;

  ControlEventVector         ui2dsp_events_;
  ControlEventVector         dsp2ui_events_;
  ControlEventVector         trash_events_;
  std::atomic<bool>          dsp2ui_notifications_enabled_ { false };
  std::unique_ptr<PluginUI>  plugin_ui_;

  PluginHost                &plugin_host_;

  LV2_Feature                lv2_instance_access_feature_ {};
  LV2_Feature                lv2_data_access_feature_ {};
  LV2_Extension_Data_Feature lv2_ext_data {};

  LilvInstance              *instance_ = nullptr;
  vector<Port>               plugin_ports_;
  vector<PresetInfo>         presets_;
  LV2Processor              *processor_;

  enum PresetState {
    PRESET_STATE_READY,
    PRESET_STATE_LOAD,
    PRESET_STATE_FINALIZE
  };
  std::atomic<int>           preset_state_ { PRESET_STATE_READY };
  int                        preset_to_load_ = 0;
  std::unique_ptr<PortRestoreHelper>  preset_port_restore_helper_;

  void init_ports();
  void free_ports();
  void init_presets();
  void free_presets();
  void send_plugin_events_to_ui();
  void send_ui_updates (uint32_t delta_frames);
  void restore_state (LilvState *state, PortRestoreHelper *helper, PathMap *path_map = nullptr);
  void find_plugin_ui();

  static const void* get_port_value_for_save (const char *port_symbol, void *user_data, uint32_t *size, uint32_t *type);

  // lilv_state_to_string requires a non-empty URI
  static constexpr auto ANKLANG_STATE_URI = "urn:anklang:state";
public:
  PluginInstance (PluginHost &plugin_host, uint sample_rate, const LilvPlugin *plugin,
                  PortRestoreHelper *port_restore_helper, LV2Processor *processor);
  ~PluginInstance();

  static constexpr double       ui_update_fps = 60;

  bool init_ok() const { return init_ok_; }
  uint n_audio_inputs() const                         { return audio_in_ports_.size(); }
  uint n_audio_outputs() const                        { return audio_out_ports_.size(); }
  uint n_control_inputs() const                       { return control_in_ports_.size(); }
  const LV2_Feature *instance_access_feature() const  { return &lv2_instance_access_feature_; }
  const LV2_Feature *data_access_feature() const      { return &lv2_data_access_feature_; }
  const LV2_Feature *options_feature() const          { return options_.feature(); }
  const Port &control_input_port (size_t index) const { assert (index < control_in_ports_.size()); return plugin_ports_[control_in_ports_[index]]; }
  const vector<PresetInfo>& presets() const           { return presets_; }
  PluginUI *plugin_ui() const                         { return plugin_ui_.get(); }
  bool gui_supported()                                { return ui_ != nullptr; }

  void reset_event_buffers();
  void write_midi (uint32_t time, size_t size, const uint8_t *data);
  void write_position (const AudioTransport &transport);
  void connect_audio_in (uint32_t input_port, const float *buffer);
  void connect_audio_out (uint32_t input_port, float *buffer);
  void run (uint32_t n_frames);
  void activate();
  void deactivate();

  void set_initial_controls_ui();
  void handle_dsp2ui_events (void *ui_instance);
  void enable_dsp2ui_notifications (bool enabled);
  void clear_dsp2ui_events();

  void toggle_ui();
  void delete_ui();
  void set_control_param (size_t index, double value);

  bool restore_string (const String &str, PortRestoreHelper *helper, PathMap *path_map = nullptr);
  void restore_preset (int preset, PortRestoreHelper *helper);
  void restore_preset_async (int preset);
  void finalize_preset_restore();
  bool busy_loading_preset();
  void gtk_idle_timer();

  String save_string (PathMap *path_map);

  static void host_ui_write (void *controller, uint32_t port_index, uint32_t buffer_size, uint32_t protocol, const void *buffer);
  static uint32_t host_ui_index (void *controller, const char *symbol);
};

class PluginHost
{
public:
  LilvWorld   *world = nullptr;
  Map          urid_map;
  void        *suil_host = nullptr;

  struct URIDs {
    LV2_URID param_sampleRate;
    LV2_URID atom_Double;
    LV2_URID atom_Float;
    LV2_URID atom_Int;
    LV2_URID atom_Long;
    LV2_URID atom_eventTransfer;
    LV2_URID bufsz_maxBlockLength;
    LV2_URID bufsz_minBlockLength;
    LV2_URID midi_MidiEvent;
    LV2_URID time_Position;
    LV2_URID time_bar;
    LV2_URID time_barBeat;
    LV2_URID time_beatUnit;
    LV2_URID time_beatsPerBar;
    LV2_URID time_beatsPerMinute;
    LV2_URID time_frame;
    LV2_URID time_speed;

    URIDs (Map &map) :
      param_sampleRate          (map.urid_map (LV2_PARAMETERS__sampleRate)),
      atom_Double               (map.urid_map (LV2_ATOM__Double)),
      atom_Float                (map.urid_map (LV2_ATOM__Float)),
      atom_Int                  (map.urid_map (LV2_ATOM__Int)),
      atom_Long                 (map.urid_map (LV2_ATOM__Long)),
      atom_eventTransfer        (map.urid_map (LV2_ATOM__eventTransfer)),
      bufsz_maxBlockLength      (map.urid_map (LV2_BUF_SIZE__maxBlockLength)),
      bufsz_minBlockLength      (map.urid_map (LV2_BUF_SIZE__minBlockLength)),
      midi_MidiEvent            (map.urid_map (LV2_MIDI__MidiEvent)),
      time_Position             (map.urid_map (LV2_TIME__Position)),
      time_bar                  (map.urid_map (LV2_TIME__bar)),
      time_barBeat              (map.urid_map (LV2_TIME__barBeat)),
      time_beatUnit             (map.urid_map (LV2_TIME__beatUnit)),
      time_beatsPerBar          (map.urid_map (LV2_TIME__beatsPerBar)),
      time_beatsPerMinute       (map.urid_map (LV2_TIME__beatsPerMinute)),
      time_frame                (map.urid_map (LV2_TIME__frame)),
      time_speed                (map.urid_map (LV2_TIME__speed))
    {
    }
  } urids;

  struct Nodes {
    LilvNode *lv2_audio_class;
    LilvNode *lv2_atom_class;
    LilvNode *lv2_input_class;
    LilvNode *lv2_output_class;
    LilvNode *lv2_control_class;
    LilvNode *lv2_rsz_minimumSize;

    LilvNode *lv2_atom_Chunk;
    LilvNode *lv2_atom_Sequence;
    LilvNode *lv2_atom_supports;
    LilvNode *lv2_midi_MidiEvent;
    LilvNode *lv2_time_Position;
    LilvNode *lv2_time_beatsPerMinute;
    LilvNode *lv2_presets_Preset;
    LilvNode *lv2_units_unit;
    LilvNode *lv2_units_symbol;
    LilvNode *lv2_pprop_logarithmic;
    LilvNode *lv2_pprop_notOnGUI;
    LilvNode *lv2_integer;
    LilvNode *lv2_toggled;
    LilvNode *lv2_enumeration;

    LilvNode *lv2_ui_external;
    LilvNode *lv2_ui_externalkx;
    LilvNode *lv2_ui_fixedSize;
    LilvNode *lv2_ui_noUserResize;
    LilvNode *lv2_ui_x11ui;
    LilvNode *lv2_optionalFeature;
    LilvNode *lv2_requiredFeature;
    LilvNode *lv2_worker_schedule;
    LilvNode *lv2_state_loadDefaultState;

    LilvNode *rdfs_label;
    LilvNode *native_ui_type;

    void init (LilvWorld *world)
    {
      lv2_audio_class             = lilv_new_uri (world, LILV_URI_AUDIO_PORT);
      lv2_atom_class              = lilv_new_uri (world, LILV_URI_ATOM_PORT);
      lv2_input_class             = lilv_new_uri (world, LILV_URI_INPUT_PORT);
      lv2_output_class            = lilv_new_uri (world, LILV_URI_OUTPUT_PORT);
      lv2_control_class           = lilv_new_uri (world, LILV_URI_CONTROL_PORT);
      lv2_rsz_minimumSize         = lilv_new_uri (world, LV2_RESIZE_PORT__minimumSize);

      lv2_atom_Chunk              = lilv_new_uri (world, LV2_ATOM__Chunk);
      lv2_atom_Sequence           = lilv_new_uri (world, LV2_ATOM__Sequence);
      lv2_atom_supports           = lilv_new_uri (world, LV2_ATOM__supports);
      lv2_midi_MidiEvent          = lilv_new_uri (world, LV2_MIDI__MidiEvent);
      lv2_time_Position           = lilv_new_uri (world, LV2_TIME__Position);
      lv2_time_beatsPerMinute     = lilv_new_uri (world, LV2_TIME__beatsPerMinute);
      lv2_units_unit              = lilv_new_uri (world, LV2_UNITS__unit);
      lv2_units_symbol            = lilv_new_uri (world, LV2_UNITS__symbol);
      lv2_pprop_logarithmic       = lilv_new_uri (world, LV2_PORT_PROPS__logarithmic);
      lv2_pprop_notOnGUI          = lilv_new_uri (world, LV2_PORT_PROPS__notOnGUI);
      lv2_integer                 = lilv_new_uri (world, LV2_CORE__integer);
      lv2_toggled                 = lilv_new_uri (world, LV2_CORE__toggled);
      lv2_enumeration             = lilv_new_uri (world, LV2_CORE__enumeration);

      lv2_ui_external             = lilv_new_uri (world, "http://lv2plug.in/ns/extensions/ui#external");
      lv2_ui_externalkx           = lilv_new_uri (world, "http://kxstudio.sf.net/ns/lv2ext/external-ui#Widget");
      lv2_ui_fixedSize            = lilv_new_uri (world, LV2_UI__fixedSize);
      lv2_ui_noUserResize         = lilv_new_uri (world, LV2_UI__noUserResize);
      lv2_ui_x11ui                = lilv_new_uri (world, LV2_UI__X11UI);

      lv2_optionalFeature         = lilv_new_uri (world, LV2_CORE__optionalFeature);
      lv2_requiredFeature         = lilv_new_uri (world, LV2_CORE__requiredFeature);
      lv2_worker_schedule         = lilv_new_uri (world, LV2_WORKER__schedule);
      lv2_state_loadDefaultState  = lilv_new_uri (world, LV2_STATE__loadDefaultState);

      lv2_presets_Preset = lilv_new_uri (world, LV2_PRESETS__Preset);
      rdfs_label         = lilv_new_uri (world, LILV_NS_RDFS "label");

      native_ui_type     = lilv_new_uri (world, "http://lv2plug.in/ns/extensions/ui#GtkUI");
    }
  } nodes;

private:
  PluginHost() :
    urids (urid_map)
  {
    if (!x11wrapper)
      x11wrapper = get_x11wrapper();
    if (x11wrapper)
      {
        if (x11wrapper->have_display())
          {
            suil_host = x11wrapper->create_suil_host (PluginInstance::host_ui_write, PluginInstance::host_ui_index);
            // TODO: free suil_host when done
           }
      }

    world = lilv_world_new();
    lilv_world_load_all (world);

    nodes.init (world);
  }
  vector<PluginInstance *> plugin_instances_;
  uint                     timer_id_ = 0;
public:
  static PluginHost&
  the()
  {
    static PluginHost host;
    return host;
  }
  bool have_display() { return suil_host != nullptr; }
  PluginInstance *instantiate (const char *plugin_uri, uint sample_rate, PortRestoreHelper *port_restore_helper, LV2Processor *processor);
  void add_instance (PluginInstance *instance);
  void remove_instance (PluginInstance *instance);
  void post_load();

private:
  DeviceInfoS devs;
  map<String, DeviceInfo> lv2_device_info_map;

  bool
  required_features_supported (const LilvPlugin *plugin, const String &name)
  {
    bool can_use_plugin = true;

    set<String> supported_features {
      LV2_WORKER__schedule,
      LV2_URID_MAP_URI,
      LV2_URID_UNMAP_URI,
      LV2_OPTIONS__options,
      LV2_BUF_SIZE__boundedBlockLength,
      LV2_STATE__loadDefaultState,
    };

    LilvNodes *req_features = lilv_plugin_get_required_features (plugin);
    LILV_FOREACH (nodes, i, req_features)
      {
        const LilvNode *feature = lilv_nodes_get (req_features, i);
        if (!supported_features.contains (lilv_node_as_string (feature)))
          {
#ifdef DEBUG_MISSING_FEATURES
            printerr ("LV2: unsupported feature %s required for plugin %s\n", lilv_node_as_string (feature), name.c_str());
#endif
            can_use_plugin = false;
          }
      }
    lilv_nodes_free (req_features);

    return can_use_plugin;
  }
  bool
  required_ui_features_supported (const LilvUI *ui, const String &name)
  {
    const LilvNode *s = lilv_ui_get_uri (ui);
    bool can_use_ui = true;

    set<String> supported_features {
      LV2_INSTANCE_ACCESS_URI,
      LV2_DATA_ACCESS_URI,
      LV2_URID_MAP_URI,
      LV2_URID_UNMAP_URI,
      LV2_OPTIONS__options,
      LV2_UI_PREFIX "makeResident",  // feature is pointless/deprecated so we simply ignore that some plugins want it
    };
    if (lilv_ui_is_a (ui, nodes.lv2_ui_x11ui))
      {
        supported_features.insert (LV2_UI__idleInterface); // SUIL provides this interface for X11 UIs
      }
    if (lilv_ui_is_a (ui, nodes.lv2_ui_external) ||  lilv_ui_is_a (ui, nodes.lv2_ui_externalkx))
      {
        supported_features.insert (lilv_node_as_string (nodes.lv2_ui_externalkx));
      }
    else
      {
        supported_features.insert (LV2_UI__parent);
        supported_features.insert (LV2_UI__resize); // SUIL provides this interface
      }

    LilvNodes *req_features = lilv_world_find_nodes (world, s, nodes.lv2_requiredFeature, nullptr);
    LILV_FOREACH (nodes, i, req_features)
      {
        const LilvNode *feature = lilv_nodes_get (req_features, i);
        if (!supported_features.contains (lilv_node_as_string (feature)))
          {
#ifdef DEBUG_MISSING_FEATURES
            printerr ("LV2: unsupported feature %s required for plugin ui %s\n", lilv_node_as_string (feature), name.c_str());
#endif
            can_use_ui = false;
          }
      }
    lilv_nodes_free (req_features);
    return can_use_ui;
  }
public:
  DeviceInfo
  lv2_device_info (const String &uri)
  {
    if (devs.empty())
      list_plugins();

    return lv2_device_info_map[uri];
  }

  DeviceInfoS
  list_plugins()
  {
    if (!devs.empty())
      return devs;

    const LilvPlugins* plugins = lilv_world_get_all_plugins (world);
    LILV_FOREACH (plugins, i, plugins)
      {
        const LilvPlugin* p = lilv_plugins_get (plugins, i);

        DeviceInfo device_info;
        String lv2_uri = lilv_node_as_uri (lilv_plugin_get_uri (p));
        device_info.uri = "LV2:" + lv2_uri;

        LilvNode* n = lilv_plugin_get_name (p);
        device_info.name = lilv_node_as_string (n);
        lilv_node_free (n);

        auto plugin_class = lilv_plugin_get_class (p);
        device_info.category = string_format ("LV2 %s", lilv_node_as_string (lilv_plugin_class_get_label (plugin_class)));

        if (required_features_supported (p, device_info.name))
          {
            devs.push_back (device_info);
            lv2_device_info_map[lv2_uri] = device_info;

            LilvUIs *uis = lilv_plugin_get_uis (p);
            LILV_FOREACH (uis, u, uis)
              {
                const LilvUI *ui = lilv_uis_get (uis, u);

                // just check required features here for debugging missing features for UIs
                // don't exclude plugin if UI not supported, since we can instantiate the plugin without custom UI
                required_ui_features_supported (ui, device_info.name);
              }
            lilv_uis_free (uis);
          }
      }
    std::stable_sort (devs.begin(), devs.end(), [] (auto &d1, auto &d2) { return string_casecmp (d1.name, d2.name) < 0; });
    return devs;
  }
};

class PluginUI
{
  PluginHost             &plugin_host_;
  bool                    init_ok_ = false;
  bool                    ui_closed_ = false;
  bool                    external_ui_ = false;
  LV2_External_UI_Host    external_ui_host_ {};
  LV2_External_UI_Widget *external_ui_widget_ = nullptr;
  void                   *window_ = nullptr;
  uint                    timer_id_ = 0;
  PluginInstance         *plugin_instance_ = nullptr;
  void                   *ui_instance_ = nullptr;

  bool ui_is_resizable (const LilvUI *ui);
public:
  PluginUI (PluginHost &plugin_host, PluginInstance *plugin_instance, const String &plugin_uri, const String &ui_type_uri, const LilvUI *ui);
  ~PluginUI();
  bool init_ok() const { return init_ok_; }
};

struct PortRestoreHelper
{
  PluginHost &plugin_host;
  map<String, double> values;

  PortRestoreHelper (PluginHost &host) :
    plugin_host (host)
  {
  }

  static void
  set (const char *port_symbol,
       void       *user_data,
       const void *value,
       uint32_t    size,
       uint32_t    type)
  {
    auto &port_restore = *(PortRestoreHelper *) user_data;
    auto &plugin_host = port_restore.plugin_host;

    double dvalue = 0;
    if (type == plugin_host.urids.atom_Float)
      {
        dvalue = *(const float*) value;
      }
    else if (type == plugin_host.urids.atom_Double)
      {
        dvalue = *(const double*) value;
      }
    else if (type == plugin_host.urids.atom_Int)
      {
        dvalue = *(const int32_t*) value;
      }
    else if (type == plugin_host.urids.atom_Long)
      {
        dvalue = *(const int64_t*) value;
      }
    else
      {
        printerr ("error: port restore symbol `%s' value has bad type <%s>\n", port_symbol, plugin_host.urid_map.urid_unmap (type));
        return;
      }
    port_restore.values[port_symbol] = dvalue;
  }
};

}

/// ----------------- PluginUI ------------------

PluginUI::PluginUI (PluginHost &plugin_host, PluginInstance *plugin_instance, const String &plugin_uri,
                    const String &ui_type_uri, const LilvUI *ui) :
  plugin_host_ (plugin_host)
{
  assert_return (this_thread_is_gtk());
  assert_return (PluginHost::the().have_display());

  plugin_instance_ = plugin_instance;

  external_ui_ = lilv_ui_is_a (ui, plugin_host_.nodes.lv2_ui_external) ||
                 lilv_ui_is_a (ui, plugin_host_.nodes.lv2_ui_externalkx);

  String window_title = PluginHost::the().lv2_device_info (plugin_uri).name;

  const char* bundle_uri  = lilv_node_as_uri (lilv_ui_get_bundle_uri (ui));
  const char* binary_uri  = lilv_node_as_uri (lilv_ui_get_binary_uri (ui));
  char*       bundle_path = lilv_file_uri_parse (bundle_uri, nullptr);
  char*       binary_path = lilv_file_uri_parse (binary_uri, nullptr);

  Features ui_features;
  ui_features.add (plugin_instance->instance_access_feature());
  ui_features.add (plugin_instance->data_access_feature());
  ui_features.add (plugin_instance->options_feature());
  ui_features.add (plugin_host_.urid_map.map_feature());
  ui_features.add (plugin_host_.urid_map.unmap_feature());

  if (external_ui_)
    {
      external_ui_host_.ui_closed = [] (LV2UI_Controller controller)
        {
          PluginInstance *plugin_instance = (PluginInstance *) controller;
          if (plugin_instance->plugin_ui())
            plugin_instance->plugin_ui()->ui_closed_ = true; // will destroy UI on next timer iteration
        };
      external_ui_host_.plugin_human_id = strdup (window_title.c_str());

      ui_features.add (LV2_EXTERNAL_UI__Host, &external_ui_host_);
      ui_features.add (LV2_EXTERNAL_UI_DEPRECATED_URI, &external_ui_host_);
    }
  else
    {
      window_ = x11wrapper->create_suil_window (window_title, ui_is_resizable (ui),
        [this] ()
          {
            ui_closed_ = true; // will destroy UI on next timer iteration
          });
      ui_features.add (LV2_UI__parent, window_);
    }

  /* enable DSP->UI notifications - we need to do this before creating the instance because
   * the newly created instance and the DSP code can already start to communicate while
   * the rest of the UI initialization is still being performed
   */
  plugin_instance_->enable_dsp2ui_notifications (true);

  String ui_uri = lilv_node_as_uri (lilv_ui_get_uri (ui));
  String container_ui_uri = external_ui_ ? ui_type_uri : "http://lv2plug.in/ns/extensions/ui#GtkUI";
  ui_instance_ = x11wrapper->create_suil_instance (PluginHost::the().suil_host,
                                                   plugin_instance,
                                                   container_ui_uri,
                                                   plugin_uri,
                                                   ui_uri,
                                                   ui_type_uri,
                                                   bundle_path,
                                                   binary_path,
                                                   ui_features.get_features());
  lilv_free (bundle_path);
  lilv_free (binary_path);

  if (!ui_instance_)
    {
      printerr ("LV2: ui for plugin %s could not be created\n", plugin_uri);
      return;
    }
  if (external_ui_)
    {
      external_ui_widget_ = (LV2_External_UI_Widget *) x11wrapper->get_suil_widget (ui_instance_);
      external_ui_widget_->show (external_ui_widget_);
    }
  else
    {
      x11wrapper->add_suil_widget_to_window (window_, ui_instance_);
    }

  int period_ms = 1000. / plugin_instance->ui_update_fps;
  timer_id_ = x11wrapper->register_timer (
    [this]
      {
        if (ui_closed_)
          {
            plugin_instance_->delete_ui();
            // plugin ui has been destroyed at this point, do not access this after this line
            return false;
          }
        else
          {
            plugin_instance_->handle_dsp2ui_events (ui_instance_);
            if (external_ui_ && external_ui_widget_)
              external_ui_widget_->run (external_ui_widget_);
            return true;
          }
      },
    period_ms);

  plugin_instance->set_initial_controls_ui();

  init_ok_ = true;
}

bool
PluginUI::ui_is_resizable (const LilvUI *ui)
{
  const LilvNode *s = lilv_ui_get_uri (ui);
  bool fixed_size = lilv_world_ask (plugin_host_.world, s, plugin_host_.nodes.lv2_optionalFeature, plugin_host_.nodes.lv2_ui_fixedSize) ||
                    lilv_world_ask (plugin_host_.world, s, plugin_host_.nodes.lv2_optionalFeature, plugin_host_.nodes.lv2_ui_noUserResize);
  return !fixed_size;
}

PluginUI::~PluginUI()
{
  assert_return (this_thread_is_gtk());

  // disable DSP->UI notifications and remove old events from event queue
  plugin_instance_->enable_dsp2ui_notifications (false);
  plugin_instance_->clear_dsp2ui_events();

  if (window_)
    {
      x11wrapper->destroy_suil_window (window_);
      window_ = nullptr;
    }
  if (ui_instance_)
    {
      x11wrapper->destroy_suil_instance (ui_instance_);
      ui_instance_ = nullptr;
    }
  if (timer_id_)
    {
      x11wrapper->remove_timer (timer_id_);
      timer_id_ = 0;
    }
}

/// ----------------- Options ------------------

Options::Options (PluginHost &plugin_host, float sample_rate) :
  m_sample_rate_ (sample_rate),
  lv2_options_feature_ { LV2_OPTIONS__options, nullptr }
{
  const_opts_.push_back ({ LV2_OPTIONS_INSTANCE, 0, plugin_host.urids.param_sampleRate,
                           sizeof (float), plugin_host.urids.atom_Float, &m_sample_rate_ });
  const_opts_.push_back ({ LV2_OPTIONS_INSTANCE, 0, plugin_host.urids.bufsz_minBlockLength,
                           sizeof (int32_t), plugin_host.urids.atom_Int, &m_min_block_length_ });
  const_opts_.push_back ({ LV2_OPTIONS_INSTANCE, 0, plugin_host.urids.bufsz_maxBlockLength,
                           sizeof (int32_t), plugin_host.urids.atom_Int, &m_max_block_length_ });
  const_opts_.push_back ({ LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr });

  lv2_options_feature_.data = const_opts_.data();
}

/// ----------------- PluginHost ------------------

PluginInstance *
PluginHost::instantiate (const char *plugin_uri, uint sample_rate, PortRestoreHelper *port_restore_helper, LV2Processor *processor)
{
  assert_return (this_thread_is_gtk(), nullptr);
  LilvNode* uri = lilv_new_uri (world, plugin_uri);
  if (!uri)
    {
      printerr ("Invalid plugin URI <%s>\n", plugin_uri);
      return nullptr;
    }
  if (!x11wrapper)
    {
      printerr ("LV2: cannot instantiate plugin: missing x11wrapper\n");
      return nullptr;
    }

  const LilvPlugins* plugins = lilv_world_get_all_plugins (world);

  const LilvPlugin*  plugin  = lilv_plugins_get_by_uri (plugins, uri);

  if (!plugin)
    {
      printerr ("plugin is nil\n");
      return nullptr;
    }
  lilv_node_free (uri);

  PluginInstance *plugin_instance = new PluginInstance (*this, sample_rate, plugin, port_restore_helper, processor);
  if (!plugin_instance->init_ok())
    {
      printerr ("plugin instantiate failed\n");
      delete plugin_instance;

      return nullptr;
    }

  return plugin_instance;
}

void
PluginHost::add_instance (PluginInstance *instance)
{
  if (plugin_instances_.empty() && x11wrapper)
    {
      /* do this for the first plugin */
      timer_id_ = x11wrapper->register_timer (
        [this]
          {
            for (auto instance : plugin_instances_)
              instance->gtk_idle_timer();
            return true;
          },
        100);
    }
  plugin_instances_.push_back (instance);
}

void
PluginHost::remove_instance (PluginInstance *instance)
{
  std::erase (plugin_instances_, instance);
  if (plugin_instances_.empty() && timer_id_)
    {
      /* remove if this was the last plugin */
      x11wrapper->remove_timer (timer_id_);
    }
}

void
PluginHost::post_load()
{
  for (auto instance : plugin_instances_)
    instance->finalize_preset_restore();
}

class LV2Processor : public AudioProcessor {
  IBusId              stereo_in_;
  OBusId              stereo_out_;
  vector<IBusId>      mono_ins_;
  vector<OBusId>      mono_outs_;

  ProjectImpl        *project_ = nullptr;
  PluginInstance     *plugin_instance_;
  PluginHost         &plugin_host_;

  int                 current_preset_ = 0;
  String              lv2_uri_;

  enum
    {
      PID_PRESET         = 1,
      PID_CONTROL_OFFSET = 10
    };


  void
  gtk_thread (const std::function<void()> &fun)
  {
    // make calling a function in gtk thread a little shorter by wrapping this
    assert_return (x11wrapper);
    x11wrapper->exec_in_gtk_thread (fun);
  }
  void
  initialize (SpeakerArrangement busses) override
  {
    PortRestoreHelper port_restore_helper (plugin_host_);

    gtk_thread ([&] { plugin_instance_ = plugin_host_.instantiate (lv2_uri_.c_str(), sample_rate(), &port_restore_helper, this); });

    // TODO: this is probably not the right location for restoring the parameter values
    restore_params (port_restore_helper);

    if (!plugin_instance_)
      return;

    ParameterMap pmap;

    if (plugin_instance_->presets().size()) /* choice with 1 entry will crash */
      {
        ChoiceS centries;
        int preset_num = 0;
        centries += { "0", "-none-" };
        for (auto preset : plugin_instance_->presets())
          centries += { string_format ("%d", ++preset_num), preset.name };
        pmap[PID_PRESET] = Param { "device_preset", "Device Preset", "Preset", 0, "", std::move (centries), GUIONLY, { String ("blurb=") + _("Device Preset to be used") } };
      }
    current_preset_ = 0;

    for (uint p = 0; p < plugin_instance_->n_control_inputs(); p++)
      {
        auto &port = plugin_instance_->control_input_port (p);
        String hints = "r:w:";
        if (!(port.flags & Port::HIDDEN))
          hints = "G:" + hints;

        // TODO: this currently only supports the common LV2 stuff we can straight-forward map to our hints
        int pid = PID_CONTROL_OFFSET + port.control_in_idx;
        if (port.flags & Port::ENUMERATION)
          {
            ChoiceS centries;
            for (size_t i = 0; i < port.scale_points.size(); i++)
              centries += { string_format ("%d", i), port.scale_points[i].label };
            pmap[pid] = Param { port.symbol, port.name, "", port.param_from_lv2 (port.control), "", std::move (centries), hints };
          }
        else if (port.flags & Port::LOGARITHMIC)
          pmap[pid] = Param { port.symbol, port.name, "", port.param_from_lv2 (port.control), "", { 0, 1 }, hints };
        else if (port.flags & Port::INTEGER)
          {
            if (port.flags & Port::TOGGLED)
              hints += "toggle:";
            pmap[pid] = Param { port.symbol, port.name, "", port.control, "", { port.min_value, port.max_value, 1 }, hints };
          }
        else
          pmap[pid] = Param { port.symbol, port.name, "", port.control, "", { port.min_value, port.max_value }, hints };
      }

    install_params (pmap);

    prepare_event_input();

    /* map audio inputs/outputs to busses;
     *
     *   channels == 1 -> one mono bus
     *   channels == 2 -> one stereo bus
     *   channels >= 3 -> N mono busses
     *
     * TODO: this is a simple solution - for some plugins we could use
     * https://lv2plug.in/ns/ext/port-groups to do a better job
     */
    mono_ins_.clear();
    mono_outs_.clear();

    uint n_audio_inputs = plugin_instance_->n_audio_inputs();
    if (n_audio_inputs == 2)
      {
        stereo_in_ = add_input_bus ("Stereo In", SpeakerArrangement::STEREO);
        assert_return (bus_info (stereo_in_).ident == "stereo_in");
      }
    else
      {
        for (uint i = 0; i < n_audio_inputs; i++)
          mono_ins_.push_back (add_input_bus (string_format ("Mono In %zd", i + 1), SpeakerArrangement::MONO));
      }

    uint n_audio_outputs = plugin_instance_->n_audio_outputs();
    if (n_audio_outputs == 2)
      {
        stereo_out_ = add_output_bus ("Stereo Out", SpeakerArrangement::STEREO);
        assert_return (bus_info (stereo_out_).ident == "stereo_out");
      }
    else
      {
        for (uint i = 0; i < n_audio_outputs; i++)
          mono_outs_.push_back (add_output_bus (string_format ("Mono Out %zd", i + 1), SpeakerArrangement::MONO));
      }
  }
  void
  reset (uint64 target_stamp) override
  {
    if (!plugin_instance_)
      return;

    adjust_all_params();
  }
  void
  adjust_param (uint32_t tag) override
  {
    if (!plugin_instance_)
      return;

    // controls for LV2Device
    if (int (tag) == PID_PRESET)
      {
        int want_preset = irintf (get_param (tag));
        if (current_preset_ != want_preset && !plugin_instance_->busy_loading_preset())
          {
            current_preset_ = want_preset;
            plugin_instance_->restore_preset_async (want_preset - 1);
          }
      }

    // real LV2 controls start at PID_CONTROL_OFFSET
    auto control_id = tag - PID_CONTROL_OFFSET;
    if (control_id >= 0 && control_id < plugin_instance_->n_control_inputs())
      plugin_instance_->set_control_param (control_id, get_param (tag));
  }
  void
  render (uint n_frames) override
  {
    assert_return (plugin_instance_);

    // reset event buffers and write midi events
    plugin_instance_->reset_event_buffers();
    plugin_instance_->write_position (transport());

    MidiEventInput evinput = midi_event_input();
    for (const auto &ev : evinput)
      {
        const int time_stamp = std::max<int> (ev.frame, 0);
        uint8_t midi_data[3] = { 0, };

        switch (ev.message())
          {
          case MidiMessage::NOTE_OFF:
            midi_data[0] = 0x80 | ev.channel;
            midi_data[1] = ev.key;
            plugin_instance_->write_midi (time_stamp, 3, midi_data);
            break;
          case MidiMessage::NOTE_ON:
            midi_data[0] = 0x90 | ev.channel;
            midi_data[1] = ev.key;
            midi_data[2] = std::clamp (irintf (ev.velocity * 127), 0, 127);
            plugin_instance_->write_midi (time_stamp, 3, midi_data);
            break;
#if 0
          case Message::ALL_NOTES_OFF:
          case Message::ALL_SOUND_OFF:
            synth_.all_sound_off();    // NOTE: there is no extra "all notes off" in liquidsfz
            break;
#endif
          case MidiMessage::PARAM_VALUE:
            apply_event (ev);
            adjust_param (ev.param);
            break;
          default: ;
          }
      }

    const uint n_audio_inputs = plugin_instance_->n_audio_inputs();
    const uint n_audio_outputs = plugin_instance_->n_audio_outputs();

    if (plugin_instance_->busy_loading_preset())
      {
        /* we need to guaranee that run() is not called while loading a preset */
        if (n_audio_outputs == 2)
          {
            floatfill (oblock (stereo_out_, 0), 0, n_frames);
            floatfill (oblock (stereo_out_, 1), 0, n_frames);
          }
        else
          {
            for (uint i = 0; i < n_audio_outputs; i++)
              floatfill (oblock (mono_outs_[i], 0), 0, n_frames);
          }
        return;
      }

    if (n_audio_inputs == 2)
      {
        plugin_instance_->connect_audio_in (0, ifloats (stereo_in_, 0));
        plugin_instance_->connect_audio_in (1, ifloats (stereo_in_, 1));
      }
    else
      {
        for (uint i = 0; i < n_audio_inputs; i++)
          plugin_instance_->connect_audio_in (i, ifloats (mono_ins_[i], 0));
      }

    if (n_audio_outputs == 2)
      {
        plugin_instance_->connect_audio_out (0, oblock (stereo_out_, 0));
        plugin_instance_->connect_audio_out (1, oblock (stereo_out_, 1));
      }
    else
      {
        for (uint i = 0; i < n_audio_outputs; i++)
          plugin_instance_->connect_audio_out (i, oblock (mono_outs_[i], 0));
      }
    plugin_instance_->run (n_frames);
  }
  String
  param_value_to_text (uint32_t paramid, double value) const override
  {
    auto control_id = paramid - PID_CONTROL_OFFSET;
    if (control_id >= 0 && control_id < plugin_instance_->n_control_inputs())
      {
        const Port &port = plugin_instance_->control_input_port (control_id);

        if ((port.flags & Port::ENUMERATION) == 0)
          {
            String text;
            if (port.flags & Port::INTEGER)
              text = string_format ("%d", irintf (port.param_to_lv2 (value)));
            else
              text = string_format ("%.3f", port.param_to_lv2 (value));
            if (port.unit != "")
              text += " " + port.unit;
            return text;
          }
      }
    return AudioProcessor::param_value_to_text (paramid, value);
  }
  double
  param_value_from_text (uint32_t paramid, const String &text) const override
  {
    auto control_id = paramid - PID_CONTROL_OFFSET;
    if (control_id >= 0 && control_id < plugin_instance_->n_control_inputs())
      {
        const Port &port = plugin_instance_->control_input_port (control_id);

        if ((port.flags & Port::ENUMERATION) == 0)
          return port.param_from_lv2 (string_to_double (text));
      }
    return AudioProcessor::param_value_from_text (paramid, text);
  }
public:
  void
  control_in_changed (const Port *port)
  {
    // called if parameters are changed using the LV2 custom UI during render
    set_param_from_render (PID_CONTROL_OFFSET + port->control_in_idx, port->param_from_lv2 (port->control));
  }
  void
  restore_params (const PortRestoreHelper &port_restore_helper)
  {
    for (int i = 0; i < (int) plugin_instance_->n_control_inputs(); i++)
      {
        const Port &port = plugin_instance_->control_input_port (i);
        auto it = port_restore_helper.values.find (port.symbol);
        if (it != port_restore_helper.values.end())
          send_param (i + PID_CONTROL_OFFSET, port.param_from_lv2 (it->second));
      }
  }
  LV2Processor (const ProcessorSetup &psetup) :
    AudioProcessor (psetup),
    plugin_host_ (PluginHost::the())
  {}
  ~LV2Processor()
  {
    destroy_instance();
  }
  void
  destroy_instance()
  {
    if (plugin_instance_)
      {
        gtk_thread ([&] { delete plugin_instance_; });
        plugin_instance_ = nullptr;
      }
  }
  static void
  static_info (AudioProcessorInfo &info)
  {
    info.label = "Anklang.Devices.LV2Processor";
  }
  void
  set_uri (const String &lv2_uri)
  {
    lv2_uri_ = lv2_uri;
  }
  bool
  gui_supported()
  {
    return plugin_host_.have_display() && plugin_instance_->gui_supported();
  }
  void
  gui_toggle()
  {
    if (plugin_host_.have_display())
      gtk_thread ([&] { plugin_instance_->toggle_ui(); });
  }
  void
  save_state (WritNode &xs, const String& device_path, ProjectImpl *project)
  {
    if (project_)
      assert_return (project == project_);
    else
      project_ = project;

    /* save to string */
    PathMap path_map;
    path_map.abstract_path = [&] (const String &path) { String hash; project_->writer_collect (path, &hash); return hash; };

    String str; // ttl files are typically really small, so save to string is perfect
    gtk_thread ([&] { str = plugin_instance_->save_string (&path_map); });

    if (str != "")
      {
        str = zstd_compress (str);
        String blobname = string_format ("lv2-%s.ttl.zst", device_path);
        const String blobfile = project->writer_file_name (blobname);

        if (!Path::stringwrite (blobfile, str, false))
          printerr ("%s: %s: stringwrite failed\n", program_alias(), blobfile);
        else
          {
            Error err = project->writer_add_file (blobfile);
            if (!!err)
              printerr ("%s: %s: %s\n", program_alias(), blobfile, ase_error_blurb (err));
            else
              xs["state_blob"] & blobname;
          }
      }
    else
      {
        printerr ("%s: LV2: save to string failed\n", program_alias());
      }
  }
  void
  load_state (WritNode &xs, ProjectImpl *project)
  {
    if (project_)
      assert_return (project == project_);
    else
      project_ = project;

    String blobname;
    xs["state_blob"] & blobname;
    StreamReaderP blob = blobname.empty() ? nullptr : project->load_blob (blobname);
    if (blob)
      {
        int ret = 0;
        String blob_data;
        std::vector<char> buffer (StreamReader::buffer_size);
        while ((ret = blob->read (buffer.data(), buffer.size())) > 0)
          {
            blob_data.insert (blob_data.end(), buffer.data(), buffer.data() + ret);
          }
        if (ret == 0)
          {
            blob_data = zstd_uncompress (blob_data);
            PathMap path_map;
            path_map.absolute_path = [&] (String hash) { return project_->loader_resolve (hash); };

            PortRestoreHelper port_restore_helper (plugin_host_);
            bool restore_ok = false;
            gtk_thread ([&] { restore_ok = plugin_instance_->restore_string (blob_data, &port_restore_helper, &path_map); });
            if (restore_ok)
              {
                restore_params (port_restore_helper);
              }
            else
              {
                printerr ("%s: LV2Device: blob read error: '%s' LV2 state from string failed\n", program_alias(), blobname);
              }
          }
        else
          {
            printerr ("%s: LV2Device: blob read error: '%s' read failed\n", program_alias(), blobname);
          }
        blob->close();
      }
    else
      {
        printerr ("%s: LV2Device: blob read error: '%s' open failed\n", program_alias(), blobname);
      }
  }
  void
  activate()
  {
    assert_return (plugin_instance_);
    gtk_thread ([&] { plugin_instance_->activate(); });
  }
  void
  deactivate()
  {
    assert_return (plugin_instance_);
    gtk_thread ([&] { plugin_instance_->deactivate(); });
  }
};

static auto lv2processor_aseid = register_audio_processor<LV2Processor>();

/// ----------------- PluginInstance ------------------

PluginInstance::PluginInstance (PluginHost &plugin_host, uint sample_rate, const LilvPlugin *plugin,
                                PortRestoreHelper *port_restore_helper, LV2Processor *processor) :
  options_ (plugin_host, sample_rate),
  plugin_ (plugin),
  sample_rate_ (sample_rate),
  plugin_host_ (plugin_host),
  processor_ (processor)
{
  assert_return (this_thread_is_gtk());

  if (lilv_plugin_has_feature (plugin, plugin_host.nodes.lv2_worker_schedule))
    {
      worker_ = std::make_unique<Worker>();
      features_.add (worker_->feature());
    }

  features_.add (plugin_host.urid_map.map_feature());
  features_.add (plugin_host.urid_map.unmap_feature());
  features_.add (options_.feature());
  features_.add (LV2_BUF_SIZE__boundedBlockLength, nullptr);
  features_.add (LV2_STATE__loadDefaultState, nullptr);

  lv2_atom_forge_init (&forge_, plugin_host.urid_map.lv2_map());

  instance_ = lilv_plugin_instantiate (plugin, sample_rate, features_.get_features());
  if (!instance_)
    {
      printerr ("LV2: failed to create plugin instance");
      return;
    }
  init_ports();
  init_presets();
  if (worker_)
    worker_->set_instance (instance_);

  // provide instance access feature
  lv2_instance_access_feature_ = { LV2_INSTANCE_ACCESS_URI, lilv_instance_get_handle (instance_) };

  // provide data access feature
  lv2_ext_data.data_access = lilv_instance_get_descriptor (instance_)->extension_data;
  lv2_data_access_feature_ = { LV2_DATA_ACCESS_URI, &lv2_ext_data };

  uis_ = lilv_plugin_get_uis (plugin);
  find_plugin_ui();

  if (lilv_plugin_has_feature (plugin, plugin_host.nodes.lv2_state_loadDefaultState))
    {
      // load the plugin as a preset to get default
      if (LilvState *default_state = lilv_state_new_from_world (plugin_host.world, plugin_host.urid_map.lv2_map(), lilv_plugin_get_uri (plugin)))
        {
          restore_state (default_state, port_restore_helper);
          lilv_state_free (default_state);
        }
    }
  plugin_host_.add_instance (this);
  init_ok_ = true;
}

PluginInstance::~PluginInstance()
{
  assert_return (this_thread_is_gtk());

  plugin_host_.remove_instance (this);
  if (worker_)
    worker_->stop();

  if (instance_)
    {
      if (active_)
        deactivate();

      if (instance_)
        {
          lilv_instance_free (instance_);
          instance_ = nullptr;
        }
    }

  free_presets();
  free_ports();

  lilv_uis_free (uis_);
}

void
PluginInstance::init_ports()
{
  const int n_ports = lilv_plugin_get_num_ports (plugin_);

  // don't resize later, otherwise control connections get lost
  plugin_ports_.resize (n_ports);

  vector<float> defaults (n_ports);
  vector<float> min_values (n_ports);
  vector<float> max_values (n_ports);

  lilv_plugin_get_port_ranges_float (plugin_, min_values.data(), max_values.data(), defaults.data());
  for (int i = 0; i < n_ports; i++)
    {
      const LilvPort *port = lilv_plugin_get_port_by_index (plugin_, i);
      if (port)
        {
          int port_buffer_size = 4096;
          LilvNode *min_size = lilv_port_get (plugin_, port, plugin_host_.nodes.lv2_rsz_minimumSize);
          if (min_size && lilv_node_is_int (min_size))
            {
              port_buffer_size = max (lilv_node_as_int (min_size), port_buffer_size);
              lilv_node_free (min_size);
            }

          LilvNode *nname = lilv_port_get_name (plugin_, port);
          plugin_ports_[i].name = lilv_node_as_string (nname);
          lilv_node_free (nname);

          const LilvNode *nsymbol = lilv_port_get_symbol (plugin_, port);
          plugin_ports_[i].symbol = lilv_node_as_string (nsymbol);
          plugin_ports_[i].index = i;

          if (lilv_port_has_property (plugin_, port, plugin_host_.nodes.lv2_pprop_logarithmic))
            {
              // min/max for logarithmic ports should not be zero, max larger than min
              // in theory LV2 allows negative values (as long as they have the same sign), but we don't support that
              if (min_values[i] > 0 && max_values[i] > 0 && max_values[i] > min_values[i])
                plugin_ports_[i].flags |= Port::LOGARITHMIC;
            }
          if (lilv_port_has_property (plugin_, port, plugin_host_.nodes.lv2_pprop_notOnGUI))
            plugin_ports_[i].flags |= Port::HIDDEN;
          if (lilv_port_has_property (plugin_, port, plugin_host_.nodes.lv2_integer))
            plugin_ports_[i].flags |= Port::INTEGER;
          if (lilv_port_has_property (plugin_, port, plugin_host_.nodes.lv2_toggled))
            plugin_ports_[i].flags |= Port::TOGGLED;
          if (lilv_port_has_property (plugin_, port, plugin_host_.nodes.lv2_enumeration))
            {
              LilvScalePoints *points = lilv_port_get_scale_points (plugin_, port);
              LILV_FOREACH (scale_points, j, points)
                {
                  const LilvScalePoint *p     = lilv_scale_points_get (points, j);
                  const LilvNode       *label = lilv_scale_point_get_label (p);
                  const LilvNode       *value = lilv_scale_point_get_value (p);

                  if (label && (lilv_node_is_int (value) || lilv_node_is_float (value)))
                    plugin_ports_[i].scale_points.emplace_back (lilv_node_as_string (label), lilv_node_as_float (value));
                }
              lilv_scale_points_free (points);

              if (plugin_ports_[i].scale_points.size() >= 2) // need scale points for valid enumeration
                plugin_ports_[i].flags |= Port::ENUMERATION;
             }
          std::sort (plugin_ports_[i].scale_points.begin(), plugin_ports_[i].scale_points.end(), [] (auto &p1, auto &p2) { return p1.value < p2.value; });

          if (lilv_port_is_a (plugin_, port, plugin_host_.nodes.lv2_input_class))
            {
              plugin_ports_[i].flags |= Port::INPUT;

              if (lilv_port_is_a (plugin_, port, plugin_host_.nodes.lv2_audio_class))
                {
                  plugin_ports_[i].flags |= Port::AUDIO;
                  audio_in_ports_.push_back (i);
                }
              else if (lilv_port_is_a (plugin_, port, plugin_host_.nodes.lv2_atom_class))
                {
                  plugin_ports_[i].flags |= Port::ATOM;
                  plugin_ports_[i].evbuf = lv2_evbuf_new (port_buffer_size,
                                                          plugin_host_.urid_map.urid_map (lilv_node_as_string (plugin_host_.nodes.lv2_atom_Chunk)),
                                                          plugin_host_.urid_map.urid_map (lilv_node_as_string (plugin_host_.nodes.lv2_atom_Sequence)));
                  lilv_instance_connect_port (instance_, i, lv2_evbuf_get_buffer (plugin_ports_[i].evbuf));

                  if (LilvNodes *atom_supports = lilv_port_get_value (plugin_, port, plugin_host_.nodes.lv2_atom_supports))
                    {
                      if (lilv_nodes_contains (atom_supports, plugin_host_.nodes.lv2_midi_MidiEvent))
                        midi_in_ports_.push_back (i);
                      if (lilv_nodes_contains (atom_supports, plugin_host_.nodes.lv2_time_Position))
                        position_in_ports_.push_back (i);

                      lilv_nodes_free (atom_supports);
                    }

                  atom_in_ports_.push_back (i);
                }
              else if (lilv_port_is_a (plugin_, port, plugin_host_.nodes.lv2_control_class))
                {
                  plugin_ports_[i].flags |= Port::CONTROL;
                  plugin_ports_[i].control = defaults[i];      // start with default value
                  plugin_ports_[i].min_value = min_values[i];
                  plugin_ports_[i].max_value = max_values[i];

                  LilvNodes *units = lilv_port_get_value (plugin_, port, plugin_host_.nodes.lv2_units_unit);
                  LILV_FOREACH (nodes, pos, units)
                    {
                      const LilvNode *unit = lilv_nodes_get (units, pos);

                      auto unit_symbol = [&] (const char *str, const char *sym) {
                        if (strcmp (lilv_node_as_string (unit), str) == 0)
                          plugin_ports_[i].unit = sym;
                      };

                      unit_symbol (LV2_UNITS__bar, "bars");
                      unit_symbol (LV2_UNITS__beat, "beats");
                      unit_symbol (LV2_UNITS__bpm, "BPM");
                      unit_symbol (LV2_UNITS__cent, "ct");
                      unit_symbol (LV2_UNITS__cm, "cm");
                      unit_symbol (LV2_UNITS__coef, "(coef)");
                      unit_symbol (LV2_UNITS__db, "dB");
                      unit_symbol (LV2_UNITS__degree, "deg");
                      unit_symbol (LV2_UNITS__frame, "frames");
                      unit_symbol (LV2_UNITS__hz, "Hz");
                      unit_symbol (LV2_UNITS__inch, "in");
                      unit_symbol (LV2_UNITS__khz, "kHz");
                      unit_symbol (LV2_UNITS__km, "km");
                      unit_symbol (LV2_UNITS__m, "m");
                      unit_symbol (LV2_UNITS__mhz, "MHz");
                      unit_symbol (LV2_UNITS__midiNote, "note");
                      unit_symbol (LV2_UNITS__mile, "mi");
                      unit_symbol (LV2_UNITS__min, "min");
                      unit_symbol (LV2_UNITS__mm, "mm");
                      unit_symbol (LV2_UNITS__ms, "ms");
                      unit_symbol (LV2_UNITS__oct, "oct");
                      unit_symbol (LV2_UNITS__pc, "%");
                      unit_symbol (LV2_UNITS__s, "s");
                      unit_symbol (LV2_UNITS__semitone12TET, "semi");

                      if (LilvNode *symbol = lilv_world_get (plugin_host_.world, unit, plugin_host_.nodes.lv2_units_symbol, nullptr))
                        {
                          if (auto sym = lilv_node_as_string (symbol))
                            plugin_ports_[i].unit = sym;
                          lilv_node_free (symbol);
                        }
                    }
                  lilv_nodes_free (units);

                  lilv_instance_connect_port (instance_, i, &plugin_ports_[i].control);

                  plugin_ports_[i].control_in_idx = control_in_ports_.size();
                  control_in_ports_.push_back (i);
                }
              else
                {
                  printerr ("LV2: found unknown input port\n");
                }
            }
          if (lilv_port_is_a (plugin_, port, plugin_host_.nodes.lv2_output_class))
            {
              plugin_ports_[i].flags |= Port::OUTPUT;
              if (lilv_port_is_a (plugin_, port, plugin_host_.nodes.lv2_audio_class))
                {
                  plugin_ports_[i].flags |= Port::AUDIO;
                  audio_out_ports_.push_back (i);
                }
              else if (lilv_port_is_a (plugin_, port, plugin_host_.nodes.lv2_atom_class))
                {
                  plugin_ports_[i].flags |= Port::ATOM;
                  atom_out_ports_.push_back (i);

                  plugin_ports_[i].evbuf = lv2_evbuf_new (port_buffer_size,
                                                          plugin_host_.urid_map.urid_map (lilv_node_as_string (plugin_host_.nodes.lv2_atom_Chunk)),
                                                          plugin_host_.urid_map.urid_map (lilv_node_as_string (plugin_host_.nodes.lv2_atom_Sequence)));
                  lilv_instance_connect_port (instance_, i, lv2_evbuf_get_buffer (plugin_ports_[i].evbuf));

                }
              else if (lilv_port_is_a (plugin_, port, plugin_host_.nodes.lv2_control_class))
                {
                  plugin_ports_[i].flags |= Port::CONTROL;
                  plugin_ports_[i].control = defaults[i];      // start with default value

                  lilv_instance_connect_port (instance_, i, &plugin_ports_[i].control);
                }
              else
                {
                  printerr ("LV2: found unknown output port\n");
                }
            }
        }
    }
  const LilvPort *bpm_port = lilv_plugin_get_port_by_designation (plugin_,
                                                                  plugin_host_.nodes.lv2_input_class,
                                                                  plugin_host_.nodes.lv2_time_beatsPerMinute);
  if (bpm_port)
    {
      bpm_port_index_ = lilv_port_get_index (plugin_, bpm_port);
      plugin_ports_[bpm_port_index_].flags |= Port::HIDDEN;
    }

  if (midi_in_ports_.size() > 1)
    printerr ("LV2: more than one midi input found - this is not supported\n");
  if (position_in_ports_.size() > 1)
    printerr ("LV2: more than one time position input found - this is not supported\n");
  printerr ("--------------------------------------------------\n");
  printerr ("audio IN:%zd OUT:%zd\n", audio_in_ports_.size(), audio_out_ports_.size());
  printerr ("control IN:%zd\n", control_in_ports_.size());
  printerr ("--------------------------------------------------\n");
}

void
PluginInstance::free_ports()
{
  for (auto &port : plugin_ports_)
    {
      if (port.evbuf)
        lv2_evbuf_free (port.evbuf);
    }
  plugin_ports_.clear();
}

void
PluginInstance::init_presets()
{
  LilvNodes *lilv_presets = lilv_plugin_get_related (plugin_, plugin_host_.nodes.lv2_presets_Preset);
  LILV_FOREACH (nodes, i, lilv_presets)
    {
      const LilvNode *preset = lilv_nodes_get (lilv_presets, i);
      lilv_world_load_resource (plugin_host_.world, preset);
      LilvNodes *labels = lilv_world_find_nodes (plugin_host_.world, preset, plugin_host_.nodes.rdfs_label, NULL);
      if (labels)
        {
          LilvNode *label = lilv_nodes_get_first (labels);
          presets_.push_back ({lilv_node_as_string (label), lilv_node_duplicate (preset)});
          lilv_nodes_free (labels);
        }
    }
  lilv_nodes_free (lilv_presets);
}

void
PluginInstance::free_presets()
{
  for (auto &preset : presets_)
    lilv_node_free (preset.preset);
  presets_.clear();
}

void
PluginInstance::write_midi (uint32_t time, size_t size, const uint8_t *data)
{
  if (midi_in_ports_.empty())
    return;

  LV2_Evbuf           *evbuf = plugin_ports_[midi_in_ports_.front()].evbuf;
  LV2_Evbuf_Iterator    iter = lv2_evbuf_end (evbuf);

  lv2_evbuf_write (&iter, time, 0, plugin_host_.urids.midi_MidiEvent, size, data);
}

void
PluginInstance::write_position (const AudioTransport &transport)
{
  const auto &tick_sig = transport.tick_sig;

  if (bpm_port_index_ >= 0)
    plugin_ports_[bpm_port_index_].control = tick_sig.bpm();

  if (position_in_ports_.empty())
    return;

  uint64 frames_since_start = llrint (transport.current_seconds * transport.samplerate) + transport.current_minutes * 60 * transport.samplerate;

  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_set_buffer (&forge_, position_buffer_.data(), position_buffer_.size());
  lv2_atom_forge_object (&forge_, &frame, 0, plugin_host_.urids.time_Position);
  lv2_atom_forge_key    (&forge_, plugin_host_.urids.time_frame);
  lv2_atom_forge_long   (&forge_, frames_since_start);
  lv2_atom_forge_key    (&forge_, plugin_host_.urids.time_speed);
  lv2_atom_forge_float  (&forge_, transport.running() ? 1.0 : 0.0);
  lv2_atom_forge_key    (&forge_, plugin_host_.urids.time_bar);
  lv2_atom_forge_long   (&forge_, transport.current_bar);
  lv2_atom_forge_key    (&forge_, plugin_host_.urids.time_barBeat);
  lv2_atom_forge_float  (&forge_, transport.current_beat + transport.current_semiquaver / 16.);
  lv2_atom_forge_key    (&forge_, plugin_host_.urids.time_beatUnit);
  lv2_atom_forge_int    (&forge_, tick_sig.beat_unit());
  lv2_atom_forge_key    (&forge_, plugin_host_.urids.time_beatsPerBar);
  lv2_atom_forge_float  (&forge_, tick_sig.beats_per_bar());
  lv2_atom_forge_key    (&forge_, plugin_host_.urids.time_beatsPerMinute);
  lv2_atom_forge_float  (&forge_, tick_sig.bpm());

  const LV2_Atom *lv2_pos = (LV2_Atom *) position_buffer_.data();
  const size_t buffer_used = lv2_pos->size + sizeof (LV2_Atom);
  if (!std::equal (position_buffer_.begin(), position_buffer_.begin() + buffer_used, last_position_buffer_.begin()))
    {
      LV2_Evbuf           *evbuf = plugin_ports_[position_in_ports_.front()].evbuf;
      LV2_Evbuf_Iterator    iter = lv2_evbuf_end (evbuf);

      lv2_evbuf_write (&iter, 0, 0, lv2_pos->type, lv2_pos->size, (uint8 *) LV2_ATOM_BODY (lv2_pos));
      std::copy (position_buffer_.begin(), position_buffer_.begin() + buffer_used, last_position_buffer_.begin());
    }
}

void
PluginInstance::reset_event_buffers()
{
  for (int p : atom_out_ports_)
    {
      /* Clear event output for plugin to write to */
      LV2_Evbuf *evbuf = plugin_ports_[p].evbuf;

      lv2_evbuf_reset (evbuf, false);
    }
  for (int p : atom_in_ports_)
    {
      LV2_Evbuf *evbuf = plugin_ports_[p].evbuf;

      lv2_evbuf_reset (evbuf, true);
    }
}

void
PluginInstance::activate()
{
  assert_return (this_thread_is_gtk());
  if (!active_)
    {
      printerr ("activate\n");
      lilv_instance_activate (instance_);
      active_ = true;
    }
}

void
PluginInstance::deactivate()
{
  assert_return (this_thread_is_gtk());
  if (active_)
    {
      printerr ("deactivate\n");
      lilv_instance_deactivate (instance_);
      active_ = false;
    }
}

void
PluginInstance::connect_audio_in (uint32_t input_port, const float *buffer)
{
  lilv_instance_connect_port (instance_, audio_in_ports_[input_port], const_cast<float *> (buffer));
}

void
PluginInstance::connect_audio_out (uint32_t output_port, float *buffer)
{
  lilv_instance_connect_port (instance_, audio_out_ports_[output_port], buffer);
}

void
PluginInstance::enable_dsp2ui_notifications (bool enabled)
{
  dsp2ui_notifications_enabled_.store (enabled);
}

void
PluginInstance::send_plugin_events_to_ui()
{
  for (int port_index : atom_out_ports_)
    {
      LV2_Evbuf *evbuf = plugin_ports_[port_index].evbuf;

      for (LV2_Evbuf_Iterator i = lv2_evbuf_begin (evbuf); lv2_evbuf_is_valid (i); i = lv2_evbuf_next (i))
        {
          uint32_t frames, subframes, type, size;
          void    *body;
          lv2_evbuf_get (i, &frames, &subframes, &type, &size, &body);

          ControlEvent *event = ControlEvent::loft_new (port_index, plugin_host_.urids.atom_eventTransfer, sizeof (LV2_Atom) + size);

          LV2_Atom *atom = (LV2_Atom *) event->data();
          atom->type = type;
          atom->size = size;

          memcpy (event->data() + sizeof (LV2_Atom), body, size);
          // printerr ("send_plugin_events_to_ui: push event: index=%zd, protocol=%d, sz=%zd\n", event->port_index, event->protocol, event->data.size());
          dsp2ui_events_.push (event);
        }
    }
}

void
PluginInstance::handle_dsp2ui_events (void *ui_instance)
{
  assert_return (this_thread_is_gtk());
  dsp2ui_events_.for_each (trash_events_,
    [&] (const ControlEvent *event)
      {
        assert_return (plugin_ui_ && event->port_index() < plugin_ports_.size());
        // printerr ("handle_dsp2ui_events: pop event: index=%zd, protocol=%d, sz=%zd\n", event->port_index(), event->protocol(), event->size());
        x11wrapper->suil_instance_port_event (ui_instance, event->port_index(), event->size(), event->protocol(), event->data());
      });
  // free both: old dsp2ui events and old ui2dsp events
  trash_events_.free_all();
}

void
PluginInstance::set_initial_controls_ui()
{
  /* Set initial control values on UI */
  for (size_t port_index = 0; port_index < plugin_ports_.size(); port_index++)
    {
      const auto &port = plugin_ports_[port_index];

      if (port.flags & Port::CONTROL)
        {
          ControlEvent *event = ControlEvent::loft_new (port_index, 0, sizeof (float), &port.control);
          dsp2ui_events_.push (event);
        }
    }
}

void
PluginInstance::set_control_param (size_t index, double param_value)
{
  auto &port = plugin_ports_[control_in_ports_[index]];
  // param_value is the parameter value used in LV2Processor and needs to be converted to lv2 value first
  port.control = port.param_to_lv2 (param_value);

  if (dsp2ui_notifications_enabled_.load())
    {
      ControlEvent *event = ControlEvent::loft_new (port.index, 0, sizeof (float), &port.control);
      dsp2ui_events_.push (event);
    }
}

void
PluginInstance::send_ui_updates (uint32_t delta_frames)
{
  ui_update_frame_count_ += delta_frames;

  uint update_n_frames = sample_rate_ / ui_update_fps;
  if (ui_update_frame_count_ >= update_n_frames)
    {
      ui_update_frame_count_ -= update_n_frames;
      if (ui_update_frame_count_ > update_n_frames)
        {
          /* corner case: if block size is very large, we simply need to update every time */
          ui_update_frame_count_ = update_n_frames;
        }
      for (size_t port_index = 0; port_index < plugin_ports_.size(); port_index++)
        {
          const Port &port = plugin_ports_[port_index];

          if ((port.flags & Port::CONTROL) && (port.flags & Port::OUTPUT))
            {
              ControlEvent *event = ControlEvent::loft_new (port_index, 0, sizeof (float), &port.control);
              dsp2ui_events_.push (event);
            }
        }
    }
}

void
PluginInstance::find_plugin_ui()
{
  const LilvNode *ui_type {};
  LILV_FOREACH (uis, u, uis_)
    {
      const LilvUI *this_ui = lilv_uis_get (uis_, u);

      if (lilv_ui_is_supported (this_ui,
                                [](const char *host_type_uri, const char *ui_type_uri)
                                  {
                                    return x11wrapper->suil_ui_supported (host_type_uri, ui_type_uri);
                                  },
                                plugin_host_.nodes.native_ui_type,
                               &ui_type))
        {
          ui_          = this_ui;
          ui_type_uri_ = lilv_node_as_uri (ui_type);
          return;
        }
    }
  // if no suil supported UI is available try external UI
  LILV_FOREACH (uis, u, uis_)
    {
      const LilvUI *this_ui = lilv_uis_get (uis_, u);

      if (lilv_ui_is_a (this_ui, plugin_host_.nodes.lv2_ui_externalkx))
        {
          ui_          = this_ui;
          ui_type      = plugin_host_.nodes.lv2_ui_externalkx;
          ui_type_uri_ = lilv_node_as_uri (ui_type);
          return;
        }
      if (lilv_ui_is_a (this_ui, plugin_host_.nodes.lv2_ui_external))
        {
          ui_          = this_ui;
          ui_type      = plugin_host_.nodes.lv2_ui_external;
          ui_type_uri_ = lilv_node_as_uri (ui_type);
          return;
        }
    }
}

void
PluginInstance::toggle_ui()
{
  if (plugin_ui_) // ui already opened? -> close!
    {
      plugin_ui_.reset();
      return;
    }
  // ---------------------ui------------------------------
  if (ui_)
    {
      String plugin_uri  = lilv_node_as_uri (lilv_plugin_get_uri (plugin_));
      plugin_ui_ = std::make_unique <PluginUI> (plugin_host_, this, plugin_uri, ui_type_uri_, ui_);

      // if UI could not be created (for whatever reason) reset pointer to nullptr to free stuff and avoid crashes
      if (!plugin_ui_->init_ok())
        plugin_ui_.reset();
    }
}

void
PluginInstance::delete_ui()
{
  assert_return (this_thread_is_gtk());
  plugin_ui_.reset();
}

void
PluginInstance::clear_dsp2ui_events()
{
  dsp2ui_events_.free_all();
}

void
PluginInstance::run (uint32_t n_frames)
{
  ui2dsp_events_.for_each (trash_events_,
    [&] (const ControlEvent *event)
      {
        assert_return (event->port_index() < plugin_ports_.size());
        Port* port = &plugin_ports_[event->port_index()];
        if (event->protocol() == 0)
          {
            assert_return (event->size() == sizeof (float));
            port->control = *(float *) event->data();

            processor_->control_in_changed (port);
          }
        else if (event->protocol() == plugin_host_.urids.atom_eventTransfer)
          {
            LV2_Evbuf_Iterator    e    = lv2_evbuf_end (port->evbuf);
            const LV2_Atom* const atom = (const LV2_Atom *) event->data();
            lv2_evbuf_write (&e, n_frames, 0, atom->type, atom->size, (const uint8_t*) LV2_ATOM_BODY_CONST (atom));
          }
        else
          {
            printerr ("LV2: PluginInstance: protocol: %d not implemented\n", event->protocol());
          }
      });

  lilv_instance_run (instance_, n_frames);

  if (worker_)
    {
      worker_->handle_responses();
      worker_->end_run();
    }

  if (dsp2ui_notifications_enabled_.load())
    {
      send_plugin_events_to_ui();
      send_ui_updates (n_frames);
    }
}

void
PluginInstance::restore_state (LilvState *state, PortRestoreHelper *helper, PathMap *path_map)
{
  assert_return (this_thread_is_gtk());

  Features restore_features;
  if (path_map)
    {
      restore_features.add (LV2_STATE__mapPath, &path_map->map_path);
      restore_features.add (LV2_STATE__freePath, &path_map->free_path);
    }
  restore_features.add (plugin_host_.urid_map.map_feature());
  restore_features.add (plugin_host_.urid_map.unmap_feature());

  lilv_state_restore (state, instance_, PortRestoreHelper::set, helper, 0, restore_features.get_features());
}

bool
PluginInstance::restore_string (const String &str, PortRestoreHelper *helper, PathMap *path_map)
{
  assert_return (this_thread_is_gtk(), false);

  if (LilvState *state = lilv_state_new_from_string (plugin_host_.world, plugin_host_.urid_map.lv2_map(), str.c_str()))
    {
      restore_state (state, helper, path_map);
      lilv_state_free (state);
      return true;
    }
  else
    {
      return false;
    }
}

const void*
PluginInstance::get_port_value_for_save (const char *port_symbol,
                                         void       *user_data,
                                         uint32_t   *size,
                                         uint32_t   *type)
{
  PluginInstance *plugin_instance = (PluginInstance *) user_data;
  for (auto &port : plugin_instance->plugin_ports_)
    {
      if (port.symbol == port_symbol && (port.flags & Port::INPUT) && (port.flags & Port::CONTROL))
        {
          *size = sizeof (float);
          *type = plugin_instance->plugin_host_.urids.atom_Float;
          return &port.control;
        }
    }
  *size = *type = 0;
  return nullptr;
}

String
PluginInstance::save_string (PathMap *path_map)
{
  assert_return (this_thread_is_gtk(), "");

  Features save_features;
  if (path_map)
    {
      save_features.add (LV2_STATE__mapPath, &path_map->map_path);
      save_features.add (LV2_STATE__freePath, &path_map->free_path);
    }
  save_features.add (plugin_host_.urid_map.map_feature());
  save_features.add (plugin_host_.urid_map.unmap_feature());

  String str;
  LilvState *state = lilv_state_new_from_instance (plugin_,
                                                   instance_,
                                                   plugin_host_.urid_map.lv2_map(),
                                                   nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   get_port_value_for_save,
                                                   this,
                                                   0,
                                                   save_features.get_features());
  char *c_str = lilv_state_to_string (plugin_host_.world,
                                      plugin_host_.urid_map.lv2_map(),
                                      plugin_host_.urid_map.lv2_unmap(),
                                      state,
                                      ANKLANG_STATE_URI,
                                      nullptr);
  str = c_str;
  free (c_str);
  lilv_state_free (state);
  return str;
}

void
PluginInstance::host_ui_write (void          *controller,
                               uint32_t       port_index,
                               uint32_t       buffer_size,
                               uint32_t       protocol,
                               const void*    buffer)
{
  PluginInstance *plugin_instance = (PluginInstance *) controller;
  ControlEvent *event = ControlEvent::loft_new (port_index, protocol, buffer_size, buffer);
  plugin_instance->ui2dsp_events_.push (event);
}

uint32_t
PluginInstance::host_ui_index (void *controller, const char* symbol)
{
  PluginInstance *plugin_instance = (PluginInstance *) controller;
  const vector<Port> &ports = plugin_instance->plugin_ports_;
  for (size_t i = 0; i < ports.size(); i++)
    if (ports[i].symbol == symbol)
      return i;
  return LV2UI_INVALID_PORT_INDEX;
}


void
PluginInstance::restore_preset (int preset, PortRestoreHelper *helper)
{
  assert_return (this_thread_is_gtk());
  assert_return (preset >= 0 && preset < int (presets_.size()));

  if (LilvState *state = lilv_state_new_from_world (plugin_host_.world, plugin_host_.urid_map.lv2_map(), presets_[preset].preset))
    {
      restore_state (state, helper);
      lilv_state_free (state);
    }
}

/*
 * Asynchronous preset loading: a user can select a new preset in the UI -
 * since this is a choice propery of the LV2Processor, the audio thread will
 * get notified during render() that a preset should be loaded, but we don't
 * want to stall the audio thread to actually load the preset (which can take a
 * while, depending on the plugin). See also #37.
 *
 * So loading a preset works like this:
 *  - audio thread checks if the preset choice property has been changed by the user
 *  - audio thread sets preset_to_load_ with the new preset if choice has been changed
 *  - audio thread stores PRESET_STATE_LOAD to the preset_state_ std::atomic
 *  - gtk thread periodically scans all PluginInstance objects if std::atomic contains PRESET_STATE_LOAD
 *  - if so, it restores the preset preset_to_load_ using restore_preset()
 *  - gtk thread sets std::atomic to PRESET_STATE_FINALIZE
 *  - gtk thread wakes up ase thread
 *  - ase thread loops over all PluginInstance objects and checks if std::atomic contains PRESET_STATE_FINALIZE
 *  - ase thread applies the parameters to the plugin and resets std::atomic to PRESET_STATE_READY
 *
 * Since LV2 does not allow preset loading and calling the run function at the
 * same time, the AudioProcessor checks if the std::atomic contains
 * PRESET_STATE_READY before calling the run function. Only if it does, it calls
 * the run() function. Otherwise, it fills all output buffers with zeros.
 */
void
PluginInstance::restore_preset_async (int preset)
{
  preset_to_load_ = preset;
  preset_state_.store (PRESET_STATE_LOAD);
}

bool
PluginInstance::busy_loading_preset()
{
  return preset_state_.load() != PRESET_STATE_READY;
}

void
PluginInstance::gtk_idle_timer()
{
  if (preset_state_.load() == PRESET_STATE_LOAD)
    {
      assert_return (this_thread_is_gtk());

      preset_port_restore_helper_ = std::make_unique<PortRestoreHelper> (plugin_host_);
      restore_preset (preset_to_load_, preset_port_restore_helper_.get());

      // send new parameters from ASE thread
      preset_state_.store (PRESET_STATE_FINALIZE);

      // TODO: it may be possible to restore the parameters here as well, however
      // this would mean that AudioProcessor::send_param should be supported from
      // the gtk thread as well
      main_loop->exec_idle ([] { PluginHost::the().post_load(); });
    }
}

void
PluginInstance::finalize_preset_restore()
{
  if (preset_state_.load() == PRESET_STATE_FINALIZE)
    {
      assert_return (this_thread_is_ase());

      processor_->restore_params (*preset_port_restore_helper_.get());
      preset_port_restore_helper_.reset();
      preset_state_.store (PRESET_STATE_READY);
    }
}

/// ----------------- LV2DeviceImpl ------------------

DeviceInfoS
LV2DeviceImpl::list_lv2_plugins()
{
  PluginHost &plugin_host = PluginHost::the();
  return plugin_host.list_plugins();
}

DeviceP
LV2DeviceImpl::create_lv2_device (AudioEngine &engine, const String &lv2_uri_with_prefix)
{
  assert_return (string_startswith (lv2_uri_with_prefix, "LV2:"), nullptr);
  const String lv2_uri = lv2_uri_with_prefix.substr (4);

  auto make_device = [lv2_uri] (const String &aseid, AudioProcessor::StaticInfo static_info, AudioProcessorP aproc) -> LV2DeviceImplP {
    /* TODO: is this good code to handle LV2Processor URI initialization */
    auto lv2aproc = dynamic_cast<LV2Processor *> (aproc.get());
    lv2aproc->set_uri (lv2_uri);

    return LV2DeviceImpl::make_shared (lv2_uri, aproc);
  };
  DeviceP devicep = AudioProcessor::registry_create (lv2processor_aseid, engine, make_device);
  // return_unless (devicep && devicep->_audio_processor("Ase::Device::LV2Processor", nullptr);
  return devicep;
}

bool
LV2DeviceImpl::gui_supported()
{
  auto lv2aproc = dynamic_cast<LV2Processor *> (proc_.get());
  return lv2aproc->gui_supported();
}

void
LV2DeviceImpl::gui_toggle()
{
  auto lv2aproc = dynamic_cast<LV2Processor *> (proc_.get());
  lv2aproc->gui_toggle();
}

LV2DeviceImpl::LV2DeviceImpl (const String &lv2_uri, AudioProcessorP proc) :
  proc_ (proc), info_ (PluginHost::the().lv2_device_info (lv2_uri))
{
}

PropertyS
LV2DeviceImpl::access_properties ()
{
  return proc_->access_properties();
}

void
LV2DeviceImpl::serialize (WritNode &xs)
{
  DeviceImpl::serialize (xs);

  if (auto lv2aproc = dynamic_cast<LV2Processor *> (proc_.get()))
    {
      if (xs.in_save())
        lv2aproc->save_state (xs, get_device_path(), _project());
      if (xs.in_load())
        lv2aproc->load_state (xs, _project());
    }
}

void
LV2DeviceImpl::_activate()
{
  DeviceImpl::_activate();
  auto lv2aproc = dynamic_cast<LV2Processor *> (proc_.get());
  lv2aproc->activate();
}

void
LV2DeviceImpl::_deactivate()
{
  auto lv2aproc = dynamic_cast<LV2Processor *> (proc_.get());
  lv2aproc->deactivate();
  DeviceImpl::_deactivate();
}

void
LV2DeviceImpl::_set_parent (GadgetImpl *parent)
{
  DeviceImpl::_set_parent (parent);
  // deactivate and destroy plugin
  if (!parent)
    {
      auto lv2aproc = dynamic_cast<LV2Processor *> (proc_.get());
      if (lv2aproc)
        lv2aproc->destroy_instance();
    }
}


} // Ase

/* --- TODO ---
 *
 * - some plugins (with lots of properties?) freeze UI - padthv1, drmr (#31)
 * - ui resizing doesn't seem to work for SpectMorph
 */
