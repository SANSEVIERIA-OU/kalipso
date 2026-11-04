// SPDX-License-Identifier: AGPL-3.0-or-later
// Generate the shipped, localized Open Graph images from the website's actual titles and tokens.
// PLAYWRIGHT=/path/to/playwright/index.mjs node tools/render-social-cards.mjs
import {readFile} from 'node:fs/promises';
import {pathToFileURL, fileURLToPath} from 'node:url';
import path from 'node:path';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const {chromium}=await import(pathToFileURL(process.env.PLAYWRIGHT||path.join(root,'tools/browser/node_modules/playwright/index.mjs')).href);
const locales=JSON.parse(await readFile(path.join(root,'translations/locales.json'),'utf8'));
const browser=await chromium.launch();
const page=await browser.newPage({viewport:{width:1200,height:630},deviceScaleFactor:1,reducedMotion:'reduce'});
// A file origin keeps generation entirely offline, including the shipped writing-system fonts.
await page.goto(pathToFileURL(path.join(root,'site/index.html')).href);
const base=pathToFileURL(path.join(root,'site/')).href;
for(const locale of locales){
 const catalog=JSON.parse(await readFile(path.join(root,`translations/catalogs/${locale.code}.json`),'utf8'));
 await page.setContent(`<!doctype html><html lang="${locale.code}" dir="${locale.direction}"><head><base href="${base}"><meta charset="utf-8">
 <link rel="stylesheet" href="assets/tokens.css"><link rel="stylesheet" href="assets/fonts.css"><link rel="stylesheet" href="assets/noto-fonts.css">
 <style>
 *{box-sizing:border-box}html,body{margin:0;width:1200px;height:630px;overflow:hidden}
 body{background:var(--site-paper);color:var(--site-ink);font-family:var(--site-font-body);padding:56px 64px;display:flex;flex-direction:column;justify-content:space-between}
 html:lang(ja) body{font-family:var(--site-font-japanese)}
 header,footer{display:flex;align-items:center;justify-content:space-between;gap:32px}
 header img{width:64px;height:64px}header span,footer{font-size:26px;color:var(--site-muted)}
 main{border-inline-start:8px solid var(--site-accent);padding-inline-start:32px}
 h1{font-size:68px;line-height:1.2;letter-spacing:-.02em;margin:0;font-weight:700;overflow-wrap:anywhere;text-wrap:balance}
 footer span:last-child{unicode-bidi:isolate}
 </style></head><body><header><img src="assets/mark.svg" alt=""><span dir="ltr">sansevieria-ou.github.io/kalipso</span></header>
 <main><h1></h1></main><footer><span dir="ltr">Linux · Windows · macOS</span><span id="language"></span></footer></body></html>`);
 await page.locator('h1').evaluate((e,title)=>{e.textContent=title},catalog.s79b9b113ba17);
 await page.locator('#language').evaluate((e,title)=>{e.textContent=title},locale.name);
 await page.evaluate(()=>document.fonts.ready);
 const geometry=await page.locator('h1').evaluate(e=>{
  for(let size=68;e.getBoundingClientRect().height>350&&size>40;size-=2)e.style.fontSize=`${size}px`;
  const r=e.getBoundingClientRect();return {overflow:r.height>350||r.bottom>document.querySelector('footer').getBoundingClientRect().top-20,bottom:r.bottom};
 });
 if(geometry.overflow||geometry.bottom>510){await page.screenshot({path:path.join(root,'artifacts/seo-geo/social-failure.png')});throw new Error(`${locale.code}: social title does not fit ${JSON.stringify(geometry)}`);}
 await page.screenshot({path:path.join(root,`site/assets/media/social-${locale.code}.png`)});
 console.log(`${locale.code}: 1200×630`);
}
await browser.close();
