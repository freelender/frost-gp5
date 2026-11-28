// Interface languages for i18n.js, in the order the language menu shows them. English is the default and
// needs no file: the page itself is written in English. Each other language is one file in this folder,
// loaded only when someone chooses it. To add one, see README.md in this folder.
export const LANGUAGES = [
  { code: 'en', name: 'English' },
  { code: 'pt-BR', name: 'Português (Brasil)', load: () => import('./pt-BR.js?v=1.9.1') },
];
