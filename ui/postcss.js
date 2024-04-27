// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
'use strict';

/* PostCSS driver for the web browser and CLI.
 * This is an ESM module that uses require() to load PostCSS CJS modules from
 * node_modules/ or via a browserified window.require implementation.
 */
const __BROWSER__ = !globalThis.process?.versions?.node, __NODE__ = !__BROWSER__;
const __filename = !__NODE__ ? 'postcss.js' : import.meta.url.replace (/^.*?:\/\/\//, '/');
const __MAIN__ = __NODE__ && process.argv[1] == __filename;
const require = globalThis.require || (await import ("module")).createRequire (__filename);

// == imports ==
const FS = __NODE__ && require ('fs');
const css_color_converter = require ('css-color-converter');
const PostCss = require ('postcss');
const postcss_scss = require ('postcss-scss');
const import_paths = [];
import * as Colors from './colors.js';


// == TailwindCSS ==
import tailwind_config from './tailwind.config.mjs';

// == Plugins ==
const postcss_advanced_variables = require ('postcss-advanced-variables');
const postcss_color_hwb = require ('postcss-color-hwb');
const postcss_discard_comments = require ('postcss-discard-comments');
const postcss_discard_duplicates = require ('postcss-discard-duplicates');
const postcss_functions = require ('postcss-functions');
const postcss_lab_function = require ('postcss-lab-function');
const postcss_normalize_charset = require ('postcss-normalize-charset');
// Configure processing behaviour
function postcss_plugins (options = {})
{
  const opts = Object.assign ({ import_all: false, import_root: '/' }, options);
  const variables = options.vars || {};
  const plugins = [
    postcss_discard_comments ({ remove: comment => true }),
    postcss_advanced_variables ({ disable: '@content, @each, @for', // @if, @else, @mixin, @include, @import
				  importRoot: opts.import_root,
				  importFilter: string => opts.import_all || !string.endsWith ('.css'),
				  importResolve: __NODE__ ? load_import : find_import,
				  variables }),
    postcss_color_hwb,
    postcss_lab_function,
    postcss_functions (css_functions()),
    require ('tailwindcss/nesting') ({}), // require ('postcss-nested'),
    require ('tailwindcss') (tailwind_config),
    postcss_discard_duplicates,
    postcss_normalize_charset,
  ];
  return plugins;
}

// == Options ==
export const postcss_options = {
  syntax: postcss_scss,
  map: false,
};

// == Processing ==
async function postcss_process (css_string, fromname = '<style string>', options = {})
{
  const postcss = PostCss (postcss_plugins (options));
  const poptions = Object.assign ({ from: fromname }, postcss_options);
  let result;
  try {
    result = await postcss.process (css_string, poptions);
  } catch (ex) {
    console.warn ('PostCSS input:', fromname + ':\n', css_string);
    console.error (ex);
    if (options.rethrow)
      throw (ex);
    return '';
  }
  return result.content;
}

// == CSS Functions ==
const clamp = (v,l,u) => v < l ? l : v > u ? u : v;

/// Yield `value` as number in `[min..max]`, converts percentages.
function tofloat (value, fallback = NaN, min = -Number.MAX_VALUE, max = Number.MAX_VALUE)
{
  if (typeof value === 'string')
    {
      const isperc = value.indexOf ('%') >= 0;
      value = parseFloat (value);
      if (isperc)
	value = min + (max - min) * (0.01 * value);
    }
  value = clamp (value, min, max);
  return isNaN (value) ? fallback : value;
}

const FLOAT_REGEXP = /^([+-]?(?:(?:[1-9][0-9]*|0)(?:\.[0-9]*)?(?:[eE][+-]?[0-9]+)?|\.[0-9]+(?:[eE][+-]?[0-9]+)?))/;

// Provide CSS functions that are used after variable expansion
function css_functions()
{
  const color = css_color_converter;
  const functions = {
    info: function (...args) {
      console.info ("Postcss:info:", ...args);
      return ''; // args.join (',');
    },
    fade (col, perc) { // LESS fade, sets absolute alpha
      const a = tofloat (perc, 1, 0, 1);
      const [r,g,b] = color.fromString (col).toRgbaArray();
      const rgba = [ r, g, b, a ];
      return color.fromRgba (rgba).toHexString();
    },
    asfactor (val) {
      const f = tofloat (val);
      return '' + f;
    },
    div (dividend, divisor) {
      let [ _z, dz = 0, zrest = '' ] = dividend.trim().split (FLOAT_REGEXP);
      let [ _n, dn = 0, nrest = '' ] = divisor.trim().split (FLOAT_REGEXP);
      const val = dz / dn;
      zrest = zrest.trim();
      nrest = nrest.trim();
      const unit = !nrest ? zrest : zrest + '/' + nrest;
      return val + unit;
    },
    pow (number, exponent) {
      let [ _n, num = 0, _r = '' ] = number.trim().split (FLOAT_REGEXP);
      let [ _e, exp = 0, _s = '' ] = exponent.trim().split (FLOAT_REGEXP);
      return '' + Math.pow (num, exp);
    },
    mix (col1, col2, frac) {
      const rgba1 = color.fromString (col1).toRgbaArray();
      const rgba2 = color.fromString (col2).toRgbaArray();
      const f = frac === undefined ? 0.5 : clamp (parseFloat (frac), 0, 1);
      const rgba = rgba1.map ((v,i) => v * (1-f) + rgba2[i] * f);
      return color.fromRgb (rgba).toHexString();
    },
    zmod: Colors.zmod,
    zmod4: Colors.zmod4,
    zlerp: Colors.zlerp,
    zhsl: Colors.zhsl,
    zHsl: Colors.zHsl,
    zhsv: Colors.zhsv,
    zHsv: Colors.zHsv,
    lgrey: Colors.lgrey,
  };
  return { functions };
}

/// Load import files in nodejs to implement `@import`.
function load_import (id, cwd, opts)
{
  for (let path of [ cwd, './' ].concat (import_paths)) {
    const file = path + '/' + id;
    if (FS.existsSync (file)) {
      const input = FS.readFileSync (file);
      return { file, contents: input || '' };
    }
  }
  console.error (id + ': failed to load CSS import (wd: ' + cwd + ')');
  throw new Error ('Import failed');
}

/// Provide canned CSS files or fetch css for use with `@import`.
async function find_import (id, cwd, opts)
{
  const csstext = css_import_list[id];
  if (csstext === undefined)
    {
      const frequest = {
	headers: { "Content-Type": "text/css" },
	redirect: "follow",
      };
      const fresponse = await fetch (id, frequest);
      if (fresponse.ok) {
	const content_type = fresponse.headers.get ("content-type");
	if (content_type && (content_type.includes ("text/css") || content_type.includes ("text/x-scss"))) {
	  const blob = await fresponse.blob(), text = await blob.text();
	  return { file: id, contents: text || '' };
	}
	console.warn ('Import file not of CSS type:', id + ', Content-Type:', content_type);
	throw new Error ('Import failed');
      }
      console.warn ('Failed to load CSS import: ' + cwd + '/' + id);
      throw new Error ('Import failed');
    }
  return new Promise (r => r ({ file: id, contents: csstext || '' }));
}
const css_import_list = {};

/// Provide `filename` as `@import "filename";` source with contents `csstext`.
function add_import (filename, csstext)
{
  css_import_list[filename] = '' + csstext;
}

// == self-test (nodejs) ==
const test_rules = {
  '$padpx: 11px; vars { padding: $padpx; }':		'vars{ padding:11px; }',
  '$ivar: 22px; interpolation { margin: #{$ivar}; }':	'interpolation{ margin:22px; }',
  '@mixin foo() { f: "Mf"; } mfoo { @include foo(); }':	'mfoo{ f:"Mf"; }',
  '@media screen { color: var(--text-color) }':		'@media screen{ color:var(--text-color) }',
  'b { hwbcolor: hwb(90deg, 13%, 70%); }':		'b{ hwbcolor:rgb(55, 77, 33); }',
  'l { color: zmod(#abc, Jz+=14%); }':			'l{ color:#c7d9eb; }',
  'd { color: zmod(#abc, Jz-=45%); }':			'd{ color:#566472; }',
  'n { &:hover { nested: 1; } }':			'n:hover{ nested:1; }',
  'h { lchcolor: lch(62% 54 63); }':			'h{ lchcolor:rgb(205, 132, 63);', // match only first part to allow display-p3 extension
  't { --cd-5: div(5,2) div(5s,2) div(5km,2h); }':	'--cd-5:2.5 2.5s 2.5km/h;',
  'tn { font-variant-numeric:tabular-nums; }':		'tn{ font-variant-numeric:tabular-nums; }', // preprocessors must NOT reset font-feature-settings here
  // add_import ('imp.css', '$importval: "imported";');
  // '@import "imp.css"; i { --importval: $importval; }': 'i { --importval: "imported"; }',
};
async function test_css (verbose)
{
  test_rules[`--text-color: ${'#ABCDEF'};`] = '--text-color:#ABCDEF;';
  const input = Object.keys (test_rules).join ('\n');
  console.log ('  CHECK   ', __filename);
  const result = await postcss_process (input, 'test.css');
  if (verbose >= 2)
    console.log (__filename + ': result:', '\n' + result);
  let last = -1, errors = 0;
  for (const e of Object.entries (test_rules))
    {
      const matchpos = result.indexOf (e[1]);
      const cssmatch = matchpos > last;
      console.assert (cssmatch, "CSS test unmatched: " + e[1]);
      errors += !cssmatch;
      if (cssmatch && verbose >= 1)
	console.log ("√", e[1]);
      last = matchpos;
    }
  if (errors) {
    console.log (__filename + ': result:', '\n' + result);
    throw new Error (`${__filename}: self test errors: ` + errors);
  }
}

// == Exports ==
export const options = postcss_options;
export { postcss_process };
export { add_import };
export { test_css };

// == main ==
// Usage: postcss.js <inputfile.css> <outputfile.css>
// Usage: postcss.js --test [1|0]
if (__MAIN__) {
  async function main (argv) {
    let n = 0;
    const cwd = process.cwd();
    const opt = { import_all: false, import_root: cwd, vars: {}, rethrow: true };
    while (n < argv.length && argv[n].startsWith ('-')) {
      // run unit tests
      if (argv[n] === '--test')
	return test_css (argv[++n] | 0);
      // parse options
      else if (argv[n] == '--map')
	postcss_options.map = true;
      // defines
      else if (argv[n].startsWith ('-D')) {
	const str = argv[n].length == 2 && n+1 < argv.length ? argv[++n] : argv[n].substr (2);
	const eq = str.indexOf ('=');
	const k = eq <= 0 ? str : str.substr (0, eq);
	const v = eq <= 0 ? '1' : str.substr (eq + 1);
	if (k)
	  opt.vars[k] = v;
	if (!opt.import_root.startsWith ('/'))
	  opt.import_root = cwd + '/' + opt.import_root;
      }
      // include root dir
      else if (argv[n].startsWith ('-C')) {
	const dir = argv[n].length == 2 && n+1 < argv.length ? argv[++n] : argv[n].substr (2);
	opt.import_root = dir.startsWith ('/') ? dir : cwd + '/' + opt.import_root;
      }
      // include dirs
      else if (argv[n].startsWith ('-I')) {
	const dir = argv[n].length == 2 && n+1 < argv.length ? argv[++n] : argv[n].substr (2);
	import_paths.push (dir.startsWith ('/') ? dir : cwd + '/' + dir);
      }
      // include imports
      else if (argv[n] == '-i')
	opt.import_all = true;
      n++;
    }
    const filename = argv[n++];
    if (!filename)
      throw new Error (`${__filename}: missing input filename`);
    const ofilename = argv[n++];
    // process and printout
    const result = await postcss_process (FS.readFileSync (filename), filename, opt);
    if (ofilename)
      FS.writeFileSync (ofilename, result);
    else
      console.log (result);
  }
  process.exit (await main (process.argv.splice (2)));
}
