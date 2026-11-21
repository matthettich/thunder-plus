// Where the native tools read the web app's code from: the pinned beta copy in native/tests/web-ref (see web-pin.mjs). Set THUNDER_WEB_REF=<dir>
// to read another pin (same file names), or THUNDER_WEB_REF=root to read the old snapshot at the repository root (core/seq-core.js).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url)), ROOT = path.resolve(here, '../..');
const env = process.env.THUNDER_WEB_REF;
export const REF_DIR = env && env !== 'root' ? path.resolve(env) : path.join(ROOT, 'native/tests/web-ref');
export const seqCorePath = () => env === 'root' ? path.join(ROOT, 'core/seq-core.js') : path.join(REF_DIR, 'seq-core.js');
export const seqCoreText = () => fs.readFileSync(seqCorePath(), 'utf8').replace(/\r\n/g, '\n');
export const excerptText = () => fs.readFileSync(path.join(REF_DIR, 'index-excerpt.js'), 'utf8').replace(/\r\n/g, '\n');
export const betaCommit = () => fs.readFileSync(path.join(REF_DIR, 'BETA-COMMIT'), 'utf8').trim();
