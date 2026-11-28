#!/usr/bin/env node
// LINT (review P2-8): copies each page's inline <script> blocks into .lint/ (blank lines kept, so line numbers match the page),
// then runs ESLint with eslint.config.mjs over them, electron/, sw.js and thunder-core-client.js. Needs eslint@9 + globals:
//   npm i --no-save eslint@9 globals@15 && npm run lint
import fs from 'node:fs'; import path from 'node:path'; import url from 'node:url';
const ROOT = path.resolve(path.dirname(url.fileURLToPath(import.meta.url)), '..'), OUT = path.join(ROOT, '.lint');
let ESLint; try { ({ ESLint } = await import('eslint')); } catch (e) { console.error('lint: eslint is not installed (npm i --no-save eslint@9 globals@15)'); process.exit(2); }
fs.rmSync(OUT, { recursive: true, force: true }); fs.mkdirSync(OUT, { recursive: true });
const name = (page, mod) => page.replace(/\//g, '__') + (mod ? '.module.mjs' : '.js');
for (const page of ['index.html', 'chains/index.html', 'waves/index.html']){
  // a page's classic scripts share one scope, as in the browser: one file; each module script is its own file. Tags count only at a
  // line's start (a '<script>' inside a JS string is not one); lines outside the scripts are left blank so line numbers match the page.
  const lines = fs.readFileSync(path.join(ROOT, page), 'utf8').split('\n'), files = { classic: lines.map(() => '') }; let cur = null, n = 0;
  lines.forEach((l, i) => {
    if (cur === null){ const m = l.match(/^(\s*)<script( type="module")?>(.*)$/); if (m && !/<\/script>\s*$/.test(l)){ cur = m[2] ? 'm' + (n++) : 'classic'; files[cur] = files[cur] || lines.map(() => ''); files[cur][i] = ' '.repeat(l.length - m[3].length) + m[3]; } return; }
    if (/^\s*<\/script>/.test(l)){ cur = null; return; } files[cur][i] = l;
  });
  for (const [k, v] of Object.entries(files)) fs.writeFileSync(path.join(OUT, name(page, k !== 'classic') .replace('.module', k === 'classic' ? '' : '.' + k + '.module')), v.join('\n'));
}
const eslint = new ESLint({ cwd: ROOT });
const res = await eslint.lintFiles(['.lint/*.js', '.lint/*.mjs', 'electron/*.js', 'sw.js', 'thunder-core-client.js']);
for (const r of res) if (r.filePath.startsWith(OUT)) r.filePath = path.relative(OUT, r.filePath).replace(/__/g, '/').replace(/\.(m\d+\.module\.mjs|js)$/, (_, x) => x === 'js' ? '' : ' (module script)');
const out = (await eslint.loadFormatter('stylish')).format(res), errs = res.reduce((n, r) => n + r.errorCount, 0);
if (out) console.log(out);
console.log('lint: ' + res.length + ' files, ' + errs + ' error(s)');
process.exit(errs ? 1 : 0);
