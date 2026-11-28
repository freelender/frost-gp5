# Interface languages

The website is written in English, which is the default language. `../i18n.js`
swaps the English the page shows for the language chosen in the menu at the top,
using one file per language in this folder:

| File | Language |
|---|---|
| `index.js` | the list of languages offered in the menu |
| `pt-BR.js` | Portuguese (Brazil) |

## Adding a language

1. Copy `pt-BR.js` to a file named after the language's code, for example `es.js`
   or `de.js`, and translate the values (the right-hand side). Keep the keys: they
   are the English the page writes.
2. Add one line to `LANGUAGES` in `index.js`, with the language's own name:
   `{ code: 'es', name: 'Español', load: () => import('./es.js?v=1.9.1') }`.
3. Run `node tests/js/i18n.mjs`: it checks every language file.

A table has two parts. `text` maps exact English to its translation. `patterns`
maps English with `{}` where the page puts a value to a translation that uses
`{0}`, `{1}`... in any order. Anything without a translation stays in English, so a
partial table is fine.

Do not translate the pedal's own words (effect and knob names, PRESETS, SAVE,
DSP LIMIT...), so the website matches the pedal's screen. Names the user chose
(songs, presets, models, cabinets) are never translated: the elements that show
them carry `data-i18n-skip`.
