// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
'use strict';

import MarkdownIt from '/markdown-it.mjs';

/** Generate `element.innerHTML` from `markdown_text` */
export function markdown_to_html (element, markdown_text) {
  // configure Markdown generator
  const config = { linkify: true };
  const md = new MarkdownIt (config);
  // add target=_blank to all links
  const orig_link_open = md.renderer.rules.link_open || function (tokens, idx, options, env, self) {
    return self.renderToken (tokens, idx, options); // default renderer
  };
  md.renderer.rules.link_open = function (tokens, idx, options, env, self) {
    const aIndex = tokens[idx].attrIndex ('target'); // attribute could be present already
    if (aIndex >= 0)
      tokens[idx].attrs[aIndex][1] = '_blank';       // override when present
    else
      tokens[idx].attrPush (['target', '_blank']);   // or add new attribute
    return orig_link_open (tokens, idx, options, env, self); // resume
  };
  // render HTML
  const html = md.render (markdown_text);
  element.classList.add ('b-markdown-it-outer');
  element.innerHTML = html;
}

/// Check if a particular font is present (and deviates from the fallback font)
export function font_family_loaded (options = {})
{
  const defaults = { font_family: 'emoji', text: 'abc_0123' };
  options = Object.assign (defaults, options);
  const el = document.createElement ('span');
  el.style.margin = 0;
  el.style.padding = 0;
  el.style.border = 0;
  el.style.fontSize = 'initial';
  el.style.display = 'inline';
  el.style.visibility = 'hidden';
  el.style.position = 'absolute';
  el.style.top = '-9999px';
  el.style.left = '-9999px';
  el.style.fontSize = '99px';
  el.innerHTML = options.text;
  // compare with sans-serif size
  el.style.fontFamily = 'sans-serif';
  document.body.appendChild (el);
  const sans_offset_width = el.offsetWidth, sans_offset_height = el.offsetHeight;
  el.style.fontFamily = '"'+options.font_family+'"' + ', sans-serif';
  let font_offset_width = el.offsetWidth, font_offset_height = el.offsetHeight;
  document.body.removeChild (el);
  if (sans_offset_width != font_offset_width || sans_offset_height != font_offset_height)
    return true;
  // compare with monospace size
  el.style.fontFamily = 'monospace';
  document.body.appendChild (el);
  const monospace_offset_width = el.offsetWidth, monospace_offset_height = el.offsetHeight;
  el.style.fontFamily = '"'+options.font_family+'"' + ', monospace';
  font_offset_width = el.offsetWidth; font_offset_height = el.offsetHeight;
  document.body.removeChild (el);
  if (monospace_offset_width != font_offset_width || monospace_offset_height != font_offset_height)
    return true;
  return false;
}

/// Fetch URI from a DOM element, returns undefined if none is found (e.g. Number(0) is a valid URI).
export function get_uri (element)
{
  if (!element) return undefined;
  let uri = element['uri'];
  if (uri === undefined || uri === null || uri === '')
    uri = !element.getAttribute ? undefined : element.getAttribute ('uri');
  if (uri === undefined || uri === null || uri === '')
    return get_uri (element.parentElement);
  return uri;
}

/// Check if URI is not undefined.
export function valid_uri (uri)
{
  return uri !== undefined;
}

/// Check if DOM element has valid URI.
export function has_uri (element)
{
  return valid_uri (get_uri (element));
}

/// Get `.textContent` with or without children from a DOM element.
export function text_content (element, with_children = true)
{
  if (with_children) return element.textContent;
  let s = '';
  for (let i = 0; i < element.childNodes.length; ++i)
    if (element.childNodes[i].nodeType === Node.TEXT_NODE)
      s += element.childNodes[i].textContent;
  return s;
}

/// Show a `dialog` via showModal() and close it on backdrop clicks.
export function show_modal (dialog)
{
  if (dialog.open) return;
  // close dialog on backdrop clicks, but:
  // - avoid matching text-select drags that end up on backdrop area
  // - avoid matching Enter-click event coordinates from input-submit with clientX*clientY==0
  // - avoid matching an outside popup click, after a previous pointerdown+Escape combination
  // - avoid re-popup by clicking on outside menu button and closing early on pointerdown
  let pointer_outside = false; // must reset on every dialog.showModal()
  const pointerdown = event => {
    pointer_outside = (event.buttons && event.target === dialog && // backdrop has target==dialog
		       (event.offsetX < 0 || event.offsetX >= event.target.offsetWidth ||
			event.offsetY < 0 || event.offsetY >= event.target.offsetHeight));
  };
  const pointerup = event => {
    if (pointer_outside && event.target === dialog && // backdrop as target is dialog
	(event.offsetX < 0 || event.offsetX >= event.target.offsetWidth ||
	 event.offsetY < 0 || event.offsetY >= event.target.offsetHeight))
      dialog.close();
    else
      pointer_outside = false;
  };
  const mousedown = event => {
    // prevent focus on the dialog itself
    if (event.buttons)
      event.preventDefault();
  };
  const capture = { capture: true };
  const close = event => {
    dialog.removeEventListener ('pointerdown', pointerdown, capture);
    dialog.removeEventListener ('pointerup', pointerup);
    dialog.removeEventListener ('mousedown', mousedown);
    dialog.removeEventListener ('close', close);
  };
  dialog.addEventListener ('pointerdown', pointerdown, capture);
  dialog.addEventListener ('pointerup', pointerup);
  dialog.addEventListener ('mousedown', mousedown);
  dialog.addEventListener ('close', close);
  dialog.showModal();
  return dialog;
}
