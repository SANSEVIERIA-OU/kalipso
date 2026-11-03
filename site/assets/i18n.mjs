// SPDX-License-Identifier: AGPL-3.0-or-later
// The words the scripts write, in the page's language; English when there is none for it.
import en from './strings/en.mjs';
import it from './strings/it.mjs';
const all = {en, it};
export const t = all[document.documentElement.lang.slice(0, 2)] ?? en;

// A link to the page in another language keeps the place you are reading (the same section).
for (const link of document.querySelectorAll('a[data-language]'))
  link.addEventListener('click', () => { if (location.hash) link.hash = location.hash; });
