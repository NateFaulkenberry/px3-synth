#!/usr/bin/env node
//
// Generates the PX3 Uninstaller icon from the application icon.
//
// The uninstaller is a destructive one-shot tool that sits next to the app it
// removes, so its icon should be recognisably the SAME artwork with an
// unmistakable "remove" mark over it - not a different picture. Anything else
// and the two are hard to tell apart in a folder, which is the one place it
// matters.
//
// The mark is a red X drawn across the whole icon: two strokes with round caps,
// each carrying a dark outline underneath so it stays legible whatever the
// artwork behind it happens to be.
//
// Usage:
//   node scripts/make-uninstaller-icon.mjs [--source <png>] [--out-dir <dir>]
//
// Outputs into <out-dir>:
//   px3-uninstall-icon.png   1024x1024 master
//   px3-uninstall.icns       macOS icon, built from a full iconset via iconutil
//   px3-uninstall.iconset/   the intermediate sizes (kept; iconutil needs them)

import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdir, rm } from 'node:fs/promises';
import { createRequire } from 'node:module';
import path from 'node:path';

const run = promisify(execFile);
const require = createRequire(import.meta.url);

const args = process.argv.slice(2);
const argValue = (name, fallback) => {
    const i = args.indexOf(name);
    return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
};

const repoRoot = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const SOURCE = path.resolve(argValue('--source', path.join(repoRoot, 'Source/Assets/px3-icon.png')));
const OUT_DIR = path.resolve(argValue('--out-dir', path.join(repoRoot, 'Source/Assets')));

const SIZE = 1024;

// Proportions of the canvas, so the mark scales with the icon rather than being
// tuned for one size.
const INSET = 0.19;          // how far in from each edge the X starts
// Deliberately slim. The first attempt used 0.115 and the strokes covered the
// wordmark completely - which defeats the point, since the icon has to stay
// recognisably the same artwork for the two to be told apart in a folder.
const STROKE = 0.055;        // stroke width
const OUTLINE_EXTRA = 0.016; // the dark edging under each stroke

// The same search the app icon uses: sharp is a large native dependency and is
// deliberately NOT vendored into a C++ repository, so the release script
// installs it out of the way and points here with PX3_SHARP_PATH.
function loadSharp() {
    const candidates = [
        process.env.PX3_SHARP_PATH,
        path.join(repoRoot, '.tools/node_modules'),
        path.join(repoRoot, 'node_modules'),
    ].filter(Boolean);

    for (const base of candidates) {
        try {
            return require(path.join(base, 'sharp'));
        } catch {
            // try the next location
        }
    }
    try {
        return require('sharp');
    } catch {
        console.error('ERROR: sharp is not installed.');
        console.error('Install it with:  npm install --prefix .tools sharp');
        console.error('or point PX3_SHARP_PATH at a node_modules directory containing it.');
        process.exit(2);
    }
}

const sharp = loadSharp();

const a = Math.round(SIZE * INSET);
const b = SIZE - a;
const stroke = Math.round(SIZE * STROKE);
const outline = stroke + Math.round(SIZE * OUTLINE_EXTRA);

// The X as SVG so the strokes are anti-aliased and round-capped without hand
// rasterising anything. Drawn twice: a dark edging first, the red over it.
const cross = `
<svg width="${SIZE}" height="${SIZE}" viewBox="0 0 ${SIZE} ${SIZE}" xmlns="http://www.w3.org/2000/svg">
  <g stroke-linecap="round" fill="none">
    <g stroke="#1A0B0B" stroke-opacity="0.45" stroke-width="${outline}">
      <line x1="${a}" y1="${a}" x2="${b}" y2="${b}"/>
      <line x1="${b}" y1="${a}" x2="${a}" y2="${b}"/>
    </g>
    <g stroke="#D8322B" stroke-opacity="0.94" stroke-width="${stroke}">
      <line x1="${a}" y1="${a}" x2="${b}" y2="${b}"/>
      <line x1="${b}" y1="${a}" x2="${a}" y2="${b}"/>
    </g>
  </g>
</svg>`;

const masterPath = path.join(OUT_DIR, 'px3-uninstall-icon.png');

await mkdir(OUT_DIR, { recursive: true });

await sharp(SOURCE)
    .resize(SIZE, SIZE, { fit: 'cover' })
    .composite([{ input: Buffer.from(cross), top: 0, left: 0 }])
    .png()
    .toFile(masterPath);

// macOS iconset. iconutil requires exactly these names, and a missing size
// makes it refuse the whole set rather than substituting.
const iconsetDir = path.join(OUT_DIR, 'px3-uninstall.iconset');
await rm(iconsetDir, { recursive: true, force: true });
await mkdir(iconsetDir, { recursive: true });

const sizes = [
    [16, 'icon_16x16.png'], [32, 'icon_16x16@2x.png'],
    [32, 'icon_32x32.png'], [64, 'icon_32x32@2x.png'],
    [128, 'icon_128x128.png'], [256, 'icon_128x128@2x.png'],
    [256, 'icon_256x256.png'], [512, 'icon_256x256@2x.png'],
    [512, 'icon_512x512.png'], [1024, 'icon_512x512@2x.png'],
];

for (const [px, name] of sizes) {
    await sharp(masterPath).resize(px, px).png().toFile(path.join(iconsetDir, name));
}

const icnsPath = path.join(OUT_DIR, 'px3-uninstall.icns');
try {
    await run('iconutil', ['-c', 'icns', iconsetDir, '-o', icnsPath]);
    console.log(`Uninstaller icon written to ${path.relative(repoRoot, icnsPath)}`);
} catch (error) {
    console.error(`WARNING: iconutil failed, uninstaller .icns not produced: ${error.message}`);
}
