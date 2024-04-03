// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
// @ts-check

/** @class BMenuRow
 * @description
 * The <b-menurow> element can contain `<button/>` menu items of a [BContextMenu](#BContextMenu),
 * that are packed horizontally inside a menurow.
 *
 * ### Props:
 * *noturn*
 * : Avoid turning the icon-label direction in menu items to be upside down.
 *
 * ### Slots:
 * *default*
 * : All contents passed into this slot will be rendered as contents of this element.
 */

import { LitComponent, html, JsExtract, docs } from '../little.js';

// == STYLE ==
JsExtract.css`
b-menurow {
  @apply m-0 flex flex-initial items-baseline justify-center text-center;
  flex-flow: row nowrap;
}`;

// == SCRIPT ==
const BOOL_ATTRIBUTE = { type: Boolean, reflect: true }; // sync attribute with property

class BMenuRow extends LitComponent {
  createRenderRoot() { return this; }
  static properties = {
    noturn: BOOL_ATTRIBUTE,
  };
  constructor()
  {
    super();
    this.noturn = false;
  }
}
customElements.define ('b-menurow', BMenuRow);
