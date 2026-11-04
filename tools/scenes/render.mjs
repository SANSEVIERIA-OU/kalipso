// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// Renders each scene of scenes.html (with the captures in artifacts/scenes/) to a transparent WebP
// in site/assets/media/. Run with PLAYWRIGHT=/path/to/playwright/index.mjs; needs ffmpeg with libwebp,
// or Python with Pillow. Scene ids as arguments render only those (for example call-macos), when only
// some captures changed.
//   node render.mjs [scene id ...]
import {execFileSync} from 'node:child_process';
import {mkdirSync} from 'node:fs';
import {fileURLToPath, pathToFileURL} from 'node:url';
const {chromium} = await import(pathToFileURL(process.env.PLAYWRIGHT).href);
const here = fileURLToPath(new URL('.', import.meta.url)), root = fileURLToPath(new URL('../..', import.meta.url));
const work = root + 'artifacts/scenes/render/';
mkdirSync(work, {recursive: true});
const browser = await chromium.launch();
const page = await browser.newPage({viewport: {width: 1440, height: 900}, deviceScaleFactor: 1.5});
await page.goto(pathToFileURL(here + 'scenes.html').href);
const only = process.argv.slice(2);
const wanted = id => !only.length || only.includes(id);
await page.evaluate(ids => Promise.all([document.fonts.ready, ...[...document.images]
  .filter(image => !ids.length || ids.includes(image.closest('.scene')?.id)).map(image => image.decode())]), only);
const webp = (png, target) => {
  try {
    execFileSync('ffmpeg', ['-loglevel', 'error', '-y', '-i', png, '-c:v', 'libwebp', '-quality', '82', '-pix_fmt', 'yuva420p', target]);
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
    execFileSync('python3', ['-c', 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2], "WEBP", quality=82, method=6)', png, target]);
  }
};
for (const scene of await page.locator('.scene').all()) {
  const id = await scene.getAttribute('id');
  if (!wanted(id)) continue;
  await scene.screenshot({path: `${work}${id}.png`, omitBackground: true});
  webp(`${work}${id}.png`, `${root}site/assets/media/scene-${id}.webp`);
  console.log(`site/assets/media/scene-${id}.webp`);
}
await browser.close();
