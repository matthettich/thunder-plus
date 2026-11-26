// node --import ./native/desktop/tests/sweep/register.mjs tests/sweep/menus.mjs   (with the shell running: Thunder Plus.exe --null-audio --cdp-port 9222)
import { register } from 'node:module';
register('./hooks.mjs', import.meta.url);
