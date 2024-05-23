// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
// @ts-check

import { LitComponent, html, JsExtract, live, docs, ref } from '../little.js';
import * as Util from '../util.js';
import * as Ase from '../aseapi.js';
import { hex, basename, dirname, displayfs, displaybasename, displaydirname } from '../strings.js';

/** @class BMenuBar
 * @description
 * The <b-menubar> element contains main menus at the top of the window.
 */

// <STYLE/>
JsExtract.css`
b-menubar {
  push-button {
    @apply button-down-within size-10;
    position: relative;
  }
  .b-menubar-icon {
    position: absolute; inset: 0;
    display: inline-flex; justify-content: center;
    [ic] { inset: 0; position: absolute; vertical-align: middle; } // display: inline-block;
    [ic="bc-menumore"] { position: absolute; top: unset; left: unset; }
  }
}`;

// <HTML/>
const HTML = (t, d) =>  html`
  <h-flex class="b-menubar m-2" style="justify-content: space-between" >
    <!-- main menu & controlbar -->

    <!-- menubar left -->
    <b-buttonbar class="-menubar" >
      <!-- File Menu -->
      <push-button class="button-dim" data-tip="**CLICK** File Menu" data-hotkey="Alt+F" @click=${e => t.filemenu.popup (e)} @mousedown=${e => t.filemenu.popup (e)} >
	<div class="b-menubar-icon" >
	  <b-icon ic="bc-folder" ></b-icon>
	  <b-icon ic="bc-menumore" ></b-icon>
	</div>
	<b-contextmenu ${ref (h => t.filemenu = h)} id="g-filemenu" .activate=${activate} .isactive=${isactive} startfocus1 >
	  <button ic="fa-file-o"	kbd="Ctrl+N"		uri="loadnew" >	New Project		</button>
	  <button ic="fa-file-audio-o" 	kbd="Ctrl+O"		uri="load"    >	Open Project…		</button>
	  <button ic="mi-save_alt"     	kbd="Ctrl+S"		uri="save"    >	Save Project		</button>
	  <button ic="fa-save"	   	kbd="Shift+Ctrl+S"	uri="saveas"  >	Save As…		</button>
	  <b-menuseparator></b-menuseparator>
	  <button ic="fa-cog"          	kbd="Ctrl+RawComma"	uri="prefs"   >	Preferences		</button>
	  <b-menuseparator></b-menuseparator>
	  <button ic="mi-close"        	kbd="Shift+Ctrl+Q" 	uri="quit"    >	Quit			</button>
	</b-contextmenu>
      </push-button>

      <!-- Edit Menu -->
      <push-button class="button-dim" data-tip="**CLICK** Edit Menu" data-hotkey="Alt+E" @click=${e => t.editmenu.popup (e)} @mousedown=${e => t.editmenu.popup (e)} >
	<div class="b-menubar-icon" >
	  <b-icon ic="mi-draw" ></b-icon>
	  <b-icon ic="bc-menumore" ></b-icon>
	</div>
	<b-contextmenu ${ref (h => t.editmenu = h)} id="g-editmenu" .activate=${activate} .isactive=${isactive} startfocus >
	  <button ic="mi-undo" .disabled=${!true} kbd="Ctrl+Z"       uri="undo">	Undo	</button>
	  <button ic="mi-redo" .disabled=${!true} kbd="Shift+Ctrl+Z" uri="redo">	Redo	</button>
	</b-contextmenu>
      </push-button>

      <!-- View Menu -->
      <push-button class="button-dim" data-tip="**CLICK** View Menu" data-hotkey="Alt+V" @click=${e => t.viewmenu.popup (e)} @mousedown=${e => t.viewmenu.popup (e)} >
	<div class="b-menubar-icon" >
	  <b-icon ic="fa-eye" ></b-icon>
	  <b-icon ic="bc-menumore" ></b-icon>
	</div>
	<b-contextmenu ${ref (h => t.viewmenu = h)} id="g-viewmenu" .activate=${activate} .isactive=${isactive} startfocus >
	  <button ic="mi-fullscreen" .disabled=${!document.fullscreenEnabled}
		  kbd="F11" uri="fullscreen">	Toggle Fullscreen	</button>
	  ${ELECTRON_MENUITEMS (t)}
	</b-contextmenu>
      </push-button>

    </b-buttonbar>

    <!-- playcontrols -->
    <h-flex>
      <b-playcontrols></b-playcontrols>
      <b-positionview></b-positionview>
    </h-flex>

    <!-- menubar right -->
    <b-buttonbar class="-menubar" >
      <!-- Help Menu -->
      <push-button class="button-dim" data-tip="**CLICK** Help Menu" data-hotkey="Alt+H" @click=${e => t.helpmenu.popup (e)} @mousedown=${e => t.helpmenu.popup (e)} >
	<div class="b-menubar-icon" >
	  <b-icon ic="fa-life-ring" ></b-icon>
	  <b-icon ic="bc-menumore" ></b-icon>
	</div>
	<b-contextmenu ${ref (h => t.helpmenu = h)} id="g-helpmenu" .activate=${activate} .isactive=${isactive} startfocus >
	  <button ic="mi-chrome_reader_mode"	uri="user-manual">	Anklang Manual…		</button>
	  <button ic="mi-chrome_reader_mode"	uri="dev-manual">	Development Reference…	</button>
	  <b-menuseparator></b-menuseparator>
	  <button ic="fa-id-card-o"		uri="about">		About…			</button>
	</b-contextmenu>
      </push-button>
    </b-buttonbar>

  </h-flex>
`;
const ELECTRON_MENUITEMS = (t) => window['Electron'] && html`
  <button ic="mi-zoom_in"   kbd="Ctrl++" uri="zoom-in">			Zoom In		</button>
  <button ic="mi-zoom_out"  kbd="Ctrl+-" uri="zoom-out">		Zoom Out	</button>
  <button ic="—"            kbd="Ctrl+0" uri="zoom-reset">		Reset Zoom	</button>
`;

// == SCRIPT ==
export class BMenuBar extends LitComponent {
  createRenderRoot() { return this; }
  render()
  {
    return HTML (this, {});
  }
  constructor()
  {
    super();
    this.kbd_mapped = false;
    this.project_cleanup1 = null;
    this.filemenu = null;
    this.editmenu = null;
    this.viewmenu = null;
    this.helpmenu = null;
  }
  connectedCallback()
  {
    super.connectedCallback();
    this.project_cleanup1 = Data.project.on ("notify:dirty", () => this.check_isactive());
  }
  disconnectedCallback()
  {
    super.disconnectedCallback();
    this.project_cleanup1?.();
    this.project_cleanup1 = null;
  }
  updated (changed_props)
  {
    if (!this.kbd_mapped && this.isConnected && this.filemenu) {
      this.filemenu.map_kbd_hotkeys (true);
      this.editmenu.map_kbd_hotkeys (true);
      this.viewmenu.map_kbd_hotkeys (true);
      this.helpmenu.map_kbd_hotkeys (true);
      this.kbd_mapped = true;
    }
    this.check_isactive();
  }
  check_isactive()
  {
    this.filemenu.check_isactive();
    this.editmenu.check_isactive();
    this.viewmenu.check_isactive();
    this.helpmenu.check_isactive();
  }
}
customElements.define ('b-menubar', BMenuBar);

async function isactive (uri)
{
  switch (uri) {
    case 'undo':
      return Data.project.can_undo();
    case 'redo':
      return Data.project.can_redo();
    default:
      return true;
  }
}

async function activate (uri, event)
{
  const Electron = window["Electron"] || null;
  let u, v;
  switch (uri) {
    case 'quit_discard':
      window.close();
      break;
    case 'quit':
      v = App.async_button_dialog ("Save Project?",
				   "The current project may contain unsaved changes.\n" +
				   "Save changes to the project before closing?",
				   (Electron ?
				    [ 'Discard Changes', { label: 'Cancel', autofocus: true }, 'Save' ] :
				    [ 'Discard Changes', { label: 'Cancel', autofocus: true },
				      { label: 'Save', disabled: true } ]),
				   'QUESTION');
      v = await v;
      if (v == 0)
	return window_close();
      if (v == 2 && await save_project())
	return window_close();
      break;
    case 'about':
      Shell.show_about_dialog (true);
      break;
    case 'user-manual':
      u = location.origin + '/doc/anklang-manual.html';
      window.open (u, '_blank');
      break;
    case 'dev-manual':
      u = location.origin + '/doc/anklang-internals.html';
      window.open (u, '_blank');
      break;
    case 'prefs':
      Data.show_preferences_dialog = !Data.show_preferences_dialog;
      break;
    case 'zoom-reset':
      await Electron.call ('zoom_level', 0.0);
      break;
    case 'zoom-in':
      await Electron.call ('zoom_level', (await Electron.call ('zoom_level') + 1));
      break;
    case 'zoom-out':
      await Electron.call ('zoom_level', (await Electron.call ('zoom_level')) - 1);
      break;
    case 'fullscreen':
      if (document.fullscreen)
	document.exitFullscreen();
      else
	document.body.requestFullscreen();
      break;
    case 'undo':
      await Data.project.undo();
      break;
    case 'redo':
      await Data.project.redo();
      break;
    case 'loadnew':
      App.load_project_checked();
      break;
    case 'load':
      open_file();
      break;
    case 'save':
      save_project();
      break;
    case 'saveas':
      save_project (true);
      break;
  }
}

function window_close()
{
  window.close();
  // when we're running in the browser, window.close() might not work, so...
  setTimeout (() => {
    window.location.href = 'about:blank';
  }, 0);
}

async function open_file() {
  const opt = {
    title:  _('Open Project'),
    button: _('Open File'),
    cwd:    open_file_last_dir,
    filters: [ { name: 'Projects', extensions: ['anklang'] }, // TODO: filters
	       { name: 'Audio Files', extensions: [ 'anklang', 'mid', 'wav', 'mp3', 'ogg' ] },
	       { name: 'All Files', extensions: [ '*' ] }, ],
  };
  const filename = await App.shell.select_file (opt);
  if (!filename)
    return;
  open_file_last_dir = dirname (filename);
  const err = await App.load_project_checked (filename);
  if (err != Ase.Error.NONE)
    {
      // load_project_checked() displays a dialog
    }
}
let open_file_last_dir = "~MUSIC";

async function save_project (asnew = false) {
  const opt = {
    title:  _('Save Project'),
    button: _('Save As'),
    cwd:    save_project_last_dir || "~MUSIC",
    existing: false,
    filters: [ { name: 'Projects', extensions: ['anklang'] }, ],
  };
  let filename = await Data.project.saved_filename();
  let replace = asnew ? 0 : !!filename;
  if (asnew || !filename)
    filename = await App.shell.select_file (opt);
  if (!filename)
    return false;
  save_project_last_dir = dirname (filename);
  //if (!filename.endsWith ('.anklang'))
  //  filename += '.anklang';
  if (0 && !replace) // TODO: if file_exists (filename)
    {
      replace = await App.async_button_dialog ("Replace Project?",
					       "Replace existing project file?\n" +
					       displayfs (filename) + ": File exists", [
						 'Cancel',
						 { label: 'Replace', autofocus: true }, ],
					       'QUESTION');
      if (replace != 1)
	return false;
    }
  const err = await App.save_project (filename);
  if (err === Ase.Error.NONE)
    {
      filename = await Data.project.saved_filename();
      let msg = '### Project Saved\n';
      msg += '  \n  \nProject saved to: ``' + displayfs (filename) + '``\n';
      App.show_notice (msg);
      return true;
    }
  await App.async_button_dialog ("File IO Error",
				 "Failed to save project.\n" +
				 displayfs (filename) + ": " +
				 await Ase.server.error_blurb (err), [
				   { label: 'Dismiss', autofocus: true }
				 ], 'ERROR');
  return false;
}
let save_project_last_dir = "~MUSIC";
