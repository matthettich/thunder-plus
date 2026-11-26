// H6: a Node module-resolve hook for running the repo's UI sweeps (tests/sweep/*.mjs) against the Windows shell's WebView2 instead of a Chrome of their own. Whenever a sweep imports
// tests/sweep/ui-lib.mjs, it gets ui-lib-shell.mjs, which re-exports everything of ui-lib and replaces boot() with a connection to the running shell. No file under tests/sweep is edited.
const SHIM = new URL('./ui-lib-shell.mjs', import.meta.url).href;
export async function resolve(specifier, context, next) {
  const r = await next(specifier, context);
  if (/\/tests\/sweep\/ui-lib\.mjs$/.test(r.url) && !(context.parentURL || '').endsWith('/ui-lib-shell.mjs')) return { ...r, url: SHIM, shortCircuit: true };
  return r;
}
