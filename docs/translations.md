# Translating Kalipso

Kalipso has 39 interface languages across the Qt app, Linux companion, desktop integrations and
static website. The interface language is independent of speech recognition: Voice settings show
which recognition models are available. Manual and steady reading accept scripts in any language.

## Editing and rebuilding

`translations/catalogs/<BCP-47 code>.json` is the editable catalog. Each stable key points to a source
unit in `translations/source.json`, whose locations provide context. `structure.json` maps units to
Qt contexts, gettext entries, website controls and package metadata. Shared English wording has one
translation; surrounding website fragments must also be checked as complete sentences.

Run `python3 tools/generate-translations.py` after editing a catalog. It runs offline using Python's
standard library and generates the `.ts`, `.po`, package labels, native language registry, plural
resources, static HTML and browser strings. `--check` fails on missing translations, empty values,
wrong placeholders or stale generated files. Do not edit generated files directly.

When adding wording, add its English source, stable key and surface mapping, then supply the unit in
every catalog. Preserve `%1`, `%2`, `%n`, `%s`, `%d`, `{seconds}`, `{version}`, Markdown headings,
cue brackets, line breaks and product names. For website copy, edit `translations/site.html` and use
`{{key}}` placeholders; English lives in its catalog too. The project version is inserted at generation.

`translations/locales.json` records native language names, Qt/gettext identifiers, text direction,
integer CLDR plural categories and review status. Regional formatting is preserved when the system
and interface language match (for example British English and Swiss German). Chinese uses `zh-Hans` on the website and `zh_CN`
for gettext. Egyptian Arabic (`arz`) and Nigerian Pidgin (`pcm`) retain their own catalogs even though
Qt uses `ar_EG` and `en_NG` for locale formatting. Qt's older plural tables differ for some languages;
Kalipso's translator uses the canonical CLDR forms before its normal context-sensitive `.qm` lookup.

## Glossary

| Source | Meaning to preserve |
| --- | --- |
| Reader | The small reading surface, not the person reading or a screen reader. |
| Script | The text a person will speak, not software code. |
| Steady speed | Automatic scrolling at a constant pace. |
| Follow my voice | Advance with locally recognized speech, not voice recording. |
| Under the camera | Place the reading window near the camera to maintain eye contact. |
| Companion | The separately installed local speech service used by desktop integrations. |
| Cue | A bracketed stage direction displayed but not counted as spoken words. |

Use natural local terms consistently. Preserve the distinction between on-device processing and
model downloads: audio stays on the device; downloading a model needs a network connection.

## Review status

English is the source. All other catalogs remain **generated, pending native-speaker review**,
including additions to the pre-existing Italian translation. Automated validation and multilingual
editorial checks are not native review. The owner reviews Italian; other languages need a volunteer
native reviewer. No reviewer or date is inferred.

For each language, review the app at 100% and 200% text size, every Settings page, the reader and
welcome flow, the phone remote, desktop integrations and the assembled website. Verify terminology,
plural counts, placeholders, accelerators, voice-model limitations, right-to-left layout, punctuation,
glyph coverage and natural sentences around links and file extensions. Record the reviewer and ISO
date in that language's `review` object, and change its status only after the review is complete.

Arabic, Egyptian Arabic and Urdu are separate RTL interfaces. A possible merge of the two Arabic
catalogs requires native-review evidence and the owner's decision; neither is silently aliased.

## Validation and fonts

- `python3 tools/generate-translations.py --check` checks complete and reproducible outputs.
- `python3 tests/translations_test.py` decodes every desktop catalog and checks integer plurals against
  Node's ICU rules, including zero, teens, hundreds and millions.
- CTest `interface_languages` checks every compiled Qt entry and plural form. `layout_matrix` accepts
  `LANGUAGE=<code>` and checks the real widgets at both text scales, with optional screenshots.
- Serve `site/` on port 8770 and run `tools/check-translations.mjs` with `PLAYWRIGHT` and `AXE_CORE`.
  It checks all languages in Chromium, Firefox and WebKit, static pages without JavaScript, narrow
  layouts, text at 200%, keyboard-readable controls and language switches preserving the address.

The app and Plasma package bundle OFL Noto fallbacks; the site self-hosts WOFF2 versions. Alphabet
fonts retain their full character sets. Chinese and Japanese fonts are subset to catalog text and
language names; user-authored text outside those subsets can use system fonts. Rebuild after adding
CJK characters using `python3 tools/build-translation-fonts.py` with Debian's `fonts-noto-core`,
`fonts-noto-cjk`, `python3-fonttools` and `python3-brotli`. Font copyright and OFL texts accompany both
copies. GNOME uses the desktop's Pango font fallback; install the system Noto fonts for all scripts.
