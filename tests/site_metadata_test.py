#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
"""Validate every shipped social preview, software record and sitemap URL without a network."""
from html.parser import HTMLParser
import json
from pathlib import Path
import struct
from urllib.parse import urlparse
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
BASE = 'https://sansevieria-ou.github.io/kalipso/'
LOCALES = json.loads((ROOT / 'translations/locales.json').read_text(encoding='utf-8'))


class Head(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.meta = {}
        self.structured = []
        self.script = None
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'meta':
            key = attrs.get('property', attrs.get('name'))
            self.meta.setdefault(key, []).append(attrs.get('content', ''))
        if tag == 'script' and attrs.get('type') == 'application/ld+json':
            self.script = ''

    def handle_data(self, data):
        if self.script is not None:
            self.script += data

    def handle_endtag(self, tag):
        if tag == 'script' and self.script is not None:
            self.structured.append(json.loads(self.script))
            self.script = None


def validate(text, locale):
    head = Head(text)
    code = locale['code']
    url = BASE + ('' if code == 'en' else code + '/')
    catalog = json.loads((ROOT / 'translations/catalogs' / (code + '.json')).read_text(encoding='utf-8'))
    expected = {
        'og:title': catalog['s79b9b113ba17'], 'og:description': catalog['s8a3f34f21be6'],
        'og:type': 'website', 'og:site_name': 'Kalipso', 'og:url': url,
        'og:locale': locale['web']['openGraphLocale'], 'og:image:width': '1200',
        'og:image:height': '630', 'og:image:type': 'image/png',
        'og:image:alt': catalog['s79b9b113ba17'], 'twitter:card': 'summary_large_image',
    }
    for key, value in expected.items():
        assert head.meta.get(key) == [value], (code, key, head.meta.get(key))
    image = BASE + f'assets/media/social-{code}.png'
    assert head.meta.get('og:image') == head.meta.get('og:image:secure_url') == head.meta.get('twitter:image') == [image]
    for field in ['title', 'description', 'image:alt']:
        assert head.meta.get('twitter:' + field) == head.meta.get('og:' + field), (code, field)
    assert set(head.meta['og:locale:alternate']) == {x['web']['openGraphLocale'] for x in LOCALES if x['code'] != code}
    png = (ROOT / 'site' / urlparse(image).path.removeprefix('/kalipso/')).read_bytes()
    assert png[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II', png[16:24]) == (1200, 630), code
    assert len(head.structured) == 1
    app = head.structured[0]
    assert app['@type'] == 'SoftwareApplication' and app['name'] == 'Kalipso'
    assert app['url'] == url and app['inLanguage'] == code
    assert app['description'] == catalog['s8a3f34f21be6'] and app['image'] == image
    assert app['offers']['price'] == '0' and 'aggregateRating' not in app and 'review' not in app


for locale in LOCALES:
    page = ROOT / 'site' / ('' if locale['code'] == 'en' else locale['code']) / 'index.html'
    validate(page.read_text(encoding='utf-8'), locale)
# Negative control: a preview with no primary image must be rejected.
english = next(x for x in LOCALES if x['code'] == 'en')
broken = (ROOT / 'site/index.html').read_text(encoding='utf-8').replace('property="og:image"', 'property="missing-image"')
try:
    validate(broken, english)
except AssertionError:
    pass
else:
    raise AssertionError('A preview with no og:image passed validation')
ns = {'s': 'http://www.sitemaps.org/schemas/sitemap/0.9', 'h': 'http://www.w3.org/1999/xhtml'}
sitemap = ET.parse(ROOT / 'site/sitemap.xml')
urls = sitemap.findall('s:url', ns)
assert {u.findtext('s:loc', namespaces=ns) for u in urls} == {BASE + ('' if l['code'] == 'en' else l['code'] + '/') for l in LOCALES}
for entry in urls:
    links = entry.findall('h:link', ns)
    assert {l.get('hreflang') for l in links} == {'x-default'} | {l['web']['hreflang'] for l in LOCALES if l['web']['hreflang']}
    for link in links:
        target = ROOT / 'site' / urlparse(link.get('href')).path.removeprefix('/kalipso/') / 'index.html'
        assert target.is_file(), target
print(f'{len(LOCALES)} localized Open Graph/X previews, PNGs, software records and sitemap entries passed; missing-image negative control rejected.')
