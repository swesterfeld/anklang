// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

import { LitElement, html, css, postcss, docs } from '../little.js';

/** # B-MORE
 * Indicator for adding or dropping new UI elements.
 */

// == STYLE ==
const STYLE = await postcss`
@import 'mixins.scss';
:host { // b-more
  display: flex;
  align-items: center;
  justify-content: space-evenly;
}
.-plus {
  display: flex;
  height: 90%;
  align-items: center;
  justify-content: space-evenly;
  padding: 0 0.2em;
  margin: 0 0.2em;
  border-radius: 3px;
  font-weight: bold;
  font-size: 120%;
  cursor: default; user-select: none;
  border: 1px solid #bbb3;
  color: #bbb;
  &:hover { border: 1px solid #bbba; }
}
`;

// == HTML ==
const HTML = html`
  <v-flex class="-plus" >
    +
  </v-flex>
`;

// == SCRIPT ==
class BMore extends LitElement {
  render = () => HTML;
  static styles = [ STYLE ];
}
customElements.define ('b-more', BMore);
