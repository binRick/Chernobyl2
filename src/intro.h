// Title intro: the archival montage (assets/intro.mpg, MPEG-1 via pl_mpeg) played to
// its own music (assets/intro_music.mp3), then the CHERNOBYL 2 title held with
// "PRESS ANY KEY" while Hell March plays on. Any key during the montage jumps to the
// title (landing on the hammer-and-sickle stamp); a key on the title fades out into
// the game. Built by tools/intro/build_intro.py; included once from main.c.
//
// The video is slaved to the music clock (GetMusicTimePlayed), so picture and sound
// never drift. The intro keeps its music even under --mute (the montage is mostly its
// soundtrack); --mute still silences the game itself. --no-intro skips it entirely, and
// automated runs (--frames/--stress/--shot/--shot-every/--kite) always skip it.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#pragma clang diagnostic pop

static int g_noIntro = 0;                 // --no-intro (or CHERNOBYL_NO_INTRO=1)

static const char *IntroAsset(const char *name, char *buf, int n){
    snprintf(buf, n, "assets/%s", name);   if (FileExists(buf)) return buf;
    snprintf(buf, n, "../assets/%s", name); if (FileExists(buf)) return buf;
    return NULL;
}

// Draw a texture letterboxed to the window.
static void IntroBlit(Texture2D t, Color tint){
    float sw=(float)GetScreenWidth(), sh=(float)GetScreenHeight();
    float s=fminf(sw/(float)t.width, sh/(float)t.height);
    float w=t.width*s, h=t.height*s;
    DrawTexturePro(t, (Rectangle){0,0,(float)t.width,(float)t.height},
                   (Rectangle){(sw-w)*0.5f,(sh-h)*0.5f,w,h}, (Vector2){0,0}, 0.0f, tint);
}

// CHERNOBYL_INTRO_AUTO="skipAt,startAt" (seconds since the intro began): dev/test only --
// presses "a key" itself, so the intro can be exercised without a human at the keyboard.
static float g_introAuto[2]={-1,-1}, g_introWall=0.0f;
static float g_introFocusT=0.0f, g_introShot=-1.0f;          // seconds the window has been focused
static int IntroAnyInput(void){
    for (int i=0;i<2;i++) if (g_introAuto[i]>=0 && g_introWall>=g_introAuto[i]){ g_introAuto[i]=-1; return 1; }
    int k=GetKeyPressed();
    int click=IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    // the click that focuses the window (or anything in the first moments) is not a skip
    if (g_introWall<1.5f || g_introFocusT<0.6f){ while (GetKeyPressed()!=0) {} return 0; }
    return k!=0 || click;
}

// Returns 0 if the window was closed during the intro (quit), else 1.
static int RunIntro(void){
    if (g_noIntro || getenv("CHERNOBYL_NO_INTRO") || g_maxFrames>0 || g_stress || g_shotFrame>0 || g_shotEvery>0 || g_kite)
        return 1;
    char pv[256], pm[256], pt[256], pp[256], px[256];
    const char *fv=IntroAsset("intro.mpg",pv,sizeof pv), *fm=IntroAsset("intro_music.mp3",pm,sizeof pm);
    const char *ft=IntroAsset("intro_title.png",pt,sizeof pt), *fp=IntroAsset("intro_press.png",pp,sizeof pp);
    const char *fx=IntroAsset("intro_times.txt",px,sizeof px);
    char pc[256]; const char *fc=IntroAsset("intro_controls.png",pc,sizeof pc);
    if (!fv || !ft){ DebugLog("intro","\"skipped\":\"assets missing\""); return 1; }

    float tStamp=55.0f, tEnd=59.0f;                       // from the build (intro_times.txt)
    if (fx){ char *txt=LoadFileText(fx);
        if (txt){ const char *s;
            if ((s=strstr(txt,"stamp "))) tStamp=(float)atof(s+6);
            if ((s=strstr(txt,"end ")))   tEnd=(float)atof(s+4);
            UnloadFileText(txt); } }

    plm_t *plm=plm_create_with_filename(fv);
    if (!plm){ DebugLog("intro","\"skipped\":\"bad video\""); return 1; }
    plm_set_audio_enabled(plm,0); plm_set_loop(plm,0);
    int vw=plm_get_width(plm), vh=plm_get_height(plm);
    unsigned char *rgba=(unsigned char*)malloc((size_t)vw*vh*4);
    memset(rgba,255,(size_t)vw*vh*4);                    // opaque: plm_frame_to_rgba never writes alpha
    Image blank={ rgba, vw, vh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D vt=LoadTextureFromImage(blank);
    Texture2D tt=LoadTexture(ft);
    Texture2D pt2=fp?LoadTexture(fp):(Texture2D){0};
    Texture2D ct=fc?LoadTexture(fc):(Texture2D){0};     // controls panel (same 1280x720 frame as the title)
    if (ct.id) SetTextureFilter(ct,TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(vt,TEXTURE_FILTER_BILINEAR); SetTextureFilter(tt,TEXTURE_FILTER_BILINEAR);

    float vol=GetMasterVolume();
    if (g_mute || getenv("CHERNOBYL_MUTE")) SetMasterVolume(1.0f);   // the intro keeps its soundtrack
    Music mu={0}; int hasMu=0;
    if (g_audio && fm){ mu=LoadMusicStream(fm); hasMu=IsMusicValid(mu); }
    if (hasMu){ mu.looping=true; SetMusicVolume(mu,1.0f); PlayMusicStream(mu); }
    DebugLog("intro","\"video\":\"%s\",\"w\":%d,\"h\":%d,\"music\":%d", fv, vw, vh, hasMu);

    double clock=0.0, vidT=-1.0, fps=plm_get_framerate(plm); if (fps<=0) fps=25.0;
    { const char *au=getenv("CHERNOBYL_INTRO_AUTO"); if (au) sscanf(au,"%f,%f",&g_introAuto[0],&g_introAuto[1]); }
    { const char *sh=getenv("CHERNOBYL_INTRO_SHOT"); if (sh) g_introShot=(float)atof(sh); }
    int stage=0, ok=1;                                    // 0 montage, 1 title, 2 fade out
    float stageT=0.0f;
    while (GetKeyPressed()!=0) {}                         // drop keys queued before the window
    for (;;){
        if (WindowShouldClose()){ ok=0; break; }
        float dt=GetFrameTime(); if (dt>0.1f) dt=0.1f;
        stageT+=dt; g_introWall+=dt;
        g_introFocusT = IsWindowFocused() ? g_introFocusT+dt : 0.0f;
        if (hasMu){ UpdateMusicStream(mu); clock=GetMusicTimePlayed(mu); } else clock+=dt;
        if (stage==0){
            int dirty=0, ended=0;
            while (vidT+1.0/fps <= clock){                // catch the picture up to the music
                plm_frame_t *fr=plm_decode_video(plm);
                if (!fr){ ended=1; break; }
                vidT=fr->time; plm_frame_to_rgba(fr,rgba,vw*4); dirty=1;
            }
            if (dirty) UpdateTexture(vt,rgba);
            if (IntroAnyInput()){                         // skip: straight to the stamp
                if (hasMu) SeekMusicStream(mu, tStamp-0.05f);
                stage=1; stageT=0.0f; DebugLog("intro","\"skip\":%.2f", clock);
            } else if (ended || clock>=tEnd-0.05){ stage=1; stageT=0.0f; }
        } else if (stage==1){
            if (stageT>1.0f && IntroAnyInput()){ stage=2; stageT=0.0f; }
        } else if (stage==2){
            float k=1.0f-stageT/1.2f; if (k<0) k=0;
            if (hasMu) SetMusicVolume(mu,k);
            if (stageT>=1.25f) break;
        }
        BeginDrawing();
        ClearBackground(BLACK);
        if (stage==0) IntroBlit(vt,WHITE);
        else {
            // the held title: a faint projector flicker keeps the still alive
            unsigned char f=(unsigned char)(232+GetRandomValue(0,23));
            IntroBlit(tt,(Color){f,f,f,255});
            if (ct.id){                                   // the controls fade in once the stamp has landed
                float a=(stageT-0.6f)/0.6f; if (stage==2) a=1.0f; a=a<0?0:(a>1?1:a);
                IntroBlit(ct,(Color){255,255,255,(unsigned char)(a*255)});
            }
            if (pt2.id && stageT>0.9f && stage==1){
                float a=0.55f+0.45f*sinf((float)GetTime()*3.2f);
                float sw=(float)GetScreenWidth(), sh=(float)GetScreenHeight();
                float sc=sh/720.0f;
                DrawTextureEx(pt2,(Vector2){sw*0.5f-pt2.width*sc*0.5f, sh*0.905f-pt2.height*sc*0.5f},0.0f,sc,
                              (Color){255,255,255,(unsigned char)(a*255)});
            }
            if (stage==2){ float k=stageT/1.2f; if (k>1) k=1; DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),(Color){0,0,0,(unsigned char)(k*255)}); }
        }
        EndDrawing();
        if (g_introShot>=0 && g_introWall>=g_introShot){       // dev: CHERNOBYL_INTRO_SHOT=seconds
            TakeScreenshot("intro_shot.png"); DebugLog("intro","\"shot\":%.2f,\"clock\":%.2f,\"stage\":%d", g_introWall, clock, stage);
            g_introShot=-1;
        }
    }
    while (GetKeyPressed()!=0) {}                         // the start key doesn't leak into the game
    if (hasMu){ StopMusicStream(mu); UnloadMusicStream(mu); }
    SetMasterVolume(vol);
    UnloadTexture(vt); UnloadTexture(tt); if (pt2.id) UnloadTexture(pt2); if (ct.id) UnloadTexture(ct);
    free(rgba); plm_destroy(plm);
    DebugLog("intro","\"done\":%d", ok);
    return ok;
}
