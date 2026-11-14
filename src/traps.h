// traps.h -- the plant's traps, and a sandbox to try them in (./dev-traps.sh  ==  --traps).
//
// Eight devices the player can set off against whatever walks through them. Here they stand in a ring round the
// middle of the open arena (no map), nine metres out, so everything that comes for the player has to cross one:
//   1 STEAM MAIN     a valve on a pipe: the bay vents scalding steam -- burns and slows
//   2 LIVE PLATE     a fuse board arms a floor plate -- arcs hold everything on it, twitching
//   3 FIRE HOSE      a fire point's hose down a line -- no damage, knocks flat and shoves back
//   4 BREACH SURGE   a control post vents a breach -- a column of light; near it burned and thrown, at its mouth killed
//   5 CRANE DROP     the load comes down on its chain -- crushes what is under it, then is winched back
//   6 BLAST DOOR     a door slammed shut -- crushes what is in the way, holds the doorway, rises again
//   7 COOLANT SPILL  a tank ruptured over the floor -- they slip in it; set it off again and it burns
//   8 TURBINE        spun up, it drags in whatever is in front of it
// Walk up to one and press E (its lamp is green when it is ready), or T to fire the next in turn, Y for all of them.
// CHERNOBYL_TRAP_AUTO=<seconds> fires them one after another on that clock (stills / unattended demo).
//
// Each trap is a small state machine (ready -> active for its run -> cooling), drawn from primitives and the game's own
// smoke / flame / spark / light systems, and it works on the real enemies through the real damage and knockdown code --
// so what is seen here is what a trap built into the map will do. Placing them in maps/chernobyl.map is the next step.
enum { TR_STEAM, TR_PLATE, TR_HOSE, TR_SURGE, TR_CRANE, TR_DOOR, TR_SPILL, TR_TURBINE, TR_N };
typedef struct { Vector3 c; float ang; Vector3 tv, rv; int state; float t, run, cool; int stage; float aux; int kind, hole; float ys; } Trap;   // kind: TR_*; hole: the map breach a surge vents (-1: its own, in the sandbox); ys: height scale (the crane, under the plant's ceiling)   // tv / rv: its own across / outward axes
#define MAX_TRAPS 32
static int g_trapDemoDirect=0;        // set round a sandbox key / the unattended run: fire without arming
static Trap g_traps[MAX_TRAPS]; static int g_nTraps=0, g_trapsFromMap=0, g_trapDemo=0, g_trapNext=0; static float g_trapSlip[MAX_ENEMIES];
static const char *kTrapName[TR_N]={"STEAM MAIN","LIVE PLATE","FIRE HOSE","BREACH SURGE","CRANE DROP","BLAST DOOR","COOLANT SPILL","TURBINE"};
static const float kTrapRun[TR_N]={8.0f,3.0f,4.0f,2.6f,7.0f,8.0f,16.0f,10.0f}, kTrapSwitchX[TR_N]={3.4f,3.4f,-4.2f,3.6f,3.0f,2.9f,-3.6f,2.6f}, kTrapSwitchZ[TR_N]={0,0,0,0,-2.2f,-2.6f,0,0},
                   kTrapCool[TR_N]={25.0f,20.0f,20.0f,40.0f,45.0f,40.0f,45.0f,60.0f};      // in the plant: seconds before it can be set off again (the sandbox: three)
static Vector3 TW(const Trap *t, float x, float y, float z){ return (Vector3){t->c.x+t->tv.x*x+t->rv.x*z, t->c.y+y, t->c.z+t->tv.z*x+t->rv.z*z}; }
static void TL(const Trap *t, Vector3 p, float *x, float *z){ float dx=p.x-t->c.x, dz=p.z-t->c.z; *x=dx*t->tv.x+dz*t->tv.z; *z=dx*t->rv.x+dz*t->rv.z; }
static void TrapsInit(void){
    g_nTraps=TR_N;
    for (int k=0;k<TR_N;k++){ Trap *t=&g_traps[k]; float a=k*0.7853982f+0.3927f; memset(t,0,sizeof *t); t->kind=k; t->hole=-1; t->ys=1.0f;
        t->ang=a; t->rv=(Vector3){cosf(a),0,sinf(a)}; t->tv=(Vector3){sinf(a),0,-cosf(a)}; t->c=(Vector3){cosf(a)*9.0f,0,sinf(a)*9.0f}; t->run=kTrapRun[k]; }
    for (int i=0;i<MAX_ENEMIES;i++) g_trapSlip[i]=0;
}
// ---- What they sound like. Synthesized here (no recordings): each trap's own noise for the length it runs, the switch
// being thrown, the slam of the load / the door, the pool catching. All of them are on the dev sound board (TRAPS).
enum { TS_ARM, TS_STEAM, TS_ARC, TS_HOSE, TS_SURGE, TS_RATTLE, TS_SLAM, TS_SPILL, TS_TURBINE, TS_IGNITE, TS_N };
static Sound g_trapSnd[TS_N]; static int g_hasTrapSnd=0;
static const char *kTrapSndName[TS_N]={"Trap armed","Steam main","Live plate","Fire hose","Breach surge","Crane release","Slam (crane load / blast door)","Coolant spill","Turbine","Coolant ignites"};
static const char *kTrapSndWhen[TS_N]={"E at a trap's switch","the steam main vents (8 s)","the plate is live (3 s)","the hose runs (4 s)","a breach is vented","the crane lets go of its load","the load lands / the door comes down","the tank bursts","the turbine spins up, runs and winds down (10 s)","the pool catches fire"};
static void TrapSoundsApplied(void);
static void TrapSoundsInit(void){
    const int SR=22050; static const float len[TS_N]={0.42f,8.0f,3.0f,4.0f,2.8f,0.75f,1.5f,4.2f,10.0f,1.3f};
    for (int k=0;k<TS_N;k++){ int n=(int)(len[k]*SR); short *d=(short*)MemAlloc(n*sizeof(short)); float lp=0, lp2=0, ph=0, ph2=0, hold=0; unsigned int rng=1234567u+k*7919u;
        for (int i=0;i<n;i++){ float t=(float)i/SR, T=len[k], v=0; rng=rng*1664525u+1013904223u; float nz=(float)((rng>>8)&0xFFFF)/32768.0f-1.0f;
            float in=fminf(1.0f,t*30.0f), out=fminf(1.0f,(T-t)*4.0f);
            switch (k){
            case TS_ARM: v=nz*expf(-t*70.0f)*0.6f + sinf(t*6.2832f*120.0f)*expf(-t*28.0f)*0.6f                                   // the lever goes over: a clack...
                          + ((t>0.14f)?sinf(6.2832f*(t<0.26f?660.0f:990.0f)*t)*0.32f*fminf(1.0f,(t-0.14f)*60.0f)*fminf(1.0f,(T-t)*14.0f):0.0f); break;   // ...then two rising tones: set
            case TS_STEAM: { lp+=(nz-lp)*0.55f; lp2+=(lp-lp2)*0.10f; float hiss=lp-lp2;                                         // a hard hiss, breathing, with the burst of the valve opening
                v=hiss*(0.55f+0.12f*sinf(t*6.2832f*0.7f)+0.06f*sinf(t*6.2832f*3.1f))*in*fminf(1.0f,(T-t)*1.2f) + nz*expf(-t*9.0f)*0.5f + sinf(t*6.2832f*(2400.0f+300.0f*sinf(t*4.0f)))*0.035f*out; } break;
            case TS_ARC: { ph+=100.0f/SR; if (ph>1) ph-=1; float saw=ph*2.0f-1.0f; if ((rng>>20)%700==0) hold=1.0f; hold*=0.9985f;   // mains hum torn by snaps
                lp+=(nz-lp)*0.7f; v=(saw*0.30f+((saw>0)?0.22f:-0.22f))*(0.55f+0.45f*((nz>0.2f)?1.0f:0.4f)) + lp*hold*0.9f; v*=in*out*0.85f; } break;
            case TS_HOSE: { lp+=(nz-lp)*0.22f; lp2+=(nz-lp2)*0.6f; float pr=fminf(1.0f,t*3.0f);                                 // water under pressure: a roar with spit in it
                v=(lp*1.25f+(lp2-lp)*0.35f)*(0.7f+0.3f*pr)*(0.9f+0.1f*sinf(t*6.2832f*11.0f))*fminf(1.0f,t*8.0f)*fminf(1.0f,(T-t)*2.0f); } break;
            case TS_SURGE: { float u=fminf(1.0f,t/0.45f); ph+=(70.0f+900.0f*u*u)/SR; lp+=(nz-lp)*0.12f;                           // it winds up, and goes
                float wind=sinf(6.2832f*ph)*0.35f*u*((t<0.45f)?1.0f:expf(-(t-0.45f)*9.0f));
                float bt=fmaxf(0.0f,t-0.45f), boom=(t>=0.45f)?(sinf(6.2832f*(52.0f-bt*14.0f)*bt)*expf(-bt*2.4f)*0.95f + lp*expf(-bt*3.0f)*1.6f + nz*expf(-bt*26.0f)*0.6f):0.0f;
                v=wind+boom; } break;
            case TS_RATTLE: { float tick=fmodf(t*38.0f,1.0f); lp+=(nz-lp)*0.8f;                                                 // chain running through a block
                v=(sinf(t*6.2832f*2900.0f)*0.4f+sinf(t*6.2832f*4300.0f)*0.3f+lp*0.5f)*expf(-tick*9.0f)*(0.5f+0.5f*nz*nz)*fminf(1.0f,(T-t)*6.0f); } break;
            case TS_SLAM: { lp+=(nz-lp)*0.10f;                                                                                  // tonnes of steel meeting the floor
                v=sinf(6.2832f*(46.0f-t*10.0f)*t)*expf(-t*4.5f)*1.0f + lp*expf(-t*7.0f)*2.0f + nz*expf(-t*45.0f)*0.7f
                 +(sinf(t*6.2832f*188.0f)*0.16f+sinf(t*6.2832f*431.0f)*0.10f+sinf(t*6.2832f*913.0f)*0.05f)*expf(-t*3.2f); } break;
            case TS_SPILL: { lp+=(nz-lp)*0.16f; if ((rng>>18)%420==0){ hold=1.0f; ph2=300.0f+(float)((rng>>4)%500); } hold*=0.9990f; ph+=ph2/SR;   // a gush, glugging as the tank draws air
                float g=(t<3.0f)?1.0f:fmaxf(0.25f,1.0f-(t-3.0f)*0.7f);
                v=(lp*1.5f*(0.8f+0.2f*sinf(t*6.2832f*6.5f)) + sinf(6.2832f*ph)*hold*0.22f)*g*fminf(1.0f,t*12.0f)*fminf(1.0f,(T-t)*1.5f) + nz*expf(-t*14.0f)*0.6f; } break;
            case TS_TURBINE: { float w=(t<2.0f)?t/2.0f:(t>T-2.0f)?(T-t)/2.0f:1.0f; w=w*w*(3.0f-2.0f*w); ph+=(38.0f+560.0f*w)/SR; ph2+=(13.0f+187.0f*w)/SR; lp+=(nz-lp)*(0.05f+0.25f*w);   // the whine climbing, the air it moves
                v=(sinf(6.2832f*ph)*0.22f+sinf(6.2832f*ph*2.0f)*0.10f+sinf(6.2832f*ph*3.01f)*0.05f+sinf(6.2832f*ph2)*0.25f)*(0.25f+0.75f*w) + lp*1.5f*w; v*=fminf(1.0f,t*6.0f)*fminf(1.0f,(T-t)*3.0f)*0.8f; } break;
            case TS_IGNITE: { lp+=(nz-lp)*0.07f; lp2+=(nz-lp2)*0.35f; float e=fminf(1.0f,t*5.0f)*expf(-t*2.6f);                    // whoomph
                v=lp*e*3.2f + lp2*e*0.5f + sinf(6.2832f*(60.0f+t*30.0f)*t)*e*0.5f; } break;
            }
            if (v>1) v=1; if (v<-1) v=-1; d[i]=(short)(v*29000.0f); }
        Wave w={ .frameCount=(unsigned int)n, .sampleRate=SR, .sampleSize=16, .channels=1, .data=d };
        g_trapSnd[k]=LoadSoundFromWave(w); MemFree(d); }
    g_hasTrapSnd=1; TrapSoundsApplied();
}
// A recording can take a synthesized sound's place: assets/traps/sounds.txt lists "slot file" pairs (the dev sound board
// writes it when a candidate is APPLIED there, copying the file in as assets/traps/sound_<slot>.<ext>). The synthesized
// one is kept, to go back to.
static Sound g_trapSynth[TS_N]; static char g_trapSndFile[TS_N][96];
static const char *kTrapSndKey[TS_N]={"arm","steam","plate","hose","surge","crane","slam","spill","turbine","ignite"};
static void TrapSoundsSave(void){
    FILE *f=fopen("assets/traps/sounds.txt","w"); if (!f) return;
    for (int k=0;k<TS_N;k++) if (g_trapSndFile[k][0]) fprintf(f,"%s %s\n",kTrapSndKey[k],g_trapSndFile[k]);
    fclose(f);
}
static int TrapSoundSet(int k, const char *path){                                    // "" / NULL: back to the synthesized one
    if (k<0 || k>=TS_N) return 0;
    if (!path || !path[0]){ g_trapSnd[k]=g_trapSynth[k]; g_trapSndFile[k][0]=0; return 1; }
    if (!FileExists(path)) return 0;
    Sound sd=LoadSound(path); if (sd.frameCount<=0) return 0;
    g_trapSnd[k]=sd; snprintf(g_trapSndFile[k],sizeof g_trapSndFile[k],"%s",path); return 1;
}
static void TrapSoundsApplied(void){                                                 // at load, after the synthesized set is made
    for (int k=0;k<TS_N;k++){ g_trapSynth[k]=g_trapSnd[k]; g_trapSndFile[k][0]=0; }
    FILE *f=fopen("assets/traps/sounds.txt","r"); if (!f) return;
    char key[32], path[96];
    while (fscanf(f,"%31s %95s",key,path)==2) for (int k=0;k<TS_N;k++) if (!strcmp(key,kTrapSndKey[k])) TrapSoundSet(k,path);
    fclose(f);
}
static void TrapSound(int ts, Vector3 at, float vol){ if (g_audio && g_hasTrapSnd) SfxAt(g_trapSnd[ts], at, vol); }
static const int kTrapSndOf[TR_N]={TS_STEAM,TS_ARC,TS_HOSE,TS_SURGE,TS_RATTLE,TS_SLAM,TS_SPILL,TS_TURBINE};       // what each makes as it goes off
// A trap is ARMED by hand and SPRINGS by itself: E at its switch sets it (state 3: lamp amber), and the first hostile to walk
// into it sets it off -- so it is laid ahead of them, not timed against them. (E again on an armed trap springs it at once.)
static void TrapSpring(int k){                                                       // it goes off now
    Trap *t=&g_traps[k]; if (t->state!=0 && t->state!=3) return;
    t->state=1; t->t=0; t->stage=0; t->aux=0; t->cool=0; DebugLog("trap","\"k\":%d,\"name\":\"%s\"",k,kTrapName[t->kind]);
    TrapSound(kTrapSndOf[t->kind], (Vector3){t->c.x,t->c.y+1.2f,t->c.z}, 1.0f);
}
static void TrapFire(int k){                                                         // the switch, thrown
    Trap *t=&g_traps[k];
    if (t->kind==TR_SPILL && t->state==1 && t->t>2.0f && t->stage==0){ t->stage=1; t->aux=t->t; DebugLog("trap","\"k\":%d,\"what\":\"ignite\"",k); TrapSound(TS_IGNITE,TW(t,-1.0f,0.5f,0),1.0f); return; }   // again while it lies there: lit
    if (t->state==3){ TrapSpring(k); return; }
    if (t->state!=0) return;
    if (g_trapDemo && g_trapDemoDirect){ TrapSpring(k); return; }                    // (the sandbox's T / Y / unattended run: straight off)
    TrapSound(TS_ARM, TW(t,kTrapSwitchX[t->kind],1.2f,kTrapSwitchZ[t->kind]), 0.9f);
    t->state=3; t->t=0; DebugLog("trap","\"k\":%d,\"what\":\"armed\",\"name\":\"%s\"",k,kTrapName[t->kind]);
}
static int TrapInZone(const Trap *t, const Enemy *e){                                // is he where it would catch him?
    float x,z; TL(t,e->pos,&x,&z); int k=t->kind;
    if (e->climbT>0) return (k==TR_SURGE && t->hole>=0 && e->hole==t->hole);         // (still in a breach: not there yet -- unless it is the breach the surge vents)
    if (e->riseT>0) return 0;
    switch (k){
    case TR_STEAM:   return fabsf(x)<3.0f && fabsf(z)<1.2f;
    case TR_PLATE:   return fabsf(x)<2.2f && fabsf(z)<2.2f;
    case TR_HOSE:    return x>-3.0f && x<6.5f && fabsf(z)<0.9f;
    case TR_SURGE:   return x*x+z*z<3.2f*3.2f;
    case TR_CRANE:   return fabsf(x)<1.2f && fabsf(z)<1.2f;
    case TR_DOOR:    return fabsf(x)<1.4f && fabsf(z)<0.5f;
    case TR_SPILL:   return (x+1.0f)*(x+1.0f)+z*z<2.4f*2.4f;
    case TR_TURBINE: return z<0.4f && z>-5.5f && fabsf(x)<1.6f;
    }
    return 0;
}
static void TrapHurt(int i, float dmg, int cause, Vector3 from){
    Enemy *e=&g_enemies[i]; int oc=g_hurtCause; Vector3 of=g_hurtFrom; g_hurtCause=cause; g_hurtFrom=from;
    Vector3 d=Vector3Subtract(e->pos,from); d.y=0; float l=Vector3Length(d); d=(l>1e-3f)?Vector3Scale(d,1.0f/l):(Vector3){0,0,1};
    HurtEnemy(i,dmg,0,d,(Vector3){e->pos.x,e->pos.y+1.0f,e->pos.z},cause!=HC_FLAME);
    g_hurtCause=oc; g_hurtFrom=of;
}
static float CraneTop(const Trap *t){ return 4.6f*t->ys-(t->ys<1.0f?0.25f:0.0f); }                 // where its load hangs
static float CraneY(const Trap *t, float on){ float y0=CraneTop(t), y=y0; if (on<0) return y0;
    y=y0-0.5f*24.0f*on*on; if (y<0.9f || t->stage==1){ float up=(on-2.2f)/(t->run-2.2f); y=0.9f+(up>0?up*up*(3-2*up)*(y0-0.9f):0.0f); } return y; }
static void TrapsSync(void){                                                         // in the plant: the traps the map places ("info_trap")
    if (g_trapDemo) return;
    if (!g_hasMap || g_nMapTraps<=0){ g_nTraps=0; g_trapsFromMap=0; return; }
    if (g_trapsFromMap) return;
    g_trapsFromMap=1; g_nTraps=0;
    for (int i=0;i<g_nMapTraps && g_nTraps<MAX_TRAPS;i++){ Trap *t=&g_traps[g_nTraps++]; float a=g_mapTraps[i].ang*DEG2RAD; memset(t,0,sizeof *t);
        t->kind=g_mapTraps[i].kind; t->hole=g_mapTraps[i].hole; t->ys=(t->kind==TR_CRANE)?0.74f:1.0f; t->ang=a; t->c=g_mapTraps[i].pos;
        t->rv=(Vector3){cosf(a),0,sinf(a)}; t->tv=(Vector3){sinf(a),0,-cosf(a)}; t->run=kTrapRun[t->kind]; }
    DebugLog("traps","\"placed\":%d",g_nTraps);
}
static int TrapNear(void){ int near=-1; float bd=2.6f;                               // the switch post he is standing at, if any
    for (int k=0;k<g_nTraps;k++){ Vector3 p=TW(&g_traps[k],kTrapSwitchX[g_traps[k].kind],0,kTrapSwitchZ[g_traps[k].kind]); float d=hypotf(p.x-g_pos.x,p.z-g_pos.z);
        if (d<bd && fabsf((g_pos.y-EYE_H)-p.y)<2.5f){ bd=d; near=k; } }
    return near; }
// What of a trap is solid: its posts, legs, tank, casing -- and the door when it is down, the load when it lies on the floor.
static int TrapCircles(const Trap *t, float out[][3]){ int n=0, k=t->kind;
    #define TC(x,z,r) { out[n][0]=(x); out[n][1]=(z); out[n][2]=(r); n++; }
    TC(kTrapSwitchX[k],kTrapSwitchZ[k],0.26f)
    if (k==TR_STEAM){ TC(-3.3f,0,0.3f) TC(3.3f,0,0.36f) }
    if (k==TR_HOSE){ TC(-4.2f,0.3f,0.5f) }
    if (k==TR_CRANE){ TC(-3.7f,0,0.5f) TC(3.7f,0,0.5f) if (t->state==1 && t->stage==1 && t->t<t->run-1.5f) TC(0,0,1.85f) }
    if (k==TR_DOOR){ TC(-1.95f,0,0.6f) TC(1.95f,0,0.6f) if (t->state==1 && t->stage>=1 && t->t<t->run-1.6f){ TC(-1.1f,0,0.6f) TC(0,0,0.6f) TC(1.1f,0,0.6f) } }
    if (k==TR_SPILL){ TC(-3.6f,0.9f,1.05f) }
    if (k==TR_TURBINE){ TC(0,1.75f,1.6f) TC(0,3.0f,1.6f) TC(0,4.3f,1.6f) }
    #undef TC
    return n; }
static int __attribute__((unused)) TrapBlocked(float x, float z, float pad){
    float c[8][3];
    for (int i=0;i<g_nTraps;i++){ const Trap *t=&g_traps[i]; if (fabsf(x-t->c.x)>9.0f || fabsf(z-t->c.z)>9.0f) continue;
        int n=TrapCircles(t,c); for (int q=0;q<n;q++){ Vector3 p=TW(t,c[q][0],0,c[q][1]); float r=c[q][2]+pad; if ((x-p.x)*(x-p.x)+(z-p.z)*(z-p.z)<r*r) return 1; } }
    return 0; }
// Enemies are not STOPPED by a trap's solid parts (the route they follow knows nothing of them: one walked into a switch
// post and stood there treading) -- they are slid off: pushed out of the circle, and a little round it, the same way
// each time for the same enemy, so he goes past on one side.
static void TrapPushEnemy(int i){
    Enemy *e=&g_enemies[i]; float c[8][3]; if (g_nTraps<=0 || e->climbT>0) return;
    for (int ti=0;ti<g_nTraps;ti++){ const Trap *t=&g_traps[ti]; if (fabsf(e->pos.x-t->c.x)>9.0f || fabsf(e->pos.z-t->c.z)>9.0f || fabsf(e->pos.y-t->c.y)>2.5f) continue;
        int n=TrapCircles(t,c); for (int q=0;q<n;q++){ Vector3 p=TW(t,c[q][0],0,c[q][1]); float r=c[q][2]+0.32f, dx=e->pos.x-p.x, dz=e->pos.z-p.z, d=sqrtf(dx*dx+dz*dz);
            if (d>=r) continue; if (d<1e-3f){ dx=1; dz=0; d=1; }
            float ov=r-d, sgn=(i&1)?1.0f:-1.0f, nx=p.x+dx/d*r-dz/d*ov*0.6f*sgn, nz=p.z+dz/d*r+dx/d*ov*0.6f*sgn;
            if (!g_mapCol.ready || !MapSphereHitsWall((Vector3){nx,e->pos.y+ENEMY_STEP+ENEMY_MOVE_R+0.03f,nz},ENEMY_MOVE_R)){ e->pos.x=nx; e->pos.z=nz; e->stuckT=0.0f; } } }
}
static void TrapPushPlayer(void){                                                    // the player slides round them
    float c[8][3]; if (g_noclip) return;
    for (int i=0;i<g_nTraps;i++){ const Trap *t=&g_traps[i]; if (fabsf(g_pos.x-t->c.x)>9.0f || fabsf(g_pos.z-t->c.z)>9.0f || fabsf((g_pos.y-EYE_H)-t->c.y)>3.0f) continue;
        int n=TrapCircles(t,c); for (int q=0;q<n;q++){ Vector3 p=TW(t,c[q][0],0,c[q][1]); float r=c[q][2]+0.36f, dx=g_pos.x-p.x, dz=g_pos.z-p.z, d=sqrtf(dx*dx+dz*dz);
            if (d<r && d>1e-4f){ g_pos.x=p.x+dx/d*r; g_pos.z=p.z+dz/d*r; } } }
}
static int TrapMarks(Vector3 *pos, int *ready, int max){ int n=0;                     // for the minimap: each trap's switch post, and whether it can be set off
    for (int i=0;i<g_nTraps && n<max;i++){ const Trap *t=&g_traps[i]; pos[n]=TW(t,kTrapSwitchX[t->kind],0,kTrapSwitchZ[t->kind]); ready[n]=(t->state==0)?1:(t->state==3)?2:0; n++; }   // 1 can be armed, 2 armed and waiting, 0 going off / recharging
    return n; }
static void TrapsUpdate(float dt){
    TrapsSync(); if (g_nTraps<=0) return;
    { int near=TrapNear();                                                           // E at a switch (sandbox: T the next, Y the lot)
      if (near>=0 && IsKeyPressed(KEY_E) && !g_dead) TrapFire(near);
      if (g_trapDemo && IsKeyPressed(KEY_T)){ TrapSpring(g_trapNext); g_trapNext=(g_trapNext+1)%TR_N; }
      if (g_trapDemo && IsKeyPressed(KEY_Y)) for (int k=0;k<TR_N;k++) TrapSpring(k);
      { static float fireIn=-2.0f; static int fireK=-1;                             // dev: CHERNOBYL_TRAP_FIRE=<n> sets off trap n two seconds in (stills of a trap in the plant)
        if (fireIn<-1.0f){ const char *v=getenv("CHERNOBYL_TRAP_FIRE"); fireK=v?atoi(v):-1; fireIn=v?2.0f:-1.0f; }
        if (fireIn>0.0f && fireK>=0 && fireK<g_nTraps){ fireIn-=dt; if (fireIn<=0.0f){ TrapSpring(fireK); fireIn=-1.0f; } }
        static int armAll=-1; if (armAll<0) armAll=getenv("CHERNOBYL_TRAP_ARM")?1:0;                 // dev: CHERNOBYL_TRAP_ARM=1 keeps every trap armed (tests of them springing)
        if (armAll) for (int k=0;k<g_nTraps;k++) if (g_traps[k].state==0) g_traps[k].state=3; }
      if (!g_trapDemo) goto run;
      static float autoT=-1, autoGap=0; if (autoT<0){ const char *v=getenv("CHERNOBYL_TRAP_AUTO"); autoGap=v?(float)atof(v):0; autoT=autoGap>0?1.5f:1e9f; }
      if (autoGap>0){ { static int look=0; const Trap *lt=&g_traps[look]; g_pos.x=0; g_pos.z=0; g_yaw=atan2f(lt->c.x,lt->c.z); g_pitch=0.10f; if (autoT<=dt) look=g_trapNext; }   // (unattended: stand in the middle, face the one going off)
        autoT-=dt; if (autoT<=0){ autoT=autoGap; TrapSpring(g_trapNext); if (g_trapNext==TR_SPILL) g_traps[TR_SPILL].aux=-1; g_trapNext=(g_trapNext+1)%TR_N; } }
      if (g_traps[TR_SPILL].state==1 && g_traps[TR_SPILL].aux<0 && g_traps[TR_SPILL].t>6.0f){ g_traps[TR_SPILL].aux=0; TrapFire(TR_SPILL); }   // (auto demo: the spill is lit after six seconds)
    }
    run:
    for (int i=0;i<MAX_ENEMIES;i++) if (g_trapSlip[i]>0) g_trapSlip[i]-=dt;
    for (int ti=0;ti<g_nTraps;ti++){ Trap *t=&g_traps[ti]; int k=t->kind;
        if (t->state==2){ t->t-=dt; if (t->t<=0){ t->state=0; t->t=0; } continue; }
        if (t->state==3){ for (int i=0;i<MAX_ENEMIES && t->state==3;i++) if (g_enemies[i].state==1 && TrapInZone(t,&g_enemies[i])){ DebugLog("trap","\"k\":%d,\"what\":\"sprung\",\"by\":%d",ti,i); TrapSpring(ti); } }   // armed: the first one into it sets it off
        if (t->state!=1) continue;
        float pt=t->t; t->t+=dt;
        for (int i=0;i<MAX_ENEMIES;i++){ Enemy *e=&g_enemies[i]; if (e->state!=1) continue;
            float x,z; TL(t,e->pos,&x,&z); Vector3 ep=e->pos;
            switch (k){
            case TR_STEAM: if (fabsf(x)<3.1f && fabsf(z)<1.6f){ TrapHurt(i,48.0f*dt,HC_FLAME,TW(t,x,3.0f,0)); e->limpT=fmaxf(e->limpT,1.2f); } break;
            case TR_PLATE: if (fabsf(x)<2.5f && fabsf(z)<2.5f){ e->stagT=fmaxf(e->stagT,0.35f); e->hitT=fmaxf(e->hitT,0.35f); e->vel=(Vector3){0,0,0};
                               if (floorf(t->t*2.0f)!=floorf(pt*2.0f)){ TrapHurt(i,26.0f,HC_BULLET,TW(t,x,-1.0f,z)); SpawnImpact((Vector3){ep.x,ep.y+1.0f,ep.z},(Vector3){0,1,0}); } } break;
            case TR_HOSE: if (t->t>0.35f && x>-3.6f && x<-3.6f+11.0f*fminf(1.0f,(t->t-0.35f)*4.0f) && fabsf(z)<1.0f) EnemyKnock(i,Vector3Scale(t->tv,7.5f)); break;       // (along the jet)
            case TR_SURGE: if (pt<0.45f && t->t>=0.45f){ float d=sqrtf(x*x+z*z); if (t->hole>=0 && e->climbT>0 && e->hole==t->hole) d=0.0f;   /* still climbing out of the breach it vents */ if (d<1.9f) TrapHurt(i,99999.0f,HC_BLAST,TW(t,0,-1.0f,0)); else if (d<5.5f) TrapHurt(i,85.0f,HC_BLAST,t->c); } break;
            case TR_CRANE: if (t->stage==1 && t->aux==0 && fabsf(x)<1.5f && fabsf(z)<1.5f) TrapHurt(i,99999.0f,HC_BLAST,TW(t,x,6.0f,z)); break;
            case TR_DOOR: if (t->stage==1 && t->aux==0 && fabsf(x)<1.5f && fabsf(z)<0.55f) TrapHurt(i,99999.0f,HC_BLAST,TW(t,x,5.0f,z));
                          else if (t->stage>=1 && t->t<t->run-2.0f && fabsf(x)<1.6f && fabsf(z)<0.75f) EnemyNudge(i,t->rv.x*(z<0?-1:1)*(0.75f-fabsf(z)),t->rv.z*(z<0?-1:1)*(0.75f-fabsf(z))); break;   // shut: nobody through
            case TR_SPILL: { float r=3.3f*fminf(1.0f,t->t*0.5f), d=sqrtf((x+1.0f)*(x+1.0f)+z*z); if (d<r){
                               if (t->stage==1 && t->t-t->aux<6.5f) TrapHurt(i,62.0f*dt,HC_FLAME,TW(t,x,0,z));
                               else if (g_trapSlip[i]<=0 && e->knockT<=0 && Vector3Length(e->mv)>0.6f){ g_trapSlip[i]=4.5f; if (t->cool<=0) t->cool=t->t+1.6f;   /* they are down in it: it lights itself a moment later */ EnemyKnock(i,Vector3Scale(Vector3Normalize(e->mv),2.2f)); } } } break;
            case TR_TURBINE: if (t->t>2.0f && t->t<t->run-2.0f){ float d=sqrtf(x*x+(z-1.0f)*(z-1.0f));                    // its mouth is a metre out from the ring, facing in
                               if (z<1.2f && d<8.0f && fabsf(x)<1.2f+(1.0f-z)*0.75f){
                                   if (d<1.35f) TrapHurt(i,99999.0f,HC_BLAST,TW(t,x,1.5f,z-2.5f));
                                   else { float pull=(2.2f+5.0f*(1.0f-d/8.0f))*dt; Vector3 to=Vector3Subtract(TW(t,0,0,1.0f),ep); to.y=0; to=Vector3Normalize(to); EnemyNudge(i,to.x*pull,to.z*pull); e->limpT=fmaxf(e->limpT,0.4f); } } } break;
            }
        }
        // what each does besides: its one-off moments, its light, its debris
        if (k==TR_STEAM){ if (GetRandomValue(0,99)<(int)(900*dt)) SpawnSmoke(TW(t,GetRandomValue(-28,28)/10.0f,0.3f,GetRandomValue(-10,10)/10.0f),1); }
        if (k==TR_PLATE){ float f=0.5f+GetRandomValue(0,100)/100.0f; DynLightAdd(TW(t,0,1.0f,0),(Vector3){0.5f*f,0.8f*f,1.6f*f},9.0f,0.06f,1); }
        if (k==TR_SURGE){ float f=fmaxf(0.0f,1.0f-t->t/t->run)*3.0f; DynLightAdd(TW(t,0,2.0f,0),(Vector3){0.35f*f,0.7f*f,1.3f*f},16.0f,0.06f,1);
            if (pt<0.45f && t->t>=0.45f){ g_shake=fminf(1.0f,g_shake+0.6f); for (int q=0;q<14;q++) SpawnSmoke(TW(t,GetRandomValue(-15,15)/10.0f,0.2f,GetRandomValue(-15,15)/10.0f),0); } }
        if (k==TR_CRANE){ float y=CraneTop(t)-0.5f*24.0f*t->t*t->t;                                    // the load: falls, lies, is winched back
            if (t->stage==0 && y<=0.9f){ t->stage=1; t->aux=0; g_shake=1.0f; TrapSound(TS_SLAM,(Vector3){t->c.x,t->c.y+0.8f,t->c.z},1.0f);
                for (int q=0;q<22;q++) SpawnSmoke(TW(t,GetRandomValue(-22,22)/10.0f,0.15f,GetRandomValue(-22,22)/10.0f),0);
                for (int q=0;q<10;q++) SpawnImpact(TW(t,GetRandomValue(-14,14)/10.0f,0.1f,GetRandomValue(-14,14)/10.0f),(Vector3){0,1,0}); }
            else if (t->stage==1) t->aux=1; }
        if (k==TR_DOOR){ if (t->stage==0 && t->t>=0.16f){ t->stage=1; t->aux=0; g_shake=fminf(1.0f,g_shake+0.7f);
                for (int q=0;q<12;q++) SpawnSmoke(TW(t,GetRandomValue(-15,15)/10.0f,0.15f,GetRandomValue(-6,6)/10.0f),0);
                for (int q=0;q<6;q++) SpawnImpact(TW(t,GetRandomValue(-14,14)/10.0f,0.05f,0),(Vector3){0,1,0}); }
            else if (t->stage==1) t->aux=1; }
        if (k==TR_SPILL && t->stage==0 && t->cool>0 && t->t>=t->cool){ t->stage=1; t->aux=t->t; DebugLog("trap","\"k\":%d,\"what\":\"ignite\",\"auto\":1",ti); TrapSound(TS_IGNITE,TW(t,-1.0f,0.5f,0),1.0f); }
        if (k==TR_SPILL){ if (!(t->stage==1) && t->t<t->run-1.0f) DynLightAdd(TW(t,-1.0f,0.5f,0),(Vector3){0.10f,0.55f,0.46f},6.5f,0.06f,1);   /* its own cold light on what stands in it */
            if (t->stage==1 && t->t-t->aux<6.5f){ float r=3.3f*fminf(1.0f,t->t*0.5f);
                for (int q=0;q<3;q++){ float a=GetRandomValue(0,628)/100.0f, rr=r*sqrtf(GetRandomValue(0,100)/100.0f); SpawnFlame(TW(t,-1.0f+cosf(a)*rr,0.1f,sinf(a)*rr),(Vector3){0,1,0}); }
                DynLightAdd(TW(t,-1.0f,1.0f,0),(Vector3){1.6f,0.8f,0.25f},11.0f,0.06f,1); } }
        if (k==TR_TURBINE && t->t>2.0f && t->t<t->run-2.0f && GetRandomValue(0,99)<(int)(500*dt)) SpawnImpact(TW(t,GetRandomValue(-10,10)/10.0f,1.5f+GetRandomValue(-8,8)/10.0f,1.1f),Vector3Negate(t->rv));
        if (t->t>=t->run){ t->state=2; t->t=g_trapDemo?3.0f:kTrapCool[k]; t->stage=0; t->aux=0; }
    }
}
// ---- The coolant. Not a disc: a liquid. One quad over the floor, and everything else in the fragment shader --
// a pool whose edge creeps out in lobes, thicker and darker toward the middle; a surface that moves (two layers of
// slow swell, rings running out from where the stream lands) and so throws the lamps back as hard glints, more at
// grazing angles; a bright meniscus at the edge and a wet halo on the floor beyond it; a faint glow of its own.
// Lit, it turns to burning oil. The stream, the splash and the vapour are drawn round it (TrapsDraw).
static Shader g_poolSh; static Model g_poolM; static int g_poolOK=-1, g_poolLoc[10];
static void PoolInit(void){
    const char *vs=GLSL_HDR
        "in vec3 vertexPosition; uniform mat4 mvp; uniform mat4 matModel; out vec3 wp;\n"
        "void main(){ wp=(matModel*vec4(vertexPosition,1.0)).xyz; gl_Position=mvp*vec4(vertexPosition,1.0); }\n";
    const char *fs=GLSL_HDR
        "in vec3 wp; out vec4 finalColor;\n"
        "uniform vec3 centre; uniform vec3 src; uniform vec3 viewPos; uniform float radius; uniform float time; uniform float fade; uniform float lit; uniform float gush;\n"
        "uniform vec3 lpos[6]; uniform vec3 lcol[6]; uniform int ln; uniform vec4 holes[3]; uniform int hn;\n"
        "float h2(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }\n"
        "float vn(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
        "  return mix(mix(h2(i),h2(i+vec2(1,0)),f.x),mix(h2(i+vec2(0,1)),h2(i+vec2(1,1)),f.x),f.y); }\n"
        "float fbm(vec2 p){ return 0.55*vn(p)+0.28*vn(p*2.1+3.7)+0.17*vn(p*4.3+9.1); }\n"
        "float surf(vec2 p){ float d=length(p-src.xz);\n"
        "  return 0.050*fbm(p*2.2+vec2(time*0.16,time*0.11)) + 0.022*fbm(p*5.5-vec2(time*0.31,-time*0.24))\n"
        "       + 0.030*gush*sin(d*13.0-time*9.5)*exp(-d*1.15); }\n"
        "void main(){\n"
        "  for (int i=0;i<3;i++){ if (i>=hn) break; if (abs(wp.x-holes[i].x)<holes[i].z && abs(wp.z-holes[i].y)<holes[i].w) discard; }\n"   // a breach in the floor: there is no floor to lie on -- it runs over the lip and is gone
        "  vec2 p=wp.xz-centre.xz; float d=length(p); vec2 dir=p/max(d,1e-4);\n"
        "  float R=radius*(0.74+0.34*fbm(dir*1.7+centre.xz*0.37)+0.07*vn(p*1.9));\n"          // the edge: lobed, the same lobes all the way out
        "  float inside=smoothstep(R,R-0.07,d), halo=(1.0-smoothstep(R,R+0.30,d))*(1.0-inside);\n"
        "  if (inside+halo<0.004) discard;\n"
        "  float depth=clamp((R-d)/1.3,0.0,1.0);\n"
        "  float e=0.035; vec2 q=wp.xz; float h0=surf(q), hx=surf(q+vec2(e,0.0)), hz=surf(q+vec2(0.0,e));\n"
        "  vec3 N=normalize(vec3(-(hx-h0)/e*(0.35+0.65*depth), 1.0, -(hz-h0)/e*(0.35+0.65*depth)));\n"
        "  vec3 V=normalize(viewPos-wp); float ndv=max(dot(N,V),0.0), fres=0.035+0.965*pow(1.0-ndv,5.0);\n"
        "  vec3 shallow=vec3(0.10,0.58,0.50), deep=vec3(0.005,0.17,0.17);\n"
        "  vec3 body=mix(shallow,deep,smoothstep(0.0,0.85,depth));\n"
        "  vec3 dif=vec3(0.30,0.33,0.34), spe=vec3(0.0);\n"
        "  for (int i=0;i<6;i++){ if (i>=ln) break; vec3 dl=lpos[i]-wp; float dd=length(dl); vec3 L=dl/max(dd,1e-4); float att=max(0.0,1.0-dd/16.0); att*=att;\n"
        "    dif+=lcol[i]*max(dot(N,L),0.0)*att*0.9; vec3 H=normalize(L+V);\n"
        "    spe+=lcol[i]*att*(pow(max(dot(N,H),0.0),260.0)*5.0+pow(max(dot(N,H),0.0),28.0)*0.22); }\n"
        "  vec3 env=mix(vec3(0.10,0.13,0.15),vec3(0.42,0.50,0.54),clamp(N.y*0.5+0.5+0.6*(hx-h0)/e,0.0,1.0));\n"       // what it throws back of the room
        "  float glow=(0.16+0.07*sin(time*1.6+fbm(p*1.5)*6.0))*smoothstep(0.0,0.6,depth);\n"
        "  vec3 c=body*dif + vec3(0.03,0.42,0.35)*glow;\n"
        "  c=mix(c,env,fres*0.85) + spe;\n"
        "  float men=smoothstep(R-0.11,R-0.035,d)*(1.0-smoothstep(R-0.035,R,d));\n"                                   // the meniscus: a bright line just inside the edge
        "  c+=vec3(0.55,0.95,0.88)*men*0.35;\n"
        "  float foam=smoothstep(0.55,0.85,fbm(q*9.0+time*vec2(0.6,-0.4)))*exp(-length(q-src.xz)*2.4)*gush;\n"            // churned white where the stream lands
        "  c=mix(c,vec3(0.80,0.97,0.93),clamp(foam*1.4,0.0,0.85));\n"
        "  if (lit>0.0){ float fl=fbm(q*3.0+vec2(time*1.9,-time*1.3)); float fl2=fbm(q*7.0-vec2(time*2.6,time*2.1));\n"   // alight: black oil, fire crawling on it
        "    vec3 oil=vec3(0.035,0.022,0.016)*dif+spe*0.6+env*fres*0.5;\n"
        "    vec3 fire=vec3(1.0,0.42,0.06)*smoothstep(0.35,0.75,fl)*1.5+vec3(1.0,0.85,0.35)*smoothstep(0.62,0.9,fl*fl2*1.9);\n"
        "    c=mix(c,oil+fire*(0.4+0.6*depth),lit); }\n"
        "  float a=inside*(0.80+0.18*fres)*(0.55+0.45*smoothstep(0.0,0.25,depth)) + halo*0.38;\n"
        "  if (inside<0.5) c=mix(vec3(0.015,0.03,0.03),c,inside);\n"                                                  // the wet halo: just the floor, darkened
        "  finalColor=vec4(c,clamp(a*fade,0.0,1.0));\n"
        "}\n";
    g_poolSh=LoadShaderFromMemory(vs,fs); g_poolOK=(g_poolSh.id>0 && g_poolSh.id!=rlGetShaderIdDefault());
    if (!g_poolOK) return;
    static const char *un[10]={"centre","src","viewPos","radius","time","fade","lit","gush","lpos","lcol"};
    for (int i=0;i<10;i++) g_poolLoc[i]=GetShaderLocation(g_poolSh,un[i]);
    g_poolM=LoadModelFromMesh(GenMeshPlane(1.0f,1.0f,1,1)); g_poolM.materials[0].shader=g_poolSh;
}
// Where the pool meets a breach it goes over the lip as a FALL, not as dots: a sheet hung from the lip down the shaft wall,
// bowing out as it drops, drawn by a shader -- whole and glassy where it curls over the edge, tearing into strands a
// metre down and into drops below that (the noise it is cut from runs downward, faster the further it has fallen), with
// white foam at the lip, hard glints riding down it, and its own cold light. Lit, it falls as fire.
static Shader g_fallSh; static int g_fallOK=-1, g_fallLoc[2];
static void CoolantFalls(Vector3 c, float r, float fade, float lit, float tm){
    if (g_fallOK<0){
        const char *vs=GLSL_HDR
            "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor; uniform mat4 mvp; out vec2 uv; out float cov;\n"
            "void main(){ uv=vertexTexCoord; cov=vertexColor.a; gl_Position=mvp*vec4(vertexPosition,1.0); }\n";
        const char *fs=GLSL_HDR
            "in vec2 uv; in float cov; out vec4 finalColor; uniform float time; uniform float lit;\n"
            "float h2(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }\n"
            "float vn(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
            "  return mix(mix(h2(i),h2(i+vec2(1,0)),f.x),mix(h2(i+vec2(0,1)),h2(i+vec2(1,1)),f.x),f.y); }\n"
            "float fbm(vec2 p){ return 0.55*vn(p)+0.28*vn(p*2.1+3.7)+0.17*vn(p*4.3+9.1); }\n"
            "void main(){\n"
            "  float u=uv.x, v=uv.y, sp=1.7+1.3*v;\n"                                                       // it speeds up as it falls
            "  float a1=fbm(vec2(u*5.0, v*0.9-time*sp)), a2=fbm(vec2(u*14.0+3.1, v*1.6-time*sp*1.6));\n"
            "  float body=a1*0.62+a2*0.38;\n"
            "  float th=mix(0.30,0.70,clamp(v/2.0,0.0,1.0));\n"                                              // how much of it is still sheet at this depth
            "  float m=smoothstep(th,th+0.12,body);\n"
            "  float lip=1.0-smoothstep(0.0,0.14,v); m=max(m,lip*0.92);\n"                                    // the curl over the edge: unbroken
            "  vec3 col=mix(vec3(0.015,0.27,0.25),vec3(0.14,0.72,0.63),smoothstep(th,th+0.45,body));\n"
            "  float gl=pow(fbm(vec2(u*30.0, v*2.2-time*sp*2.4)),7.0)*1.5;\n"
            "  col+=vec3(0.70,1.0,0.94)*gl + vec3(0.02,0.22,0.18)*0.5;\n"
            "  col+=vec3(0.82,1.0,0.96)*lip*0.45*smoothstep(0.45,0.85,a2);\n"                                 // foam where it breaks over
            "  if (lit>0.0) col=mix(col, vec3(1.0,0.46,0.08)*(0.55+1.5*body)+vec3(1.0,0.9,0.5)*gl, lit);\n"
            "  finalColor=vec4(col, m*cov*(1.0-smoothstep(2.3,3.3,v))*0.94);\n"
            "}\n";
        g_fallSh=LoadShaderFromMemory(vs,fs); g_fallOK=(g_fallSh.id>0 && g_fallSh.id!=rlGetShaderIdDefault());
        if (g_fallOK){ g_fallLoc[0]=GetShaderLocation(g_fallSh,"time"); g_fallLoc[1]=GetShaderLocation(g_fallSh,"lit"); }
    }
    for (int i=0;i<g_nMapHoles;i++){ const MapHole *H=&g_mapHoles[i];
        if (fabsf(H->pos.x-c.x)>r+H->hx || fabsf(H->pos.z-c.z)>r+H->hz) continue;
        float D=fminf(H->depth,3.3f);
        if (g_fallOK==1){
            SetShaderValue(g_fallSh,g_fallLoc[0],&tm,SHADER_UNIFORM_FLOAT); SetShaderValue(g_fallSh,g_fallLoc[1],&lit,SHADER_UNIFORM_FLOAT);
            rlDrawRenderBatchActive(); BeginShaderMode(g_fallSh); rlDisableBackfaceCulling(); rlSetTexture(rlGetTextureIdDefault()); rlBegin(RL_QUADS);
            float run=0.0f;
            for (int sd=0;sd<4;sd++){ float L=(sd<2)?H->hz:H->hx, nx=(sd==0)?-1.0f:(sd==1)?1.0f:0.0f, nz=(sd==2)?-1.0f:(sd==3)?1.0f:0.0f;      // this side's outward direction
                int ns=(int)(2.0f*L/0.11f)+1;
                for (int q=0;q<ns;q++){ float f0=-L+2.0f*L*q/ns, f1=-L+2.0f*L*(q+1)/ns;
                    Vector3 A={H->pos.x+nx*H->hx+(nz!=0?f0:0.0f), H->pos.y+0.016f, H->pos.z+nz*H->hz+(nx!=0?f0:0.0f)}, B={H->pos.x+nx*H->hx+(nz!=0?f1:0.0f), H->pos.y+0.016f, H->pos.z+nz*H->hz+(nx!=0?f1:0.0f)};
                    float ca=Clamp((r*0.90f-hypotf(A.x-c.x,A.z-c.z))/0.45f,0.0f,1.0f), cb=Clamp((r*0.90f-hypotf(B.x-c.x,B.z-c.z))/0.45f,0.0f,1.0f);   // only the stretch of lip the pool has reached
                    float u0=run+2.0f*L*q/ns+i*3.7f, u1=run+2.0f*L*(q+1)/ns+i*3.7f;
                    if (ca>0.0f || cb>0.0f) for (int k=0;k<5;k++){ float v0=D*k/5.0f, v1=D*(k+1)/5.0f, in0=0.02f+0.26f*(v0/D)*(v0/D), in1=0.02f+0.26f*(v1/D)*(v1/D);   // bowing away from the wall as it drops
                        rlColor4f(1,1,1,ca*fade); rlTexCoord2f(u0,v0); rlVertex3f(A.x-nx*in0,A.y-v0,A.z-nz*in0);
                        rlColor4f(1,1,1,cb*fade); rlTexCoord2f(u1,v0); rlVertex3f(B.x-nx*in0,B.y-v0,B.z-nz*in0);
                        rlColor4f(1,1,1,cb*fade); rlTexCoord2f(u1,v1); rlVertex3f(B.x-nx*in1,B.y-v1,B.z-nz*in1);
                        rlColor4f(1,1,1,ca*fade); rlTexCoord2f(u0,v1); rlVertex3f(A.x-nx*in1,A.y-v1,A.z-nz*in1); } }
                run+=2.0f*L; }
            rlEnd(); rlDrawRenderBatchActive(); rlEnableBackfaceCulling(); EndShaderMode();
        }
        if (g_hasSmokeTex) for (int n=0;n<22;n++){ float u=(n+0.5f)/22.0f*4.0f; int sd=(int)u; float f=(u-sd)*2.0f-1.0f;        // and the drops thrown clear of it, catching the light
            float lx=(sd==0)?-H->hx:(sd==1)?H->hx:f*H->hx, lz=(sd==2)?-H->hz:(sd==3)?H->hz:f*H->hz; Vector3 lp={H->pos.x+lx,H->pos.y,H->pos.z+lz};
            if (hypotf(lp.x-c.x,lp.z-c.z)>r*0.86f) continue;
            float s1=sinf(n*12.9898f+i)*43758.547f, r1=s1-floorf(s1), ph=fmodf(tm*(0.8f+0.5f*r1)+r1,1.0f), in=0.10f+0.35f*ph;
            Vector3 p={lp.x-(sd<2?lx/H->hx:0.0f)*in, lp.y-ph*ph*D, lp.z-(sd>=2?lz/H->hz:0.0f)*in};
            DrawBillboard(g_cam,g_smokeTex,p,0.035f+0.03f*r1,(lit>0.5f)?(Color){255,170,70,(unsigned char)(210*(1.0f-ph)*fade)}:(Color){200,255,245,(unsigned char)(210*(1.0f-ph)*fade)}); }
    }
}
static float SpillGush(float on){ return (on<0.25f)?on*4.0f:(on<3.2f)?1.0f:fmaxf(0.22f,1.0f-(on-3.2f)*0.3f); }      // the outlet: bursts, runs hard, falls to a dribble
static void DrawCoolant(const Trap *t){                                              // (called with the depth mask off, before the billboards)
    if (g_poolOK<0) PoolInit();
    float on=t->t, tm=(float)GetTime(), r=3.45f*fminf(1.0f,on*0.5f)*(0.35f+0.65f*fminf(1.0f,on*0.5f)), fade=fminf(1.0f,(t->run-on)/2.5f);
    float gush=(on<t->run-3.0f)?SpillGush(on):0.0f, lit=(t->stage==1)?fminf(1.0f,(on-t->aux)*1.5f)*fminf(1.0f,fmaxf(0.0f,(t->aux+7.5f-on)*0.8f)):0.0f;
    Vector3 c=TW(t,-1.0f,0.014f,0), out=TW(t,-2.26f,0.50f,0.70f), land=TW(t,-1.72f,0.0f,0.42f);
    if (g_poolOK==1){
        float lp[18]={0}, lc[18]={0}; int n=0;
        for (int i=0;i<g_atm.n && n<6;i++){ lp[n*3]=g_atm.pos[i*3]; lp[n*3+1]=g_atm.pos[i*3+1]; lp[n*3+2]=g_atm.pos[i*3+2]; lc[n*3]=g_atm.col[i*3]; lc[n*3+1]=g_atm.col[i*3+1]; lc[n*3+2]=g_atm.col[i*3+2]; n++; }
        if (n==0){ lp[0]=c.x+2.5f; lp[1]=c.y+4.0f; lp[2]=c.z+1.5f; lc[0]=lc[1]=lc[2]=1.0f; n=1; }                                  // (nothing lit nearby: one lamp overhead, so it still reads as wet)
        SetShaderValue(g_poolSh,g_poolLoc[0],&c,SHADER_UNIFORM_VEC3); SetShaderValue(g_poolSh,g_poolLoc[1],&land,SHADER_UNIFORM_VEC3); SetShaderValue(g_poolSh,g_poolLoc[2],&g_cam.position,SHADER_UNIFORM_VEC3);
        SetShaderValue(g_poolSh,g_poolLoc[3],&r,SHADER_UNIFORM_FLOAT); SetShaderValue(g_poolSh,g_poolLoc[4],&tm,SHADER_UNIFORM_FLOAT); SetShaderValue(g_poolSh,g_poolLoc[5],&fade,SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_poolSh,g_poolLoc[6],&lit,SHADER_UNIFORM_FLOAT); SetShaderValue(g_poolSh,g_poolLoc[7],&gush,SHADER_UNIFORM_FLOAT);
        SetShaderValueV(g_poolSh,g_poolLoc[8],lp,SHADER_UNIFORM_VEC3,6); SetShaderValueV(g_poolSh,g_poolLoc[9],lc,SHADER_UNIFORM_VEC3,6);
        { int ln=n, loc=GetShaderLocation(g_poolSh,"ln"); SetShaderValue(g_poolSh,loc,&ln,SHADER_UNIFORM_INT); }
        { float hv[12]={0}; int hn=0;                                               // the breaches it reaches: cut out of the pool
          for (int i=0;i<g_nMapHoles && hn<3;i++){ const MapHole *H=&g_mapHoles[i]; if (fabsf(H->pos.x-c.x)>r+H->hx+0.5f || fabsf(H->pos.z-c.z)>r+H->hz+0.5f) continue;
              hv[hn*4]=H->pos.x; hv[hn*4+1]=H->pos.z; hv[hn*4+2]=H->hx; hv[hn*4+3]=H->hz; hn++; }
          SetShaderValueV(g_poolSh,GetShaderLocation(g_poolSh,"holes"),hv,SHADER_UNIFORM_VEC4,3); SetShaderValue(g_poolSh,GetShaderLocation(g_poolSh,"hn"),&hn,SHADER_UNIFORM_INT); }
        rlDrawRenderBatchActive(); DrawModelEx(g_poolM,c,(Vector3){0,1,0},0.0f,(Vector3){10.0f,1.0f,10.0f},WHITE);
    } else DrawCylinder(c,r,r,0.004f,32,(Color){30,150,130,(unsigned char)(170*fade)});
    if (!g_hasSmokeTex) return;
    if (gush>0.0f){                                                                  // the stream: out of the burst outlet in an arc, breaking up as it falls
        for (int n=0;n<26;n++){ float u=fmodf(tm*2.3f+n/26.0f,1.0f), w=0.5f+0.5f*sinf(n*12.9898f);
            Vector3 p={out.x+(land.x-out.x)*u+0.03f*sinf(tm*21.0f+n)*u, out.y*(1.0f-u*u)+land.y+0.03f, out.z+(land.z-out.z)*u+0.03f*cosf(tm*17.0f+n*1.7f)*u};
            p.y=land.y+(out.y-land.y)*(1.0f-u*u)+0.02f;
            float sz=(0.10f+0.13f*u)*(0.7f+0.5f*w)*(0.5f+0.5f*gush);
            DrawBillboard(g_cam,g_smokeTex,p,sz*1.5f,(Color){20,120,105,(unsigned char)(150*gush)});
            DrawBillboard(g_cam,g_smokeTex,p,sz,(Color){150,245,225,(unsigned char)(215*gush)}); }
        for (int n=0;n<18;n++){ float s1=sinf(n*78.233f)*12543.123f, r1=s1-floorf(s1), u=fmodf(tm*(1.6f+r1)+r1*3.0f,1.0f), a=n*2.399f, sp=(0.25f+0.55f*r1)*gush;      // what it throws up where it lands
            Vector3 p={land.x+cosf(a)*sp*u, land.y+0.03f+(0.5f+0.5f*r1)*0.55f*gush*4.0f*u*(1.0f-u), land.z+sinf(a)*sp*u};
            DrawBillboard(g_cam,g_smokeTex,p,0.05f+0.05f*r1,(Color){185,250,235,(unsigned char)(200*(1.0f-u)*gush)}); }
    }
    CoolantFalls(c,r,fade,lit,tm);
    if (lit<=0.01f) for (int n=0;n<9;n++){ float s1=sinf(n*12.9898f)*43758.547f, r1=s1-floorf(s1), ph=fmodf(tm*(0.10f+0.07f*r1)+r1,1.0f), a=n*2.4f, rr=r*0.7f*sqrtf(r1);   // vapour coming off it, slow
        DrawBillboard(g_cam,g_smokeTex,(Vector3){c.x+cosf(a)*rr+0.2f*sinf(tm*0.4f+n),c.y+0.15f+ph*1.5f,c.z+sinf(a)*rr},1.0f+ph*1.4f,(Color){120,215,195,(unsigned char)(34*sinf(ph*PI)*fade*fminf(1.0f,on*0.4f))}); }
}
static void TrapBox(Vector3 p, Vector3 sz, Color c){ DrawCubeV(p,sz,c); DrawCubeWiresV(p,sz,(Color){(unsigned char)(c.r/2),(unsigned char)(c.g/2),(unsigned char)(c.b/2),255}); }
// The modelled parts (tools/traps/build_traps.py -> assets/traps/*.gltf): bevelled geometry, weathered paint with normal
// and roughness maps, drawn through the enemies' lit shader (map lamps, flashes, fog) with no bones. Any that is missing
// falls back to the plain boxes below.
enum { TM_POST, TM_STEAM, TM_WHEEL, TM_PLATE, TM_HOSE, TM_SURGE, TM_CRANE, TM_LOAD, TM_DOORF, TM_DOORS, TM_TANK, TM_TURB, TM_ROTOR, TM_N };
static const char *kTrapModel[TM_N]={"post","steam_frame","steam_wheel","plate","hose","surge","crane_frame","crane_load","door_frame","door_slab","spill_tank","turbine_body","turbine_rotor"};
static Model g_trapM[TM_N]; static int g_trapMok[TM_N], g_trapMtried=0;
// The parts share twelve materials, each three images (assets/traps/<material>_c / _n / _orm.png). raylib's glTF loader
// reads a part's own copy of the colour image and then looks for the normal and roughness images in the wrong folder
// ("./assets/..._n.png: failed to open") -- so they were never applied, and every part held duplicate colour textures.
// The images are loaded here instead, once each, and handed to every part by its materials' names (read from the .gltf).
typedef struct { char name[24]; Texture2D c, n, o; } TrapTex;
static TrapTex g_trapTex[20]; static int g_nTrapTex=0;
static Texture2D TrapTexLoad(const char *path){ Texture2D t={0}; char alt[128];
    if (!FileExists(path)){ snprintf(alt,sizeof alt,"%s",path); char *d=strrchr(alt,'.'); if (d) strcpy(d,".jpg"); if (!FileExists(alt)) return t; path=alt; }   // (the browser pack carries them as .jpg)
    t=LoadTexture(path);
    if (t.id>0){ GenTextureMipmaps(&t); SetTextureFilter(t,TEXTURE_FILTER_ANISOTROPIC_8X); SetTextureWrap(t,TEXTURE_WRAP_REPEAT); } return t; }
static const TrapTex *TrapTexOf(const char *name){
    for (int i=0;i<g_nTrapTex;i++) if (!strcmp(g_trapTex[i].name,name)) return &g_trapTex[i];
    if (g_nTrapTex>=20) return NULL;
    TrapTex *t=&g_trapTex[g_nTrapTex++]; snprintf(t->name,sizeof t->name,"%s",name);
    t->c=TrapTexLoad(TextFormat("assets/traps/%s_c.png",name)); t->n=TrapTexLoad(TextFormat("assets/traps/%s_n.png",name)); t->o=TrapTexLoad(TextFormat("assets/traps/%s_orm.png",name));
    return t; }
static void TrapsLoadModels(void){
    g_trapMtried=1; if (EnemySkinInit()!=1) return;
    Image a=GenImageColor(1,1,(Color){128,128,255,255}), b=GenImageColor(1,1,(Color){255,204,0,255}), c=GenImageColor(1,1,(Color){0,0,0,255});
    Texture2D flatN=LoadTextureFromImage(a), defOrm=LoadTextureFromImage(b), noEmi=LoadTextureFromImage(c); UnloadImage(a); UnloadImage(b); UnloadImage(c);
    for (int k=0;k<TM_N;k++){ char path[96]; snprintf(path,sizeof path,"assets/traps/%s.gltf",kTrapModel[k]); if (!FileExists(path)) continue;   // (its own buffer: TextFormat's are reused by the texture loads below)
        SetTraceLogLevel(LOG_ERROR); Model m=LoadModel(path); SetTraceLogLevel(LOG_WARNING);      // (its own look for the normal / roughness images fails, loudly and harmlessly: they are loaded just below)
        if (m.meshCount<=0) continue;
        { char *txt=LoadFileText(path); int mi=1;                                     // its materials, by name, in the file's order (raylib keeps its default material at 0)
          if (txt){ char *a=strstr(txt,"\"materials\""), *z=a?strstr(a,"\"meshes\""):NULL;
              for (char *q=a; q && z && (q=strstr(q,"\"name\""))!=NULL && q<z && mi<m.materialCount; mi++){
                  char nm[24]={0}; char *v=strchr(q+6,'"'); if (!v) break; v++; int n=0; while (v[n] && v[n]!='"' && n<23){ nm[n]=v[n]; n++; } q=v+n;
                  const TrapTex *tt=TrapTexOf(nm); MaterialMap *mm=m.materials[mi].maps; if (!tt) continue;
                  if (tt->c.id>0){ if (mm[MATERIAL_MAP_DIFFUSE].texture.id>0 && mm[MATERIAL_MAP_DIFFUSE].texture.id!=rlGetTextureIdDefault()) UnloadTexture(mm[MATERIAL_MAP_DIFFUSE].texture); mm[MATERIAL_MAP_DIFFUSE].texture=tt->c; }
                  if (tt->n.id>0) mm[MATERIAL_MAP_NORMAL].texture=tt->n;
                  if (tt->o.id>0) mm[MATERIAL_MAP_OCCLUSION].texture=tt->o; }
              UnloadFileText(txt); } }
        for (int i=0;i<m.meshCount;i++){ Mesh *ms=&m.meshes[i]; if (!ms->tangents && ms->texcoords && ms->normals) GenMeshTangents(ms); }
        for (int i=0;i<m.materialCount;i++){ MaterialMap *mm=m.materials[i].maps; m.materials[i].shader=g_skinShader;
            if (mm[MATERIAL_MAP_NORMAL].texture.id==0) mm[MATERIAL_MAP_NORMAL].texture=flatN;
            if (mm[MATERIAL_MAP_OCCLUSION].texture.id==0) mm[MATERIAL_MAP_OCCLUSION].texture=defOrm;
            if (mm[MATERIAL_MAP_EMISSION].texture.id==0 || mm[MATERIAL_MAP_EMISSION].texture.id==rlGetTextureIdDefault()) mm[MATERIAL_MAP_EMISSION].texture=noEmi;
            int ids[3]={MATERIAL_MAP_DIFFUSE,MATERIAL_MAP_NORMAL,MATERIAL_MAP_OCCLUSION};
            for (int q=0;q<3;q++){ Texture2D *t=&mm[ids[q]].texture; if (t->id>0 && t->width>1 && t->id!=rlGetTextureIdDefault() && t->mipmaps<=1){ GenTextureMipmaps(t); SetTextureFilter(*t,TEXTURE_FILTER_ANISOTROPIC_8X); SetTextureWrap(*t,TEXTURE_WRAP_REPEAT); } } }
        g_trapM[k]=m; g_trapMok[k]=1; }
    int n=0; for (int k=0;k<TM_N;k++) n+=g_trapMok[k]; DebugLog("trapmodels","\"loaded\":%d,\"of\":%d",n,TM_N);
}
static int TrapPart(const Trap *t, int m, Matrix local){                // draw part m of trap t, placed by `local` in the trap's own frame; 0 if it has no model
    if (!g_trapMok[m]) return 0;
    static const float zero[4]={0,0,0,0};                               // (no bones: the skinning inputs read as none)
    rlSetVertexAttributeDefault(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEWEIGHTS,zero,SHADER_ATTRIB_VEC4,4); rlSetVertexAttributeDefault(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES,zero,SHADER_ATTRIB_VEC4,4);
    Matrix W=MatrixMultiply(local,MatrixMultiply(MatrixRotateY((90.0f-t->ang*RAD2DEG)*DEG2RAD),MatrixTranslate(t->c.x,t->c.y,t->c.z)));
    g_trapM[m].transform=W; DrawModel(g_trapM[m],(Vector3){0,0,0},1.0f,WHITE); return 1;
}
static void TrapsDraw(void){
    if (g_nTraps<=0) return;
    if (!g_trapMtried) TrapsLoadModels();
    float tm=(float)GetTime();
    const Color steel={86,90,98,255}, dark={44,46,52,255}, red={170,34,28,255}, haz={214,170,40,255};
    rlDrawRenderBatchActive();
    for (int ti=0;ti<g_nTraps;ti++){ Trap *t=&g_traps[ti]; int k=t->kind; float on=(t->state==1)?t->t:-1.0f; Matrix I=MatrixIdentity();   // ---- the modelled parts
        { Vector3 to=Vector3Subtract((Vector3){t->c.x,t->c.y+1.5f,t->c.z},g_cam.position), fw=Vector3Normalize(Vector3Subtract(g_cam.target,g_cam.position)); float dist=Vector3Length(to);
          if (dist>60.0f || (dist>9.0f && Vector3DotProduct(to,fw)<dist*0.25f)) continue; }        // far off, or well outside the view: not drawn (and not lit: finding its lamps costs a ray each)
        EnemyLightUpload(t->c); EnemyGlowDim(0.0f);                                              // one set of lamps for the whole trap
        int mp=TrapPart(t,TM_POST,MatrixTranslate(kTrapSwitchX[k],0,kTrapSwitchZ[k])), mb=0;
        switch (k){
        case TR_STEAM: mb=TrapPart(t,TM_STEAM,I); TrapPart(t,TM_WHEEL,MatrixMultiply(MatrixRotateZ(((on>=0)?fminf(on,1.5f)*240.0f:0.0f)*DEG2RAD),MatrixTranslate(3.4f,1.2f,-0.2f))); break;
        case TR_PLATE: mb=TrapPart(t,TM_PLATE,I); break;
        case TR_HOSE:  mb=TrapPart(t,TM_HOSE,I); break;
        case TR_SURGE: mb=(t->hole>=0)?1:TrapPart(t,TM_SURGE,I); break;                 // (in the plant it vents a real breach, which has its own rim)
        case TR_CRANE: { float y=CraneY(t,on);
            mb=TrapPart(t,TM_CRANE,MatrixScale(1.0f,t->ys,1.0f)); TrapPart(t,TM_LOAD,MatrixTranslate(0,y,0)); } break;
        case TR_DOOR: { float h=3.9f; if (on>=0){ float dn=fminf(1.0f,on/0.16f); float up=(on-(t->run-2.0f))/2.0f; h=3.9f*(1.0f-dn*dn); if (up>0) h=3.9f*up; }
            mb=TrapPart(t,TM_DOORF,I); TrapPart(t,TM_DOORS,MatrixTranslate(0,h,0)); } break;
        case TR_SPILL: mb=TrapPart(t,TM_TANK,MatrixTranslate(-3.6f,0,0.9f)); break;
        case TR_TURBINE: { float w=0; if (on>=0) w=(on<2.0f)?on/2.0f:(on>t->run-2.0f)?(t->run-on)/2.0f:1.0f; t->aux+=w*1500.0f*GetFrameTime();
            mb=TrapPart(t,TM_TURB,I); TrapPart(t,TM_ROTOR,MatrixMultiply(MatrixRotateZ(t->aux*DEG2RAD),MatrixTranslate(0,1.6f,0.98f))); } break;
        }
        // ---- what stays drawn by hand: the lamp, the arcs, rings, pools and cables -- and the plain boxes where a model is missing
        rlPushMatrix(); rlTranslatef(t->c.x,t->c.y,t->c.z); rlRotatef(90.0f-t->ang*RAD2DEG,0,1,0);          // local x = tv (across), z = rv (outward)
        { float sx=kTrapSwitchX[k], sz=kTrapSwitchZ[k]; Color lamp=(t->state==0)?(Color){60,230,90,255}:(t->state==1)?(Color){255,70,50,255}:(t->state==3)?((fmodf(tm,0.5f)<0.3f)?(Color){255,190,40,255}:(Color){120,80,10,255}):(Color){90,90,90,255};   // green: can be armed; amber, blinking: armed; red: going off; grey: recharging
          if (!mp){ TrapBox((Vector3){sx,0.55f,sz},(Vector3){0.14f,1.1f,0.14f},dark); TrapBox((Vector3){sx,1.2f,sz},(Vector3){0.34f,0.3f,0.22f},(k==TR_HOSE)?red:steel); }
          DrawSphere((Vector3){sx,1.47f,sz},0.055f,lamp); }
        switch (k){
        case TR_STEAM: if (!mb){ TrapBox((Vector3){-3.3f,1.7f,0},(Vector3){0.16f,3.4f,0.16f},steel); TrapBox((Vector3){3.3f,1.7f,0},(Vector3){0.16f,3.4f,0.16f},steel);
            DrawCylinderEx((Vector3){-3.4f,3.3f,0},(Vector3){3.4f,3.3f,0},0.14f,0.14f,10,(Color){110,112,118,255}); } break;
        case TR_PLATE: { if (!mb) TrapBox((Vector3){0,0.025f,0},(Vector3){5.0f,0.05f,5.0f},(Color){58,60,68,255});
            if (on>=0) for (int s=0;s<14;s++){ Vector3 a={GetRandomValue(-24,24)/10.0f,0.07f,GetRandomValue(-24,24)/10.0f}, b=a;                 // arcs: re-struck every frame
                for (int q=0;q<4;q++){ Vector3 n={b.x+GetRandomValue(-7,7)/10.0f,fmaxf(0.07f,b.y+GetRandomValue(-2,6)/10.0f),b.z+GetRandomValue(-7,7)/10.0f}; DrawLine3D(b,n,(s&1)?(Color){170,215,255,255}:(Color){235,245,255,255}); b=n; } } } break;
        case TR_HOSE: if (!mb) TrapBox((Vector3){-4.2f,1.2f,0.3f},(Vector3){0.7f,0.75f,0.3f},red); break;
        case TR_SURGE: { if (t->hole<0) DrawCylinder((Vector3){0,0.012f,0},1.7f,1.7f,0.01f,24,(Color){4,8,16,255});
            if (!mb) DrawCylinderWires((Vector3){0,0.02f,0},1.75f,1.75f,0.01f,20,(Color){70,90,120,255});
            if (on>=0.45f){ float r=(on-0.45f)*9.0f; if (r<7.0f) for (int q=0;q<3;q++) DrawCircle3D((Vector3){0,0.1f+0.05f*q,0},r-0.2f*q,(Vector3){1,0,0},90,(Color){150,210,255,255}); } } break;
        case TR_CRANE: { float y=CraneY(t,on);
            if (!mb){ TrapBox((Vector3){0,6.3f,0},(Vector3){7.8f,0.34f,0.5f},(Color){170,140,40,255}); TrapBox((Vector3){0,y,0},(Vector3){2.6f,1.6f,2.6f},(Color){70,74,82,255}); }
            for (int s=-1;s<=1;s+=2) DrawCylinderEx((Vector3){s*0.12f,5.72f*t->ys,0},(Vector3){s*0.12f,y+1.05f,0},0.012f,0.012f,6,(Color){150,152,156,255});   // the falls it hangs on
            DrawCylinder((Vector3){0,0.011f,0},1.9f,1.9f,0.005f,18,(Color){0,0,0,90}); } break;                                                            // where it lands
        case TR_DOOR: if (!mb){ TrapBox((Vector3){-1.9f,2.0f,0},(Vector3){0.6f,4.0f,0.9f},(Color){98,100,104,255}); TrapBox((Vector3){1.9f,2.0f,0},(Vector3){0.6f,4.0f,0.9f},(Color){98,100,104,255}); } break;
        case TR_SPILL: { if (!mb) DrawCylinder((Vector3){-3.6f,0,0.9f},0.9f,0.9f,2.3f,16,(Color){40,120,112,255});
            } break;
        case TR_TURBINE: if (!mb) DrawCylinderEx((Vector3){0,1.6f,1.0f},(Vector3){0,1.6f,5.0f},1.55f,1.55f,20,(Color){72,84,92,255}); break;
        }
        rlPopMatrix();
    }
    (void)haz;
    if (!g_hasSmokeTex) return;                                                           // ---- the soft parts: steam, water, the surge, the turbine's draught
    rlDisableDepthMask();
    for (int ti=0;ti<g_nTraps;ti++){ Trap *t=&g_traps[ti]; if (t->kind==TR_SPILL && t->state==1 && Vector3Distance(t->c,g_cam.position)<60.0f) DrawCoolant(t); }   // the pools first: everything soft goes over them
    for (int ti=0;ti<g_nTraps;ti++){ Trap *t=&g_traps[ti]; int k=t->kind; if (t->state!=1) continue; float on=t->t;
        if (k==TR_STEAM){ float lv=fminf(1.0f,on*1.5f)*fminf(1.0f,(t->run-on)*1.2f);
            for (int j=-2;j<=2;j++) for (int n=0;n<7;n++){ float ph=fmodf(tm*1.5f+n/7.0f+j*0.37f,1.0f);
                Vector3 p=TW(t,j*1.25f+0.25f*sinf(tm*3.0f+n+j)*ph,3.0f-ph*3.0f,0.5f*ph*sinf(n*2.4f+j));
                DrawBillboard(g_cam,g_smokeTex,p,0.5f+ph*2.3f,(Color){235,240,245,(unsigned char)(150*lv*(1.0f-ph*0.75f))}); } }
        if (k==TR_HOSE && on>0.3f){ float lv=fminf(1.0f,(t->run-on)*2.0f), reach=11.0f*fminf(1.0f,(on-0.35f)*4.0f);
            for (int n=0;n<46;n++){ float ph=fmodf(tm*3.2f+n/46.0f,1.0f), d=ph*reach; Vector3 p=TW(t,-3.5f+d,1.15f-0.09f*d*d*0.1f+0.05f*sinf(n*7.0f+tm*20.0f),0.06f*d*sinf(n*3.3f));
                DrawBillboard(g_cam,g_smokeTex,p,0.22f+0.05f*d,(Color){190,220,255,(unsigned char)(170*lv)}); }
            for (int n=0;n<10;n++){ float ph=fmodf(tm*1.3f+n/10.0f,1.0f); DrawBillboard(g_cam,g_smokeTex,TW(t,-3.5f+reach+0.4f*sinf(n*2.0f),0.3f+ph*1.6f,0.9f*sinf(n*4.1f)*ph),0.6f+ph*1.6f,(Color){210,230,255,(unsigned char)(90*lv*(1.0f-ph))}); } }
        if (k==TR_SURGE){ BeginBlendMode(BLEND_ADDITIVE); float lv=(on<0.45f)?on/0.45f*0.4f:fmaxf(0.0f,1.0f-(on-0.45f)/(t->run-0.45f)), up=(on<0.45f)?0.4f:1.0f;
            for (int n=0;n<9;n++){ float h=n*0.9f*up; unsigned char b=(unsigned char)(255*lv*(1.0f-n/11.0f));
                DrawBillboard(g_cam,g_smokeTex,TW(t,0,0.2f+h,0),(3.6f-n*0.2f)*(1.0f+0.06f*sinf(tm*30.0f+n)),(Color){(unsigned char)(b*0.45f),(unsigned char)(b*0.8f),b,255}); }
            for (int n=0;n<26;n++){ float ph=fmodf(tm*0.9f+n/26.0f,1.0f), a=n*2.4f; DrawBillboard(g_cam,g_smokeTex,TW(t,cosf(a)*(0.4f+ph*1.6f),ph*7.0f*up,sinf(a)*(0.4f+ph*1.6f)),0.14f,(Color){(unsigned char)(160*lv),(unsigned char)(220*lv),(unsigned char)(255*lv),255}); }
            EndBlendMode(); }
        if (k==TR_SURGE) continue;
        if (k==TR_TURBINE && on>1.2f && on<t->run-1.0f){                                 // dust and grit streaming into its mouth
            for (int n=0;n<40;n++){ float s1=sinf(n*12.9898f)*43758.547f, r1=s1-floorf(s1), s2=sinf(n*78.233f)*12543.123f, r2=s2-floorf(s2);
                float ph=fmodf(tm*(0.7f+0.9f*r1)+r2,1.0f), d=7.5f*(1.0f-ph), sp=(0.3f+d*0.32f);
                Vector3 p=TW(t,(r1-0.5f)*2.0f*sp,1.6f+(r2-0.5f)*1.6f*sp*0.6f,1.0f-d);
                DrawBillboard(g_cam,g_smokeTex,p,0.10f+0.25f*(1.0f-ph),(Color){170,160,140,(unsigned char)(120*sinf(ph*PI))}); } }
    }
    if (g_trapDemo){ Trap *t=&g_traps[TR_SURGE]; if (t->state!=1){ BeginBlendMode(BLEND_ADDITIVE);                                   // the breach, at rest: its own low light
          for (int n=0;n<2;n++) DrawBillboard(g_cam,g_smokeTex,TW(t,0,0.15f+n*0.8f,0),3.2f-n*0.9f,(Color){(unsigned char)(26-10*n),(unsigned char)(60-22*n),(unsigned char)(120-45*n),255}); EndBlendMode(); } }
    rlEnableDepthMask();
}
static void TrapsHud(void){
    if (g_nTraps<=0) return;
    int W=GetScreenWidth(), H=GetScreenHeight(), near=TrapNear();
    for (int ti=0;ti<g_nTraps;ti++){ Trap *t=&g_traps[ti]; int k=t->kind; if (!g_trapDemo && ti!=near) continue;      // in the plant: only the one he stands at
        Vector3 at=TW(t,kTrapSwitchX[k],2.0f,kTrapSwitchZ[k]), to=Vector3Subtract(at,g_cam.position), fw=Vector3Subtract(g_cam.target,g_cam.position);
        if (Vector3DotProduct(to,fw)<=0.2f) continue;
        Vector2 s=GetWorldToScreen(at,g_cam); const char *st=(t->state==0)?"READY":(t->state==3)?"ARMED  -  springs when they reach it":(t->state==1)?"ACTIVE":"recharging";
        const char *l=g_trapDemo?TextFormat("%d  %s", k+1, kTrapName[k]):TextFormat("%s%s", (t->state==0)?"E  ARM  ":(t->state==3)?"E  SPRING NOW  ":"", kTrapName[k]); int fs=18, w=MeasureText(l,fs);
        DrawText(l,(int)s.x-w/2+1,(int)s.y-21,fs,(Color){0,0,0,200}); DrawText(l,(int)s.x-w/2,(int)s.y-22,fs,(Color){255,220,110,255});
        w=MeasureText(st,14); DrawText(st,(int)s.x-w/2,(int)s.y-2,14,(t->state==0)?(Color){90,235,110,255}:(t->state==3)?(Color){255,200,60,255}:(t->state==1)?(Color){255,110,80,255}:(Color){170,160,110,255}); }
    if (!g_trapDemo) return;
    const char *h="TRAP DEMO    E at a switch: arm it (springs when one walks in)    T: fire the next    Y: fire all    N: enemies on/off"; int w=MeasureText(h,18);
    DrawRectangle(W/2-w/2-10,H-34,w+20,26,(Color){0,0,0,150}); DrawText(h,W/2-w/2,H-30,18,(Color){225,205,150,255});
}
