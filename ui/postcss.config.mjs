// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

import css_color_converter from 'css-color-converter';
import * as Colors from './colors.js';

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

// == PostCSS config ==
export default {
  map: { inline: false },
  syntax: 'postcss-scss',
  plugins: {
    'postcss-import': {
      filter: string => !string.endsWith ('.css'),
      path: [ '.' ], // , 'ui/', 'out/ui/' ],
    },
    'postcss-discard-comments': { remove: comment => true },
    'postcss-advanced-variables': { importFilter: string => false, },

    'postcss-color-hwb': {},
    'postcss-lab-function': {},

    'postcss-functions': css_functions(),

    'tailwindcss/nesting': {}, // : 'postcss-nesting',

    tailwindcss: {
      content: [
	'ui/*.html', 'ui/*.*js', 'ui/*.*css',
	'ui/b/*.*js', 'ui/b/*.vue',
      ],
      theme: {
	borderColor: ({ theme }) => ({
	  ...theme ('colors'),
	  DEFAULT: 'var(--tw-border-default-color)', // theme ('colors.gray.200', 'currentColor'),
	}),
	extend: {
	  colors: {
            dim: {
              50:  '#f9f9ff',
              100: '#f3f3fe',
              200: '#e3e3ef',
              300: '#c7c7d2',
              400: '#a0a0aa',
              500: '#76767f',
              600: '#515159',
              700: '#36363e',
              800: '#26262e',
              900: '#16161e',
              950: '#07060f',
            },
	  },
	},
      }
    },

    'postcss-discard-duplicates': {},
  }
};
