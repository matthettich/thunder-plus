import fs from 'fs'; import { session } from './ri-lib.mjs';
const S = await session(fs.readFileSync('/home/claude/acid/acid.thunder','utf8'));
const r = await S.run(`const sq=makeSeq(S,'song',{row:0,once:true}); const u={}; let g=0; while(sq.alive()&&g++<100000){const r=sq.step(); for(const e of r.evs) if(e.type==='note'){ const k=e.track+':pad'+e.pad+':ii'+e.ii; u[k]=(u[k]||0)+1; }}
 const o={}; for(const k in u){ const pad=+k.split(':')[1].slice(3); const v=kit.voices[pad]; o[k]=u[k]+' '+JSON.stringify({name:v&&v.name, syn:isSyn(pad), live:isLivePad(pad), eng:v&&v.engine, mo:(S.insts[+k.split(':')[2].slice(2)]||{}).mo}); } return o;`);
console.log(JSON.stringify(r,null,1)); await S.close();
