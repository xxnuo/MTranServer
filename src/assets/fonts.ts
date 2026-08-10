import latin from './fonts/noto-sans-sc-latin-400-normal.woff2' with { type: 'file' };
import latinExt from './fonts/noto-sans-sc-latin-ext-400-normal.woff2' with { type: 'file' };
import cjk112 from './fonts/noto-sans-sc-112-400-normal.woff2' with { type: 'file' };
import cjk113 from './fonts/noto-sans-sc-113-400-normal.woff2' with { type: 'file' };
import cjk114 from './fonts/noto-sans-sc-114-400-normal.woff2' with { type: 'file' };
import cjk115 from './fonts/noto-sans-sc-115-400-normal.woff2' with { type: 'file' };
import cjk116 from './fonts/noto-sans-sc-116-400-normal.woff2' with { type: 'file' };
import cjk117 from './fonts/noto-sans-sc-117-400-normal.woff2' with { type: 'file' };
import cjk118 from './fonts/noto-sans-sc-118-400-normal.woff2' with { type: 'file' };
import cjk119 from './fonts/noto-sans-sc-119-400-normal.woff2' with { type: 'file' };

export interface FontAsset {
  path: string;
  unicodeRange: string;
}

export const OCR_FONT_FAMILY = 'MTranOCRSans';

export const OCR_FONT_ASSETS: FontAsset[] = [
  {
    path: latin,
    unicodeRange: 'U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+0304,U+0308,U+0329,U+2000-206F,U+20AC,U+2122,U+2191,U+2193,U+2212,U+2215,U+FEFF,U+FFFD',
  },
  {
    path: latinExt,
    unicodeRange: 'U+0100-02BA,U+02BD-02C5,U+02C7-02CC,U+02CE-02D7,U+02DD-02FF,U+0304,U+0308,U+0329,U+1D00-1DBF,U+1E00-1E9F,U+1EF2-1EFF,U+2020,U+20A0-20AB,U+20AD-20C0,U+2113,U+2C60-2C7F,U+A720-A7FF',
  },
  { path: cjk112, unicodeRange: 'U+23,U+3D,U+4E00-9FFF,U+FF00-FFEF' },
  { path: cjk113, unicodeRange: 'U+23,U+3D,U+4E00-9FFF,U+FF00-FFEF' },
  { path: cjk114, unicodeRange: 'U+23,U+3D,U+4E00-9FFF,U+FF00-FFEF' },
  { path: cjk115, unicodeRange: 'U+23,U+3D,U+4E00-9FFF,U+FF00-FFEF' },
  { path: cjk116, unicodeRange: 'U+23,U+3D,U+4E00-9FFF,U+FF00-FFEF' },
  { path: cjk117, unicodeRange: 'U+23,U+3D,U+4E00-9FFF,U+FF00-FFEF' },
  { path: cjk118, unicodeRange: 'U+23,U+3D,U+4E00-9FFF,U+FF00-FFEF' },
  { path: cjk119, unicodeRange: 'U+23,U+3D,U+4E00-9FFF,U+FF00-FFEF,U+3000-30FF' },
];
