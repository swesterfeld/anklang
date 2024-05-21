// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "driver.hh"
#include "path.hh"
#include "platform.hh"
#include "datautils.hh"
#include "internal.hh"
#include <algorithm>

#define DDEBUG(...)     Ase::debug ("driver", __VA_ARGS__)

namespace Ase {

// == Driver ==
Driver::Driver (const String &driver, const String &devid) :
  driver_ (driver), devid_ (devid)
{}

Driver::~Driver ()
{}

/// Return a string which uniquely identifies this driver and device.
String
Driver::devid () const
{
  return devid_.empty() ? driver_ : driver_ + "=" + devid_;
}

/// Return string which represents the given priority mask.
String
Driver::priority_string (uint priority)
{
  StringS b;
  if (priority & SURROUND)      b.push_back ("SURROUND");
  if (priority & HEADSET)       b.push_back ("HEADSET");
  if (priority & RECORDER)      b.push_back ("RECORDER");
  if (priority & MIDI_THRU)     b.push_back ("MIDI_THRU");
  if (priority & JACK)          b.push_back ("JACK");
  if (priority & ALSA_USB)      b.push_back ("ALSA_USB");
  if (priority & ALSA_KERN)     b.push_back ("ALSA_KERN");
  if (priority & OSS)           b.push_back ("OSS");
  if (priority & PULSE)         b.push_back ("PULSE");
  if (priority & ALSA_USER)     b.push_back ("ALSA_USER");
  if (priority & PSEUDO)        b.push_back ("PSEUDO");
  if (priority & PAUTO)         b.push_back ("PAUTO");
  if (priority & PNULL)         b.push_back ("PNULL");
  if (priority & WCARD)         b.push_back ("WCARD");
  if (priority & WDEV)          b.push_back ("WDEV");
  if (priority & WSUB)          b.push_back ("WSUB");
  return string_join ("|", b);
}

// == loaders ==
using RegisteredLoaderFunc = Error (*) ();
struct RegisteredLoader {
  const char *const what;
  const RegisteredLoaderFunc func;
};
using RegisteredLoaderVector = std::vector<RegisteredLoader>;
static RegisteredLoaderVector& registered_loaders() { static RegisteredLoaderVector lv; return lv; }
static bool registered_loaders_executed = false;

/// Register loader callbacks at static constructor time.
bool*
register_driver_loader (const char *staticwhat, Error (*loader) ())
{
  assert_return (loader != NULL, nullptr);
  assert_return (registered_loaders_executed == false, nullptr);
  RegisteredLoaderVector &lv = registered_loaders();
  lv.push_back ({ staticwhat, loader });
  return &registered_loaders_executed;
}

/// Load all registered drivers.
void
load_registered_drivers()
{
  assert_return (registered_loaders_executed == false);
  registered_loaders_executed = true;
  for (auto &loader : registered_loaders())
    {
      Error error = loader.func ();
      if (error != Error::NONE)
        printerr ("ASE: %s: loading failed: %s\n", loader.what, ase_error_blurb (error));
    }
}

// == RegisteredDriver ==
template<typename DriverP>
struct RegisteredDriver {
  const String                            driver_id_;
  std::function<DriverP (const String&)>  create_;
  std::function<void (Driver::EntryVec&)> list_;
  using RegisteredDriverVector = std::vector<RegisteredDriver>;

  static RegisteredDriverVector&
  registered_driver_vector()
  {
    static RegisteredDriverVector registered_driver_vector_;
    return registered_driver_vector_;
  }

  static DriverP
  open (const String &devid, Driver::IODir iodir, Error *errorp,
        const std::function<Error (DriverP, Driver::IODir)> &opener)
  {
    std::function<DriverP (const String&)> create;
    const String driverid = kvpair_key (devid);
    for (const auto &driver : registered_driver_vector())
      if (driver.driver_id_ == driverid)
        {
          create = driver.create_;
          break;
        }
    Error error = Error::DEVICE_NOT_AVAILABLE;
    DriverP driver = create ? create (devid) : NULL;
    if (driver)
      {
        error = opener (driver, iodir);
        if (errorp)
          *errorp = error;
        if (error == Error::NONE)
          {
            assert_return (driver->opened() == true, nullptr);
            assert_return (!(iodir & Driver::READONLY) || driver->readable(), nullptr);
            assert_return (!(iodir & Driver::WRITEONLY) || driver->writable(), nullptr);
          }
        else
          driver = nullptr;
      }
    else if (errorp)
      *errorp = error;
    return driver;
  }

  static String
  register_driver (const String &driverid,
                   const std::function<DriverP (const String&)> &create,
                   const std::function<void (Driver::EntryVec&)> &list)
  {
    auto &vec = registered_driver_vector();
    RegisteredDriver rd = { driverid, create, list, };
    vec.push_back (rd);
    return driverid;
  }
  static Driver::EntryVec
  list_drivers (const Driver::EntryVec &pseudos)
  {
    Driver::EntryVec entries;
    std::copy (pseudos.begin(), pseudos.end(), std::back_inserter (entries));
    auto &vec = registered_driver_vector();
    for (const auto &rd : vec)
      {
        Driver::EntryVec dentries;
        rd.list_ (dentries);
        for (auto &entry : dentries)
          entry.devid = entry.devid.empty() ? rd.driver_id_ : rd.driver_id_ + "=" + entry.devid;
        entries.insert (entries.end(), std::make_move_iterator (dentries.begin()), std::make_move_iterator (dentries.end()));
      }
    std::sort (entries.begin(), entries.end(), [] (const Driver::Entry &a, const Driver::Entry &b) {
        return a.priority < b.priority;
      });
    return entries;
  }
};

// == PcmDriver ==
PcmDriver::PcmDriver (const String &driver, const String &devid) :
  Driver (driver, devid)
{}

/// Open PCM device and return a pointer to it, or nullptr with `*ep` set on error.
PcmDriverP
PcmDriver::open (const String &devid, IODir desired, IODir required, const PcmDriverConfig &config, Error *ep)
{
  auto opener =  [&config] (PcmDriverP d, IODir iodir) {
    return d->open (iodir, config);
  };
  if (devid == "auto")
    {
      for (const auto &entry : list_drivers())
        if (entry.priority < PSEUDO &&          // ignore pseudo devices during auto-selection
            !(entry.priority & 0x0000ffff))     // ignore secondary devices during auto-selection
          {
            PcmDriverP pcm_driver = RegisteredDriver<PcmDriverP>::open (entry.devid, desired, ep, opener);
            loginf ("PcmDriver::open: devid=%s: %s\n", entry.devid, ase_error_blurb (*ep));
            if (!pcm_driver && required && desired != required) {
              pcm_driver = RegisteredDriver<PcmDriverP>::open (entry.devid, required, ep, opener);
              loginf ("PcmDriver::open: devid=%s: %s\n", entry.devid, ase_error_blurb (*ep));
            }
            if (pcm_driver)
              return pcm_driver;
          }
    }
  else
    {
      PcmDriverP pcm_driver = RegisteredDriver<PcmDriverP>::open (devid, desired, ep, opener);
      if (!pcm_driver && required && desired != required)
        pcm_driver = RegisteredDriver<PcmDriverP>::open (devid, required, ep, opener);
      if (pcm_driver)
        return pcm_driver;
    }
  return nullptr;
}

String
PcmDriver::register_driver (const String &driverid,
                            const std::function<PcmDriverP (const String&)> &create,
                            const std::function<void (EntryVec&)> &list)
{
  return RegisteredDriver<PcmDriverP>::register_driver (driverid, create, list);
}

Driver::EntryVec
PcmDriver::list_drivers ()
{
  Driver::Entry entry;
  entry.devid = "auto";
  entry.device_name = _("Automatic driver selection");
  entry.device_info = _("Selects the first available PCM card or sound server");
  entry.readonly = false;
  entry.writeonly = false;
  entry.priority = Driver::PAUTO;
  Driver::EntryVec pseudos;
  pseudos.push_back (entry);
  return RegisteredDriver<PcmDriverP>::list_drivers (pseudos);
}

// == MidiDriver ==
MidiDriver::MidiDriver (const String &driver, const String &devid) :
  Driver (driver, devid)
{}

MidiDriverP
MidiDriver::open (const String &devid, IODir iodir, Error *ep)
{
  auto opener = [] (MidiDriverP d, IODir iodir) {
    return d->open (iodir);
  };
  if (devid == "auto")
    {
      for (const auto &entry : list_drivers())
        if (entry.priority < PSEUDO)    // ignore pseudo devices during auto-selection
          {
            MidiDriverP midi_driver = RegisteredDriver<MidiDriverP>::open (entry.devid, iodir, ep, opener);
            loginf ("MidiDriver::open: devid=%s: %s\n", entry.devid, ase_error_blurb (*ep));
            if (midi_driver)
              return midi_driver;
          }
    }
  else
    {
      MidiDriverP midi_driver = RegisteredDriver<MidiDriverP>::open (devid, iodir, ep, opener);
      if (midi_driver)
        return midi_driver;
    }
  return nullptr;
}

String
MidiDriver::register_driver (const String &driverid,
                             const std::function<MidiDriverP (const String&)> &create,
                             const std::function<void (EntryVec&)> &list)
{
  return RegisteredDriver<MidiDriverP>::register_driver (driverid, create, list);
}

Driver::EntryVec
MidiDriver::list_drivers ()
{
  Driver::Entry entry;
  entry.devid = "auto";
  entry.device_name = _("Automatic MIDI driver selection");
  entry.device_info = _("Selects the first available MIDI device");
  entry.readonly = false;
  entry.writeonly = false;
  entry.priority = Driver::PAUTO;
  Driver::EntryVec pseudos;
  pseudos.push_back (entry);
  return RegisteredDriver<MidiDriverP>::list_drivers (pseudos);
}

// == NullPcmDriver ==
class NullPcmDriver : public PcmDriver {
  uint  n_channels_ = 0;
  uint  mix_freq_ = 0;
  uint  block_size_ = 0;
  int64 resumetime_ = 0;
public:
  explicit      NullPcmDriver (const String &driver, const String &devid) : PcmDriver (driver, devid) {}
  static PcmDriverP
  create (const String &devid)
  {
    auto pdriverp = std::make_shared<NullPcmDriver> (kvpair_key (devid), kvpair_value (devid));
    return pdriverp;
  }
  uint
  pcm_n_channels () const override
  {
    return n_channels_;
  }
  uint
  pcm_mix_freq () const override
  {
    return mix_freq_;
  }
  uint
  pcm_block_length () const override
  {
    return block_size_;
  }
  void
  pcm_latency (uint *rlatency, uint *wlatency) const override
  {
    *rlatency = mix_freq_ / 10;
    *wlatency = mix_freq_ / 10;
  }
  virtual void
  close () override
  {
    assert_return (opened());
    flags_ &= ~size_t (Flags::OPENED | Flags::READABLE | Flags::WRITABLE);
  }
  virtual Error
  open (IODir iodir, const PcmDriverConfig &config) override
  {
    assert_return (!opened(), Error::INTERNAL);
    // setup request
    const bool require_readable = iodir == READONLY || iodir == READWRITE;
    const bool require_writable = iodir == WRITEONLY || iodir == READWRITE;
    flags_ |= Flags::READABLE * require_readable;
    flags_ |= Flags::WRITABLE * require_writable;
    n_channels_ = config.n_channels;
    mix_freq_ = config.mix_freq;
    block_size_ = config.block_length;
    flags_ |= Flags::OPENED;
    DDEBUG ("NULL-PCM: opening with freq=%f channels=%d: %s", mix_freq_, n_channels_, ase_error_blurb (Error::NONE));
    return Error::NONE;
  }
  virtual bool
  pcm_check_io (int64 *timeout_usecs) override
  {
    int64 current_usecs = timestamp_realtime();
    if (resumetime_ > current_usecs)
      {
        *timeout_usecs = resumetime_ - current_usecs;
        return false;
      }
    resumetime_ = current_usecs;
    return true;
  }
  virtual void
  pcm_write (size_t n, const float *values) override
  {
    const int64 busy_usecs = n * 1000000 / (mix_freq_ * n_channels_);
    resumetime_ += busy_usecs;
  }
  virtual size_t
  pcm_read (size_t n, float *values) override
  {
    floatfill (values, 0.0, n);
    return n;
  }
  static void
  list_drivers (Driver::EntryVec &entries)
  {
    Driver::Entry entry;
    entry.devid = ""; // "null"
    entry.device_name = "Null PCM Driver";
    entry.device_info = _("Discard all PCM output and provide zeros as PCM input");
    entry.notice = "Warning: The Null driver has no playback timing support";
    entry.readonly = false;
    entry.writeonly = false;
    entry.priority = Driver::PNULL;
    entries.push_back (entry);
  }
};

static const String null_pcm_driverid = PcmDriver::register_driver ("null", NullPcmDriver::create, NullPcmDriver::list_drivers);

// == NullMidiDriver ==
class NullMidiDriver : public MidiDriver {
public:
  explicit      NullMidiDriver (const String &driver, const String &devid) : MidiDriver (driver, devid) {}
  static MidiDriverP
  create (const String &devid)
  {
    auto pdriverp = std::make_shared<NullMidiDriver> (kvpair_key (devid), kvpair_value (devid));
    return pdriverp;
  }
  virtual void
  close () override
  {
    assert_return (opened());
    flags_ &= ~size_t (Flags::OPENED | Flags::READABLE | Flags::WRITABLE);
  }
  virtual Error
  open (IODir iodir) override
  {
    assert_return (!opened(), Error::INTERNAL);
    // setup request
    const bool require_readable = iodir == READONLY || iodir == READWRITE;
    const bool require_writable = iodir == WRITEONLY || iodir == READWRITE;
    flags_ |= Flags::READABLE * require_readable;
    flags_ |= Flags::WRITABLE * require_writable;
    flags_ |= Flags::OPENED;
    DDEBUG ("NULL-MIDI: opening: %s", ase_error_blurb (Error::NONE));
    return Error::NONE;
  }
  bool
  has_events () override
  {
    return false;
  }
  uint
  fetch_events (MidiEventOutput&, double) override
  {
    return 0;
  }
  static void
  list_drivers (Driver::EntryVec &entries)
  {
    Driver::Entry entry;
    entry.devid = ""; // "null"
    entry.device_name = "Null MIDI Driver";
    entry.device_info = _("Discard all MIDI events");
    entry.readonly = false;
    entry.writeonly = false;
    entry.priority = Driver::PNULL;
    entries.push_back (entry);
  }
};

static const String null_midi_driverid = MidiDriver::register_driver ("null", NullMidiDriver::create, NullMidiDriver::list_drivers);

} // Ase

// == jackdriver.so ==
#include <dlfcn.h>

static Ase::Error
try_load_libasejack ()
{
  using namespace Ase;
  const std::string libasejack = string_format ("%s/lib/jackdriver.so", anklang_runpath (RPath::INSTALLDIR));
  if (Path::check (libasejack, "fr"))
    {
      void *dlhandle = dlopen (libasejack.c_str(), RTLD_LOCAL | RTLD_NOW); // no API import
      const char *err = dlerror();
      DDEBUG ("%s: dlopen: %s", libasejack, dlhandle ? "OK" : err ? err : "unknown dlerror");
    }
  return Error::NONE;
}

static bool *asejack_loaded = Ase::register_driver_loader ("asejack", try_load_libasejack);
