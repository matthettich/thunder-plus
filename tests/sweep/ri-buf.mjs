import fs from 'fs'; import { session } from './ri-lib.mjs';
const S = await session(fs.readFileSync('/home/claude/acid/acid.thunder','utf8'));
const r = await S.run(`const o={}; for (const pad of [0,1,15]){ const v=kit.voices[pad]; const c=ensurePlay(pad); const ra = await renderAsync(v, ex.sr, isSyn(pad)?v.len:0, false); let mx=0, pk1=0, pk2=0, e1=0, e2=0; for(let i=0;i<c.data.length;i++){ mx=Math.max(mx,Math.abs(c.data[i]-ra.data[i])); pk1=Math.max(pk1,Math.abs(c.data[i])); pk2=Math.max(pk2,Math.abs(ra.data[i])); e1+=c.data[i]**2; e2+=ra.data[i]**2; } o[pad]={maxdiff:mx, peakLive:pk1, peakRender:pk2, rmsRatioDb:10*Math.log10(e2/e1)}; } return o;`);
console.log(JSON.stringify(r)); await S.close();
