import fs from 'fs'; import { session, variant } from './ri-lib.mjs';
const S = await session(fs.readFileSync('/home/claude/acid/acid.thunder','utf8'));
const r = await S.run(`const sq=makeSeq(S,'song',{row:0,once:true}); const rows={}; let g=0; while(sq.alive()&&g++<100000){const r=sq.step(); if(r.pos){ for(const k in r.pos){ const p=r.pos[k]; if(p && rows[p.row]==null) rows[p.row]=r.t; } } } return rows;`);
console.log(JSON.stringify(r)); console.log(JSON.stringify(await S.info()));
await S.close();
