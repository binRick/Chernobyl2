// mp.h -- multiplayer: up to eight players in the same run.
//
// Every game meets at a relay (server/mp-relay.js) over one WebSocket (net.h); nobody connects to
// anybody else. The player who starts a game is its HOST: his game runs the wave director, decides
// who each enemy is after, and is the only one that takes an enemy's health away. Everybody else's
// game is a CLIENT of his.
//
// What keeps eight screens showing one fight, with a quarter of a second between them:
//   - every game runs the SAME enemy code on its own copy of the enemies, each enemy chasing the
//     player the host says he is after. So what a player sees an enemy do to him is what happens to
//     him: an enemy only ever hurts the player whose own screen he is on (g_tMine), and that player's
//     health is his own game's business.
//   - the host says, as it happens, when an enemy appears ('s': the whole of his state) and when he
//     dies ('d': the hit that killed him, so he dies the same way everywhere), and ten times a second
//     where each one is and what he has left ('e'). A client's copy is pulled toward that.
//   - a client's own shots are judged on his own screen and sent to the host as hits ('h'); they
//     bleed and flinch at once, and die when the host says so.
//   - each player says twenty times a second where he is ('p'), and each shot he fires ('f').
// When you die you watch through a living player's eyes; when nobody is left the host restarts the
// run for everyone ('r').
//
// Included twice by main.c: MP_PHASE 1 (early: the few things the enemy and damage code need),
// MP_PHASE 2 (the rest, once everything it calls exists).
#if MP_PHASE==1

#define MP_MAX 8
enum { MP_OFF=0, MP_HOST=1, MP_CLIENT=2 };
typedef struct { int used, alive, weapon, firing, kills, seen; char name[4];
                 Vector3 pos, net; float yaw, pitch, nyaw, npitch, hp, move, speed; double shotAt;
                 Enemy body; } MpPlayer;                            // body: the figure the others see (an enemy rig, posed as one)
static int      g_mpRole=MP_OFF, g_mpMe=0, g_mpHostId=0, g_mpScreen=0;
static MpPlayer g_mpP[MP_MAX];
static unsigned char g_mpGen[MAX_ENEMIES], g_mpDirty[MAX_ENEMIES], g_mpTgt[MAX_ENEMIES];   // gen: which enemy this slot now holds; dirty: the host owes the others his arrival; tgt: the player he is after
static Vector3  g_tPos, g_tEye; static float g_tMove=0.0f; static int g_tMine=1;        // the player the enemy being updated is after (MpTarget), and whether that is this game's own
static int      g_mpCos=0, g_mpDeath=0, g_mpRemote=0, g_mpGibForce=-1, g_mpGibbed=0;    // HurtEnemyCore: a hit that must not kill / a death the host decided / somebody else's hit
static int      g_hurtWorld=0, g_hurtWpnF=-1; static Vector3 g_hurtShooter;             // the hit is nobody's shot (a trap, a bolt) / the shooter's gun and where he stood
static int      g_mpFiring=0, g_mpOpenAtStart=0;                                         // this player's trigger is down / go to the multiplayer screen as the game starts (--mp, M on the title)
static int  MpActive(void){ return g_mpRole!=MP_OFF; }
static void MpTarget(int i);
static void MpNavUpdate(void);
static void MpHurt(int ei, float dmg, int head, Vector3 dir, Vector3 hitPt, int blood);
static void MpShot(int weapon, Vector3 from, Vector3 to);
static void MpTrapFired(int k);
static void MpDrawPlayers(void);
static void MpTick(float dt); static int MpSpectate(int next); static void MpPump(void); static void MpLobbyUpdate(void); static void MpLobbyDraw(void);
static void MpDrawHud(int W, int H); static void MpNote(const char *s); static void MpRestartAll(void); static void MpOpenLobby(void); static void MpStop(const char *why); static void MpAfterMapStart(void); static int MpCount(void);

#else   // ---------------------------------------------------------------------------- MP_PHASE 2
#include <stddef.h>
#include "net.h"

#define MP_URL_DEFAULT "wss://chernobyl2.ximg.app/mp"
static char   g_mpUrl[200]=MP_URL_DEFAULT, g_mpCode[5]="", g_mpName[4]="", g_mpMsg[96]="", g_mpJoin[5]="";
static float  g_mpMsgT=0.0f, g_mpOverT=0.0f, g_mpFlame[MAX_ENEMIES];
static int    g_mpConn=0, g_mpWant=0, g_mpSel=0, g_mpEdit=0, g_mpSpec=-1, g_mpAuto=0, g_mpNetGuard=0;
static double g_mpConnAt=0, g_mpListAt=0, g_mpStateAt=0, g_mpSnapAt=0, g_mpShotAt=0, g_mpFlameAt=0, g_mpSeen[MAX_ENEMIES];
static struct { char code[5], name[4]; int players, wave; } g_mpList[40]; static int g_mpListN=0;
static const Color kMpCol[MP_MAX]={{255,214,90,255},{110,200,255,255},{255,120,110,255},{140,235,140,255},{230,150,255,255},{255,170,80,255},{120,240,220,255},{240,240,240,255}};
static void TextSh(const char *t, int x, int y, int sz, Color c);
static void TrapFire(int k);

// ---- messages: a few bytes written in order, read back the same way -------------------------
typedef struct { unsigned char b[2048]; int n; } MpW;
static void W8(MpW *w, int v){ if (w->n<(int)sizeof w->b) w->b[w->n++]=(unsigned char)v; }
static void WN(MpW *w, const void *p, int n){ if (w->n+n<=(int)sizeof w->b){ memcpy(w->b+w->n,p,(size_t)n); w->n+=n; } }
static void WF(MpW *w, float f){ WN(w,&f,4); }
static void WV(MpW *w, Vector3 v){ WN(w,&v,12); }
static MpW  WTo(int relayOp, int kind){ MpW w; w.n=0; W8(&w,relayOp); W8(&w,kind); return w; }   // 0x04 everyone else, 0x05 the host
static void WSend(MpW *w){ NetSend(w->b,w->n); }
typedef struct { const unsigned char *p; int n, at; } MpR;
static int     R8(MpR *r){ return (r->at<r->n) ? r->p[r->at++] : 0; }
static void    RN(MpR *r, void *o, int n){ if (r->at+n<=r->n) memcpy(o,r->p+r->at,(size_t)n); else memset(o,0,(size_t)n); r->at+=n; }
static float   RF(MpR *r){ float f; RN(r,&f,4); return isfinite(f)?f:0.0f; }
static Vector3 RV(MpR *r){ Vector3 v; v.x=RF(r); v.y=RF(r); v.z=RF(r); return v; }

static void MpSay(const char *s){ snprintf(g_mpMsg,sizeof g_mpMsg,"%s",s); g_mpMsgT=5.0f; DebugLog("mp","\"say\":\"%s\"",JStr(s)); }
static void MpNote(const char *s){ MpSay(s); }
static int  MpPlayerAlive(int id){ return id==g_mpMe ? !g_dead : (id>=0 && id<MP_MAX && g_mpP[id].used && g_mpP[id].alive); }
static int  MpCount(void){ int n=1; for (int i=0;i<MP_MAX;i++) if (i!=g_mpMe && g_mpP[i].used) n++; return n; }
static const char *MpNameOf(int id){ return id==g_mpMe ? g_mpName : g_mpP[id].name; }

// ---- who each enemy is after --------------------------------------------------------------
static void MpTarget(int i){
    g_tPos=g_pos; g_tEye=g_cam.position; g_tMove=g_playerMoveAmt; g_tMine=1;
    if (!MpActive()) return;
    int t=g_mpTgt[i];
    if (t!=g_mpMe){ g_tMine=0; if (t<MP_MAX && g_mpP[t].used){ g_tPos=g_tEye=g_mpP[t].pos; g_tMove=g_mpP[t].move; } }
    if (g_dead) g_tMine=0;
}
static void MpNavUpdate(void){                              // the flow field leads to the nearest living player, whoever that is
    Vector3 src[MP_MAX]; int n=0;
    if (!MpActive()){ MapNavUpdateN(&g_pos,1); return; }
    for (int id=0;id<MP_MAX;id++) if (MpPlayerAlive(id)) src[n++]=(id==g_mpMe)?g_pos:g_mpP[id].pos;
    if (n==0) src[n++]=g_pos;
    MapNavUpdateN(src,n);
}
static void MpPickTargets(void){                            // host: the nearest living player, with a lean toward the one he already has
    for (int i=0;i<MAX_ENEMIES;i++){ const Enemy *e=&g_enemies[i]; if (e->state!=1) continue;
        int best=-1; float bd=1e18f;
        for (int id=0;id<MP_MAX;id++){ if (!MpPlayerAlive(id)) continue;
            Vector3 p=(id==g_mpMe)?g_pos:g_mpP[id].pos; float dx=p.x-e->pos.x, dz=p.z-e->pos.z, dy=(p.y-EYE_H)-e->pos.y, d=dx*dx+dz*dz+4.0f*dy*dy;
            if (id==g_mpTgt[i]) d*=0.55f;
            if (d<bd){ bd=d; best=id; } }
        if (best>=0) g_mpTgt[i]=(unsigned char)best; }
}

// ---- hits and deaths ----------------------------------------------------------------------
typedef struct { int slot, gen, who, head, cause, wf, blood, gib; float dmg; Vector3 dir, pt, from, spos; } MpHit;
static void WHit(MpW *w, const MpHit *h){ W8(w,h->slot); W8(w,h->gen); W8(w,h->who); W8(w,h->head); W8(w,h->cause); W8(w,h->wf); W8(w,h->blood); W8(w,h->gib);
    WF(w,h->dmg); WV(w,h->dir); WV(w,h->pt); WV(w,h->from); WV(w,h->spos); }
static void RHit(MpR *r, MpHit *h){ h->slot=R8(r); h->gen=R8(r); h->who=R8(r); h->head=R8(r)?1:0; h->cause=R8(r)&3; h->wf=R8(r)&3; h->blood=R8(r)?1:0; h->gib=R8(r)?1:0;
    h->dmg=RF(r); h->dir=RV(r); h->pt=RV(r); h->from=RV(r); h->spos=RV(r); if (h->dmg<0) h->dmg=0; }
static void MpAfterHit(MpHit *h, const Enemy *e){            // host: the others hear what came of it
    if (e->state!=1){ h->gib=g_mpGibbed; MpW w=WTo(0x04,'d'); WHit(&w,h); WSend(&w);
        if (h->who<MP_MAX && h->who!=g_mpMe) g_mpP[h->who].kills++; }
    else if (h->cause!=HC_FLAME){ MpW w=WTo(0x04,'h'); WHit(&w,h); WSend(&w); }
}
static void MpHurt(int ei, float dmg, int head, Vector3 dir, Vector3 hitPt, int blood){   // a hit that began in THIS game
    const Enemy *e=&g_enemies[ei]; const Weapon *cw=&g_weapons[g_curWeapon];
    MpHit h={ ei, g_mpGen[ei], g_hurtWorld?255:g_mpMe, head, g_hurtCause, ((cw->spinUp||cw->soundGated)?1:0)|((cw->pellets>1)?2:0), blood, 0, dmg, dir, hitPt, g_hurtFrom, g_pos };
    g_hurtShooter=g_pos; g_hurtWpnF=-1;
    if (g_mpRole==MP_CLIENT){
        if (!g_hurtWorld){
            if (g_hurtCause==HC_FLAME) g_mpFlame[ei]+=dmg;               // every frame of flame: sent as a sum, ten times a second
            else { MpW w=WTo(0x05,'h'); WHit(&w,&h); WSend(&w); } }
        g_mpCos=1; HurtEnemyCore(ei,dmg,head,dir,hitPt,blood); g_mpCos=0;   // it shows at once; whether he dies is the host's to say
        return;
    }
    g_mpGibbed=0;
    HurtEnemyCore(ei,dmg,head,dir,hitPt,blood);
    MpAfterHit(&h,e);
}
static void MpNetHit(MpHit *h, int death){                   // a hit that began in somebody else's game
    if (h->slot<0 || h->slot>=MAX_ENEMIES) return;
    const Enemy *e=&g_enemies[h->slot];
    if (e->state!=1 || g_mpGen[h->slot]!=h->gen) return;
    int mine=(h->who==g_mpMe), sc=g_hurtCause, sn=g_noCredit; Vector3 sf=g_hurtFrom;
    if (g_mpRole==MP_CLIENT && !death && mine) return;       // (already shown when it was fired)
    g_hurtCause=h->cause; g_hurtFrom=h->from; g_hurtShooter=h->spos; g_hurtWpnF=h->wf; g_mpRemote=!mine; g_noCredit=!mine;
    if (g_mpRole==MP_HOST){ g_mpGibbed=0; HurtEnemyCore(h->slot,h->dmg,h->head,h->dir,h->pt,h->blood); MpAfterHit(h,e); }
    else if (death){ g_mpDeath=1; g_mpGibForce=h->gib; HurtEnemyCore(h->slot,h->dmg,h->head,h->dir,h->pt,h->blood); g_mpDeath=0; g_mpGibForce=-1; }
    else { g_mpCos=1; HurtEnemyCore(h->slot,h->dmg,h->head,h->dir,h->pt,h->blood); g_mpCos=0; }
    g_hurtCause=sc; g_hurtFrom=sf; g_noCredit=sn; g_mpRemote=0; g_hurtWpnF=-1;
}

// ---- enemies: arrivals and the ten-a-second roll call -----------------------------------------
#define MP_EPREFIX ((int)offsetof(Enemy,wN))                 // an enemy's state up to his wounds: plain numbers, the same in every build
static void MpSendSpawn(int i){ MpW w=WTo(0x04,'s'); W8(&w,i); W8(&w,g_mpGen[i]); W8(&w,g_mpTgt[i]); WN(&w,&g_enemies[i],MP_EPREFIX); WSend(&w); }
static void MpBossIn(const Enemy *e){ if (e->boss && g_audio && g_hasBossSnd && !IsSoundPlaying(g_bossSnd)){ SetSoundVolume(g_bossSnd,0.80f); PlaySound(g_bossSnd); } }
static void MpApplySpawn(MpR *r){
    int slot=R8(r), gen=R8(r), tgt=R8(r); Enemy t; memset(&t,0,sizeof t); RN(r,&t,MP_EPREFIX);
    if (slot>=MAX_ENEMIES || t.type<0 || t.type>=EN_NTYPES || t.state!=1) return;
    if (!isfinite(t.pos.x) || !isfinite(t.pos.y) || !isfinite(t.pos.z) || !isfinite(t.hp) || !isfinite(t.animT)) return;
    if (t.clip<0 || t.clip>=CL_NROLES || t.hole>=g_nMapHoles || t.edge<0 || t.edge>3 || t.burnN<0 || t.burnN>MAX_BURN) return;
    if (t.hitRole<0 || t.hitRole>=CL_NROLES || t.deathRole<0 || t.deathRole>=CL_NROLES || t.prevClip<0 || t.prevClip>=CL_NROLES) return;
    Enemy *e=&g_enemies[slot];
    if (e->state==1 && g_mpGen[slot]==gen) return;           // (already here: the host re-sends everyone when a player joins)
    if (t.hole<0) t.climbT=0;
    EnemyArm(slot,t.pos,t.type);                             // everything an enemy needs set, set; then what the host says of this one
    memcpy(e,&t,(size_t)MP_EPREFIX); e->flameFlinchAt=-100.0; e->raiseC=-1; e->lastPos=e->pos;
    g_mpGen[slot]=(unsigned char)gen; g_mpDirty[slot]=0; g_mpTgt[slot]=(unsigned char)tgt; g_mpSeen[slot]=GetTime();
    if (e->climbT>0 || e->riseT>0) MpBossIn(e);
}
static void MpSendSnap(void){
    MpW w=WTo(0x04,'e'); int n=0, at;
    W8(&w,g_wave); WF(&w,g_waveBreather); W8(&w,(g_bossThisWave?1:0)|(g_waveBudget>0?2:0)); at=w.n; W8(&w,0);
    for (int i=0;i<MAX_ENEMIES;i++){ const Enemy *e=&g_enemies[i]; if (e->state!=1) continue;
        W8(&w,i); W8(&w,g_mpGen[i]); W8(&w,e->type); W8(&w,g_mpTgt[i]); W8(&w,e->boss?1:0); WF(&w,e->hp); WF(&w,e->maxHp); WV(&w,e->pos); n++; }
    w.b[at]=(unsigned char)n; WSend(&w);
    { unsigned char info[2]={0x07,(unsigned char)g_wave}; NetSend(info,2); }
}
static void MpApplySnap(MpR *r){
    int wave=R8(r); float br=RF(r); int fl=R8(r), n=R8(r); double now=GetTime();
    if (wave!=g_wave){ if (wave>g_wave && wave>0) StartWave(wave); else g_wave=wave; }
    g_waveBreather=br; g_bossThisWave=fl&1; g_waveBudget=(fl&2)?1:0;
    for (int k=0;k<n && r->at<r->n;k++){
        int slot=R8(r), gen=R8(r), type=R8(r), tgt=R8(r), boss=R8(r); float hp=RF(r), mhp=RF(r); Vector3 pos=RV(r);
        if (slot>=MAX_ENEMIES || type>=EN_NTYPES) continue;
        Enemy *e=&g_enemies[slot];
        if (g_mpGen[slot]!=gen || e->state==0){              // one we never heard arrive: he is simply there
            if (g_mpGen[slot]==gen && now-g_mpSeen[slot]<1.0) continue;         // (he died here a moment ago: the host has not caught up)
            EnemyArm(slot,pos,type); e->boss=boss; if (mhp>1.0f) e->maxHp=mhp;
            g_mpGen[slot]=(unsigned char)gen; }
        if (e->state!=1){ continue; }                        // dying on this screen already
        g_mpSeen[slot]=now; g_mpTgt[slot]=(unsigned char)tgt; if (hp>0.0f) e->hp=hp;
        if (e->climbT>0 || e->riseT>0) continue;             // on his way up: the clip places him
        float dx=pos.x-e->pos.x, dz=pos.z-e->pos.z, d=sqrtf(dx*dx+dz*dz);
        if (d>4.0f || fabsf(pos.y-e->pos.y)>2.5f){ e->pos=pos; e->lastPos=pos; e->drawY=pos.y; }   // lost him: put him where he is
        else if (d>0.25f){ float k=(d>1.5f)?0.5f:0.22f; e->pos.x+=dx*k; e->pos.z+=dz*k; e->lastPos.x+=dx*k; e->lastPos.z+=dz*k; }   // drifted: ease him over (his legs are not told)
    }
    for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state==1 && now-g_mpSeen[i]>1.5) g_enemies[i].state=0;   // the host no longer has him
}

// ---- the other players ----------------------------------------------------------------------
static void MpSendState(void){
    MpW w=WTo(0x04,'p'); WV(&w,g_pos); WF(&w,g_yaw); WF(&w,g_pitch); WF(&w,g_playerHp);
    W8(&w,(g_dead?0:1)|(g_mpFiring?2:0)); W8(&w,g_curWeapon); W8(&w,(int)(Clamp(g_playerMoveAmt,0,1)*255)); W8(&w,g_kills&255); W8(&w,(g_kills>>8)&255);
    WSend(&w);
}
static void MpApplyState(int from, MpR *r){
    MpPlayer *p=&g_mpP[from]; Vector3 pos=RV(r); float yaw=RF(r), pit=RF(r), hp=RF(r); int fl=R8(r), wp=R8(r), mv=R8(r), k=R8(r); k|=R8(r)<<8;
    if (!p->used){ p->used=1; snprintf(p->name,sizeof p->name,"P%d",from+1); }
    p->net=pos; p->nyaw=yaw; p->npitch=Clamp(pit,-1.5f,1.5f); p->hp=hp; p->alive=fl&1; p->firing=(fl>>1)&1; p->weapon=wp; p->move=mv/255.0f;
    if (g_mpRole!=MP_HOST) p->kills=k;
    if (!p->seen){ p->seen=1; p->pos=pos; p->yaw=yaw; p->pitch=p->npitch; }
}
static void MpShot(int weapon, Vector3 from, Vector3 to){    // this game's player fired: the others see the tracer
    if (!MpActive() || GetTime()-g_mpShotAt<0.07) return;
    g_mpShotAt=GetTime();
    MpW w=WTo(0x04,'f'); W8(&w,weapon); WV(&w,from); WV(&w,to); WSend(&w);
}
static void MpApplyShot(int from, MpR *r){
    int wp=R8(r); Vector3 a=RV(r), b=RV(r); Vector3 d=Vector3Subtract(b,a); float L=Vector3Length(d);
    if (wp>=g_numWeapons || L<1e-3f || L>200.0f) return;
    if (from<MP_MAX && g_mpP[from].used){ const MpPlayer *p=&g_mpP[from];      // from his gun as this screen shows him, not where he was a moment ago
        Vector3 f={sinf(p->yaw)*cosf(p->pitch),sinf(p->pitch),cosf(p->yaw)*cosf(p->pitch)}; a=Vector3Add(p->pos,Vector3Add(Vector3Scale(f,0.7f),(Vector3){0,-0.25f,0})); d=Vector3Subtract(b,a); L=Vector3Length(d); if (L<1e-3f) return; }
    d=Vector3Scale(d,1.0f/L);
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life<=0){ g_tracers[i]=(Tracer){a,b,0.05f}; break; }
    SpawnMuzzleFlash(a,d); DynLightAdd(Vector3Add(a,Vector3Scale(d,0.4f)), (Vector3){1.9f,1.35f,0.70f}, 10.0f, 0.075f, 0);
    const Weapon *cw=&g_weapons[wp];
    if (g_audio && cw->hasSnd && !cw->sndLoop) SfxAt(cw->fireSnd, a, 0.85f);
    else if (g_audio && g_hasEShot && from<MP_MAX && GetTime()-g_mpP[from].shotAt>0.09){ g_mpP[from].shotAt=GetTime(); SfxAt(g_eShotSnd, a, 0.5f); }   // an automatic: its own loop belongs to whoever holds it
}
static void MpTrapFired(int k){ if (!MpActive() || g_mpNetGuard) return; MpW w=WTo(0x04,'t'); W8(&w,k); WSend(&w); }

// ---- starting, joining, leaving --------------------------------------------------------------
static void MpResetPlayers(void){ memset(g_mpP,0,sizeof g_mpP); g_mpSpec=-1; g_mpOverT=0.0f; memset(g_mpFlame,0,sizeof g_mpFlame); }
static void MpStop(const char *why){                         // back to a game of one
    int was=MpActive();
    NetClose(); g_mpRole=MP_OFF; g_mpConn=0; g_mpWant=0; g_mpCode[0]=0; MpResetPlayers();
    if (why) MpSay(why);
    if (was) g_returnToMenu=1;                               // what is on the field was the host's: a fresh run
}
static void MpConnect(void){
    if (g_mpConn) return;
    const char *u=getenv("CHERNOBYL_MP_URL"); if (u && *u) snprintf(g_mpUrl,sizeof g_mpUrl,"%s",u);
#if defined(__EMSCRIPTEN__)
    { char *q=(char*)EM_ASM_PTR({ var u=(window.__c2_mp_url||new URLSearchParams(location.search).get('mp')||''); return stringToNewUTF8(u); });
      if (q && *q) snprintf(g_mpUrl,sizeof g_mpUrl,"%s",q); if (q) free(q); }
#endif
    if (!g_mpName[0]){ const char *nm=getenv("CHERNOBYL_MP_NAME");
        if (nm && strlen(nm)>=3) snprintf(g_mpName,sizeof g_mpName,"%.3s",nm);
        else if (strlen(g_lbSaved)==3) snprintf(g_mpName,sizeof g_mpName,"%s",g_lbSaved);
        else snprintf(g_mpName,sizeof g_mpName,"P%02d",GetRandomValue(10,99));
        for (int i=0;i<3;i++){ char c=g_mpName[i]; if (c>='a' && c<='z') c-=32; if (!((c>='0'&&c<='9')||(c>='A'&&c<='Z'))) c='X'; g_mpName[i]=c; } }
    g_mpListN=0;
    if (!C2_HAS_NET || !NetOpen(g_mpUrl)){ MpSay("MULTIPLAYER IS NOT AVAILABLE IN THIS BUILD"); return; }
    g_mpConn=1; g_mpConnAt=GetTime(); DebugLog("mp","\"connect\":\"%s\"",JStr(g_mpUrl));
}
static void MpOpenLobby(void){
    if (!C2_HAS_NET){ MpSay("MULTIPLAYER IS NOT IN THIS VERSION YET"); DisableCursor(); return; }   // (the phone: later)
    g_mpScreen=1; g_mpSel=0; g_mpEdit=0; g_menu=0; EnableCursor(); MpConnect(); }
static void MpCloseLobby(void){ g_mpScreen=0; if (!MpActive()){ NetClose(); g_mpConn=0; } DisableCursor(); }
static void MpCreate(void){ unsigned char m[4]={0x01,(unsigned char)g_mpName[0],(unsigned char)g_mpName[1],(unsigned char)g_mpName[2]}; NetSend(m,4); }
static void MpJoinCode(const char *code){ unsigned char m[8]={0x02,(unsigned char)code[0],(unsigned char)code[1],(unsigned char)code[2],(unsigned char)code[3],(unsigned char)g_mpName[0],(unsigned char)g_mpName[1],(unsigned char)g_mpName[2]}; NetSend(m,8); }
static void MpRestartAll(void){ if (g_mpRole!=MP_HOST) return; MpW w=WTo(0x04,'r'); WSend(&w); g_mpOverT=0.0f; }   // host: everyone starts the run again (he sets his own g_returnToMenu)
static void MpAfterMapStart(void){                           // a run is beginning (StartSelectedMap): not everybody on the one spot
    g_mpOverT=0.0f; g_mpSpec=-1; memset(g_mpFlame,0,sizeof g_mpFlame); memset(g_mpDirty,0,sizeof g_mpDirty);
    for (int i=0;i<MAX_ENEMIES;i++) g_mpSeen[i]=0;
    if (!MpActive()) return;
    for (int i=0;i<MP_MAX;i++) g_mpP[i].alive=g_mpP[i].used;
    if (g_mpMe>0 && g_hasMap && g_mapCol.ready){ float a=g_mpMe*0.785f, r=1.6f; Vector3 p={g_pos.x+cosf(a)*r, g_pos.y, g_pos.z+sinf(a)*r};
        if (!MapSphereHitsWall((Vector3){p.x,p.y-0.55f,p.z},0.45f) && HoleAt(p.x,p.z,0.6f)<0 && MapRayNearest(p,(Vector3){0,-1,0},4.0f)>0.0f) g_pos=p; }
}

static void MpOnFrame(const unsigned char *b, int n){
    MpR r={b,n,1};
    switch (b[0]){
    case 0x81: {                                             // WELCOME: in a game
        g_mpMe=R8(&r)&7; g_mpHostId=R8(&r)&7; RN(&r,g_mpCode,4); g_mpCode[4]=0; int np=R8(&r);
        MpResetPlayers();
        for (int k=0;k<np;k++){ int id=R8(&r)&7; MpPlayer *p=&g_mpP[id]; p->used=1; p->alive=1; RN(&r,p->name,3); p->name[3]=0; }
        g_mpRole=(g_mpMe==g_mpHostId)?MP_HOST:MP_CLIENT; g_mpScreen=0; g_mpWant=0; g_menu=0; DisableCursor();
        g_returnToMenu=1;                                    // a fresh run for this game
        MpSay(TextFormat(g_mpRole==MP_HOST?"GAME %s STARTED  -  OTHERS CAN JOIN IT":"JOINED GAME %s",g_mpCode));
        DebugLog("mp","\"welcome\":\"%s\",\"me\":%d,\"host\":%d,\"peers\":%d",g_mpCode,g_mpMe,g_mpHostId,np);
    } break;
    case 0x82: {                                             // LIST
        int c=R8(&r); g_mpListN=0;
        for (int k=0;k<c && k<40;k++){ RN(&r,g_mpList[k].code,4); g_mpList[k].code[4]=0; RN(&r,g_mpList[k].name,3); g_mpList[k].name[3]=0; g_mpList[k].players=R8(&r); g_mpList[k].wave=R8(&r); g_mpListN++; }
    } break;
    case 0x83: {                                             // JOINED
        int id=R8(&r)&7; MpPlayer *p=&g_mpP[id]; memset(p,0,sizeof *p); p->used=1; p->alive=1; RN(&r,p->name,3); p->name[3]=0;
        MpSay(TextFormat("%s JOINED",p->name));
        if (g_mpRole==MP_HOST) for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state==1) g_mpDirty[i]=1;   // he is told of everyone already on the field
    } break;
    case 0x84: { int id=R8(&r)&7; if (g_mpP[id].used) MpSay(TextFormat("%s LEFT",g_mpP[id].name)); memset(&g_mpP[id],0,sizeof g_mpP[id]); if (g_mpSpec==id) g_mpSpec=-1; } break;
    case 0x86: { int why=R8(&r); MpSay(why==1?"THAT GAME HAS ENDED":why==2?"THAT GAME IS FULL":"THE SERVER REFUSED THAT"); g_mpWant=0; } break;
    case 0x87: MpStop("THE HOST LEFT  -  THE GAME IS OVER"); break;
    case 0x85: {                                             // a message from another player
        int from=R8(&r)&7, kind=R8(&r);
        if (!MpActive() || from==g_mpMe) break;
        if (kind=='p') MpApplyState(from,&r);
        else if (kind=='f') MpApplyShot(from,&r);
        else if (kind=='t'){ int k=R8(&r); if (k<g_nTraps){ g_mpNetGuard=1; TrapFire(k); g_mpNetGuard=0; } }
        else if (kind=='h' && (g_mpRole==MP_HOST || from==g_mpHostId)){ MpHit h; RHit(&r,&h); if (g_mpRole==MP_HOST) h.who=from; MpNetHit(&h,0); }
        else if (from!=g_mpHostId) break;                    // the rest is the host's to say
        else if (kind=='s') MpApplySpawn(&r);
        else if (kind=='e') MpApplySnap(&r);
        else if (kind=='d'){ MpHit h; RHit(&r,&h); MpNetHit(&h,1); }
        else if (kind=='r'){ g_returnToMenu=1; DebugLog("mp","\"restart\":true"); }
    } break;
    default: break; }
}

// Once a frame, whatever the game is doing: the connection's state, and everything that has arrived.
static void MpPump(void){
    static int first=1;
    if (first){ first=0; const char *a=getenv("CHERNOBYL_MP");                       // dev: CHERNOBYL_MP=host | join -- no screen, straight in (join: the first game listed)
        if (a && !strcmp(a,"host")) g_mpAuto=1; else if (a && !strcmp(a,"join")) g_mpAuto=2;
#if defined(__EMSCRIPTEN__)
        g_mpAuto=EM_ASM_INT({ var a=new URLSearchParams(location.search).get('mpauto'); return a==='host'?1:(a==='join'?2:0); });   // (the same, for a page: ?mpauto=host|join)
#endif
        if (g_mpAuto) MpConnect(); else if (g_mpOpenAtStart) MpOpenLobby(); }
    if (g_mpMsgT>0) g_mpMsgT-=GetFrameTime();
    if (g_mpConn==1){
        int st=NetState();
        if (st==2){ g_mpConn=2; g_mpListAt=0; DebugLog("mp","\"open\":true"); }
        else if (st==0 || GetTime()-g_mpConnAt>8.0){ NetClose(); g_mpConn=0; MpSay("COULD NOT REACH THE MULTIPLAYER SERVER"); }
    } else if (g_mpConn==2 && NetState()!=2){
        if (MpActive()) MpStop("CONNECTION LOST"); else { g_mpConn=0; MpSay("CONNECTION LOST"); }
    }
    if (g_mpConn==2 && !MpActive()){
        if (GetTime()-g_mpListAt>1.5){ unsigned char m=0x03; NetSend(&m,1); g_mpListAt=GetTime(); }
        if (g_mpAuto==1 && !g_mpWant){ g_mpWant=1; MpCreate(); }
        else if (g_mpAuto==2 && !g_mpWant && g_mpListN>0){ g_mpWant=2; MpJoinCode(g_mpList[0].code); }
    }
    static unsigned char buf[4096]; int n, guard=0;
    while ((n=NetRecv(buf,(int)sizeof buf))!=0 && guard++<400){ if (n>0) MpOnFrame(buf,n); if (g_returnToMenu) break; }   // (a restart: what follows belongs to the next run)
}

// Once a frame while a run is on (from Update, after the enemies and the wave director have moved).
static void MpTick(float dt){
    if (!MpActive()) return;
    double now=GetTime();
    for (int id=0;id<MP_MAX;id++){ MpPlayer *p=&g_mpP[id]; if (!p->used || id==g_mpMe) continue;       // the others, eased toward where they last said they were
        Vector3 d=Vector3Subtract(p->net,p->pos); float L=Vector3Length(d), k=fminf(1.0f,dt*14.0f);
        if (L>6.0f) p->pos=p->net; else p->pos=Vector3Add(p->pos,Vector3Scale(d,k));
        p->speed+=((dt>1e-4f?sqrtf(d.x*d.x+d.z*d.z)*k/dt:0.0f)-p->speed)*fminf(1.0f,dt*8.0f);
        float dy=p->nyaw-p->yaw; while (dy>PI) dy-=2*PI; while (dy<-PI) dy+=2*PI; p->yaw+=dy*k; p->pitch+=(p->npitch-p->pitch)*k;
        if (p->alive && p->firing && p->weapon==5){                                                    // his flamethrower's stream
            Vector3 f={sinf(p->yaw)*cosf(p->pitch),sinf(p->pitch),cosf(p->yaw)*cosf(p->pitch)}, m=Vector3Add(p->pos,Vector3Add(Vector3Scale(f,0.8f),(Vector3){0,-0.3f,0}));
            for (int q=0;q<3;q++) SpawnFlame(m,f); } }
    if (now-g_mpStateAt>0.05){ g_mpStateAt=now; MpSendState(); }
    if (g_mpRole==MP_HOST){
        MpPickTargets();
        for (int i=0;i<MAX_ENEMIES;i++) if (g_mpDirty[i]){ g_mpDirty[i]=0; if (g_enemies[i].state==1) MpSendSpawn(i); }
        if (now-g_mpSnapAt>0.1){ g_mpSnapAt=now; MpSendSnap(); }
        int any=0; for (int id=0;id<MP_MAX;id++) if (MpPlayerAlive(id)) any=1;                          // nobody left: a few seconds, then again from wave 1
        if (any) g_mpOverT=0.0f; else { g_mpOverT+=dt; if (g_mpOverT>5.0f){ MpRestartAll(); g_returnToMenu=1; } }
    } else {
        if (now-g_mpFlameAt>0.1){ g_mpFlameAt=now;
            for (int i=0;i<MAX_ENEMIES;i++) if (g_mpFlame[i]>0.0f){ const Enemy *e=&g_enemies[i];
                MpHit h={ i, g_mpGen[i], g_mpMe, 0, HC_FLAME, 0, 0, 0, g_mpFlame[i], (Vector3){0,0,1}, (Vector3){e->pos.x,e->pos.y+ENEMY_HEIGHT*0.5f,e->pos.z}, g_pos, g_pos };
                g_mpFlame[i]=0.0f; if (e->state==1){ MpW w=WTo(0x05,'h'); WHit(&w,&h); WSend(&w); } } }
        int any=0; for (int id=0;id<MP_MAX;id++) if (MpPlayerAlive(id)) any=1;
        if (any) g_mpOverT=0.0f; else g_mpOverT+=dt;                                                   // (for the countdown on screen; the host does the restarting)
    }
}
// Dead, in a game that goes on: through a living player's eyes. A click moves on to the next one.
static int MpSpectate(int next){
    if (!MpActive() || !g_dead) return -1;
    int cur=g_mpSpec;
    if (next || cur<0 || !MpPlayerAlive(cur) || cur==g_mpMe){
        for (int k=1;k<=MP_MAX;k++){ int id=((cur<0?0:cur)+k)%MP_MAX; if (id!=g_mpMe && MpPlayerAlive(id)){ cur=id; break; } }
        if (cur<0 || cur==g_mpMe || !MpPlayerAlive(cur)) cur=-1; }
    g_mpSpec=cur;
    if (cur>=0){ const MpPlayer *p=&g_mpP[cur]; g_pos=p->pos; g_yaw=p->yaw; g_pitch=p->pitch; g_vy=0.0f; g_eyeSmooth=0.0f; }
    return cur;
}

// ---- the other players, drawn (inside DrawEnemies: lit and shadowed as the enemies are) ------------
static int MpBodyType(void){ static const int pref[3]={EN_LIQUIDATOR,EN_RIFLEMAN,EN_COOK}; for (int k=0;k<3;k++) if (g_rig[pref[k]].ok) return pref[k]; return EN_COOK; }
static void MpDrawPlayers(void){
    if (!MpActive() || !g_hasEnemy) return;
    float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;
    for (int id=0;id<MP_MAX;id++){ MpPlayer *p=&g_mpP[id];
        if (!p->used || !p->alive || !p->seen || id==g_mpMe || (g_dead && id==g_mpSpec)) continue;
        Enemy *b=&p->body; int type=MpBodyType(); EnemyRig *rg=RigOf(type);
        if (rg->animN<=0) continue;
        int lw=(p->speed>6.0f)?CL_RUN:(p->speed>0.5f)?CL_WALK:CL_IDLE;
        if (b->type!=type || b->state!=1){ memset(b,0,sizeof *b); b->type=type; b->state=1; b->sc=1.0f; b->loco=b->locoPrev=lw; b->clip=CL_RUN; b->hole=-1; }
        if (lw!=b->loco){ b->locoPrev=b->loco; b->locoPrevT=b->locoT; b->locoBlend=LOCO_BLEND; b->loco=lw; b->locoT=0.0f; }
        if (b->locoBlend>0) b->locoBlend-=dt;
        { int nf=ANIM_FRAMES(*RigClip(type,b->loco)); float rate=30.0f;
          if (b->loco!=CL_IDLE){ float m=rg->mps[b->loco]; if (m<0.1f) m=(b->loco==CL_RUN)?5.0f:1.4f; rate=30.0f*Clamp(p->speed/m,0.5f,1.9f); }
          b->locoT+=rate*dt; if (nf>0){ while (b->locoT>=nf) b->locoT-=nf; } }
        b->legW=1.0f; b->twist=b->headTw=b->lean=0.0f; b->pitch=0.0f; b->blendT=0.0f; b->climbT=0.0f;
        Vector3 dpos={p->pos.x, p->pos.y-EYE_H, p->pos.z};
        b->pos=dpos; b->drawY=dpos.y; b->yaw=p->yaw*RAD2DEG+ENEMY_YAW_OFFSET;
        EnemyPoseBuild(b);
        if (b->poseOK && rg->gpu && rg->layer) RigPoseStored(rg,b->pose); else RigPose(rg,RigClip(type,b->loco),b->locoT);
        if (rg->gpu){ EnemyLightUpload(dpos); EnemyGlowDim(1.0f); }
        float esc=ENEMY_SCALE*kEnemy[type].scale;
        DrawModelEx(*EnemyLodModel(rg,dpos), dpos, (Vector3){0,1,0}, b->yaw, (Vector3){esc,esc,esc}, kMpCol[id]);
        GroundBlob(dpos,0.95f,0.42f); GroundBlob(dpos,0.50f,0.50f);
    }
}

// ---- on the HUD: who is in the game, their names over their heads, and the dead man's caption ----------
static void MpDrawHud(int W, int H){
    if (g_mpMsgT>0.0f && g_mpMsg[0]){ int fs=22, tw=MeasureText(g_mpMsg,fs); float a=fminf(1.0f,g_mpMsgT);
        DrawRectangle(W/2-tw/2-14, H-142, tw+28, 34, (Color){0,0,0,(unsigned char)(150*a)});
        DrawText(g_mpMsg, W/2-tw/2, H-136, fs, (Color){255,214,90,(unsigned char)(255*a)}); }
    if (!MpActive()) return;
    for (int id=0;id<MP_MAX;id++){ const MpPlayer *p=&g_mpP[id];                    // names over heads (through walls: they are your side)
        if (!p->used || !p->alive || !p->seen || id==g_mpMe || (g_dead && id==g_mpSpec)) continue;
        Vector3 head={p->pos.x,p->pos.y+0.45f,p->pos.z}, to=Vector3Subtract(head,g_cam.position), fw=Vector3Normalize(Vector3Subtract(g_cam.target,g_cam.position));
        if (Vector3DotProduct(to,fw)<0.5f) continue;
        Vector2 s=GetWorldToScreenEx(head,g_cam,W,H); int fs=18, tw=MeasureText(p->name,fs);
        TextSh(p->name,(int)s.x-tw/2,(int)s.y-fs,fs,kMpCol[id]);
        DrawRectangle((int)s.x-20,(int)s.y+2,40,4,(Color){0,0,0,160}); DrawRectangle((int)s.x-20,(int)s.y+2,(int)(40*Clamp(p->hp/100.0f,0,1)),4,(Color){220,60,50,230}); }
    { int x=18, y=H/2-70, fs=18;                                                     // the roster
      TextSh(TextFormat("GAME %s",g_mpCode),x,y,16,(Color){170,170,180,255}); y+=22;
      for (int id=0;id<MP_MAX;id++){ int me=(id==g_mpMe); if (!me && !g_mpP[id].used) continue;
          int alive=MpPlayerAlive(id); float hp=me?g_playerHp:g_mpP[id].hp; int kills=me?g_kills:g_mpP[id].kills;
          const char *t=alive?TextFormat("%s  %3d   %d",MpNameOf(id),(int)fmaxf(0,hp),kills):TextFormat("%s  DEAD  %d",MpNameOf(id),kills);
          TextSh(t,x,y,fs,alive?kMpCol[id]:(Color){130,60,55,255}); if (id==g_mpHostId) TextSh("*",x-11,y,fs,(Color){170,170,180,255}); y+=22; } }
    if (g_dead){
        const char *t="YOU DIED"; int fs=56, tw=MeasureText(t,fs);
        TextSh(t,W/2-tw/2,96,fs,(Color){220,40,40,255});
        const char *s;
        if (g_mpOverT>0.0f) s=TextFormat("EVERYONE IS DEAD  -  WAVE %d  -  AGAIN IN %d",g_wave,(int)fmaxf(0.0f,5.0f-g_mpOverT)+1);
        else if (g_mpSpec>=0) s=TextFormat("WATCHING %s  -  click for the next player  -  the run restarts when everyone is dead",g_mpP[g_mpSpec].name);
        else s="WAITING FOR THE OTHERS";
        int sw=MeasureText(s,22); TextSh(s,W/2-sw/2,162,22,(Color){235,210,150,255});
    }
}

// ---- the multiplayer screen: start a game, or join one that is running ------------------------------
static Rectangle MpRowRect(int i, int W, int H){ float bw=520, bh=44, gap=10; (void)H; return (Rectangle){(W-bw)*0.5f, 250.0f+i*(bh+gap), bw, bh}; }
static int MpRows(void){ return 1+g_mpListN+2; }             // NEW GAME, the games, NAME, BACK
static void MpLobbyGo(int row){
    int games=g_mpListN;
    if (row==0){ if (g_mpConn==2 && !g_mpWant){ g_mpWant=1; MpCreate(); } else if (g_mpConn==0) MpConnect(); }
    else if (row<=games){ if (g_mpConn==2 && !g_mpWant){ g_mpWant=2; MpJoinCode(g_mpList[row-1].code); } }
    else if (row==games+1){ g_mpEdit=1; g_mpName[0]=0; while (GetCharPressed()>0) {} }
    else MpCloseLobby();
}
static void MpLobbyUpdate(void){
    int W=GetScreenWidth(), H=GetScreenHeight(), rows=MpRows();
    if (g_mpEdit){                                           // three letters or digits
        int c; while ((c=GetCharPressed())>0){ if (c>='a' && c<='z') c-=32; int len=(int)strlen(g_mpName);
            if (((c>='A'&&c<='Z')||(c>='0'&&c<='9')) && len<3){ g_mpName[len]=(char)c; g_mpName[len+1]=0; } }
        if (IsKeyPressed(KEY_BACKSPACE) && g_mpName[0]) g_mpName[strlen(g_mpName)-1]=0;
        if (strlen(g_mpName)==3 && (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) g_mpEdit=0;
        return; }
    if (IsKeyPressed(KEY_ESCAPE)){ MpCloseLobby(); return; }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) g_mpSel=(g_mpSel+1)%rows;
    if (IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W)) g_mpSel=(g_mpSel-1+rows)%rows;
    if (g_mpSel>=rows) g_mpSel=rows-1;
    Vector2 mp=GetMousePosition();
    for (int i=0;i<rows;i++) if (CheckCollisionPointRec(mp,MpRowRect(i,W,H))){ if (Vector2Length(GetMouseDelta())>0.0f) g_mpSel=i; if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){ g_mpSel=i; MpLobbyGo(i); return; } }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) MpLobbyGo(g_mpSel);
}
static void MpLobbyDraw(void){
    int W=GetScreenWidth(), H=GetScreenHeight(), rows=MpRows();
    const Color cream={238,234,222,255}, red={214,40,28,255}, dim={150,146,138,255};
    DrawRectangle(0,0,W,H,(Color){4,3,3,232});
    DrawRectangleGradientV(0,0,W,H/3,(Color){60,6,4,70},(Color){0,0,0,0});
    Rectangle r0=MpRowRect(0,W,H);
    DrawText("CHERNOBYL 2  //  LIQUIDATOR",(int)r0.x,110,16,red);
    DrawText("MULTIPLAYER",(int)r0.x,132,44,cream);
    DrawRectangle((int)r0.x,184,(int)r0.width,2,red);
    const char *st=(g_mpConn==2)?(g_mpWant?"starting...":(g_mpListN?"Up to 8 players. Start a game, or join one that is running:":"Up to 8 players. Nobody is playing just now: start a game and others can join it."))
                  :(g_mpConn==1)?"reaching the server...":(g_mpMsgT>0.0f?g_mpMsg:"not connected");
    DrawText(st,(int)r0.x,200,18,dim);
    for (int i=0;i<rows;i++){ Rectangle r=MpRowRect(i,W,H); int on=(i==g_mpSel), fs=22; char t[96]; const char *right="";
        if (i==0) snprintf(t,sizeof t,"START A NEW GAME");
        else if (i<=g_mpListN){ snprintf(t,sizeof t,"JOIN %s'S GAME",g_mpList[i-1].name);
            right=TextFormat("%d/8   %s",g_mpList[i-1].players,g_mpList[i-1].wave>0?TextFormat("wave %d",g_mpList[i-1].wave):"starting"); }
        else if (i==g_mpListN+1){ if (g_mpEdit) snprintf(t,sizeof t,"YOUR NAME:  %s%s",g_mpName,(fmod(GetTime(),0.8)<0.4 && strlen(g_mpName)<3)?"_":""); else snprintf(t,sizeof t,"YOUR NAME:  %s",g_mpName);
            right=g_mpEdit?"3 letters, ENTER":"change"; }
        else snprintf(t,sizeof t,"BACK");
        int off=(i==0 || (i<=g_mpListN)) && g_mpConn!=2;
        DrawRectangleRec(r, on?(Color){52,14,11,245}:(Color){22,20,20,235});
        DrawRectangle((int)r.x,(int)r.y,on?8:3,(int)r.height,on?red:(Color){110,34,28,255});
        DrawText(t,(int)r.x+26,(int)(r.y+r.height/2-fs/2),fs,off?dim:(on?(Color){255,250,240,255}:cream));
        if (*right) DrawText(right,(int)(r.x+r.width-20-MeasureText(right,18)),(int)(r.y+r.height/2-9),18,(Color){255,210,60,255}); }
    Rectangle rl=MpRowRect(rows-1,W,H);
    DrawText("UP / DOWN and ENTER, or the mouse.   ESC: back.",(int)r0.x,(int)(rl.y+rl.height+18),16,dim);
}
#endif
