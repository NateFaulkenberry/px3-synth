#!/usr/bin/env node
//
// Generates the P(X3) application icon from products/PX3Synth/Assets/px3.gif.
//
// The logo is a 564x187 wordmark - roughly 3:1 - which wastes most of a square
// canvas if it is placed horizontally. Rotating it 45 degrees lays it along the
// diagonal, which is the longest line available in a square, and lets it sit
// about 6% larger while filling the frame far better.
//
// The angle is negative, which sharp takes as anticlockwise: the wordmark runs
// from the lower left up to the upper right.
//
// The logo is never cropped. It is scaled to fit and the remainder of the
// canvas is filled with the logo's own background colour, sampled from the
// artwork rather than assumed, so the two meet without a seam.
//
// Usage:
//   node scripts/make-app-icon.mjs [--source <gif>] [--out-dir <dir>] [--angle 45]
//
// Outputs into <out-dir>:
//   px3-icon.png        1024x1024 master, used by CMake/JUCE as ICON_BIG
//   px3.icns            macOS icon, built from a full iconset via iconutil
//   px3-icon.iconset/   the intermediate sizes (kept; iconutil needs them)

import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdir, rm, writeFile } from 'node:fs/promises';
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

// sharp is resolved by hand rather than imported. An ESM `import` only looks in
// node_modules folders above this file, which would mean either committing
// node_modules into a C++ repository or installing sharp globally. Neither is
// wanted, so the release script installs it somewhere out of the way and points
// here with PX3_SHARP_PATH.
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
const SOURCE = path.resolve(argValue('--source', path.join(repoRoot, 'products/PX3Synth/Assets/px3.gif')));
const OUT_DIR = path.resolve(argValue('--out-dir', path.join(repoRoot, 'products/PX3Synth/Assets')));
const ANGLE = Number(argValue('--angle', '-45'));
const CANVAS = 1024;

// How much of the canvas the artwork may occupy. The rest is breathing room so
// the wordmark does not run into the edges of the tile.
const FILL = 0.94;

// Which frame of the animation to freeze. The GIF is an 80-frame glitch loop;
// they are all similar, and this one has the least white running off the edge.
const FRAME = 50;

async function backgroundColour(src, frame) {
    // Sampled from the artwork's border rather than hard-coded, so the canvas
    // fill matches whatever the logo actually sits on. A wrong guess here shows
    // up as a visible rectangle where the rotated logo meets the canvas.
    const { data, info } = await sharp(src, { page: frame })
        .removeAlpha()
        .raw()
        .toBuffer({ resolveWithObject: true });

    const { width: w, height: h, channels: ch } = info;
    const counts = new Map();
    const sample = (x, y) => {
        const i = (y * w + x) * ch;
        const key = `${data[i]},${data[i + 1]},${data[i + 2]}`;
        counts.set(key, (counts.get(key) ?? 0) + 1);
    };
    for (let x = 0; x < w; x += 2) { sample(x, 0); sample(x, h - 1); }
    for (let y = 0; y < h; y += 2) { sample(0, y); sample(w - 1, y); }

    let best = null;
    let bestCount = -1;
    for (const [key, count] of counts) {
        if (count > bestCount) { bestCount = count; best = key; }
    }
    const [r, g, b] = best.split(',').map(Number);
    return { r, g, b, hex: `#${[r, g, b].map(v => v.toString(16).padStart(2, '0')).join('')}` };
}

const meta = await sharp(SOURCE).metadata();
const aspect = meta.width / meta.height;
const bg = await backgroundColour(SOURCE, FRAME);

// Bounding box of a W x H rectangle rotated 45 degrees is (W + H) / sqrt(2)
// square, so the widest the logo can be while its rotated box still fits is
// solved from that rather than guessed at.
const usable = CANVAS * FILL;
const radians = (ANGLE * Math.PI) / 180;
const cos = Math.abs(Math.cos(radians));
const sin = Math.abs(Math.sin(radians));
const logoWidth = Math.floor(usable / (cos + sin / aspect));

console.log(`source      ${meta.width}x${meta.height} (${meta.pages ?? 1} frames), aspect ${aspect.toFixed(3)}:1`);
console.log(`frame       ${FRAME}`);
console.log(`background  ${bg.hex}  (sampled from the artwork border)`);
console.log(`logo width  ${logoWidth}px, rotated ${ANGLE} deg (${ANGLE < 0 ? 'anticlockwise' : 'clockwise'})`);

const rotated = await sharp(SOURCE, { page: FRAME })
    .resize({ width: logoWidth, fit: 'inside', withoutEnlargement: false })
    .rotate(ANGLE, { background: bg.hex })
    .png()
    .toBuffer();

const rotatedMeta = await sharp(rotated).metadata();
console.log(`rotated box ${rotatedMeta.width}x${rotatedMeta.height} inside ${CANVAS}x${CANVAS}`);

if (rotatedMeta.width > CANVAS || rotatedMeta.height > CANVAS) {
    console.error('ERROR: rotated artwork does not fit the canvas; it would be cropped.');
    process.exit(1);
}

await mkdir(OUT_DIR, { recursive: true });
const masterPath = path.join(OUT_DIR, 'px3-icon.png');

await sharp({ create: { width: CANVAS, height: CANVAS, channels: 4, background: bg.hex } })
    .composite([{ input: rotated, gravity: 'centre' }])
    .png()
    .toFile(masterPath);
console.log(`wrote       ${masterPath}`);

// macOS iconset. iconutil requires exactly these names, and a missing size
// makes it refuse the whole set.
const iconsetDir = path.join(OUT_DIR, 'px3-icon.iconset');
await rm(iconsetDir, { recursive: true, force: true });
await mkdir(iconsetDir, { recursive: true });

const sizes = [16, 32, 128, 256, 512];
for (const size of sizes) {
    for (const scale of [1, 2]) {
        const px = size * scale;
        const name = scale === 1 ? `icon_${size}x${size}.png` : `icon_${size}x${size}@2x.png`;
        await sharp(masterPath).resize(px, px, { kernel: 'lanczos3' }).png().toFile(path.join(iconsetDir, name));
    }
}

const icnsPath = path.join(OUT_DIR, 'px3.icns');
try {
    await run('iconutil', ['-c', 'icns', iconsetDir, '-o', icnsPath]);
    console.log(`wrote       ${icnsPath}`);
} catch (error) {
    console.error(`WARNING: iconutil failed, .icns not produced: ${error.message}`);
}

await writeFile(path.join(OUT_DIR, 'px3-icon.txt'),
    `Generated by scripts/make-app-icon.mjs from ${path.basename(SOURCE)}\n`
    + `frame ${FRAME}, rotated ${ANGLE} degrees, background ${bg.hex}\n`
    + `Do not edit by hand - re-run the script instead.\n`);

console.log('done');
