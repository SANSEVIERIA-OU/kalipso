#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
"""Complete catalogs, GNU MO decoding and integer plurals against independent ICU rules."""
import gettext
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'desktop'))
import package
spec=importlib.util.spec_from_file_location('generator',ROOT/'tools/generate-translations.py')
gen=importlib.util.module_from_spec(spec); spec.loader.exec_module(gen)
if len(sys.argv) == 3 and sys.argv[1] == '--lupdate':
    with tempfile.TemporaryDirectory() as work:
        target=Path(work)/'source.ts'
        subprocess.run([sys.argv[2], '-no-obsolete', '-locations', 'none', str(ROOT/'src'), '-ts', str(target)],
                       check=True, capture_output=True)
        extracted={(c.findtext('name'),m.findtext('source'),m.get('numerus')=='yes')
                   for c in ET.parse(target).findall('context') for m in c.findall('message')}
        mapped={(u['context'],u['source'],u.get('numerus',False))
                for u in gen.STRUCTURE['app']+gen.STRUCTURE['engine']}
        assert extracted==mapped,('Unmapped or stale Qt source strings',extracted-mapped,mapped-extracted)

# Negative controls: a missing entry, a broken placeholder and a missing plural form must fail.
source_catalog=json.loads((ROOT/'translations/catalogs/en.json').read_text(encoding='utf-8'))
broken=dict(source_catalog); broken.pop(next(iter(broken)))
cases=[broken]
key=next(k for k,v in source_catalog.items() if isinstance(v,str) and '%1' in v)
broken=dict(source_catalog); broken[key]=broken[key].replace('%1','%9'); cases.append(broken)
key=next(k for k,v in source_catalog.items() if isinstance(v,dict))
broken=dict(source_catalog); broken[key]={'one':source_catalog[key]['one']}; cases.append(broken)
for broken in cases:
    try: gen.validate('en',broken)
    except ValueError: pass
    else: raise AssertionError('Incomplete or corrupted catalog passed validation')
numbers=list(range(201))+[1000,10000,100000,1000000,2000000]
# Intl uses ICU/CLDR independently of the manifest's gettext expressions.
script='''const input=JSON.parse(process.argv[1]);
console.log(JSON.stringify(Object.fromEntries(input.locales.map(l=>{
const code=l==='arz'?'ar':l;
if(l!=='pcm' && !Intl.PluralRules.supportedLocalesOf([code]).length)
  throw new Error(`Full ICU required: ${code} unavailable in Node ${process.version} / ICU ${process.versions.icu}`);
const r=new Intl.PluralRules(code);
return [l,input.numbers.map(n=>r.select(n))];}))));'''
expected=json.loads(subprocess.check_output(['node','-e',script,json.dumps({'locales':[x['code'] for x in gen.LOCALES],'numbers':numbers})]))
for locale in gen.LOCALES:
    code=locale['code']; catalog=json.loads((ROOT/'translations/catalogs'/f'{code}.json').read_text(encoding='utf-8'))
    gen.validate(code,catalog)
    mo=gettext.GNUTranslations(io.BytesIO(package.compile_po(ROOT/'desktop/po'/f'{locale["gettext"]}.po')))
    for n,category in zip(numbers,expected[code]):
        # Node ICU does not know Nigerian Pidgin in every supported version: its CLDR rule is 0..1.
        if code=='pcm': category='one' if n in (0,1) else 'other'
        assert locale['pluralCategories'][mo.plural(n)]==category,(code,n,category)
        for unit in gen.STRUCTURE['desktop']:
            actual=mo.ngettext(unit['source'],unit['plural'],n) if unit.get('plural') else mo.gettext(unit['source'])
            wanted=catalog[unit['key']]
            if isinstance(wanted,dict): wanted=wanted[category]
            assert actual==wanted,(code,n,unit['source'],actual,wanted)
    html=(ROOT/'site'/('' if code=='en' else code)/'index.html').read_text(encoding='utf-8')
    assert f'lang="{code}" dir="{locale["direction"]}"' in html
    assert 'data-number-locale="'+locale['qt'].replace('_','-')+'"' in html
    assert html.count('hreflang=')==len(gen.LOCALES)+1+sum(bool(l['web']['hreflang']) for l in gen.LOCALES),(code,html.count('hreflang='))
    assert '{{s' not in html and '{version}' not in html
print(f'{len(gen.LOCALES)} complete catalogs; gettext decoding and ICU plural checks passed.')
