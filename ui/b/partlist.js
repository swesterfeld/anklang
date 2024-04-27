// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
// @ts-check

import { LitComponent, html, JsExtract, docs, ref } from '../little.js';
import * as Util from '../util.js';

/** @class BPartList
 * @description
 * The <b-partlist> element allows to arrange Clip objects for playback.
 */

// == STYLE ==
JsExtract.css`
b-partlist {
  display: flex;
  position: relative;
  b-clipview {
    margin: 0 1px;
    width: calc(5 * $b-clipthumb-width);
    flex-shrink: 0; flex-grow: 0;
  }
}`;

// == HTML ==
const HTML = (t, d) => [
  t.wtrack.arranger_parts.map ((clip, index) =>
    html`  <b-clipview .clip=${clip} index=${index} .track=${t.track} trackindex=${t.trackindex} ></b-clipview>`
  ),
];

// == SCRIPT ==
import * as Ase from '../aseapi.js';

const OBJECT_PROPERTY = { attribute: false };
const NUMBER_ATTRIBUTE = { type: Number, reflect: true }; // sync attribute with property

class BPartList extends LitComponent {
  createRenderRoot() { return this; }
  render()
  {
    const d = {};
    return HTML (this, d);
  }
  static properties = {
    track:	OBJECT_PROPERTY,
    trackindex:	NUMBER_ATTRIBUTE,
  };
  constructor()
  {
    super();
    this.track = null;
    this.wtrack = { arranger_parts: [] };	// dummy
    this.trackindex = -1;
    this.setAttribute ('data-f1', "#part-list");
    this.addEventListener ('dblclick', this.dblclick.bind (this));
  }
  updated (changed_props)
  {
    if (changed_props.has ('track'))
      {
	const weakthis = new WeakRef (this); // avoid strong wtrack->this refs for automatic cleanup
	this.wtrack = Util.wrap_ase_object (this.track, { arranger_parts: [] }, () => weakthis.deref()?.requestUpdate());
      }
  }
  dblclick (event)
  {
    this.track.create_part (0);
  }
}
customElements.define ('b-partlist', BPartList);
