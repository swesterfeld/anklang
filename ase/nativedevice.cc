// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "nativedevice.hh"
#include "clapdevice.hh"
#include "combo.hh"
#include "project.hh"
#include "jsonipc/jsonipc.hh"
#include "serialize.hh"
#include "internal.hh"

namespace Ase {

// == NativeDeviceImpl ==
JSONIPC_INHERIT (NativeDeviceImpl, NativeDevice);

static DeviceInfo
device_info_static_info (const String &aseid, const AudioProcessor::StaticInfo &static_info)
{
  AudioProcessorInfo pinfo;
  static_info (pinfo);
  DeviceInfo info = {
    .uri          = aseid,
    .name         = pinfo.label,
    .category     = pinfo.category,
    .description  = pinfo.description,
    .website_url  = pinfo.website_url,
    .creator_name = pinfo.creator_name,
    .creator_url  = pinfo.creator_url,
  };
  return info;
}

NativeDeviceImpl::NativeDeviceImpl (const String &aseid, AudioProcessor::StaticInfo static_info, AudioProcessorP aproc) :
  proc_ (aproc), combo_ (std::dynamic_pointer_cast<AudioCombo> (aproc)), info_ (device_info_static_info (aseid, static_info))
{
  assert_return (aproc != nullptr);
}

void
NativeDeviceImpl::serialize (WritNode &xs)
{
  GadgetImpl::serialize (xs);
  // save subdevices
  if (combo_ && xs.in_save())
    for (DeviceP subdevicep : list_devices())
      {
        WritNode xc = xs["devices"].push();
        xc & *dynamic_cast<Serializable*> (&*subdevicep);
        String uri = subdevicep->device_info().uri;
        xc.front ("Device.URI") & uri;
      }
  // load subdevices
  if (combo_ && xs.in_load())
    for (auto &xc : xs["devices"].to_nodes())
      {
        String uri = xc["Device.URI"].as_string();
        if (uri.empty())
          continue;
        auto load_subdevice = [&xc] (DeviceP subdevicep) {
          xc & *dynamic_cast<Serializable*> (&*subdevicep);
        };
        DeviceP subdevicep = insert_device (uri, nullptr, load_subdevice);
      }
}

PropertyS
NativeDeviceImpl::access_properties ()
{
  std::vector<const AudioProcessor::PParam*> pparams;
  pparams.reserve (proc_->params_.size());
  for (const AudioProcessor::PParam &p : proc_->params_)
    pparams.push_back (&p);
  std::sort (pparams.begin(), pparams.end(), [] (auto a, auto b) { return a->info->order < b->info->order; });
  PropertyS pseq;
  pseq.reserve (pparams.size());
  for (const AudioProcessor::PParam *p : pparams)
    pseq.push_back (proc_->access_property (p->id));
  return pseq;
}

void
NativeDeviceImpl::_set_event_source (AudioProcessorP esource)
{
  if (esource)
    assert_return (esource->has_event_output());
  AudioComboP combo = combo_;
  return_unless (combo);
  auto j = [combo, esource] () {
    combo->set_event_source (esource);
  };
  proc_->engine().async_jobs += j;
}

DeviceInfoS
NativeDeviceImpl::list_device_types ()
{
  DeviceInfoS iseq;
  AudioProcessor::registry_foreach ([&iseq] (const String &aseid, AudioProcessor::StaticInfo static_info) {
    AudioProcessorInfo pinfo;
    static_info (pinfo);
    DeviceInfo info;
    info.uri          = aseid;
    info.name         = pinfo.label;
    info.category     = pinfo.category;
    info.description  = pinfo.description;
    info.website_url  = pinfo.website_url;
    info.creator_name = pinfo.creator_name;
    info.creator_url  = pinfo.creator_url;
    if (!info.name.empty() && !info.category.empty())
      iseq.push_back (info);
  });
  for (const DeviceInfo &info : ClapDeviceImpl::list_clap_plugins())
    iseq.push_back (info);
  return iseq;
}

void
NativeDeviceImpl::_set_parent (Gadget *parent)
{
  GadgetImpl::_set_parent (parent);
  if (!parent)
    while (children_.size())
      remove_device (*children_.back());
}

void
NativeDeviceImpl::_activate ()
{
  if (!is_active())
    {
      this->DeviceImpl::_activate();
      for (auto &child : children_)
        child->_activate();
    }
}

template<typename E> std::pair<std::shared_ptr<E>,ssize_t>
find_shared_by_ref (const std::vector<std::shared_ptr<E> > &v, const E &e)
{
  for (ssize_t i = 0; i < v.size(); i++)
    if (&e == &*v[i])
      return std::make_pair (v[i], i);
  return std::make_pair (std::shared_ptr<E>{}, -1);
}

void
NativeDeviceImpl::remove_device (Device &sub)
{
  NativeDeviceP selfp = shared_ptr_cast<NativeDevice> (this);
  assert_return (selfp);
  assert_return (sub._parent() == this);
  auto [subp, nth] = find_shared_by_ref (children_, sub);
  DeviceP childp = subp;
  assert_return (childp && nth >= 0);
  children_.erase (children_.begin() + nth);
  AudioProcessorP sproc = childp->_audio_processor();
  if (sproc && combo_)
    {
      auto deferred_unparent = [selfp, childp] (void*) {
        childp->_set_parent (nullptr); // selfp must still be alive here
      };
      std::shared_ptr<void> atjobdtor = { nullptr, deferred_unparent };
      AudioComboP combop = combo_;
      auto j = [combop, sproc, atjobdtor] () {
        combop->remove (*sproc);
      };
      proc_->engine().async_jobs += j;
      // once job is processed, dtor runs in mainthread
    }
}

DeviceP
NativeDeviceImpl::insert_device (const String &uri, Device *sibling, const DeviceFunc &loader)
{
  DeviceP devicep;
  AudioProcessorP siblingp = sibling ? sibling->_audio_processor() : nullptr;
  if (combo_)
    {
      devicep = create_processor_device (proc_->engine(), uri, false);
      return_unless (devicep, nullptr);
      children_.push_back (devicep);
      devicep->_set_parent (this);
      if (loader)
        loader (devicep);
      AudioProcessorP sproc = devicep->_audio_processor();
      return_unless (sproc, nullptr);
      if (is_active())
        devicep->_activate();
      AudioComboP combo = combo_;
      auto j = [combo, sproc, siblingp] () {
        const size_t pos = siblingp ? combo->find_pos (*siblingp) : ~size_t (0);
        combo->insert (sproc, pos);
      };
      proc_->engine().async_jobs += j;
    }
  return devicep;
}

DeviceP
NativeDeviceImpl::append_device (const String &uri)
{
  return insert_device (uri, nullptr, nullptr);
}

DeviceP
NativeDeviceImpl::insert_device (const String &uri, Device &sibling)
{
  return insert_device (uri, &sibling, nullptr);
}

void
NativeDeviceImpl::_disconnect_remove ()
{
  AudioProcessorP proc = proc_;
  AudioEngine *engine = &proc->engine();
  auto j = [proc] () {
    proc->enable_engine_output (false);
    proc->disconnect_ibuses();
    proc->disconnect_obuses();
    proc->disconnect_event_input();
    // FIXME: remove from combo container if child
  };
  engine->async_jobs += j;
  // FIXME: recurse
}

DeviceP
NativeDeviceImpl::create_native_device (AudioEngine &engine, const String &aseid)
{
  auto make_device = [] (const String &aseid, AudioProcessor::StaticInfo static_info, AudioProcessorP aproc) -> NativeDeviceP {
    return NativeDeviceImpl::make_shared (aseid, static_info, aproc);
  };
  DeviceP devicep = AudioProcessor::registry_create (aseid, engine, make_device);
  return_unless (devicep && devicep->_audio_processor(), nullptr);
  return devicep;
}

DeviceP
create_processor_device (AudioEngine &engine, const String &uri, bool engineproducer)
{
  DeviceP devicep;
  if (string_startswith (uri, "CLAP:"))
    devicep = ClapDeviceImpl::create_clap_device (engine, uri);
  else // assume string_startswith (uri, "Ase:")
    {
      struct NativeDeviceImpl2 : NativeDeviceImpl {
        using NativeDeviceImpl::create_native_device;
      };
      devicep = NativeDeviceImpl2::create_native_device (engine, uri);
    }
  return_unless (devicep, nullptr);
  AudioProcessorP procp = devicep->_audio_processor();
  if (procp) {
    auto j = [procp,engineproducer] () {
      procp->enable_engine_output (engineproducer);
    };
    engine.async_jobs += j;
  }
  return devicep;
}

} // Ase
