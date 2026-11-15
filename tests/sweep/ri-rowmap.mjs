import fs from 'fs'; import { session } from './ri-lib.mjs';
const S = await session(fs.readFileSync('/home/claude/acid/acid.thunder','utf8'));
const r = await S.run(`const sq=makeSeq(S,'song',{row:0,once:true}); const m={}; let g=0; while(sq.alive()&&g++<100000){const r=sq.step(); const row=r.pos&&r.pos[0]?r.pos[0].row:null; for(const e of r.evs) if(e.type==='note'){ const y=Math.floor(e.t/1.832061); (m[e.track]=m[e.track]||{})[y]=((m[e.track]||{})[y]||0)+1; }} const o={}; for(const t in m) o[t]=Object.keys(m[t]).map(Number).sort((a,b)=>a-b).join(','); return o;`);
for (const t in r) console.log('track', t, 'rows with notes:', r[t]);
await S.close();
