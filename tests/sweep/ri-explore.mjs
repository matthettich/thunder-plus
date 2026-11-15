import fs from 'fs';
import { open, ev } from './seq-lib.mjs';
const A = await open();
const t = fs.readFileSync('/home/claude/acid/acid.thunder', 'utf8');
await A.page.evaluate(t => window.__seqTest.load(t, 'acid.thunder'), t);
await A.page.waitForTimeout(1500);
const r = await A.page.evaluate(async () => window.__seqTest.ev(`(async()=>{
 const o={sr:ex.sr, ntr:ntr(), NT, MT, lfo:JSON.stringify(S.perf.lfo)};
 o.lfoRes={}; for(const k in S.perf.lfo) o.lfoRes[k]=S.perf.lfo[k].map(L=>L.id+' => '+tgtLabel(L.id));
 const sq=makeSeq(S,'song',{row:0,once:true}); const first={},last={},cnt={}; let g=0;
 while(sq.alive()&&g++<100000){const r=sq.step(); for(const e of r.evs) if(e.type==='note'){ if(first[e.track]==null) first[e.track]=e.t; last[e.track]=e.t; cnt[e.track]=(cnt[e.track]||0)+1;}}
 o.first=first;o.last=last;o.cnt=cnt;o.end=sq.time; o.tfxTypes=S.tfx.map(a=>(a||[]).map(f=>f?f.type+(f.on?'':'(off)'):'-').join(','));
 o.mix=S.mix.map((m,i)=>[i,m.mute,m.solo]).filter(x=>x[1]||x[2]); o.core=[coreWanted(), core.ready, core.bad];
 return o;})()`));
console.log(JSON.stringify(r,null,1));
await A.close();
