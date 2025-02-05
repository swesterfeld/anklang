// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#ifndef __ASE_PROJECT_HH__
#define __ASE_PROJECT_HH__

#include <ase/device.hh>
#include <ase/track.hh>
#include <ase/member.hh>
#include <ase/transport.hh>

namespace Ase {

class UndoScope {
  ProjectImplP projectp_;
  friend class ProjectImpl;
  explicit  UndoScope  (ProjectImplP projectp);
public:
  /*copy*/  UndoScope  (const UndoScope&);
  /*dtor*/ ~UndoScope  ();
  void      operator+= (const VoidF &func);
};

class ProjectImpl final : public DeviceImpl, public virtual Project {
  std::vector<TrackImplP> tracks_;
  ASE_DEFINE_MAKE_SHARED (ProjectImpl);
  TickSignature tick_sig_;
  MusicalTuning musical_tuning_ = MusicalTuning::OD_12_TET;
  uint autoplay_timer_ = 0;
  uint undo_scopes_open_ = 0;
  uint undo_groups_open_ = 0;
  String undo_group_name_;
  struct UndoFunc { VoidF func; String name; };
  std::vector<UndoFunc> undostack_, redostack_;
  struct PStorage;
  PStorage *storage_ = nullptr;
  String saved_filename_;
  bool discarded_ = false;
  friend class UndoScope;
  UndoScope           add_undo_scope (const String &scopename);
protected:
  explicit            ProjectImpl    ();
  virtual            ~ProjectImpl    ();
  virtual bool        bpm_           (const double *n, double *q) override;
  virtual bool        numerator_     (const double *n, double *q) override;
  virtual bool        denominator_   (const double *n, double *q) override;
  void                serialize      (WritNode &xs) override;
  void                update_tempo   ();
  Error               snapshot_project (String &json);
  String              match_serialized (const String &regex, int group) override;
public:
  void                 _activate         () override;
  void                 _deactivate       () override;
  const TickSignature& signature         () const       { return tick_sig_; }
  void                 discard           () override;
  AudioProcessorP      _audio_processor  () const override;
  void                 _set_event_source (AudioProcessorP esource) override;
  DeviceInfo           device_info       () override;
  UndoScope            undo_scope        (const String &scopename);
  void                 push_undo         (const VoidF &func);
  void                 undo              () override;
  bool                 can_undo          () override;
  void                 redo              () override;
  bool                 can_redo          () override;
  void                 group_undo        (const String &undoname) override;
  void                 ungroup_undo      () override;
  void                 clear_undo        ();
  size_t               undo_size_guess   () const;
  void                 start_playback    (double autostop);
  void                 start_playback    () override    { start_playback (D64MAX); }
  void                 stop_playback     () override;
  bool                 is_playing        () override;
  TrackP               create_track      () override;
  bool                 remove_track      (Track &child) override;
  TrackS               all_tracks        () override;
  TrackP               master_track      () override;
  Error                load_project      (const String &utf8filename) override;
  StreamReaderP        load_blob         (const String &fspath);
  String               loader_resolve    (const String &hexhash);
  Error                save_project      (const String &utf8filename, bool collect) override;
  String               saved_filename    () override; // returns utf8filename
  String               writer_file_name  (const String &fspath) const;
  Error                writer_add_file   (const String &fspath);
  Error                writer_collect    (const String &fspath, String *hexhashp);
  TelemetryFieldS      telemetry         () const override;
  AudioProcessorP      master_processor  () const;
  ssize_t              track_index       (const Track &child) const;
  static ProjectImplP  create            (const String &projectname);
  static size_t undo_mem_counter;
};
using ProjectImplP = std::shared_ptr<ProjectImpl>;

} // Ase

#endif // __ASE_PROJECT_HH__
