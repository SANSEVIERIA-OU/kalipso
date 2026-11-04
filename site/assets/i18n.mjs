// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
// Every static page has a complete local catalog, including full BCP 47 tags.
export const t = (await import(`./strings/${document.documentElement.lang}.mjs`)).default;

// A link to the page in another language keeps the place you are reading (the same section).
function keepPlace(target) {
  const url = new URL(target, location.href);
  url.hash = location.hash;
  url.search = location.search;
  return url.href;
}
for (const link of document.querySelectorAll('a[data-language]'))
  link.addEventListener('click', () => { link.href = keepPlace(link.href); });
const picker = document.querySelector('#site-language');
if (picker) {
  picker.hidden = false;
  document.querySelector('#language-link').hidden = true;
}
picker?.addEventListener('change', event => {
  location.assign(keepPlace(event.target.value));
});
