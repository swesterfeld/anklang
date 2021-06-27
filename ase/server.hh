// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#ifndef __ASE_SERVER_HH__
#define __ASE_SERVER_HH__

#include <ase/gadget.hh>
#include <ase/memory.hh>

namespace Ase {

class ServerImpl : public GadgetImpl, public virtual Server {
  Preferences       prefs_;
  Connection        pchange_;
  FastMemory::Arena telemetry_arena;
public:
  static ServerImplP instancep ();
  explicit     ServerImpl           ();
  virtual     ~ServerImpl           ();
  String       get_version          () override;
  String       get_vorbis_version   () override;
  String       get_mp3_version      () override;
  String       error_blurb          (Error error) const override;
  String       musical_tuning_blurb (MusicalTuning musicaltuning) const override;
  String       musical_tuning_desc  (MusicalTuning musicaltuning) const override;
  uint64       user_note            (const String &text, const String &channel = "misc", UserNote::Flags flags = UserNote::TRANSIENT, const String &r = "") override;
  bool         user_reply           (uint64 noteid, uint r) override;
  bool         broadcast_telemetry  (const TelemetrySegmentS &plan, int32 interval_ms) override;
  void         shutdown             () override;
  ProjectP     last_project         () override;
  ProjectP     create_project       (String projectname) override;
  PropertyS    access_prefs         () override;
  const Preferences& preferences    () const    { return prefs_; }
  using Block = FastMemory::Block;
  Block        telemem_allocate     (uint32 length) const;
  void         telemem_release      (Block telememblock) const;
  ptrdiff_t    telemem_start        () const;
};

// static constexpr const char* telemetry_type (const int64  &field) { return "i64"; }
static constexpr const char* telemetry_type (const int32  &field) { return "i32"; }
static constexpr const char* telemetry_type (const float  &field) { return "f32"; }
static constexpr const char* telemetry_type (const double &field) { return "f64"; }

template<class T> inline TelemetryField
telemetry_field (const String &name, const T *field)
{
  auto start = ServerImpl::instancep()->telemem_start();
  const ptrdiff_t offset = ptrdiff_t (field) - start;
  ASE_ASSERT_RETURN (offset >= 0 && offset < 2147483647, {}); // INT_MAX
  TelemetryField tfield { name, telemetry_type (*field), int32 (offset), sizeof (*field) };
  return tfield;
}

} // Ase

#endif // __ASE_SERVER_HH__
