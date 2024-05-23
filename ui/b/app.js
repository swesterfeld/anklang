// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
// @ts-check

/** == B-APP ==
 * Global application instance for Anklang.
 * *zmovehooks*
 * : An array of callbacks to be notified on pointer moves.
 * *zmove()*
 * : Trigger the callback list `zmovehooks`. This is useful to get debounced
 * notifications for pointer movements, including 0-distance moves after significant UI changes.
 */

import VueComponents from '../all-components.js';
import ShellClass from '../b/shell.js';
import * as Util from '../util.js';
import * as Mouse from '../mouse.js';
import { hex, basename, dirname, displayfs, displaybasename, displaydirname } from '../strings.js';
import { Signal, State, Computed, Watcher, tracking_wrapper } from "../signal.js";

// == App ==
export class AppClass {
  panel2_types = [ 'd' /*devices*/, 'p' /*pianoroll*/ ];
  panel3_types = [ 'i' /*info*/, 'b' /*browser*/ ];
  constructor (vue_app) {
    // super();
    { // mimick familiar LitComponent API
      let update_queued = false;
      this.request_update = () => {
	if (update_queued) return;
	update_queued = true;
	queueMicrotask (() => {
	  update_queued = false;
	  this.updated ({});
	});
      };
      this.updated = tracking_wrapper (this.request_update, this.updated.bind (this));
    }
    // legacy (mostly for Vue interop compat), Data should become App once Vue is gone
    this.vue_app = vue_app;
    const data = {
      project: null,
      mtrack: null, // master track
      panel3: 'i',
      panel2: 'p',
      piano_roll_source: undefined,
      current_track: undefined,
      show_preferences_dialog: false,
    };
    this.data = Vue.reactive (data);
    Object.defineProperty (globalThis, 'App', { value: this });
    Object.defineProperty (globalThis, 'Data', { value: this.data });
    this.vue_app.config.globalProperties.App = App;   // global App, export methods
    this.vue_app.config.globalProperties.Data = Data; // global Data, reactive
    this.request_update();
  }
  #project = new Signal.State (undefined);
  get project ()  { return this.#project.get(); }
  set project (p) { this.#project.set (this.data.project = p); }
  get current_track () { return this.data.current_track; }
  set current_track (t)
  {
    if (this.data.current_track === t) return;
    this.data.current_track = t;
    if (this.shell)
      for (const tv of this.shell.$el.querySelectorAll ('b-trackview'))
	tv.notify_current_track(); // see trackview.js
  }
  updated (changed_props)
  {
    const name = this.project?.name;
    document.title = Util.format_title ('Anklang', name);
  }
  mount (id) {
    this.shell = this.vue_app.mount (id);
    Object.defineProperty (globalThis, 'Shell', { value: this.shell });
    if (!this.shell)
      throw Error (`failed to mount App at: ${id}`);
  }
  shell_unmounted() {
  }
  switch_panel3 (n) {
    const a = this.panel3_types;
    if ('string' == typeof n)
      Data.panel3 = n;
    else
      Data.panel3 = a[(a.indexOf (Data.panel3) + 1) % a.length];
  }
  switch_panel2 (n) {
    const a = this.panel2_types;
    if ('string' == typeof n)
      Data.panel2 = n;
    else
      Data.panel2 = a[(a.indexOf (Data.panel2) + 1) % a.length];
  }
  open_piano_roll (midi_source) {
    Data.piano_roll_source = midi_source;
    if (Data.piano_roll_source)
      this.switch_panel2 ('p');
  }
  async load_project_checked (project_or_path) {
    const err = await this.load_project (project_or_path);
    if (err !== Ase.Error.NONE)
      App.async_button_dialog ("Project Loading Error",
			       "Failed to open project.\n" +
			       displayfs (project_or_path) + ":\n" +
			       await Ase.server.error_blurb (err), [
				 { label: 'Dismiss', autofocus: true },
			       ], 'ERROR');
    return err;
  }
  async load_project (project_or_path) {
    // always replace the existing project with a new one
    let newproject = project_or_path instanceof Ase.Project ? project_or_path : null;
    if (!newproject)
      {
	// Create afresh
	newproject = await Ase.server.create_project ('Untitled');
	// Loads from disk
	if (project_or_path)
	  {
	    const error = await newproject.load_project (project_or_path);
	    if (error != Ase.Error.NONE)
	      return error;
	    newproject.name = displaybasename (project_or_path);
	  }
      }
    const mtrack = await newproject.master_track();
    const tracks = await newproject.all_tracks();
    // shut down old project
    let need_reload = false;
    if (App.project)
      {
	App.project.stop_playback();
	App.project = null; // TODO: should trigger FinalizationGroup
	// TODO: App.open_piano_roll (undefined);
	need_reload = true;
      }
    // replace project & master track without await, to synchronously trigger Vue updates for both
    App.project = newproject; // assigns Data.project
    Data.mtrack = mtrack;
    App.current_track = tracks[0];
    const clips = await App.current_track.launcher_clips();
    App.open_piano_roll (clips.length ? clips[0] : null);
    if (this.shell)
      this.shell.update();
    if (need_reload) {
      window.location.reload();
      // TODO: only reload the UI partially when the project changes, this requires a full port to LitElements
    }
    return Ase.Error.NONE;
  }
  async save_project (projectpath, collect = true) {
    Shell.show_spinner();
    let error = !Data.project ? Ase.Error.INTERNAL :
		  Data.project.save_project (projectpath, collect);
    error = await error;
    // await new Promise (r => setTimeout (r, 3 * 1000)); // artificial wait to test spinner
    Shell.hide_spinner();
    return error;
  }
  status (...args) {
    console.log (...args);
  }
  async_modal_dialog (dialog_setup) {
    return this.shell.async_modal_dialog (dialog_setup);
  }
  async_button_dialog (title, text, buttons = [], emblem) {
    const dialog_setup = {
      title,
      text,
      buttons,
      emblem,
    };
    return this.shell.async_modal_dialog (dialog_setup);
  }
  /** Show a notification notice, with adequate default timeout */
  show_notice (text, timeout = undefined) {
     /**@type{any}*/
    const b_noticeboard = document.body.querySelector ('b-noticeboard');
    console.assert (b_noticeboard);
    b_noticeboard.create_note (text, timeout);
  }
  zmoves_add = Mouse.zmove_add;
  zmove = Mouse.zmove_trigger;
  zmove_last = Mouse.zmove_last;
}

// == addvc ==
export async function create_app() {
  if (globalThis.App)
    return globalThis.App;
  // prepare Vue component templates
  for (const [__name, vcomponent] of Object.entries (VueComponents)) {
    /**@type{any}*/
    const component = vcomponent;
    if (component.sfc_template) // also constructs Shell.template
      component.template = component.sfc_template.call (null, Util.tmplstr, null);
  }
  // create and configure Vue App
  const vue_app = Vue.createApp (ShellClass); // must have Shell.template
  vue_app.config.compilerOptions.isCustomElement = tag => !!window.customElements.get (tag);
  vue_app.config.compilerOptions.whitespace = 'preserve';
  // common globals
  const global_properties = {
    CONFIG: globalThis.CONFIG,
    debug: globalThis.debug,
    Util: globalThis.Util,
    Ase: globalThis.Ase,
    window: globalThis.window,
    document: globalThis.document,
    observable_from_getters: Util.observable_from_getters,
  };
  Object.assign (vue_app.config.globalProperties, global_properties);
  // register directives, mixins, components
  for (let directivename in Util.vue_directives) // register all utility directives
    vue_app.directive (directivename, Util.vue_directives[directivename]);
  for (let mixinname in Util.vue_mixins)         // register all utility mixins
    vue_app.mixin (Util.vue_mixins[mixinname]);
  for (const [name, component] of Object.entries (VueComponents))
    if (component !== ShellClass)
      vue_app.component (name, component);
  // create main App instance
  const app = new AppClass (vue_app);
  console.assert (app === globalThis.App);
  return globalThis.App;
}
