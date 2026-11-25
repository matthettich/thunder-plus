// N2 verify-n2 F8: the MIDI-in parser against a spec model (MIDI 1.0: any status byte ends the open message; SysEx / system-common cancel running status), 400,000 random streams in random split sizes.
// Adopted from the independent verifier's fuzz (docs/port/reviews/verify-n2.md): before the one-line fix in midi_in.cpp it found 15,546 mismatches in 2,000,000 streams, all a phantom message after a truncated one.
#include "midi_in.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace tpa;
// reference per MIDI 1.0 spec: SysEx / any system-common status cancels running status and any open message
struct Ref {
  int status=0, need=0, have=0; int cur=0; uint8_t d[2]; bool skip=false;
  std::vector<std::vector<uint8_t>> out;
  void feed(uint8_t c){
    if(c>=0xF8){ if(c==0xF8||c==0xFA||c==0xFB||c==0xFC) out.push_back({c}); return; }
    if(c>=0x80){
      skip=false; have=0; need=0;
      if(c>=0xF0){ status=0; if(c==0xF2){cur=c;need=2;} else if(c!=0xF7){skip=true;} return; }
      status=c; cur=c; need=((c&0xF0)==0xC0||(c&0xF0)==0xD0)?1:2; return; }
    if(skip) return;
    if(need==0){ if(!status) return; cur=status; need=((status&0xF0)==0xC0||(status&0xF0)==0xD0)?1:2; have=0; }
    d[have++]=c;
    if(have==need){ std::vector<uint8_t> m{(uint8_t)cur,d[0]}; if(need==2)m.push_back(d[1]); out.push_back(m); need=0; have=0; }
  }
};
static std::vector<std::vector<uint8_t>> got;
static void fn(void*, const MidiInMsg& m){ got.push_back(std::vector<uint8_t>(m.b, m.b+m.len)); }
int main(){
  std::mt19937 rng(1); int bad=0, shown=0; long total=0;
  for(int it=0; it<400000; it++){
    int n=1+rng()%12; std::vector<uint8_t> s;
    for(int i=0;i<n;i++){ int k=rng()%6; uint8_t c; if(k==0) c=0x80+rng()%0x80; else if(k==1) c=0xF0+rng()%16; else c=rng()%0x80; s.push_back(c);} 
    MidiInParser p; got.clear(); Ref r;
    // feed in random splits
    size_t i=0; while(i<s.size()){ size_t m=1+rng()%3; if(i+m>s.size()) m=s.size()-i; p.feed(s.data()+i,(int)m,fn,nullptr); i+=m; }
    for(uint8_t c: s) r.feed(c);
    total++;
    if(got!=r.out){ bad++; if(shown++<6){ printf("MISMATCH input:"); for(uint8_t c: s) printf(" %02X",c); printf("\n  parser:"); for(auto&m:got){printf(" [");for(uint8_t c:m)printf("%02X ",c);printf("]");} printf("\n  ref   :"); for(auto&m:r.out){printf(" [");for(uint8_t c:m)printf("%02X ",c);printf("]");} printf("\n"); } }
  }
  printf("cases %ld mismatches %d\n", total, bad);
  // long sysex + garbage must not crash
  MidiInParser p; std::vector<uint8_t> big(1<<20, 0x55); big[0]=0xF0; p.feed(big.data(), (int)big.size(), fn, nullptr);
  return bad ? 1 : 0; }
