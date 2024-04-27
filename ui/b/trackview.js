// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
// @ts-check

/** == B-TRACKVIEW ==
 * A Vue template to display a project's Ase.Track.
 * ### Props:
 * *project*
 * : The *Ase.project* containing playback tracks.
 * *track*
 * : The *Ase.Track* to display.
 */

import { LitComponent, html, render, JsExtract, docs, ref } from '../little.js';
import { get_uri } from '../dom.js';
import { render_contextmenu } from './contextmenu.js';

// == STYLE ==
JsExtract.css`
b-trackview {
  display: flex;
  align-items: stretch;
  background-color: $b-button-border;
  border: 1px solid $b-button-border;
  border-top-left-radius: $b-button-radius;
  border-bottom-left-radius: $b-button-radius;
  .-lvm-main { // level meter
    height: calc($b-track-meter-thickness + $b-track-meter-gap + $b-track-meter-thickness);
    position: relative;
    // push element onto own compositing layer to reduce rendering overhead
    will-change: auto;
  }
  .-lvm-levelbg {
    height: 100%;
    --db-zpc: 66.66%;
    background: linear-gradient(to right, #0b0, #bb0 var(--db-zpc), #b00);
  }
  .-lvm-covertip0, .-lvm-covermid0, .-lvm-covertip1, .-lvm-covermid1,
  .-lvm-levelbg, .-lvm-coverspace      { position: absolute; width: 100%; }
  .-lvm-covertip0, .-lvm-covermid0     { top: 0px; }
  .-lvm-coverspace                     { top: calc($b-track-meter-thickness - 0.25px); height: calc($b-track-meter-gap + 0.5px); }
  .-lvm-covertip1, .-lvm-covermid1     { top: calc($b-track-meter-thickness + $b-track-meter-gap); }
  .-lvm-coverspace {
    background-color: rgba( 0, 0, 0, .80);
  }
  .-lvm-covertip0, .-lvm-covermid0, .-lvm-covertip1, .-lvm-covermid1 {
    height: $b-track-meter-thickness;
    background-color: rgba( 0, 0, 0, .75);
    transform-origin: center right;
    will-change: transform;
    transform: scaleX(1);
  }
  .-lvm-covertip1, .-lvm-covermid1 {
    height: calc($b-track-meter-thickness + 1px);
    // add 1px to cover for rounded coords
  }
  .b-trackview-control {
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    margin-right: 5px;
    overflow: hidden;
  }
}
b-trackview[current-track] .b-trackview-control {
  background-color: zmod($b-button-border, Jz+=25%);
}`;

// == HTML ==
const HTML = (t, d) => html`
  <div class="b-trackview-control" data-tip="**CLICK** Select Track **RIGHTCLICK** Track Menu"
    @click=${t.track_click0} @contextmenu=${t.menu_open}
    ${ref (h => t.trackviewcontrol_ = h)} >
    <b-editable ${ref (h => t.trackname_ = h)} clicks="2" style="min-width: 4em; width: 7em"
      selectall @change=${event => t.track.name (event.detail.value.trim())}
      value=${t.wtrack_.name}></b-editable>
    <div class="-lvm-main">
      <div class="-lvm-levelbg" ${ref (h => t.levelbg_ = h)}></div>
      <div class="-lvm-covermid0" ${ref (h => t.covermid0_ = h)}></div>
      <div class="-lvm-covertip0" ${ref (h => t.covertip0_ = h)}></div>
      <div class="-lvm-coverspace" ></div>
      <div class="-lvm-covermid1" ${ref (h => t.covermid1_ = h)}></div>
      <div class="-lvm-covertip1" ${ref (h => t.covertip1_ = h)}></div>
    </div>
  </div>
`;
const HTML_CONTEXTMENU = (t, d) => html`
  <b-contextmenu @activate=${t.menu_click} .isactive=${t.menu_check} @close=${t.menu_close} @cancel=${t.menu_close} >
    <b-menutitle>                                         Track             </b-menutitle>
    <button ic="fa-plus-circle"    uri="add-track" >      Add Track             </button>
    <button ic="fa-i-cursor"       uri="rename-track" >   Rename Track          </button>
    <button ic="fa-toggle-down"    uri="bounce-track" >   Bounce Track          </button>
    <button ic="mi-visibility_off" uri="track-details" >  Show / Hide Track Details </button>
    <b-menuseparator></b-menuseparator>
    <b-menurow> <!-- ic="fa-clone" uri="clone-track" >    Dupl.                 -->
      <button ic="fa-times-circle" uri="delete-track" >   Delete                </button>
      <button ic="fa-scissors"     uri="cut-track" >      Cut                   </button>
      <button ic="fa-files-o"      uri="copy-track" >     Copy                  </button>
      <button ic="fa-clipboard"    uri="paste-track" >    Paste                 </button>
    </b-menurow>
    <b-menuseparator></b-menuseparator>
    <b-menutitle> Playback </b-menutitle>
    <button ic="uc-Ｍ"             uri="mute-track" >     Mute Track            </button>
    <button ic="uc-Ｓ"             uri="solo-track" >     Solo Track            </button>
    <b-menuseparator></b-menuseparator>
    <b-menutitle> MIDI Channel </b-menutitle>
    <button   uri="mc-0"  ic=${t.mcc (0)}  > Internal Channel </button>
    <b-menurow noturn>
      <button uri="mc-1"  ic=${t.mcc (1)}  >  1 </button>
      <button uri="mc-2"  ic=${t.mcc (2)}  >  2 </button>
      <button uri="mc-3"  ic=${t.mcc (3)}  >  3 </button>
      <button uri="mc-4"  ic=${t.mcc (4)}  >  4 </button>
    </b-menurow> <b-menurow noturn>
      <button uri="mc-5"  ic=${t.mcc (5)}  >  5 </button>
      <button uri="mc-6"  ic=${t.mcc (6)}  >  6 </button>
      <button uri="mc-7"  ic=${t.mcc (7)}  >  7 </button>
      <button uri="mc-8"  ic=${t.mcc (8)}  >  8 </button>
    </b-menurow> <b-menurow noturn>
      <button uri="mc-9"  ic=${t.mcc (9)}  >  9 </button>
      <button uri="mc-10" ic=${t.mcc (10)} > 10 </button>
      <button uri="mc-11" ic=${t.mcc (11)} > 11 </button>
      <button uri="mc-12" ic=${t.mcc (12)} > 12 </button>
    </b-menurow> <b-menurow noturn>
      <button uri="mc-13" ic=${t.mcc (13)} > 13 </button>
      <button uri="mc-14" ic=${t.mcc (14)} > 14 </button>
      <button uri="mc-15" ic=${t.mcc (15)} > 15 </button>
      <button uri="mc-16" ic=${t.mcc (16)} > 16 </button>
    </b-menurow>
  </b-contextmenu>
`;

// == SCRIPT ==
import * as Ase from '../aseapi.js';
import * as Util from '../util.js';
import { clamp } from '../util.js';

const MINDB = -72.0; // -96.0;
const MAXDB =  +6.0; // +12.0;
const DBOFFSET = Math.abs (MINDB) * 1.5;
const DIV_DBRANGE = 1.0 / (MAXDB - MINDB);
const NUMBER_ATTRIBUTE = { type: Number, reflect: true }; // sync attribute with property
const OBJECT_PROPERTY = { attribute: false };
let trackview_contextmenu = null;

class BTrackView extends LitComponent {
  createRenderRoot() { return this; }
  render()
  {
    this.notify_current_track();
    return HTML (this);
  }
  static properties = {
    track: OBJECT_PROPERTY,
    trackindex: NUMBER_ATTRIBUTE,
  };
  constructor()
  {
    super();
    this.track = null;
    this.trackviewcontrol_ = null;
    this.trackindex = -1;
    this.wtrack_ = { name: '   ' };
    this.dbtip0_ = MINDB;
    this.dbtip1_ = MINDB;
    this.teleobj = null;
    this.telemetry = null;
    this.trackname_ = null;
    this.levelbg_ = null;
    this.covermid0_ = null;
    this.covertip0_ = null;
    this.covermid1_ = null;
    this.covertip1_ = null;
  }
  connectedCallback()
  {
    super.connectedCallback();
    // ensure ContextMenu is ready when needed, *without* Chrome reloading styles and causing flicker
    if (!trackview_contextmenu)
      trackview_contextmenu = render_contextmenu (trackview_contextmenu, HTML_CONTEXTMENU, { mcc: () => ' ' });
  }
  disconnectedCallback()
  {
    super.disconnectedCallback();
    this.telemetry = null;
    Util.telemetry_unsubscribe (this.teleobj);
    this.teleobj = null;
  }
  notify_current_track() // see app.js
  {
    if (this.track === App.current_track)
      this.setAttribute ('current-track', '');
    else
      this.removeAttribute ('current-track');
  }
  updated (changed_props)
  {
    if (changed_props.has ('track'))
      {
	const weakthis = new WeakRef (this); // avoid strong wtrack_->this refs for automatic cleanup
	this.wtrack_ = Util.wrap_ase_object (this.track, { name: '???', midi_channel: -1 }, () => weakthis.deref()?.requestUpdate());
	Util.telemetry_unsubscribe (this.teleobj);
	this.teleobj = null;
	// subscribe telemetry
	const async_updates = async () => {
	  this.telemetry = await Object.freeze (this.track.telemetry());
	  if (!this.teleobj && this.telemetry)
	    this.teleobj = Util.telemetry_subscribe (this.recv_telemetry.bind (this), this.telemetry);
	};
	async_updates();
      }
    // setup level gradient based on MINDB..MAXDB
    this.levelbg_.style.setProperty ('--db-zpc', -MINDB * 100.0 / (MAXDB - MINDB) + '%');
    // cache level width in pixels to avoid expensive recalculations in fps handler
    this.level_width_ = this.levelbg_.getBoundingClientRect().width;
  }
  mcc (n) // midi_channel character
  {
    return n == this.wtrack_.midi_channel ? '√' : ' ';
  }
  track_click0 (event)
  {
    event.stopPropagation();
    if (event.button == 0 && this.track)
      App.current_track = this.track;
  }
  menu_close()
  {
    trackview_contextmenu.close();
  }
  menu_open (event)
  {
    App.current_track = this.track;
    // update trackview menu for popup
    trackview_contextmenu = render_contextmenu (trackview_contextmenu, HTML_CONTEXTMENU, this);
    // popup menu at mouse coords
    trackview_contextmenu.popup (event, { origin: 'none' });
    return Util.prevent_event (event);
  }
  async menu_check (uri)
  {
    switch (uri)
    {
      case 'add-track':    return true;
      case 'delete-track': return App.current_track && !await App.current_track.is_master();
      case 'rename-track': return true;
    }
    if (uri.startsWith ('mc-'))
      return true;
    return false;
  }
  async menu_click (event)
  {
    const uri = get_uri (event.detail);
    // close popup to remove focus guards
    this.menu_close();
    if (uri == 'add-track')
      {
	const track = await Data.project.create_track ('Track');
	if (track)
	  App.current_track = track;
      }
    if (uri == 'delete-track')
      {
	const del_track = this.track;
	let tracks = App.project.all_tracks();
	Data.project.remove_track (del_track);
	tracks = await tracks;
	const index = Util.array_index_equals (tracks, del_track);
	tracks.splice (index, 1);
	if (index < tracks.length) // false if deleting Master
	  App.current_track = tracks[index];
      }
    if (uri == 'rename-track')
      this.trackname_.activate();
    if (uri.startsWith ('mc-'))
      {
	const ch = parseInt (uri.substr (3));
	this.track.midi_channel (ch);
      }
  }
  recv_telemetry (teleobj, arrays)
  {
    let dbspl0 = arrays[teleobj.dbspl0.type][teleobj.dbspl0.index];
    let dbspl1 = arrays[teleobj.dbspl1.type][teleobj.dbspl1.index];
    dbspl0 = clamp (dbspl0, MINDB, MAXDB);
    dbspl1 = clamp (dbspl1, MINDB, MAXDB);
    this.dbtip0_ = Math.max ((DBOFFSET + this.dbtip0_) * 0.99, DBOFFSET + dbspl0) - DBOFFSET;
    this.dbtip1_ = Math.max ((DBOFFSET + this.dbtip1_) * 0.99, DBOFFSET + dbspl1) - DBOFFSET;
    this.update_levels (dbspl0, this.dbtip0_, dbspl1, this.dbtip1_);
  }
  update_levels (dbspl0, dbtip0, dbspl1, dbtip1)
  {
    /* Paint model:
     * |                                           ######| covertipN_, dark tip cover layer
     * |             #############################       | covermidN_, dark middle cover
     * |-36dB+++++++++++++++++++++++++++++++0++++++++12dB| levelbg_, dB gradient
     *  ^^^^^^^^^^^^^ visible level (-24dB)       ^ visible tip (+6dB)
     */
    const covertip0 = this.covertip0_, covermid0 = this.covermid0_;
    const covertip1 = this.covertip1_, covermid1 = this.covermid1_;
    const level_width = this.level_width_, pxrs = 1.0 / level_width; // pixel width fraction between 0..1
    if (dbspl0 === undefined) {
      covertip0.style.setProperty ('transform', 'scaleX(1)');
      covertip1.style.setProperty ('transform', 'scaleX(1)');
      covermid0.style.setProperty ('transform', 'scaleX(0)');
      covermid1.style.setProperty ('transform', 'scaleX(0)');
      return;
    }
    const tw = 2; // tip thickness in pixels
    // handle multiple channels
    const per_channel = (dbspl, dbtip, covertip, covermid) => {
      // map dB SPL to a 0..1 paint range
      const tip = (dbtip - MINDB) * DIV_DBRANGE;
      const lev = (dbspl - MINDB) * DIV_DBRANGE;
      // scale covertip from 100% down to just the amount above the tip
      let transform = 'scaleX(' + (1 - tip) + ')';
      if (transform !== covertip.style.getPropertyValue ('transform'))    // reduce style recalculations
	covertip.style.setProperty ('transform', transform);
      // scale and translate middle cover
      if (lev + pxrs + tw * pxrs <= tip) {
	const width = (tip - lev) - tw * pxrs;
	const trnlx = level_width - level_width * tip + tw; // translate left in pixels
	transform = 'translateX(-' + trnlx + 'px) scaleX(' + width + ')';
      } else {
	// hide covermid if level and tip are aligned
	transform = 'scaleX(0)';
      }
      if (transform != covermid.style.getPropertyValue ('transform'))     // reduce style recalculations
	covermid.style.setProperty ('transform', transform);
    };
    per_channel (dbspl0, dbtip0, covertip0, covermid0);
    per_channel (dbspl1, dbtip1, covertip1, covermid1);
  }
}
customElements.define ('b-trackview', BTrackView);
