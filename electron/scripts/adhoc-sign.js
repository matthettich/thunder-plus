// AD-HOC SIGNING (owner, 7 Nov: 6 microphone prompts each time Record a sample opened). The Mac app has no Apple certificate yet,
// so electron-builder leaves it unsigned, and macOS can't keep a microphone grant for an unsigned app (each of its helper processes
// asked again). An ad-hoc signature of the whole bundle gives it one identity: one prompt per update until a Developer ID.
// Runs as electron-builder's afterPack, on every finished Mac app but not the halves of a universal one (signing the x64 / arm64 halves before the merge would make
// their files differ). electron-builder 26 refuses identity "-" ("no valid identity"), hence this hook.
const { execFileSync } = require('child_process');
const fs = require('fs'), path = require('path');
exports.default = async ctx => {
  if (ctx.electronPlatformName !== 'darwin') return;
  const dir = path.basename(ctx.appOutDir); if (/universal/i.test(dir) && /temp/i.test(dir)) return;   // not the halves of a universal build (mac-universal-x64-temp, -arm64-temp); every finished app, single-arch too (CodeRabbit)
  const app = fs.readdirSync(ctx.appOutDir).find(f => f.endsWith('.app'));
  if (!app) throw new Error('adhoc-sign: no .app in ' + ctx.appOutDir);
  const p = path.join(ctx.appOutDir, app);
  execFileSync('codesign', ['--force', '--deep', '--sign', '-', p], { stdio: 'inherit' });
  execFileSync('codesign', ['--verify', '--deep', '--strict', p], { stdio: 'inherit' });
  console.log('adhoc-sign: signed ' + p);
};
