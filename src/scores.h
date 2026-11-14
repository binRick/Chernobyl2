// Leaderboard (as ../Chernobyl): when a run ends you put in up to three initials and the run is
// posted to chernobyl2.ximg.app/api/scores; the death screen then shows your rank and the top of
// the board. The server recomputes the score from wave / kills / headshots, so what is sent is
// the run, not a number to be trusted.
//
// Networking is the system `curl`, run from a worker thread through popen: nothing here ever
// blocks a frame, and a machine without a network just shows "offline". Everything put on the
// command line is built from digits and sanitised A-Z0-9, never from free text.
// Cheated runs (god mode) and headless test runs (--frames) never touch the network.
//   CHERNOBYL_SCORES_URL=http://host:port   point at another server (dev)
//   CHERNOBYL_SCORES_AUTO=ABC               submit as ABC without typing (dev / tests)
//   CHERNOBYL_NO_SCORES=1                   leaderboard off
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
// The browser build has neither curl nor threads: the same two requests go out through fetch(),
// and the frame loop polls for the answers.
EM_JS_DEPS(c2lb_deps, "$UTF8ToString,$stringToUTF8");
EM_JS(void, LbJsGo, (const char *url, const char *body, int limit), {
    var base=UTF8ToString(url), b=body?UTF8ToString(body):null;
    var st=Module.c2lb={post:0, rank:0, total:0, top:0, topText:"", busy:1};
    var top=function(){ fetch(base+"/api/scores?limit="+limit).then(function(r){return r.text();})
        .then(function(t){ st.topText=t; st.top=1; st.busy=0; }).catch(function(){ st.busy=0; }); };
    if (b) fetch(base+"/api/scores",{method:"POST",headers:{"Content-Type":"application/json"},body:b})
        .then(function(r){return r.json();}).then(function(j){ st.rank=j.rank|0; st.total=j.total|0; st.post=(st.rank>0)?1:-1; top(); })
        .catch(function(){ st.post=-1; top(); });
    else top();
});
EM_JS(int, LbJsGet, (int what), { var st=Module.c2lb; if (!st) return 0; return (what==0?st.post:what==1?st.rank:what==2?st.total:what==3?st.busy:st.top)|0; });
EM_JS(int, LbJsTakeTop, (char *buf, int n), { var st=Module.c2lb; if (!st || !st.top) return 0; stringToUTF8(st.topText, buf, n); st.top=0; return 1; });
#else
#include <pthread.h>
#endif

#define LB_TOP 8
enum { LB_OFF=0, LB_ENTER, LB_POSTING, LB_DONE, LB_FAILED };
typedef struct { char ini[4]; int score, wave, kills; } LbRow;
static struct {
    int state;                       // LB_*
    char ini[4]; int n;              // initials being typed
    int prefilled;                   // they are last run's, shown ready to reuse: the first letter typed replaces them
    int score, kills, headshots, wave, shots, acc; float time; char weapon[24];
    volatile int postDone, rank, total;              // worker -> main
    volatile int topReady; LbRow top[LB_TOP]; int topN;
    LbRow topW[LB_TOP]; int topWN;                   // worker's copy, published by topReady
    int busy;                                        // a worker is running
} g_lb;
static char g_lbUrl[160] = "https://chernobyl2.ximg.app";

static int LbEnabled(void){
    if (getenv("CHERNOBYL_NO_SCORES")) return 0;
    const char *u=getenv("CHERNOBYL_SCORES_URL");
    if (u && *u){ int ok=1; for (const char *c=u;*c;c++) if (!(isalnum((unsigned char)*c) || strchr(":/.-_",*c))) ok=0;   // plain URL only
        if (ok && strlen(u)<sizeof g_lbUrl){ strcpy(g_lbUrl,u); return 1; } return 0; }
#if defined(__EMSCRIPTEN__)
    strcpy(g_lbUrl,"");              // same origin as the page: /api/scores
#endif
    return g_maxFrames<=0;           // real play only: test runs stay off the network
}
// Pull the first LB_TOP rows out of {"scores":[{...},...]} -- the server sends them best first.
static const char *LbFind(const char *p, const char *key){ char k[32]; snprintf(k,sizeof k,"\"%s\":",key); const char *q=strstr(p,k); return q?q+strlen(k):NULL; }
static int LbParseTop(const char *js, LbRow *out){
    int n=0; const char *p=strstr(js,"\"scores\":["); if (!p) return 0;
    while (n<LB_TOP){
        const char *o=strchr(p,'{'); if (!o) break;
        const char *e=strchr(o,'}'); if (!e) break;
        char obj[700]; size_t len=(size_t)(e-o); if (len>=sizeof obj) len=sizeof obj-1; memcpy(obj,o,len); obj[len]=0;
        LbRow r={0}; const char *v;
        if ((v=LbFind(obj,"initials")) && *v=='"'){ v++; int k=0; while (k<3 && isalnum((unsigned char)v[k])){ r.ini[k]=v[k]; k++; } }
        if ((v=LbFind(obj,"score"))) r.score=atoi(v);
        if ((v=LbFind(obj,"wave")))  r.wave=atoi(v);
        if ((v=LbFind(obj,"kills"))) r.kills=atoi(v);
        if (r.ini[0]) out[n++]=r;
        p=e+1;
    }
    return n;
}
#if !defined(__EMSCRIPTEN__)
#if !defined(C2_IOS)
static int LbCurl(const char *args, char *out, int outSz){
    char cmd[900]; snprintf(cmd,sizeof cmd,"/usr/bin/curl -s -m 8 %s 2>/dev/null", args);
    FILE *f=popen(cmd,"r"); if (!f) return 0;
    int n=(int)fread(out,1,outSz-1,f); out[n>0?n:0]=0; pclose(f);
    return n>0;
}
#endif
static void *LbWorker(void *arg){
    int post=(arg!=NULL); static char buf[8192];
    if (post){
        char a[700];
        snprintf(a,sizeof a,"-X POST -H 'Content-Type: application/json' -d '{\"initials\":\"%s\",\"score\":%d,\"kills\":%d,\"headshots\":%d,\"wave\":%d,"
                 "\"weapon\":\"%s\",\"time\":%.1f,\"shots\":%d,\"accuracy\":%d,\"platform\":\"macos\",\"version\":\"dev\"}' %s/api/scores",
                 g_lb.ini, g_lb.score, g_lb.kills, g_lb.headshots, g_lb.wave, g_lb.weapon, g_lb.time, g_lb.shots, g_lb.acc, g_lbUrl);
#if defined(C2_IOS)                    /* no curl on a phone: the same request through the system (ios/C2Platform.m) */
        char body[560], url[240];
        snprintf(body,sizeof body,"{\"initials\":\"%s\",\"score\":%d,\"kills\":%d,\"headshots\":%d,\"wave\":%d,"
                 "\"weapon\":\"%s\",\"time\":%.1f,\"shots\":%d,\"accuracy\":%d,\"platform\":\"ios\",\"version\":\"dev\"}",
                 g_lb.ini, g_lb.score, g_lb.kills, g_lb.headshots, g_lb.wave, g_lb.weapon, g_lb.time, g_lb.shots, g_lb.acc);
        snprintf(url,sizeof url,"%s/api/scores",g_lbUrl); (void)a;
        int ok=C2HttpRequest(url,body,buf,sizeof buf); const char *v;
#else
        int ok=LbCurl(a,buf,sizeof buf); const char *v;
#endif
        if (ok && (v=LbFind(buf,"rank"))){ g_lb.rank=atoi(v); if ((v=LbFind(buf,"total"))) g_lb.total=atoi(v); g_lb.postDone=1; }
        else g_lb.postDone=-1;
    }
    { char a[240];
#if defined(C2_IOS)
      snprintf(a,sizeof a,"%s/api/scores?limit=%d", g_lbUrl, LB_TOP);
      if (C2HttpRequest(a,NULL,buf,sizeof buf)){
#else
      snprintf(a,sizeof a,"'%s/api/scores?limit=%d'", g_lbUrl, LB_TOP);
      if (LbCurl(a,buf,sizeof buf)){
#endif
          g_lb.topWN=LbParseTop(buf,g_lb.topW); g_lb.topReady=1; } }
    g_lb.busy=0;
    return NULL;
}
static void LbSpawn(int post){
    if (g_lb.busy) return;
    g_lb.busy=1; pthread_t t;
    if (pthread_create(&t,NULL,LbWorker,post?(void*)1:NULL)==0) pthread_detach(t); else { g_lb.busy=0; if (post) g_lb.postDone=-1; }
}
static void LbPoll(void){}
#else
static void LbSpawn(int post){
    if (g_lb.busy) return;
    char body[520]="";
    if (post) snprintf(body,sizeof body,"{\"initials\":\"%s\",\"score\":%d,\"kills\":%d,\"headshots\":%d,\"wave\":%d,"
                 "\"weapon\":\"%s\",\"time\":%.1f,\"shots\":%d,\"accuracy\":%d,\"platform\":\"web\",\"version\":\"dev\"}",
                 g_lb.ini, g_lb.score, g_lb.kills, g_lb.headshots, g_lb.wave, g_lb.weapon, g_lb.time, g_lb.shots, g_lb.acc);
    g_lb.busy=1; LbJsGo(g_lbUrl, post?body:NULL, LB_TOP);
}
static void LbPoll(void){                                  // the answers, as they land
    static char buf[8192];
    if (g_lb.state==LB_POSTING && !g_lb.postDone){ int p=LbJsGet(0); if (p){ g_lb.rank=LbJsGet(1); g_lb.total=LbJsGet(2); g_lb.postDone=p; } }
    if (LbJsTakeTop(buf,sizeof buf)){ g_lb.topWN=LbParseTop(buf,g_lb.topW); g_lb.topReady=1; }
    if (g_lb.busy && !LbJsGet(3)) g_lb.busy=0;
}
#endif
// The run just ended.
static void LbOnDeath(int score, const char *weapon){
    memset(&g_lb,0,sizeof g_lb);
    if (g_cheatUsed || g_godMode || score<=0 || !LbEnabled()){ g_lb.state=LB_OFF; return; }
    g_lb.score=score; g_lb.kills=g_kills; g_lb.headshots=g_headshots; g_lb.wave=g_wave; g_lb.shots=g_shotsFired;
    g_lb.acc=g_shotsFired>0?(int)(100.0f*g_shotsHit/g_shotsFired+0.5f):0; g_lb.time=g_runT;
    { int k=0; for (const char *c=weapon; c && *c && k<23; c++) if (isalnum((unsigned char)*c) || *c==' ' || *c=='-') g_lb.weapon[k++]=*c; g_lb.weapon[k]=0; }
    strcpy(g_lb.ini,g_lbSaved); g_lb.n=(int)strlen(g_lb.ini); g_lb.prefilled=(g_lb.n>0);
    g_lb.state=LB_ENTER;
    LbSpawn(0);                                           // the board, while he types
    const char *au=getenv("CHERNOBYL_SCORES_AUTO");
    if (au && *au){ g_lb.n=0; for (const char *c=au; *c && g_lb.n<3; c++) if (isalnum((unsigned char)*c)) g_lb.ini[g_lb.n++]=(char)toupper((unsigned char)*c); g_lb.ini[g_lb.n]=0; }
}
#if defined(C2_IOS)
// The phone has no keys to type initials on: a small board of them along the bottom of the death
// screen. 26 letters, then DEL, OK (post) and SKIP (play again without posting).
#define LBK_N 29
static Rectangle LbKeyRect(int i, int W, int H){
    int row=i<10?0:(i<20?1:2), col=i-(row==0?0:(row==1?10:20)), n=(row==2)?9:10;
    float kw=(i>=26)?104.0f:64.0f, kh=54.0f, gap=8.0f;
    float rowW=(row==2)?(6*64.0f+3*104.0f+8*gap):(10*64.0f+9*gap);
    float x=W/2.0f-rowW/2.0f; for (int c=0;c<col;c++) x+=((row==2 && c>=6)?104.0f:64.0f)+gap;
    (void)n; return (Rectangle){x, H-196.0f+row*(kh+gap), kw, kh};
}
static const char *LbKeyLabel(int i){ static char b[2]; if (i<26){ b[0]=(char)('A'+i); b[1]=0; return b; } return i==26?"DEL":(i==27?"OK":"SKIP"); }
static int LbKeyHit(int W, int H){                     // the key under this frame's tap, or -1
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return -1;
    Vector2 m=GetMousePosition();
    for (int i=0;i<LBK_N;i++) if (CheckCollisionPointRec(m, LbKeyRect(i,W,H))) return i;
    return -1;
}
#endif
// One frame of the death screen's input. Returns 1 when the player asks to play again.
static int LbDeadUpdate(void){
    LbPoll();
    if (g_lb.topReady){ g_lb.topN=g_lb.topWN; memcpy(g_lb.top,g_lb.topW,sizeof g_lb.top); g_lb.topReady=0; }
    int enter=IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_KP_ENTER), click=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (g_lb.state==LB_ENTER){
        int ch; while ((ch=GetCharPressed())>0){ ch=toupper(ch); if (ch>=128 || !isalnum(ch)) continue;
            if (g_lb.prefilled){ g_lb.n=0; g_lb.ini[0]=0; g_lb.prefilled=0; }      // typing over last run's initials starts them afresh
            if (g_lb.n<3){ g_lb.ini[g_lb.n++]=(char)ch; g_lb.ini[g_lb.n]=0; } }
        if (IsKeyPressed(KEY_BACKSPACE) && g_lb.n>0){ g_lb.ini[--g_lb.n]=0; g_lb.prefilled=0; }
#if defined(C2_IOS)
        { int k=LbKeyHit(GetScreenWidth(),GetScreenHeight()); click=0;                 // only the board's keys do anything here: a stray tap is not "skip"
          if (k>=0 && k<26){ if (g_lb.prefilled){ g_lb.n=0; g_lb.ini[0]=0; g_lb.prefilled=0; }
                             if (g_lb.n<3){ g_lb.ini[g_lb.n++]=(char)('A'+k); g_lb.ini[g_lb.n]=0; } }
          else if (k==26){ if (g_lb.n>0){ g_lb.ini[--g_lb.n]=0; g_lb.prefilled=0; } }
          else if (k==27) enter=1;
          else if (k==28) return 1; }
#endif
        int autoGo=(getenv("CHERNOBYL_SCORES_AUTO")!=NULL) && g_lb.n>0 && !g_lb.busy;
        if ((enter || autoGo) && g_lb.n>0){
            if (g_lb.busy) return 0;                      // the board fetch is still out: next frame
            if (!getenv("CHERNOBYL_SCORES_AUTO")){ strcpy(g_lbSaved,g_lb.ini); SaveOptions(); }   // (a test run's initials are not the player's)
            g_lb.state=LB_POSTING; LbSpawn(1); ExtraPlay(EX_RANK,0.8f);                      // the score goes up
            DebugLog("score","\"post\":\"%s\",\"score\":%d", g_lb.ini, g_lb.score);
            return 0; }
        return click;                                     // a click skips the board and plays again
    }
    if (g_lb.state==LB_POSTING){
        if (g_lb.postDone>0){ g_lb.state=LB_DONE; DebugLog("score","\"rank\":%d,\"total\":%d", g_lb.rank, g_lb.total); }
        else if (g_lb.postDone<0){ g_lb.state=LB_FAILED; DebugLog("score","\"failed\":true"); }
        return 0;
    }
    return enter || click;
}
static void LbDraw(int W, int H){
    if (g_lb.state==LB_OFF) return;
    int y=H/2+108; char t[96]; Color hi={255,210,60,255}, dim={200,200,210,255};
    if (g_lb.state==LB_ENTER){
        snprintf(t,sizeof t,"INITIALS  %c %c %c", g_lb.n>0?g_lb.ini[0]:'_', g_lb.n>1?g_lb.ini[1]:'_', g_lb.n>2?g_lb.ini[2]:'_');
        int w=MeasureText(t,30); DrawText(t,W/2-w/2,y,30,hi);
#if defined(C2_IOS)
        (void)dim; Vector2 m=GetMousePosition(); int down=IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        for (int i=0;i<LBK_N;i++){ Rectangle r=LbKeyRect(i,W,H); int on=down && CheckCollisionPointRec(m,r);
            Color fill = i==27 ? (Color){70,120,60,on?255:215} : (i==28 ? (Color){70,70,80,on?255:215} : (Color){36,34,34,on?255:215});
            DrawRectangleRounded(r,0.22f,6,on?(Color){150,60,40,255}:fill); DrawRectangleRoundedLines(r,0.22f,6,(Color){255,210,60,i==27?230:110});
            const char *l=LbKeyLabel(i); int fs=(i<26)?30:22, lw=MeasureText(l,fs);
            DrawText(l,(int)(r.x+r.width/2-lw/2),(int)(r.y+r.height/2-fs/2),fs,(i==27 && g_lb.n==0)?(Color){150,150,150,255}:RAYWHITE); }
#else
        const char *p=g_lb.prefilled?"ENTER  -  post score      type to change      click  -  skip"
                     :g_lb.n>0?"ENTER  -  post score      BACKSPACE  -  delete      click  -  skip":"type your initials      click  -  skip";
        int pw=MeasureText(p,18); DrawText(p,W/2-pw/2,y+38,18,dim);
#endif
    } else if (g_lb.state==LB_POSTING){
        const char *p="posting score..."; int pw=MeasureText(p,22); DrawText(p,W/2-pw/2,y,22,dim);
    } else if (g_lb.state==LB_DONE){
        snprintf(t,sizeof t,"%s   RANK #%d of %d", g_lb.ini, g_lb.rank, g_lb.total);
        int w=MeasureText(t,30); DrawText(t,W/2-w/2,y,30,hi);
    } else if (g_lb.state==LB_FAILED){
        const char *p="leaderboard offline - score not posted"; int pw=MeasureText(p,20); DrawText(p,W/2-pw/2,y,20,(Color){220,120,100,255});
    }
    if (g_lb.topN>0){                                     // the top of the board, to the right
        int x=W-300, ty=H/2-150;
        DrawRectangle(x-16,ty-14,292,44+g_lb.topN*26,(Color){0,0,0,140});
        DrawText("TOP SCORES",x,ty,22,hi); ty+=34;
        for (int i=0;i<g_lb.topN;i++){ LbRow *r=&g_lb.top[i];
            int me=(g_lb.state==LB_DONE && g_lb.rank==i+1);
            snprintf(t,sizeof t,"%2d  %-3s  %6d   w%d", i+1, r->ini, r->score, r->wave);
            DrawText(t,x,ty+i*26,20, me?hi:(Color){225,225,230,255}); }
    }
}
