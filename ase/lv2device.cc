// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "processor.hh"
#include "main.hh"
#include "internal.hh"
#include "strings.hh"
#include "loft.hh"

#include "lv2/atom/atom.h"
#include "lv2/midi/midi.h"
#include "lv2/options/options.h"
#include "lv2/parameters/parameters.h"
#include "lv2/buf-size/buf-size.h"
#include "lv2/worker/worker.h"
#include "lv2/presets/presets.h"
#include "lv2/data-access/data-access.h"
#include "lv2/ui/ui.h"

#include "lv2evbuf.hh"
#include "lv2ringbuffer.hh"
#include "lv2device.hh"

#include <lilv/lilv.h>
#include <dlfcn.h>

// X11 wrapper
#include "clapplugin.hh"

namespace Ase {

namespace
{

#define NS_EXT "http://lv2plug.in/ns/ext/"

using std::vector;
using std::string;
using std::map;
using std::max;
using std::min;

class Map
{
  std::mutex            map_mutex;
  LV2_URID              next_id;
  map<string, LV2_URID> m_urid_map;
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

    LV2_URID& id = m_urid_map[str];
    if (id == 0)
      id = next_id++;

    m_urid_unmap[id] = str;
    printf ("map %s -> %d\n", str, id);
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
};

class PluginHost;

class Options
{
  PluginHost& plugin_host;
  float       m_sample_rate;
  uint32_t    m_block_length;

  vector<LV2_Options_Option> const_opts;

  LV2_Feature  lv2_options_feature;
public:
  Options (PluginHost& plugin_host);
  void
  set (float sample_rate, int32_t block_length)
  {
    m_sample_rate   = sample_rate;
    m_block_length  = block_length;
  }
  const LV2_Feature *
  feature() const
  {
    return &lv2_options_feature;
  }
};

class Worker
{
  LV2_Worker_Schedule lv2_worker_sched;
  const LV2_Feature   lv2_worker_feature;

  const LV2_Worker_Interface *worker_interface = nullptr;
  LV2_Handle                  instance = nullptr;
  RingBuffer<uint8>           work_buffer_;
  RingBuffer<uint8>           response_buffer_;
  std::thread                 thread_;
  std::atomic<int>            quit_;
  ScopedSemaphore             sem_;
public:
  Worker() :
    lv2_worker_sched { this, schedule },
    lv2_worker_feature { LV2_WORKER__schedule, &lv2_worker_sched },
    work_buffer_ (4096),
    response_buffer_ (4096),
    quit_ (0)
  {
    thread_ = std::thread (&Worker::run, this);
  }
  void
  stop()
  {
    quit_ = 1;
    sem_.post();
    thread_.join();
    printf ("worker thread joined\n");
  }

  void
  set_instance (LilvInstance *lilv_instance)
  {
    instance = lilv_instance_get_handle (lilv_instance);

    const LV2_Descriptor *descriptor = lilv_instance_get_descriptor (lilv_instance);
    if (descriptor && descriptor->extension_data)
       worker_interface = (const LV2_Worker_Interface *) (*descriptor->extension_data) (LV2_WORKER__interface);
  }

  void
  run()
  {
    while (!quit_)
      {
        sem_.wait();
        while (work_buffer_.get_readable_values())
          {
            uint32 size;
            work_buffer_.read (sizeof (size), (uint8 *) &size);
            uint8 data[size];
            work_buffer_.read (size, data);

            printf ("got work %d bytes\n", size);
            worker_interface->work (instance, respond, this, size, data);
          }
      }
  }

  LV2_Worker_Status
  send_data (RingBuffer<uint8>& ring_buffer, uint32_t size, const void *data)
  {
    const uint32 n_values = sizeof (size) + size;
    if (n_values <= ring_buffer.get_writable_values())
      {
        uint8 to_write[n_values];
        memcpy (to_write, &size, sizeof (size));
        memcpy (to_write + sizeof (size), data, size);

        ring_buffer.write (n_values, to_write);
        return LV2_WORKER_SUCCESS;
      }
    else
      {
        return LV2_WORKER_ERR_NO_SPACE;
      }
  }
  LV2_Worker_Status
  schedule (uint32_t size, const void *data)
  {
    if (!worker_interface)
      return LV2_WORKER_ERR_UNKNOWN;

    auto rc = send_data (work_buffer_, size, data);
    sem_.post();
    return rc;
  }
  LV2_Worker_Status
  respond (uint32_t size, const void *data)
  {
    if (!worker_interface)
      return LV2_WORKER_ERR_UNKNOWN;

    printf ("queue work response\n");
    return send_data (response_buffer_, size, data);
  }
  void
  handle_responses()
  {
    while (response_buffer_.get_readable_values())
      {
        uint32 size;
        response_buffer_.read (sizeof (size), (uint8 *) &size);
        uint8 data[size];
        response_buffer_.read (size, data);

        printf ("got work response %d bytes\n", size);
        worker_interface->work_response (instance, size, data);
      }
  }
  void
  end_run()
  {
    /* to be called after each run cycle */
    if (worker_interface && worker_interface->end_run)
      worker_interface->end_run (instance);
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

  const LV2_Feature *
  feature() const
  {
    return &lv2_worker_feature;
  }
};


class Features
{
  std::vector<const LV2_Feature *> features;
public:
  Features()
  {
    features.push_back (nullptr);
  }
  const LV2_Feature * const*
  get_features()
  {
    return &features[0];
  }
  void
  add (const LV2_Feature *lv2_feature)
  {
    // preserve nullptr termination
    assert_return (!features.empty());

    features.back() = lv2_feature;
    features.push_back (nullptr);
  }
};

struct Port
{
  LV2_Evbuf  *evbuf;
  float       control;    /* for control ports */
  float       min_value;  /* min control */
  float       max_value;  /* max control */
  String      name;
  String      symbol;

  enum {
    UNKNOWN,
    CONTROL_IN,
    CONTROL_OUT
  }           type;

  Port() :
    evbuf (nullptr),
    control (0.0),
    type (UNKNOWN)
  {
  }
};

struct PresetInfo
{
  String          name;
  const LilvNode *preset = nullptr;
};

struct ControlEvent
{
private:
  LoftPtr<ControlEvent>       loft_ptr_;    // keep this object alive
  uint32_t                    port_index_;
  uint32_t                    protocol_;
  size_t                      size_;
  LoftPtr<void>               data_;

 public:
  std::atomic<ControlEvent *> next_ = nullptr;

  static ControlEvent *
  loft_new (uint32_t port_index, uint32_t protocol, size_t size)
  {
    LoftPtr<ControlEvent> loft_ptr = loft_make_unique<ControlEvent>();
    ControlEvent *new_event = loft_ptr.get();
    new_event->loft_ptr_ = std::move (loft_ptr);
    new_event->port_index_ = port_index;
    new_event->protocol_ = protocol;
    new_event->size_ = size;
    new_event->data_ = loft_alloc (size);
    return new_event;
  }
  void
  loft_free()
  {
    loft_ptr_.reset(); // do not access this after this line
  }
  uint32_t port_index() { return port_index_; }
  uint32_t protocol()   { return protocol_; }
  size_t   size()       { return size_; }
  uint8_t *data()       { return reinterpret_cast<uint8_t *> (data_.get()); }
};

static inline std::atomic<ControlEvent*>&
atomic_next_ptrref (ControlEvent *event)
{
  return event->next_;
}

struct PluginUI;

struct PluginInstance
{
  PluginHost& plugin_host;
  std::unique_ptr<PluginUI>  plugin_ui;

  LV2_Extension_Data_Feature lv2_ext_data;
  LV2UI_Resize               ui_resize;

  Features features;

  Worker   worker;

  PluginInstance (PluginHost& plugin_host);
  ~PluginInstance();

  const LilvPlugin             *plugin = nullptr;
  LilvInstance                 *instance = nullptr;
  const LV2_Worker_Interface   *worker_interface = nullptr;
  std::vector<Port>             plugin_ports;
  std::vector<int>              atom_out_ports;
  std::vector<int>              atom_in_ports;
  std::vector<int>              audio_in_ports;
  std::vector<int>              audio_out_ports;
  std::vector<PresetInfo>       presets;
  bool                          active = false;

  AtomicIntrusiveStack<ControlEvent> ui2dsp_events_, dsp2ui_events_, trash_events_;

  void init_ports();
  void init_presets();
  void reset_event_buffers();
  void write_midi (uint32_t time, size_t size, const uint8_t *data);
  void connect_audio_port (uint32_t port, float *buffer);
  void run (uint32_t nframes);
  void activate();
  void deactivate();
  void free_trash();

  const LilvUI *get_plugin_ui();
  void open_ui();
  void delete_ui_request();
  void send_plugin_events_to_ui();
  void handle_dsp2ui_events();
  void send_ui_updates();
  void set_initial_controls_ui();
};

void
eh_ui_write (LV2UI_Controller controller,
             uint32_t         port_index,
             uint32_t         buffer_size,
             uint32_t         protocol,
             const void*      buffer)
{
  PluginInstance *plugin_instance = (PluginInstance *)controller;

  ControlEvent *event = ControlEvent::loft_new (port_index, protocol, buffer_size);
  std::copy_n ((const uint8_t *) buffer, buffer_size, event->data());
  plugin_instance->ui2dsp_events_.push (event);
}

/* TODO: do we need this function?
uint32_t
eh_ui_index (SuilController controller, const char* symbol)
{
  printf ("ui_index called\n");
  return 0;
}
*/

struct PluginHost
{
  LilvWorld   *world = nullptr;
  Map          urid_map;

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

    URIDs (Map& map) :
      param_sampleRate          (map.urid_map (LV2_PARAMETERS__sampleRate)),
      atom_Double               (map.urid_map (LV2_ATOM__Double)),
      atom_Float                (map.urid_map (LV2_ATOM__Float)),
      atom_Int                  (map.urid_map (LV2_ATOM__Int)),
      atom_Long                 (map.urid_map (LV2_ATOM__Long)),
      atom_eventTransfer        (map.urid_map (LV2_ATOM__eventTransfer)),
      bufsz_maxBlockLength      (map.urid_map (LV2_BUF_SIZE__maxBlockLength)),
      bufsz_minBlockLength      (map.urid_map (LV2_BUF_SIZE__minBlockLength)),
      midi_MidiEvent            (map.urid_map (LV2_MIDI__MidiEvent))
    {
    }
  } urids;

  struct Nodes {
    LilvNode *lv2_audio_class;
    LilvNode *lv2_atom_class;
    LilvNode *lv2_input_class;
    LilvNode *lv2_output_class;
    LilvNode *lv2_control_class;

    LilvNode *lv2_atom_Chunk;
    LilvNode *lv2_atom_Sequence;
    LilvNode *lv2_presets_Preset;
    LilvNode *lv2_ui_x11ui;

    LilvNode *rdfs_label;

    void init (LilvWorld *world)
    {
      lv2_audio_class   = lilv_new_uri (world, LILV_URI_AUDIO_PORT);
      lv2_atom_class    = lilv_new_uri (world, LILV_URI_ATOM_PORT);
      lv2_input_class   = lilv_new_uri (world, LILV_URI_INPUT_PORT);
      lv2_output_class  = lilv_new_uri (world, LILV_URI_OUTPUT_PORT);
      lv2_control_class = lilv_new_uri (world, LILV_URI_CONTROL_PORT);

      lv2_atom_Chunk    = lilv_new_uri (world, LV2_ATOM__Chunk);
      lv2_atom_Sequence = lilv_new_uri (world, LV2_ATOM__Sequence);

      lv2_presets_Preset = lilv_new_uri (world, LV2_PRESETS__Preset);
      lv2_ui_x11ui       = lilv_new_uri (world, LV2_UI__X11UI);
      rdfs_label         = lilv_new_uri (world, LILV_NS_RDFS "label");
    }
  } nodes;

  Options  options;

private:
  PluginHost() :
    world (nullptr),
    urids (urid_map),
    options (*this)
  {
    world = lilv_world_new();
    lilv_world_load_all (world);

    nodes.init (world);
  }
public:
  static PluginHost&
  the()
  {
    static PluginHost host;
    return host;
  }
  PluginInstance *instantiate (const char *plugin_uri, float mix_freq);

private:
  DeviceInfoS devs;
  map<string, DeviceInfo> lv2_device_info_map;
public:

  DeviceInfo
  lv2_device_info (const string& uri)
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
    LILV_FOREACH(plugins, i, plugins)
      {
        const LilvPlugin* p = lilv_plugins_get (plugins, i);
        DeviceInfo device_info;
        string lv2_uri = lilv_node_as_uri (lilv_plugin_get_uri (p));
        device_info.uri = "LV2:" + lv2_uri;

        LilvNode* n = lilv_plugin_get_name (p);
        device_info.name = lilv_node_as_string (n);
        lilv_node_free (n);

        auto plugin_class = lilv_plugin_get_class (p);
        device_info.category = string_format ("LV2 %s", lilv_node_as_string (lilv_plugin_class_get_label (plugin_class)));

        devs.push_back (device_info);

        lv2_device_info_map[lv2_uri] = device_info;
      }
    return devs;
  }
};

class PluginUI
{
public:
  void *dlhandle_ = nullptr;
  const LV2UI_Idle_Interface *idle_iface_ = nullptr;
  LV2UI_Handle handle_ = nullptr;
  const LV2UI_Descriptor *descriptor_ = nullptr;
  ulong window_id_ = 0;
  uint  timer_id_ = 0;

  PluginUI (PluginInstance *plugin_instance, const string& plugin_uri, const string& dlpath, const string& ui_uri, const string& ui_bundle_path,
            const LV2_Feature* const* features, LV2_Feature *parent_feature, LV2UI_Resize *ui_resize)
  {
    dlhandle_ = dlopen (dlpath.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (dlhandle_)
      {
        LV2UI_DescriptorFunction df = (LV2UI_DescriptorFunction) dlsym (dlhandle_, "lv2ui_descriptor");
        if (df)
          {
            const LV2UI_Descriptor *descriptor = nullptr;
            uint32_t i = 0;
            do
              {
                descriptor = df (i++);
              }
            while (descriptor && descriptor->URI != ui_uri);

            if (descriptor && descriptor->URI == ui_uri)
              {
                descriptor_ = descriptor;

                string window_title = PluginHost::the().lv2_device_info (plugin_uri).name;
                Gtk2WindowSetup wsetup {
                  .title = window_title, .width = 640, .height = 480,
                  .deleterequest_mt = [plugin_instance] ()
                    {
                      main_loop->exec_callback ([plugin_instance]() { plugin_instance->delete_ui_request(); });
                    }
                };
                LV2UI_Widget ui_widget = nullptr;

                auto x11wrapper = get_x11wrapper();
                window_id_ = x11wrapper->create_window (wsetup);
                printerr ("creation: window_id_=%ld\n", window_id_);
                parent_feature->data = (void *) window_id_;
                ui_resize->handle = this;
                handle_ = descriptor->instantiate (descriptor, plugin_uri.c_str(), ui_bundle_path.c_str(), eh_ui_write, plugin_instance, &ui_widget, features);
                assert_return (handle_ != nullptr);
                if (descriptor->extension_data)
                  idle_iface_ = (const LV2UI_Idle_Interface*) descriptor->extension_data (LV2_UI__idleInterface);
                x11wrapper->show_window (window_id_);

                int period_ms = 25;

                timer_id_ = main_loop->exec_timer ([this, plugin_instance] () {
                  if (idle_iface_)
                    idle_iface_->idle (handle_);
                  plugin_instance->handle_dsp2ui_events();

                  plugin_instance->free_trash();
                  return true;
                }, period_ms, period_ms, EventLoop::PRIORITY_UPDATE);

                plugin_instance->set_initial_controls_ui();
              }
          }
      }
  }
  ~PluginUI()
  {
    if (descriptor_ && descriptor_->cleanup)
      {
        descriptor_->cleanup (handle_);
        descriptor_ = nullptr;
      }
    if (window_id_)
      {
        auto x11wrapper = get_x11wrapper();
        x11wrapper->destroy_window (window_id_);
        window_id_ = 0;
      }
    if (timer_id_)
      {
        main_loop->remove (timer_id_);
        timer_id_ = 0;
      }
  }
};


Options::Options (PluginHost& plugin_host) :
  plugin_host (plugin_host),
  lv2_options_feature { LV2_OPTIONS__options, nullptr }
{
  const_opts.push_back ({ LV2_OPTIONS_INSTANCE, 0, plugin_host.urids.param_sampleRate,
                          sizeof(float), plugin_host.urids.atom_Float, &m_sample_rate });
  const_opts.push_back ({ LV2_OPTIONS_INSTANCE, 0, plugin_host.urids.bufsz_minBlockLength,
                          sizeof(int32_t), plugin_host.urids.atom_Int, &m_block_length });
  const_opts.push_back ({ LV2_OPTIONS_INSTANCE, 0, plugin_host.urids.bufsz_maxBlockLength,
                          sizeof(int32_t), plugin_host.urids.atom_Int, &m_block_length });
  const_opts.push_back ({ LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr });

  lv2_options_feature.data = &const_opts[0];
}

PluginInstance *
PluginHost::instantiate (const char *plugin_uri, float mix_freq)
{
  LilvNode* uri = lilv_new_uri (world, plugin_uri);
  if (!uri)
    {
      fprintf (stderr, "Invalid plugin URI <%s>\n", plugin_uri);
      return nullptr;
    }

  const LilvPlugins* plugins = lilv_world_get_all_plugins (world);

  const LilvPlugin*  plugin  = lilv_plugins_get_by_uri (plugins, uri);

  if (!plugin)
    {
      fprintf (stderr, "plugin is nil\n");
      return nullptr;
    }
  lilv_node_free (uri);

  PluginInstance *plugin_instance = new PluginInstance (*this);

  LilvInstance *instance = lilv_plugin_instantiate (plugin, mix_freq, plugin_instance->features.get_features());
  if (!instance)
    {
      fprintf (stderr, "plugin instantiate failed\n");
      delete plugin_instance;

      return nullptr;
    }

  plugin_instance->instance = instance;
  plugin_instance->plugin = plugin;
  plugin_instance->init_ports();
  plugin_instance->init_presets();
  plugin_instance->worker.set_instance (instance);
  plugin_instance->lv2_ext_data.data_access = lilv_instance_get_descriptor (instance)->extension_data;

  return plugin_instance;
}

PluginInstance::PluginInstance (PluginHost& plugin_host) :
  plugin_host (plugin_host)
{
  features.add (plugin_host.urid_map.map_feature());
  features.add (plugin_host.urid_map.unmap_feature());
  features.add (worker.feature());
  features.add (plugin_host.options.feature()); /* TODO: maybe make a local version */
}

PluginInstance::~PluginInstance()
{
  worker.stop();
  free_trash();

  if (instance)
    {
      if (active)
        deactivate();

      lilv_instance_free (instance);
      instance = nullptr;
    }
}

void
PluginInstance::free_trash()
{
  ControlEvent *event = trash_events_.pop_all();
  while (event)
    {
      ControlEvent *old = event;
      event = event->next_;
      old->loft_free();
    }
}

void
PluginInstance::init_ports()
{
  const int n_ports = lilv_plugin_get_num_ports (plugin);

  // don't resize later, otherwise control connections get lost
  plugin_ports.resize (n_ports);

  vector<float> defaults (n_ports);
  vector<float> min_values (n_ports);
  vector<float> max_values (n_ports);

  size_t n_control_ports = 0;

  lilv_plugin_get_port_ranges_float (plugin, &min_values[0], &max_values[0], &defaults[0]);
  for (int i = 0; i < n_ports; i++)
    {
      const LilvPort *port = lilv_plugin_get_port_by_index (plugin, i);
      if (port)
        {
          if (lilv_port_is_a (plugin, port, plugin_host.nodes.lv2_input_class))
            {
              if (lilv_port_is_a (plugin, port, plugin_host.nodes.lv2_audio_class))
                {
                  audio_in_ports.push_back (i);
                }
              else if (lilv_port_is_a (plugin, port, plugin_host.nodes.lv2_atom_class))
                {
                  printf ("found atom input port\n");
                  const int buf_size = 4096;
                  plugin_ports[i].evbuf = lv2_evbuf_new (buf_size, LV2_EVBUF_ATOM, plugin_host.urid_map.urid_map (lilv_node_as_string (plugin_host.nodes.lv2_atom_Chunk)),
                                                                                   plugin_host.urid_map.urid_map (lilv_node_as_string (plugin_host.nodes.lv2_atom_Sequence)));
                  lilv_instance_connect_port (instance, i, lv2_evbuf_get_buffer (plugin_ports[i].evbuf));

                  atom_in_ports.push_back (i);
                }
              else if (lilv_port_is_a (plugin, port, plugin_host.nodes.lv2_control_class))
                {
                  plugin_ports[i].control = defaults[i];      // start with default value
                  plugin_ports[i].type = Port::CONTROL_IN;
                  plugin_ports[i].min_value = min_values[i];
                  plugin_ports[i].max_value = max_values[i];

                  LilvNode *nname = lilv_port_get_name (plugin, port);
                  plugin_ports[i].name = lilv_node_as_string (nname);
                  lilv_node_free (nname);

                  const LilvNode *nsymbol = lilv_port_get_symbol (plugin, port);
                  plugin_ports[i].symbol = lilv_node_as_string (nsymbol);

                  lilv_instance_connect_port (instance, i, &plugin_ports[i].control);

                  n_control_ports++;
                }
              else
                {
                  printf ("found unknown input port\n");
                }
            }
          if (lilv_port_is_a (plugin, port, plugin_host.nodes.lv2_output_class))
            {
              if (lilv_port_is_a (plugin, port, plugin_host.nodes.lv2_audio_class))
                {
                  audio_out_ports.push_back (i);
                }
              else if (lilv_port_is_a (plugin, port, plugin_host.nodes.lv2_atom_class))
                {
                  atom_out_ports.push_back (i);

                  printf ("found atom output port\n");
                  const int buf_size = 4096;
                  plugin_ports[i].evbuf = lv2_evbuf_new (buf_size, LV2_EVBUF_ATOM, plugin_host.urid_map.urid_map (lilv_node_as_string (plugin_host.nodes.lv2_atom_Chunk)),
                                                                                   plugin_host.urid_map.urid_map (lilv_node_as_string (plugin_host.nodes.lv2_atom_Sequence)));
                  lilv_instance_connect_port (instance, i, lv2_evbuf_get_buffer (plugin_ports[i].evbuf));

                }
              else if (lilv_port_is_a (plugin, port, plugin_host.nodes.lv2_control_class))
                {
                  plugin_ports[i].control = defaults[i];      // start with default value
                  plugin_ports[i].type = Port::CONTROL_OUT;

                  lilv_instance_connect_port (instance, i, &plugin_ports[i].control);
                }
              else
                {
                  printf ("found unknown output port\n");
                }
            }
        }
    }

  printf ("--------------------------------------------------\n");
  printf ("audio IN:%zd OUT:%zd\n", audio_in_ports.size(), audio_out_ports.size());
  printf ("control IN:%zd\n", n_control_ports);
  printf ("--------------------------------------------------\n");
}

void
PluginInstance::init_presets()
{
  LilvNodes* lilv_presets = lilv_plugin_get_related (plugin, plugin_host.nodes.lv2_presets_Preset);
  LILV_FOREACH (nodes, i, lilv_presets)
    {
      const LilvNode* preset = lilv_nodes_get (lilv_presets, i);
      lilv_world_load_resource (plugin_host.world, preset);
      LilvNodes* labels = lilv_world_find_nodes (plugin_host.world, preset, plugin_host.nodes.rdfs_label, NULL);
      if (labels)
        {
          const LilvNode* label = lilv_nodes_get_first (labels);
          presets.push_back ({lilv_node_as_string (label), lilv_node_duplicate (preset)}); // TODO: preset leak
          lilv_nodes_free (labels);
        }
    }
  lilv_nodes_free (lilv_presets);
}

void
PluginInstance::write_midi (uint32_t time, size_t size, const uint8_t *data)
{
  if (!atom_in_ports.empty())
    {
      /* we use the first atom in port for midi, is there a better strategy? */
      int p = atom_in_ports[0];

      LV2_Evbuf           *evbuf = plugin_ports[p].evbuf;
      LV2_Evbuf_Iterator    iter = lv2_evbuf_end (evbuf);

      lv2_evbuf_write (&iter, time, 0, plugin_host.urids.midi_MidiEvent, size, data);
    }
}

void
PluginInstance::reset_event_buffers()
{
  for (int p : atom_out_ports)
    {
      /* Clear event output for plugin to write to */
      LV2_Evbuf *evbuf = plugin_ports[p].evbuf;

      lv2_evbuf_reset (evbuf, false);
    }
  for (int p : atom_in_ports)
    {
      LV2_Evbuf *evbuf = plugin_ports[p].evbuf;

      lv2_evbuf_reset (evbuf, true);
    }
}

void
PluginInstance::activate()
{
  if (!active)
    {
      printf ("activate\n");
      lilv_instance_activate (instance);

      active = true;
    }
}

void
PluginInstance::deactivate()
{
  if (active)
    {
      printf ("deactivate\n");
      lilv_instance_deactivate (instance);

      active = false;
    }
}

void
PluginInstance::connect_audio_port (uint32_t port, float *buffer)
{
  lilv_instance_connect_port (instance, port, buffer);
}

void
PluginInstance::run (uint32_t nframes)
{
  ControlEvent *const events = ui2dsp_events_.pop_reversed(), *last = nullptr;
  for (ControlEvent *event = events; event; last = event, event = event->next_)
    {
      assert (event->port_index() < plugin_ports.size());
      Port* port = &plugin_ports[event->port_index()];
      if (event->protocol() == 0)
        {
          assert (event->size() == sizeof (float));
          port->control = *(float *)event->data();
        }
      else if (event->protocol() == plugin_host.urids.atom_eventTransfer)
        {
          LV2_Evbuf_Iterator    e    = lv2_evbuf_end (port->evbuf);
          const LV2_Atom* const atom = (const LV2_Atom *) event->data();
          lv2_evbuf_write (&e, nframes, 0, atom->type, atom->size, (const uint8_t*)LV2_ATOM_BODY_CONST(atom));
        }
      else
        {
          printerr ("LV2: PluginInstance: protocol: %d not implemented\n", event->protocol());
        }
    }
  if (last)
    trash_events_.push_chain (events, last);

  lilv_instance_run (instance, nframes);

  worker.handle_responses();
  worker.end_run();

  // TODO: this only needs to be done if the UI is visible (otherwise control events will never get read/freed)
  send_plugin_events_to_ui();
  send_ui_updates();
}

void
PluginInstance::send_plugin_events_to_ui()
{
  for (int port_index : atom_out_ports)
    {
      LV2_Evbuf *evbuf = plugin_ports[port_index].evbuf;

      for (LV2_Evbuf_Iterator i = lv2_evbuf_begin (evbuf); lv2_evbuf_is_valid (i); i = lv2_evbuf_next (i))
        {
          uint32_t frames, subframes, type, size;
          uint8_t *body;
          lv2_evbuf_get (i, &frames, &subframes, &type, &size, &body);

          ControlEvent *event = ControlEvent::loft_new (port_index, plugin_host.urids.atom_eventTransfer, sizeof (LV2_Atom) + size);

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
PluginInstance::handle_dsp2ui_events()
{
  ControlEvent *const events = dsp2ui_events_.pop_reversed(), *last = nullptr;
  for (ControlEvent *event = events; event; last = event, event = event->next_)
    {
      assert (event->port_index() < plugin_ports.size());
      // printerr ("handle_dsp2ui_events: pop event: index=%zd, protocol=%d, sz=%zd\n", event->port_index, event->protocol, event->data.size());
      if (plugin_ui)
        {
          auto port_event = plugin_ui->descriptor_->port_event;
          if (port_event)
            port_event (plugin_ui->handle_, event->port_index(), event->size(), event->protocol(), event->data());
        }
    }
  if (last)
    trash_events_.push_chain (events, last);
}

void
PluginInstance::set_initial_controls_ui()
{
  /* Set initial control values on UI */
  for (size_t port_index = 0; port_index < plugin_ports.size(); port_index++)
    {
      const auto& port = plugin_ports[port_index];

      if (port.type == Port::CONTROL_IN || port.type == Port::CONTROL_OUT)
        {
          ControlEvent *event = ControlEvent::loft_new (port_index, 0, sizeof (float));

          *(float *) event->data() = port.control;
          dsp2ui_events_.push (event);
        }
    }
}

void
PluginInstance::send_ui_updates()
{
  bool do_send_ui_updates = true; // FIXME: hz
  if (do_send_ui_updates)
    {
      for (size_t port_index = 0; port_index < plugin_ports.size(); port_index++)
        {
          const Port& port = plugin_ports[port_index];

          if (port.type == Port::CONTROL_OUT)
            {
              ControlEvent *event = ControlEvent::loft_new (port_index, 0, sizeof (float));

              *(float *) event->data() = port.control;
              // printerr ("send_ui_updates: push event: index=%zd, protocol=0, sz=%zd\n", p, event->data.size());
              dsp2ui_events_.push (event);
            }
        }
    }
}

const LilvUI *
PluginInstance::get_plugin_ui()
{
  static LilvUIs* uis;            ///< All plugin UIs (RDF data) FIXME not static

  uis = lilv_plugin_get_uis (plugin);

  LILV_FOREACH(uis, u, uis)
    {
      const LilvUI* this_ui = lilv_uis_get (uis, u);

      printf ("UI: %s\n", lilv_node_as_uri (lilv_ui_get_uri (this_ui)));
      if (lilv_ui_is_a (this_ui, plugin_host.nodes.lv2_ui_x11ui))
        return this_ui;
    }
  return nullptr;
}

void
PluginInstance::open_ui()
{
  // ---------------------ui------------------------------
  const LilvUI *ui = get_plugin_ui();
  const char* bundle_uri  = lilv_node_as_uri(lilv_ui_get_bundle_uri(ui));
  const char* binary_uri  = lilv_node_as_uri(lilv_ui_get_binary_uri(ui));
  const char* plugin_uri  = lilv_node_as_uri (lilv_plugin_get_uri (plugin));
  char*       bundle_path = lilv_file_uri_parse(bundle_uri, NULL);
  char*       binary_path = lilv_file_uri_parse(binary_uri, NULL);

  // instance access:
  const LV2_Feature instance_feature = {
    NS_EXT "instance-access", lilv_instance_get_handle (instance)
  };
  // data access:
  const LV2_Feature ext_data_feature = {
    LV2_DATA_ACCESS_URI, &lv2_ext_data
  };
  const LV2_Feature idle_feature = {
    LV2_UI__idleInterface, nullptr
  };
  // for passing parent window id
  LV2_Feature parent_feature = {
    LV2_UI__parent, nullptr
  };
  // resize window func
  auto resize_window = [] (LV2UI_Feature_Handle handle, int width, int height) -> int {
    PluginUI *plugin_ui = (PluginUI *) handle;
    auto x11wrapper = get_x11wrapper();
    bool ok = x11wrapper->resize_window (plugin_ui->window_id_, width, height);
    if (ok)
      return 0;
    else
      return 1;
  };
  ui_resize.handle = plugin_ui.get();
  ui_resize.ui_resize = resize_window;
  const LV2_Feature ui_resize_feature = {
    LV2_UI__resize, &ui_resize
  };

  Features ui_features;
  ui_features.add (&instance_feature);
  ui_features.add (&ext_data_feature);
  ui_features.add (&idle_feature);
  ui_features.add (&parent_feature);
  ui_features.add (&ui_resize_feature);
  ui_features.add (plugin_host.urid_map.map_feature());
  ui_features.add (plugin_host.urid_map.unmap_feature());
  ui_features.add (plugin_host.options.feature()); /* TODO: maybe make a local version */

  plugin_ui = std::make_unique <PluginUI> (this, plugin_uri, binary_path, lilv_node_as_uri (lilv_ui_get_uri (get_plugin_ui())), bundle_path,
                                           ui_features.get_features(), &parent_feature, &ui_resize);
}

void
PluginInstance::delete_ui_request()
{
  plugin_ui.reset();
}

}

class LV2Processor : public AudioProcessor {
  IBusId stereo_in_;
  OBusId stereo_out_;
  vector<IBusId> mono_ins_;
  vector<OBusId> mono_outs_;

  PluginInstance *plugin_instance;
  PluginHost& plugin_host;

  vector<Port *> param_id_port;
  int current_preset = 0;

  enum
    {
      PID_PRESET         = 1,
      PID_DELETE         = 2,
      PID_CONTROL_OFFSET = 10
    };

  string lv2_uri_;

  void
  initialize (SpeakerArrangement busses) override
  {
    plugin_host.options.set (sample_rate(), AUDIO_BLOCK_MAX_RENDER_SIZE);
    const char *uri = getenv ("LV2URI");
    if (!uri)
      {
        if (lv2_uri_ != "")
          uri = lv2_uri_.c_str();
        else
          uri = "http://zynaddsubfx.sourceforge.net";
      }

    plugin_instance = plugin_host.instantiate (uri, sample_rate());
    if (!plugin_instance)
      return;

    ParameterMap pmap;

    if (plugin_instance->presets.size()) /* choice with 1 entry will crash */
      {
        ChoiceS centries;
        int preset_num = 0;
        centries += { "0", "-none-" };
        for (auto preset : plugin_instance->presets)
          centries += { string_format ("%d", ++preset_num), preset.name };
        pmap[PID_PRESET] = Param { "device_preset", "Device Preset", "Preset", 0, "", std::move (centries), "", "Device Preset to be used" };
      }
    current_preset = 0;

    pmap[PID_DELETE] = Param { "delete", _("Test Delete"), _("TestDel"), false, "", {}, GUIONLY + ":toggle" };

    int pid = PID_CONTROL_OFFSET;
    for (auto& port : plugin_instance->plugin_ports)
      if (port.type == Port::CONTROL_IN)
        {
          // TODO: lv2 port numbers are not reliable for serialization, should use port.symbol instead
          // TODO: special case boolean, enumeration, logarithmic,... controls
          pmap[pid++] = Param { port.symbol, port.name, port.name, port.control, "", { port.min_value, port.max_value } };
          param_id_port.push_back (&port);
        }

    // TODO: deactivate?
    // TODO: is this the right place?
    plugin_instance->activate();

    install_params (pmap);

    prepare_event_input();

    /* map audio inputs/outputs to busses;
     *
     *   channels == 1 -> one mono bus
     *   channels == 2 -> one stereo bus
     *   channels >= 3 -> N mono busses (TODO: is this the best mapping for all plugins?)
     */
    mono_ins_.clear();
    mono_outs_.clear();
    if (plugin_instance->audio_in_ports.size() == 2)
      {
        stereo_in_ = add_input_bus ("Stereo In", SpeakerArrangement::STEREO);
        assert_return (bus_info (stereo_in_).ident == "stereo_in");
      }
    else
      {
        for (size_t i = 0; i < plugin_instance->audio_in_ports.size(); i++)
          mono_ins_.push_back (add_input_bus (string_format ("Mono In %zd", i + 1), SpeakerArrangement::MONO));
      }

    if (plugin_instance->audio_out_ports.size() == 2)
      {
        stereo_out_ = add_output_bus ("Stereo Out", SpeakerArrangement::STEREO);
        assert_return (bus_info (stereo_out_).ident == "stereo_out");
      }
    else
      {
        for (size_t i = 0; i < plugin_instance->audio_out_ports.size(); i++)
          mono_outs_.push_back (add_output_bus (string_format ("Mono Out %zd", i + 1), SpeakerArrangement::MONO));
      }
  }
  void
  reset (uint64 target_stamp) override
  {
    if (!plugin_instance)
      return;

    adjust_all_params();
  }
  void
  adjust_param (uint32_t tag) override
  {
    if (!plugin_instance)
      return;

    // controls for LV2Device
    if (int (tag) == PID_PRESET)
      {
        int want_preset = irintf (get_param (tag));
        if (current_preset != want_preset)
          {
            current_preset = want_preset;

            if (want_preset > 0 && want_preset <= int (plugin_instance->presets.size()))
              {
                // TODO: this should not be done in audio thread

                auto preset_info = plugin_instance->presets[want_preset - 1];
                printf ("load preset %s\n", preset_info.name.c_str());
                LilvState *state = lilv_state_new_from_world (plugin_host.world, plugin_host.urid_map.lv2_map(), preset_info.preset);
                const LV2_Feature* state_features[] = { // TODO: more features
                  plugin_host.urid_map.map_feature(),
                  plugin_host.urid_map.unmap_feature(),
                  NULL
                };
                lilv_state_restore (state, plugin_instance->instance, set_port_value, this, 0, state_features);
              }
          }
      }
    if (int (tag) == PID_DELETE && get_param (tag) > 0.5) // this is just test code
      {
        delete plugin_instance;
        plugin_instance = nullptr;
      }

    // real LV2 controls start at PID_CONTROL_OFFSET
    auto control_id = tag - PID_CONTROL_OFFSET;
    if (control_id >= 0 && control_id < param_id_port.size())
      param_id_port[control_id]->control = get_param (tag);
  }
  void
  render (uint n_frames) override
  {
    if (!plugin_instance)
      {
        if (plugin_instance->audio_out_ports.size() == 2)
          {
            floatfill (oblock (stereo_out_, 0), 0.f, n_frames);
            floatfill (oblock (stereo_out_, 1), 0.f, n_frames);
          }
        else
          {
            for (size_t i = 0; i < plugin_instance->audio_out_ports.size(); i++)
              floatfill (oblock (mono_outs_[i], 0), 0.f, n_frames);
          }
        return;
      }

    // reset event buffers and write midi events
    plugin_instance->reset_event_buffers();

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
            plugin_instance->write_midi (time_stamp, 3, midi_data);
            break;
          case MidiMessage::NOTE_ON:
            midi_data[0] = 0x90 | ev.channel;
            midi_data[1] = ev.key;
            midi_data[2] = std::clamp (irintf (ev.velocity * 127), 0, 127);
            plugin_instance->write_midi (time_stamp, 3, midi_data);
            break;
#if 0
          case Message::ALL_NOTES_OFF:
          case Message::ALL_SOUND_OFF:
            synth_.all_sound_off();    // NOTE: there is no extra "all notes off" in liquidsfz
            break;
#endif
          default: ;
          }
      }

    if (plugin_instance->audio_in_ports.size() == 2)
      {
        plugin_instance->connect_audio_port (plugin_instance->audio_in_ports[0], const_cast<float *> (ifloats (stereo_in_, 0)));
        plugin_instance->connect_audio_port (plugin_instance->audio_in_ports[1], const_cast<float *> (ifloats (stereo_in_, 1)));
      }
    else
      {
        for (size_t i = 0; i < plugin_instance->audio_in_ports.size(); i++)
          plugin_instance->connect_audio_port (plugin_instance->audio_in_ports[i], const_cast<float *> (ifloats (mono_ins_[i], 0)));
      }

    if (plugin_instance->audio_out_ports.size() == 2)
      {
        plugin_instance->connect_audio_port (plugin_instance->audio_out_ports[0], oblock (stereo_out_, 0));
        plugin_instance->connect_audio_port (plugin_instance->audio_out_ports[1], oblock (stereo_out_, 1));
      }
    else
      {
        for (size_t i = 0; i < plugin_instance->audio_out_ports.size(); i++)
          plugin_instance->connect_audio_port (plugin_instance->audio_out_ports[i], oblock (mono_outs_[i], 0));
      }
    plugin_instance->run (n_frames);
  }
  void
  set_port_value (const char*         port_symbol,
                  const void*         value,
                  uint32_t            size,
                  uint32_t            type)
  {
    double dvalue = 0;
    if (type == plugin_host.urids.atom_Float)
      {
        dvalue = *(const float*)value;
      }
    else if (type == plugin_host.urids.atom_Double)
      {
        dvalue = *(const double*)value;
      }
    else if (type == plugin_host.urids.atom_Int)
      {
        dvalue = *(const int32_t*)value;
      }
    else if (type == plugin_host.urids.atom_Long)
      {
        dvalue = *(const int64_t*)value;
      }
    else
      {
        fprintf (stderr, "error: Preset `%s' value has bad type <%s>\n",
                          port_symbol, plugin_instance->plugin_host.urid_map.urid_unmap (type));
        return;
      }
    printf ("%s = %f\n", port_symbol, dvalue);
    for (int i = 0; i < (int) param_id_port.size(); i++)
      {
        if (param_id_port[i]->symbol == port_symbol)
          {
            // TODO: should set the parameters here
            //set_param (i + PID_CONTROL_OFFSET, dvalue);
          }
      }
  }

  static void
  set_port_value (const char*         port_symbol,
                  void*               user_data,
                  const void*         value,
                  uint32_t            size,
                  uint32_t            type)
  {
    LV2Processor *dev = (LV2Processor *) user_data;
    dev->set_port_value (port_symbol, value, size, type);
  }
public:
  LV2Processor (const ProcessorSetup &psetup) :
    AudioProcessor (psetup),
    plugin_host (PluginHost::the())
  {}
  ~LV2Processor()
  {
    if (plugin_instance)
      {
        delete plugin_instance;
        plugin_instance = nullptr;
      }
  }
  static void
  static_info (AudioProcessorInfo &info)
  {
    // info.uri = "Bse.LV2Device";
    // info.version = "0";
    info.version ="1";
    info.label = "LV2Processor";
    info.category = "Synth";
    info.creator_name = "Stefan Westerfeld";
    info.website_url  = "https://anklang.testbit.eu";
  }
  void
  set_uri (const string& lv2_uri)
  {
    lv2_uri_ = lv2_uri;
  }
  PluginInstance *
  instance()
  {
    return plugin_instance;
  }
};

DeviceInfoS
LV2DeviceImpl::list_lv2_plugins()
{
  PluginHost& plugin_host = PluginHost::the();
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
  DeviceP devicep = AudioProcessor::registry_create ("Ase::Devices::LV2Processor", engine, make_device);
  // return_unless (devicep && devicep->_audio_processor("Ase::Device::LV2Processor", nullptr);
  return devicep;
}

bool
LV2DeviceImpl::gui_supported()
{
  auto lv2aproc = dynamic_cast<LV2Processor *> (proc_.get());
  auto ui = lv2aproc->instance()->get_plugin_ui();
  return ui != nullptr;
}

void
LV2DeviceImpl::gui_toggle()
{
  auto lv2aproc = dynamic_cast<LV2Processor *> (proc_.get());
  lv2aproc->instance()->open_ui();
}

LV2DeviceImpl::LV2DeviceImpl (const String &lv2_uri, AudioProcessorP proc) :
  proc_ (proc), info_ (PluginHost::the().lv2_device_info (lv2_uri))
{
}

static auto lv2processor = register_audio_processor<LV2Processor> ("Ase::Devices::LV2Processor");


} // Bse
