// H6: ui-lib.mjs with boot() attached to the running Windows shell (WebView2 over the DevTools Protocol, --cdp-port) instead of launching Chrome. Needs playwright-core (no browsers).
// The page is the shell's own: app://thunder/index.html?seqtest=1, with window.thunderCore and window.thunderDesktop injected, as the owner's copy has.
import { chromium } from 'playwright-core';
export * from '../../../../tests/sweep/ui-lib.mjs';
import { dismissTour } from '../../../../tests/sweep/ui-lib.mjs';

export async function boot({ width = 1600, height = 1000, query = '?seqtest=1', storage = null } = {}) {
  const endpoint = process.env.TPD_CDP || 'http://127.0.0.1:9222';
  const browser = await chromium.connectOverCDP(endpoint);
  const ctx = browser.contexts()[0];
  if (!ctx) throw new Error('the shell has no browser context at ' + endpoint);
  let page = ctx.pages().find(p => p.url().startsWith('app://thunder')) || ctx.pages()[0];
  if (!page) throw new Error('the shell has no page at ' + endpoint);
  const errors = [];
  page.on('pageerror', e => errors.push('pageerror: ' + e.message));
  page.on('console', m => { if (m.type() === 'error' || m.type() === 'warning') errors.push(m.type() + ': ' + m.text()); });
  page.on('dialog', d => d.dismiss().catch(() => {}));
  await page.setViewportSize({ width, height }).catch(() => {});
  const url = 'app://thunder/index.html' + query;
  if (storage) { await page.goto(url); await page.evaluate(s => { for (const k in s) localStorage.setItem(k, s[k]); }, storage); }
  await page.goto(url);
  await page.waitForFunction(() => document.readyState === 'complete', null, { timeout: 30000 });
  await page.waitForTimeout(1500);
  await dismissTour(page);
  const close = async () => { await browser.close(); };   // a connection: closing it leaves the shell running
  return { page, ctx, browser, errors, close, url, port: 0 };
}
