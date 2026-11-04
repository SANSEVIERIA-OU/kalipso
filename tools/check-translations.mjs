// SPDX-License-Identifier: AGPL-3.0-or-later
// PLAYWRIGHT=/path/to/playwright/index.mjs, AXE_CORE=/path/to/axe.min.js; serve site on :8770.
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
const pw=await import(pathToFileURL(process.env.PLAYWRIGHT).href);
const locales=JSON.parse(await readFile(new URL('../translations/locales.json',import.meta.url),'utf8'));
const selected=process.env.LANGUAGES?.split(',');
const base=process.env.DESIGN_URL||'http://localhost:8770';
const out='artifacts/translations-23/browser';await mkdir(out,{recursive:true});
const axe=process.env.AXE_CORE ? await readFile(process.env.AXE_CORE,'utf8') : null;
const results=[];
for(const engine of ['chromium','firefox','webkit']){
 const browser=await pw[engine].launch();
 for(const locale of locales.filter(l=>!selected||selected.includes(l.code))){
  const page=await browser.newPage({reducedMotion:'reduce'});const errors=[];
  page.on('pageerror',e=>errors.push(String(e)));
  page.on('response',r=>{if(r.status()>=400)errors.push(`${r.status()} ${r.url()}`)});
  const route=locale.code==='en'?'':locale.code+'/';
  await page.goto(base+'/'+route+'?mirror=vertical#try');
  await page.locator('#next-word').click();
  assert.notEqual(await page.locator('#reading-text').getAttribute('data-position'),'-1');
  const position=Number(await page.locator('#reading-text').getAttribute('data-position'));
  const localized=await page.evaluate(n=>new Intl.NumberFormat(document.documentElement.dataset.numberLocale).format(n),position+1);
  assert.equal((await page.locator('#read-count').innerText()).split(' / ')[0],localized);
  assert.equal(await page.locator('#reader-mirror').inputValue(),'vertical');
  assert.equal(await page.locator('html').getAttribute('dir'),locale.direction);
  assert.equal(await page.locator('#site-language option').count(),39);
  assert.equal(await page.locator('#language-link').isVisible(),false);
  for(const [width,scale] of [[1280,100],[390,100],[320,200]]){
   await page.setViewportSize({width,height:900});
   await page.addStyleTag({content:`html{font-size:${scale}%!important}`});
   await page.evaluate(()=>document.fonts.ready);
   await page.evaluate(()=>scrollTo(0,0));
   const pickerVisible=await page.locator('#site-language').evaluate(e=>{
    const r=e.getBoundingClientRect();
    return r.width>=44 && r.height>=44 && r.left>=0 && r.right<=innerWidth &&
      document.elementFromPoint(r.left+r.width/2,r.top+r.height/2)===e;
   });
   assert.equal(pickerVisible,true,`${engine}/${locale.code}/${width}/${scale} language picker hidden or clipped`);
   const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1);
   if(overflow) await page.screenshot({path:`${out}/${engine}-${locale.code}-${width}-overflow.png`,fullPage:true});
   assert.equal(overflow,false,`${engine}/${locale.code}/${width}/${scale} overflow`);
  }
  await page.setViewportSize({width:1280,height:900});
  await page.addStyleTag({content:'html{font-size:100%!important}'});
  if(axe){
   await page.evaluate(axe);
   const result=await page.evaluate(()=>axe.run(document,{runOnly:{type:'tag',values:['wcag2a','wcag2aa','wcag21a','wcag21aa','wcag22aa']}}));
   assert.deepEqual(result.violations.map(v=>({id:v.id,nodes:v.nodes.map(n=>n.target)})),[],`${engine}/${locale.code} accessibility`);
  }
  if(['ar','ur','zh-Hans','ja','de','hi','bn','te'].includes(locale.code)) {
   await page.locator('#try').scrollIntoViewIfNeeded();
   await page.screenshot({path:`${out}/${engine}-${locale.code}.png`});
   await page.evaluate(()=>scrollTo(0,0));
   await page.screenshot({path:`${out}/${engine}-${locale.code}-hero.png`});
  }
  const target=locale.code==='en'?'it/':'../';
  await page.locator('#site-language').selectOption(target);
  await page.waitForURL(url=>url.hash==='#try'&&url.search==='?mirror=vertical'&&url.pathname===(locale.code==='en'?'/it/':'/'));
  assert.deepEqual(errors,[],`${engine}/${locale.code} resource or script errors`);
  // Static translation and real language links must still work without JavaScript.
  const plain=await browser.newPage({javaScriptEnabled:false});await plain.goto(base+'/'+route);
  assert.equal(await plain.locator('html').getAttribute('lang'),locale.code);
  assert.equal(await plain.locator('a[data-language]').count(),39);
  assert.equal(await plain.locator('#language-link').isVisible(),true);
  assert.equal(await plain.locator('#site-language').isVisible(),false);
  assert.ok((await plain.locator('h1').innerText()).trim());await plain.close();await page.close();
  results.push({engine,language:locale.code,viewports:3,static:true});
  console.log(`${engine}/${locale.code}: passed`);
 }
 await browser.close();
}
await writeFile(`${out}/${selected ? "selected-languages-results" : "results"}.json`,JSON.stringify(results,null,2)+'\n');
