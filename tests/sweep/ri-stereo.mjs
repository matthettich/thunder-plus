import fs from 'fs'; import { session } from './ri-lib.mjs';
const S = await session(fs.readFileSync('/home/claude/acid/acid.thunder','utf8'));
const r = await S.run(`const o={}; for (const pad of [0,1,3,4,5,14,15,21,22]){ const v=kit.voices[pad]; const c=ensurePlay(pad); let ra=null; try{ ra = await renderAsync(v, ex.sr, isSyn(pad)?v.len:0, false);}catch(e){ra={err:String(e)}}
  o[pad]={name:v&&v.name, live:c?{len:c.data.length, st:!!c.data2, sr:c.sr}:null, render:ra?{len:ra.data&&ra.data.length, keys:Object.keys(ra), st:!!ra.data2}:null}; } return o;`);
console.log(JSON.stringify(r,null,1)); await S.close();
