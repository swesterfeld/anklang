// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
'use strict';

import * as Z from '../zcam-js.mjs';
const { clamp } = Z;

export const color_names = {
};

export const color_values = Array.from (new Set (Object.values (color_names)).values());

const sRGB_viewing_conditions = {
  Fs: Z.ZCAM_DIM,       // DIM comes closest to CIELAB L* in ZCAM and CIECAM97
  Yb: 9.1,              // Background luminance factor so Jz=50 yields #777777
  La: 80,               // La = Lw * Yb / 100; Safdar21, ZCAM, a colour appearance model
  Xw: Z.ZCAM_D65.x, Yw: Z.ZCAM_D65.y, Zw: Z.ZCAM_D65.z,
};
const default_gamut = new Z.Gamut (sRGB_viewing_conditions);
const atsamples = [0, 0.005, 0.01, 0.02, 0.038, 0.05, 0.062, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.938, 0.95, 0.962, 0.98, 0.99, 0.995, 1];

const ZHSV_EXP = 1.7; // exponent for ZHSV to approximate saturation distribution of HSV

/// Class with logic and spline approximations to calculate ZHSV.
export class HueSaturation {
  constructor (huelike, gamut = undefined) {
    let hz = huelike + 0;
    if (isNaN (hz))
      hz = isNaN (huelike.Hz) ? huelike.hz : Z.zcam_hue_angle (huelike.Hz);
    while (hz > 360) hz -= 360;
    while (hz < 0)   hz += 360;
    this.gamut = gamut || default_gamut;
    this.zcusp = this.gamut.find_cusp (hz); // s=1 v=1 l=cusp.Jz/100
    this.value = undefined;
    this.zv0 = undefined;	// zcam @ v=0…1 s=0
    this.zv1 = undefined;	// zcam @ v=0…1 s=1
    this.jz2cz = undefined;	// maximize_Cz (Jz) under hue
    this.z1s = undefined;	// zcam @ v=1   s=0…1
    this.cz2jz = undefined;	// maximize_Jz (Cz) under hue
  }
  make_splines() {
    const zcusp = this.zcusp, viewing = zcusp.viewing, gamut = this.gamut;
    const cxs = [], cys = [];
    // approximate Jz = maximize_Jz (Cz) under hue
    for (const s of atsamples) {
      const Cz = zcusp.Cz * s;
      const z1s = { hz: zcusp.hz, Cz, Jz: zcusp.Jz, viewing };
      z1s.Jz = gamut.maximize_Jz (z1s);
      cxs.push (Cz);
      cys.push (z1s.Jz);
    }
    this.cz2jz = new Z.CubicSpline (cxs, cys);
    // approximate Cz = maximize_Cz (Jz) under hue
    cxs.length = 0;
    cys.length = 0;
    for (const v of atsamples) {
      const Jz = zcusp.Jz * v;
      const zv1 = { hz: zcusp.hz, Jz, Cz: 0, viewing };
      zv1.Cz = gamut.maximize_Cz (zv1);
      cxs.push (Jz);
      cys.push (zv1.Cz);
    }
    this.jz2cz = new Z.CubicSpline (cxs, cys);
  }
  set_value (value) {
    const zcusp = this.zcusp, viewing = zcusp.viewing;
    this.value = clamp (value, 0, 1);
    this.zv0 = { hz: zcusp.hz, Jz: this.value * 100, Cz: 0, viewing }; // zcam @ s=0 v=0…1
    this.zv1 = undefined;
    this.z1s = undefined;
  }
  ensure_zv1() {
    if (this.zv1) return;
    const zcusp = this.zcusp, viewing = zcusp.viewing, gamut = this.gamut;
    this.zv1 = { hz: zcusp.hz, Jz: zcusp.Jz * this.value, Cz: 0, viewing };
    if (!this.jz2cz)
      this.zv1.Cz = gamut.maximize_Cz (this.zv1); // zcam @ s=1 v=0…1
    else
      this.zv1.Cz = this.jz2cz.splint (this.zv1.Jz);
  }
  ensure_z1s() {
    if (this.z1s) return;
    const zcusp = this.zcusp, viewing = zcusp.viewing, gamut = this.gamut;
    const s = this.saturation ** (1/ZHSV_EXP); // adjustment to match sRGB-HSV saturation
    this.z1s = { hz: zcusp.hz, Cz: zcusp.Cz * s, Jz: zcusp.Jz, viewing };
    if (!this.cz2jz)
      this.z1s.Jz = gamut.maximize_Jz (this.z1s); // zcam @ s=0…1 v=1
    else
      this.z1s.Jz = this.cz2jz.splint (this.z1s.Cz);
    return s;
  }
  zcam_from_saturation (saturation) {
    const zcusp = this.zcusp, viewing = zcusp.viewing;
    this.ensure_zv1(); // needs this.value
    this.saturation = clamp (saturation, 0, 1);
    this.z1s = undefined; // invalidated when this.saturation changes
    this.ensure_z1s(); // needs this.saturation
    // assume v=1, construct from hue and saturation
    const zcam = { hz: this.z1s.hz, Jz: this.z1s.Jz, Cz: this.z1s.Cz, viewing };
    // translating cusp along value yields zv1
    const rJz = this.zv1.Jz / zcusp.Jz;
    const rCz = this.zv1.Cz / zcusp.Cz;
    // apply zv1/cusp ratios
    zcam.Jz *= rJz;
    zcam.Cz *= rCz;
    return zcam;
  }
  saturation_from_Cz (Cz) {
    this.ensure_zv1(); // needs this.value
    const s = Cz / this.zv1.Cz;
    const saturation = clamp (s ** ZHSV_EXP, 0, 1);
    return saturation;
  }
  srgb_from_saturation (saturation) {
    const zcam = this.zcam_from_saturation (saturation);
    const {r,g,b} = Z.srgb_from_zcam_8bit (zcam, this.gamut.viewing);
    return {r,g,b};
  }
}

/// Calculate sRGB from `{ hue, saturation, value }`.
export function srgb_from_zhsv (hue, saturation, value, gamut = undefined) {
  const hso = new HueSaturation (hue, gamut);
  hso.set_value (value);
  return hso.srgb_from_saturation (saturation);
}

/// Calculate hexadecimal color from `{ hue, saturation, value }`.
export function hex_from_zhsv (hue, saturation, value, gamut = undefined) {
  return Z.srgb_hex (srgb_from_zhsv (hue, saturation, value, gamut));
}

/// Calculate `{ hue, saturation, value }` from `srgb`.
export function zhsv_from (srgb, gamut = undefined) {
  gamut = gamut || default_gamut;
  const {r,g,b} = Z.srgb_from (srgb);
  const pz = Z.zcam_from_srgb ({r,g,b}, gamut.viewing);
  const hue = pz.hz;
  const hso = new HueSaturation (hue, gamut);
  let va = 0, vb = 1;
  while (Math.abs (va - vb) > 0.001) {
    const vc = (va + vb) * 0.5;
    hso.set_value (vc);
    const s = hso.saturation_from_Cz (pz.Cz);
    const zc = hso.zcam_from_saturation (s);
    if (zc.Jz > pz.Jz)
      vb = vc;
    else
      va = vc;
  }
  const value = (va + vb) * 0.5;
  hso.set_value (value);
  const saturation = hso.saturation_from_Cz (pz.Cz);
  return { hue, saturation, value };
}
