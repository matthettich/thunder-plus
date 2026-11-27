// LINT GATE (review P2-8): a narrow set of rules that catch real mistakes (a misspelled name, a duplicated key, dead code
// after return, a missing break), never style. Run with `npm run lint` (tools/lint.mjs lints the pages' inline scripts too).
import globals from 'globals';
const rules = { 'no-undef': 'error', 'no-dupe-keys': 'error', 'no-unreachable': 'error', 'no-fallthrough': 'error', 'no-dupe-else-if': 'error', 'no-self-assign': 'error' };
const pageGlobals = { ...globals.browser,
  tpLog: 'readonly', tpLogs: 'readonly',                     // the first inline script puts these on window (index.html, the logger)
  PsynCore: 'readonly', LxrCore: 'readonly', WavesDSP: 'readonly',   // classic scripts the page loads before the app (pump synth, lxr, waves)
  S: 'readonly',   // the ENGINE block also runs in the render worker, whose glue declares S; on the page those reads are try-guarded
  PAGE_USE: 'readonly', PAGE_EXAMPLES: 'readonly', TRK_N: 'readonly' };   // waves / chains: optional per-page names, read behind typeof checks
export default [
  { ignores: ['beta/**', 'stable/**', '**/node_modules/**', 'electron/dist/**', 'electron/app/**'] },
  { files: ['.lint/**/*.js', 'thunder-core-client.js'], languageOptions: { ecmaVersion: 2024, sourceType: 'script', globals: pageGlobals }, rules },
  { files: ['.lint/**/*.mjs'], languageOptions: { ecmaVersion: 2024, sourceType: 'module', globals: pageGlobals }, rules },
  { files: ['electron/preload.js'], languageOptions: { ecmaVersion: 2024, sourceType: 'commonjs', globals: { ...globals.node, ...globals.browser } }, rules },   // runs in the page's renderer
  { files: ['sw.js'], languageOptions: { ecmaVersion: 2024, sourceType: 'script', globals: { ...globals.serviceworker } }, rules },
  { files: ['electron/*.js'], ignores: ['electron/preload.js'], languageOptions: { ecmaVersion: 2024, sourceType: 'commonjs', globals: { ...globals.node } }, rules },
];
