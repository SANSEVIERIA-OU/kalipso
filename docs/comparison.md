# Kalipso and other teleprompters

**Reviewed 4 November 2026; Kalipso 2.9.** This comparison describes shipped capabilities, not a
roadmap or a claim that one product is best for every use. Competitor entries come from the linked
project, vendor and store documentation. We did **not** install or benchmark those products, so a
vendor's claim is not equivalent to a successful independent test.

**Legend:** ✓ documented · ~ limited, platform-specific or only part of the feature · — absent from
Kalipso's current implementation, or explicitly excluded by the named product · ? not established
by the sources reviewed. **? does not mean the feature is missing.** Free and paid tiers differ.

## Products and scope

| Product | Scope of this comparison | Availability and commercial model |
| --- | --- | --- |
| [Kalipso][k] | Standalone app plus explicitly identified Linux integrations | Linux; experimental Windows x64 and Apple Silicon macOS. Free; current app source: SANSEVIERIA License 1.0; earlier distribution terms: AGPL-3.0-or-later; no paid feature tier. |
| [VoicePrompter][vp] | Native Mac/mobile apps and web/PWA, qualified below | Native Mac/iOS/Android and web. Mac free tier has three custom scripts; paid unlimited tier. Native Windows/Linux availability is not established: web availability is separate. |
| [Textream][t] | Mac, iPhone and iPad; feature differences marked below | Free, MIT; GitHub/App Store distribution. Its privacy policy says Apple speech can be on-device or server-based depending on device, OS and language. |
| [Flow, by LumoRez][f] | Windows app, repository `LumoRez07/Flow` | Free GPL-3.0-or-later Community build; paid Microsoft Store Pro offering. Not FlowPrompter, TeleprompterFlow or FlowPrompt. |
| [PromptSmart][ps] | Pro and PromptSmart+ mobile apps, plus Studio desktop where stated | Paid products/tiers. Studio for Windows/macOS is distinct from the mobile apps; mobile feature lists must not be applied to Studio automatically. |
| [QPrompt][q] | Desktop QPrompt 2.x | Free GPL-3.0; Linux, Windows and macOS. Website and download pages are the sources for current desktop capabilities. |
| [Teleprompter.com][tc] | Web and native product family, qualified below | Free Starter plus paid tiers; native mobile/macOS and web features differ. Browser access on Linux/Windows is not evidence of a native app. |
| [Teleprompter Pro][tp] | **Teleprompter Apps Ltd**, Apple App Store ID **941070561** | iPhone/iPad/Mac; free base plus paid Pro. Formerly Teleprompter Premium. Other products with the same name are not included. |

The earlier matrix's **PrivatePrompter** column cannot currently be substantiated. The exact-name
[repository found](https://github.com/rodgomesc/privatePrompter) is empty and does not establish that
it is the product originally meant. Its previous feature, platform and license claims are withdrawn
pending an identifiable primary source. It is **not** assumed to be the different App Store app
“Teleprompter Offline & Private.”

## Voice and reading

| Capability | Kalipso | VoicePrompter | Textream | Flow | PromptSmart | QPrompt | Teleprompter.com | Teleprompter Pro |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Speech matched to the script | ✓ | ✓ | ✓ | ✓ Vosk | ✓ VoiceTrack | ? | ✓ VoiceGlide | ? |
| Offline/on-device speech | ✓; backend requirements | ~ Mac documented; mobile/web scope varies | ~ Apple may use servers | ✓ Vosk | ~ Pro/+ mobile; Studio not established | ? | ~ vendor Web claim and narrower native-platform FAQ | ? |
| Recover after ad-lib/off-script speech | ✓; accuracy varies | ✓ | ? | ? | ✓ documented pause/resume | ? | ✓ documented pause/resume | ? |
| Recognize a return to earlier text | ✓; matching thresholds | ✓ | ? | ? | ? | ? | ? | ? |
| Click/tap a word to choose the position | ✓ | ✓ | ✓ | ? | ? | ? | ? | ? |
| Dim previously read text | ✓ | ? | ✓ | ? | ? | ? | ? | ? |
| Steady/timed scrolling | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Automatic timed fallback on speech failure | —; manual mode change | ? | ? | ? | ? | ? | ? | ? |
| Spoken control commands | — by design | ~ mobile documented | ? | ✓ documented commands | ? | ? | ✓ localized commands | ? |
| Select different speech engines | ✓ Moonshine/Vosk/system | ? | ? | ? | ? | ? | ? | ? |
| Italian speech recognition | ✓ Vosk / available system recognizer | ? not explicitly listed in reviewed Mac source | ~ Apple availability | ? not in advertised model list | ✓ PromptSmart+; not a claim about Pro/Studio | ? | ✓ VoiceGlide | ? |
| Detect the speech language automatically | — | ✓ web; Mac not established | ? | ? | ? | ? | ? | ? |

Voice-language counts are not interface-language counts. PromptSmart's official pages disagree across
editions: the Pro listing describes English VoiceTrack, the homepage lists five languages, and the
PromptSmart+ listing describes fifteen including Italian. We do not turn that into a family-wide
claim. VoicePrompter's Mac page describes 60+ languages while its web page describes 34; these refer
to different implementations. Flow advertises eight Vosk voice languages, but Italian is not in that
list; support through a manually supplied model was not established. Teleprompter.com's VoiceGlide
page claims offline Web support in its main copy while the FAQ enumerates iOS/Android/macOS; this
source discrepancy remains unresolved. See [VoicePrompter web][vpweb], [Textream privacy][tprivacy],
[PromptSmart Pro][pspro], [PromptSmart+][psplus] and [VoiceGlide][tcvoice].

## Display and hardware

| Capability | Kalipso | VoicePrompter | Textream | Flow | PromptSmart | QPrompt | Teleprompter.com | Teleprompter Pro |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Horizontal text mirroring | ✓ | ~ web/mobile documented | ✓ | ✓ documented mirroring | ✓ | ? | ✓ | ✓ |
| Vertical text mirroring | ✓ | ? | ✓ | ? | ? | ? | ✓ | ~ mirror/reverse documented; axis unspecified |
| Separate presenter controls and external mirrored output | —; one reader window | ? | ✓ Mac external display | ? | ~ duplicate display/control room | ~ multi-screen prompting; separate controls not established | ✓ remote second display | ✓ clean HDMI/AirPlay output |
| Request capture/screen-share exclusion | ~ Windows / Plasma 6.6+ | ~ Mac vendor claim | ~ Mac vendor claim | ~ Windows vendor claim | ? | ? | ? | ? |
| Floating/always-on-top reader | ✓; desktop restrictions | ✓ Mac | ✓ Mac | ✓ Windows | ? Studio-specific behavior | ✓ | ✓ Mac overlay | ? |
| Native desktop widget/shell integration | ✓ Plasma/GNOME | ~ Mac menu bar | ? notch overlay is an app window | ? | ? | ? | ? | ? |
| Adjustable reading position | ~ text size/line spacing/window | ✓ | ✓ | ? | ~ Pro/mobile margins documented | ✓ | ✓ | ✓ |
| Current-word/reading marker | ✓ | ? | ✓ | ✓ | ✓ | ✓ | ✓ | ? |
| Automatic reader color adaptation | ~ platform/permission-dependent | ? | ? | ? | ? | ? | ? | ? |
| Camera preview integrated with prompting | — | ~ mobile recording | ✓ iOS/iPadOS | ? | ~ mobile camera recording | ? | ✓ supported native apps | ~ camera recording; preview unspecified |
| Countdown before reading | ✓ steady mode | ✓ mobile | ? | ~ cue-card pause countdown | ✓ | ? | ✓ | ? |

Capture-exclusion claims require testing with the actual conferencing/capture stack. In particular,
Kalipso does not promise universal macOS invisibility; the Mac claims above are the respective
vendors' statements, not results of our tests. Kalipso's automatic color sampling is no longer
“KDE only”: it has Windows, macOS and Linux paths with permissions/fallbacks. See
[Kalipso screen-sharing details](standalone.md#screen-sharing), [Textream controls][tdocs],
[Teleprompter.com platforms][tcplatform] and [Teleprompter Pro external display][tpexternal].

## Scripts and collaboration

| Capability | Kalipso | VoicePrompter | Textream | Flow | PromptSmart | QPrompt | Teleprompter.com | Teleprompter Pro |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Persistent script library | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Restore reading position | ✓ | ? | ? | ? | ? | ? | ? | ? |
| Edit script during a running presentation | —; pause and expand to edit | ? | ~ Director browser editing | ✓ remote room editing | ~ cloud/control-room tools | ✓ editor | ? | ? |
| Unlimited free scripts | ✓ | — Mac free tier limited to three | ? no cap documented | ? Community free; cap not documented | — paid product/tiers | ? free GPL; cap not documented | ? free-tier limits | ✓ advertised base tier |
| Inline non-spoken direction cues | ✓ square brackets | ✓ Mac | ✓ documented cues | ? | ? | ? | ? | ? |
| Sections/bookmarks/jump markers | ✓ Markdown headings | ? | ✓ | ? | ✓ | ✓ markers | ? | ? |
| Settings stored per script | ~ speed and position | ✓ Mac | ✓ | ✓ | ? | ? | ? | ? |
| Import plain text / Markdown | ✓ both | ~ clipboard; file import varies | ? plain text/Markdown file formats not established | ✓ | ~ text documented | ~ text; Markdown behavior not established | ? | ~ TXT; Markdown not established |
| Import Word documents | ✓ DOCX and ODT | ? Mac file import | ? | ? | ✓ mobile | ~ via LibreOffice | ? | ✓ |
| Import PDF / RTF | — | ? | ? | ? | ✓ mobile | ~ via LibreOffice | ? | ✓ |
| Import slide speaker notes | ✓ PPTX/ODP notes, not slide bodies | ? | ✓ PPTX notes | ? | ? notes extraction | ? notes extraction | ? notes extraction | ? PowerPoint import is not proof of notes extraction |
| Follow a live slideshow's notes | ✓ LibreOffice Impress, three OSes | ? | ? live slideshow sync | ? | ? | ? | ? | ? |
| Cloud sync / shared editing | — | ~ optional iCloud on Apple | ~ local Director/remote, not cloud sync | ~ remote rooms | ✓ paid/cloud offerings | ? | ✓ paid/cloud offerings | ✓ cloud sync; shared editing not established |

Kalipso's old “live script editing while reading” claim was too broad: the standalone reader becomes
read-only while running. Its tracking code can process changed text, but that is not the same as an
editable running UI. Importing a `.pptx` file also does not establish that a competitor extracts the
speaker notes or follows the running presentation. [Kalipso import behavior](scripts.md#importing),
[reader editing implementation](../src/reader.cpp), [Textream documentation][tdocs],
[PromptSmart help][pshelp], [Teleprompter Pro listing][tpstore].

## Remotes, recording and rehearsal

| Capability | Kalipso | VoicePrompter | Textream | Flow | PromptSmart | QPrompt | Teleprompter.com | Teleprompter Pro |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Phone/browser remote paired by QR | ✓ local LAN, experimental | ? | ✓ local browser remote | ✓ QR remote room | ~ remote tools; QR not established | ~ KDE Connect keyboard remote | ✓ browser/device remote; QR not established | ~ other Apple devices; QR not established |
| Keyboard-emulating pedal/clicker | ✓ configurable keys | ? | ? | ? | ~ mobile supported controllers | ~ keyboard/KDE Connect | ✓ | ✓ platform/accessory-dependent controllers |
| Keyboard shortcuts | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Custom key bindings | ✓ | ? | ? | ? | ✓ mobile | ✓ | ✓ | ~ supported controller settings |
| Dedicated Stream Deck integration | —; external key emulation possible | ? | ? | ? | ? | ? | ? | ? |
| Remote speed adjustment | ✓ | ? | ? | ? | ~ mobile remote tools | ~ keyboard remote | ✓ | ✓ |
| Built-in camera video recording | — | ✓ mobile; Mac not established | ✓ iOS/iPadOS; Mac not established | ? | ✓ mobile | ? | ✓ supported native apps | ✓ |
| Prompt excluded from the app's own recorded video | — no recorder; external capture varies | ~ mobile; capture exclusion claim on Mac | ✓ iOS recorder | ? | ? | ? | ? | ? |
| Automatic captions / SRT export | — | ? | ? | ? | ? SRT not established | ? | ✓ subtitles; SRT not established | ✓ automatic captions / SRT |
| Estimated reading time | ✓; pace against plan after a session | ? | ~ elapsed timer; estimate not established | ~ editor word/time estimate | ✓ | ✓ chronometer/estimate | ✓ | ? |
| Rehearsal analytics (pace/long pauses/sections) | ✓ local session report | ? | ? | ? | ? | ? | ? | ? |

The old table's affirmative **QPrompt camera-video recording** entry was unsupported by the current
primary sources; it is now `?`. Likewise, a generic remote-control feature is not proof of QR pairing,
Stream Deck integration or a particular pedal protocol. See [Teleprompter.com remotes][tcremote],
[Teleprompter Pro remotes][tpremote] and [Textream recording/Director mode][tdocs].

## Platforms, distribution and localization

| Capability | Kalipso | VoicePrompter | Textream | Flow | PromptSmart | QPrompt | Teleprompter.com | Teleprompter Pro |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Native Linux desktop app | ✓ | ?; web/PWA available | — Apple platforms | — Windows product | ?; web tools separate | ✓ | ?; web available | — Apple product |
| Native Windows desktop app | ~ experimental MSI/ZIP | ?; web available | — Apple platforms | ✓ | ✓ Studio | ✓ | ?; web available | — Apple product |
| Native macOS app | ~ experimental Apple Silicon | ✓ | ✓ | — Windows product | ✓ Studio; mobile compatibility differs | ✓ Intel/Apple Silicon | ✓ | ✓ |
| Offline browser/PWA product | — online demo only | ✓ PWA | —; remote browser is not PWA | —; remote room is not PWA | ? | ? | ? offline web scope | — Apple product |
| Native mobile application | —; browser remote only | ✓ iOS/Android | ✓ iOS/iPadOS | — Windows product | ✓ iOS/Android | ~ Android 1.1.2 legacy; current release not listed | ✓ iOS/Android | ✓ iPhone/iPad |
| Trusted publisher signing/notarization | —; macOS ad-hoc only | ~ store distribution documented | ~ App Store documented | ~ Store Pro documented | ~ stores documented | ? individual packages | ~ stores documented | ~ App Store documented |
| Store/catalog distribution | ~ AUR; desktop-store uploads pending | ✓ Apple/Google stores | ✓ App Store | ✓ Microsoft Store Pro | ✓ mobile stores | ✓ Flathub/Snap | ✓ mobile stores | ✓ App Store |
| Multiple interface languages | ✓ 39; generated translations awaiting review | ✓ platform-dependent | ✓ | ~ eight languages advertised | ✓ edition-dependent | ✓ | ✓ | ✓ store lists ten |
| Accessibility / dyslexia-friendly text | ~ keyboard/text scaling/contrast tested; coverage documented | ? full audit | ~ documented fonts/readability | ? full audit | ~ text adjustments | ~ documented text/readability features | ~ text adjustments | ~ text adjustments |
| Entire product free of feature paywalls | ✓ | — free/paid tiers | ✓ | ~ Community free; Store Pro paid | — | ✓ | — free/paid tiers | — free/paid tiers |
| Public open-source license | Current app: source-available SANSEVIERIA License 1.0; earlier copies offered under AGPL | ? license of compared implementations not established | ✓ MIT | ✓ GPL-3.0-or-later | ? | ✓ GPL-3.0 | ? | ? |

Store availability is not verification that every downloadable package is signed/notarized. Similarly,
text-size controls are not a complete accessibility audit. A paid application is not automatically
closed source; entries without an established license are `?` rather than inferred claims.

Two additional Linux-relevant projects merit tracking: [Nokse22 Teleprompter](https://github.com/Nokse22/teleprompter)
is a GPL GTK4 scrolling-text application distributed on Flathub (speech tracking is not documented
in the sources reviewed). [eyeread.in](https://github.com/omniship-labs/eyeread.in) documents an AGPL
voice-following overlay, but explicitly labels Linux support experimental/best-effort and scopes
native capture exclusion to macOS/Windows. These are distinct products, not replacements for the
unidentified PrivatePrompter column.

## What Kalipso still needs

| Priority | Gap or work to evaluate | Why it matters / relevant comparator |
| --- | --- | --- |
| First | Reliability in real rehearsals, voice recovery/latency by language, long-running remote sessions, and physical Windows/Mac microphone/permission testing | Our CI checks do not measure competitors' speech accuracy or guarantee production reliability. This is more important than maximizing checkbox count. |
| First | Trusted Windows signing, macOS Developer ID/notarization, and completed desktop-store distribution | Reduces installation friction; competitors already have store distribution. AUR and GitHub packages do not replace these channels. |
| First | Native-speaker review of the 38 translated catalogs; more speech-language/model coverage separately | A 39-language interface is not 39-language speech support. |
| Next | Independent presenter controls and a mirrored external output; better controller-specific workflows | Separate-output workflows are documented by Textream and Teleprompter Pro; QPrompt also documents multi-screen prompting. Kalipso can choose a monitor, but still has one reader window. |
| Next | PDF/RTF import and a deliberate running-session editing/director workflow | Useful for existing material and assisted presentations. Live PowerPoint following is also absent; current support is notes import. |
| Product decision | Camera recording, captions/SRT, native mobile/PWA, cloud collaboration | Relevant to recording-oriented competitors, but these are substantial additions to a desktop/local-processing product, not promised work. |
| Intentionally excluded | Spoken commands that can be triggered by words in a script | Removed by the owner's decision. Dedicated keys, pedals and remotes remain the control path. |

No winner is claimed: products, platforms and paid tiers serve different needs. The comparison should
be updated from primary sources when features change, with unknowns retained until evidence is found.
The tracking discussion is [issue #41](https://github.com/SANSEVIERIA-OU/kalipso/issues/41).

## Sources and verification boundaries

Kalipso: [reader and controls](standalone.md), [imports](scripts.md), [speech backends](system-speech.md),
[Linux speech](local-engine.md), [translation status](translations.md), [accessibility scope](accessibility.md),
[platform CI](../.github/workflows/platforms.yml), [Impress CI](../.github/workflows/impress.yml), and the
[2.9 release](https://github.com/SANSEVIERIA-OU/kalipso/releases/tag/v2.9). Code and automated tests were
reviewed; physical-device and speech-accuracy limitations above still apply.

Competitors: primary documentation below, consulted on 4 November 2026. We did not buy subscriptions,
install their applications or conduct a controlled comparative benchmark. Prices, entitlements and
store availability vary by country and can change; exact prices are deliberately not frozen here.
QPrompt's linked manual was not fully readable, some store pages were inaccessible, and some
product lines have conflicting language/support claims. Those gaps are kept visible as `?` or scoped notes.

[k]: https://sansevieria-ou.github.io/kalipso/
[vp]: https://voiceprompter.app/mac/
[vpweb]: https://voiceprompter.app/web/
[t]: https://github.com/f/textream
[tdocs]: https://textream.net/docs/
[tprivacy]: https://textream.net/privacy.html
[f]: https://github.com/LumoRez07/Flow
[ps]: https://promptsmart.com/pricing.html
[pshelp]: https://www.promptsmart.com/help.html
[pspro]: https://apps.apple.com/us/app/promptsmart-pro-teleprompter/id894811756
[psplus]: https://apps.apple.com/us/app/promptsmart/id1526733375
[q]: https://qprompt.app/
[tc]: https://www.teleprompter.com/
[tcvoice]: https://www.teleprompter.com/features/voice-scrolling
[tcplatform]: https://www.teleprompter.com/platform-features
[tcremote]: https://www.teleprompter.com/features/remote-control
[tp]: https://teleprompterpro.com/pricing
[tpstore]: https://apps.apple.com/us/app/teleprompter-pro/id941070561
[tpexternal]: https://guide.teleprompterpro.com/teleprompter/external-display-mode/
[tpremote]: https://guide.teleprompterpro.com/teleprompter/remote-control/
