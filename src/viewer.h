// Enemy model viewer: two enemy .glb files side by side ("BEFORE" / "AFTER") on a dark
// stage, lit like the arena (a warm key lamp, a cold back lamp, the player's fill) and
// drawn with the game's own enemy shader, so what you judge here is what the game shows.
//
//   ./build/chernobyl2 --viewer assets/cook.glb [--viewer-b build/enemy_v0/cook.glb]
//   keys: 1-6 clip (idle walk run attack hit death)  SPACE pause  LEFT/RIGHT turn
//         UP/DOWN zoom  F face close-up  ESC quit;  mouse drag turns the models
//   --viewer-shots DIR : render a fixed set of comparison stills into DIR and exit
// Included once from main.c.

static const char *g_viewA=NULL, *g_viewB=NULL, *g_viewShots=NULL;

static void ViewerLights(Vector3 cam){
    // key: warm, high front-left; back: cold, behind right (rim + specular); fill rides the camera
    float pos[15]={ -2.2f,3.2f,2.6f,   2.4f,2.6f,-2.2f,   cam.x,cam.y+0.6f,cam.z };
    float col[15]={ 1.00f,0.66f,0.32f, 0.30f,0.55f,1.00f, 0.42f,0.42f,0.45f };
    float rad[5]={ 9.0f, 9.0f, 16.0f };
    int n=3; float amb[3]={0.30f,0.29f,0.31f}, vp[3]={cam.x,cam.y,cam.z};
    SetShaderValueV(g_skinShader,g_skinLoc[0],pos,SHADER_UNIFORM_VEC3,n);
    SetShaderValueV(g_skinShader,g_skinLoc[1],col,SHADER_UNIFORM_VEC3,n);
    SetShaderValueV(g_skinShader,g_skinLoc[2],rad,SHADER_UNIFORM_FLOAT,n);
    SetShaderValue(g_skinShader,g_skinLoc[3],&n,SHADER_UNIFORM_INT);
    SetShaderValue(g_skinShader,g_skinLoc[4],amb,SHADER_UNIFORM_VEC3);
    SetShaderValue(g_skinShader,g_skinLoc[5],vp,SHADER_UNIFORM_VEC3);
}

static void ViewerDraw(EnemyRig *rg, int role, float t, Vector3 at, float yawDeg){
    if (!rg->ok) return;
    if (rg->animN>0){ ModelAnimation *an=&rg->anim[rg->clip[role]]; int nf=ANIM_FRAMES(*an);
        float f=fmodf(t*30.0f,(float)(nf>1?nf:1)); RigPose(rg,an,f); }
    DrawModelEx(rg->model,at,(Vector3){0,1,0},yawDeg+ENEMY_YAW_OFFSET,(Vector3){ENEMY_SCALE,ENEMY_SCALE,ENEMY_SCALE},WHITE);
}

static float ViewerHeadY(EnemyRig *rg){
    Model *m=&rg->model;
    for (int b=0;b<m->skeleton.boneCount;b++)
        if (!strcmp(m->skeleton.bones[b].name,"Head") && m->skeleton.bindPose){
            float y=m->skeleton.bindPose[b].translation.y*ENEMY_SCALE;
            if (y>0.8f && y<3.5f) return y+0.06f;            // bone root sits at the jaw: aim at the eyes
        }
    return 1.60f;
}

static int RunViewer(void){
    EnemyRig A={0}, B={0};
    if (!LoadEnemyRig(&A,g_viewA)){ fprintf(stderr,"viewer: can't load %s\n",g_viewA); return 1; }
    int two = g_viewB && LoadEnemyRig(&B,g_viewB);
    static const char *roleName[]={"walk","idle","run","hit","attack","death"};
    int role=CL_IDLE, paused=0; float t=0.0f, turn=0.0f, dist=3.4f, look=1.05f; int face=0;
    EnableCursor();
    // still set: (role, time, turn degrees, close-up?)
    struct { int role; float t, turn; int face; const char *name; } shots[]={
        {CL_IDLE,0.6f,0,0,"front"}, {CL_IDLE,0.6f,35,0,"three_quarter"}, {CL_IDLE,0.6f,0,1,"face"},
        {CL_IDLE,0.6f,40,1,"face_side"}, {CL_ATTACK,0.25f,20,0,"attack"}, {CL_WALK,0.4f,-30,0,"walk"} };
    int nshots=(int)(sizeof shots/sizeof shots[0]), shot=0, settle=0;
    RenderTexture2D rtA={0}, rtB={0}; int rtOk=0;
    // each model's eye line from its rig's Head bone (bind pose), not the bounding box --
    // hats, toques and tentacles would throw that off
    float hA=ViewerHeadY(&A), hB=two?ViewerHeadY(&B):hA;
    while (!WindowShouldClose()){
        float dt=GetFrameTime();
        if (g_viewShots){                                        // scripted stills
            role=shots[shot].role; t=shots[shot].t; turn=shots[shot].turn; face=shots[shot].face; paused=1;
        } else {
            for (int k=0;k<6;k++) if (IsKeyPressed(KEY_ONE+k)){ role=(int[]){CL_IDLE,CL_WALK,CL_RUN,CL_ATTACK,CL_HIT,CL_DEATH}[k]; t=0; }
            if (IsKeyPressed(KEY_SPACE)) paused=!paused;
            if (IsKeyPressed(KEY_F)) face=!face;
            if (IsKeyDown(KEY_LEFT))  turn-=90*dt;
            if (IsKeyDown(KEY_RIGHT)) turn+=90*dt;
            if (IsKeyDown(KEY_UP))    dist-=1.5f*dt;
            if (IsKeyDown(KEY_DOWN))  dist+=1.5f*dt;
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) turn+=GetMouseDelta().x*0.4f;
            dist-=GetMouseWheelMove()*0.25f; if (dist<0.6f) dist=0.6f; if (dist>8) dist=8;
            if (!paused) t+=dt;
        }
        // split screen: each model rendered alone, centred, by the same camera -> identical framing
        int sw=GetScreenWidth(), sh=GetScreenHeight(), half=two?sw/2:sw;
        if (!rtOk || rtA.texture.width!=half || rtA.texture.height!=sh){
            if (rtOk){ UnloadRenderTexture(rtA); UnloadRenderTexture(rtB); }
            rtA=LoadRenderTexture(half,sh); rtB=LoadRenderTexture(half,sh); rtOk=1;
        }
        for (int k=0;k<(two?2:1);k++){
            // frame by the model's own height (the head chef / butcher / fiend are taller)
            float head = (k==0?hA:hB), tall = head/0.92f;     // eye line -> rough standing height
            float d = face ? 0.95f*tall/1.75f : dist*tall/1.75f, ly = face ? head+0.02f : look*tall/1.75f;
            Camera3D cam={ (Vector3){0, ly+(face?0.02f:0.15f), d}, (Vector3){0,ly,0}, (Vector3){0,1,0}, face?32.0f:42.0f, CAMERA_PERSPECTIVE };
            BeginTextureMode(k==0?rtA:rtB);
            ClearBackground((Color){14,13,15,255});
            BeginMode3D(cam);
            DrawPlane((Vector3){0,0,0},(Vector2){12,12},(Color){38,36,34,255});
            ViewerLights(cam.position);
            ViewerDraw(k==0?&A:&B,role,t,(Vector3){0,0,0},turn);
            EndMode3D();
            EndTextureMode();
        }
        BeginDrawing();
        ClearBackground(BLACK);
        // AFTER on the right, BEFORE on the left (render textures are stored upside down)
        DrawTextureRec(rtA.texture,(Rectangle){0,0,(float)half,-(float)sh},(Vector2){(float)(two?half:0),0},WHITE);
        if (two){ DrawTextureRec(rtB.texture,(Rectangle){0,0,(float)half,-(float)sh},(Vector2){0,0},WHITE);
                  DrawLine(half,0,half,sh,(Color){60,60,60,255}); }
        if (two){
            const char *lb="BEFORE", *la="AFTER";
            DrawText(lb, sw/4-MeasureText(lb,30)/2, 24, 30, (Color){200,200,200,255});
            DrawText(la, 3*sw/4-MeasureText(la,30)/2, 24, 30, (Color){255,90,70,255});
        }
        if (!g_viewShots)
            DrawText(TextFormat("%s   1-6 clip  SPACE pause  drag/arrows turn  wheel zoom  F face  ESC quit", roleName[role]),
                     14, sh-28, 18, (Color){150,150,150,255});
        EndDrawing();
        if (g_viewShots && ++settle>=4){                         // let mips/filters settle a few frames
            TakeScreenshot(TextFormat("%s/%02d_%s.png", g_viewShots, shot, shots[shot].name));
            settle=0; if (++shot>=nshots) break;
        }
    }
    return 0;
}

// ---- Enemy gallery (--viewer all / ./dev-enemies.sh) --------------------------------
// Every enemy type, one at a time, every state at its in-game speed:
//   TAB / ]  next enemy     [  previous      1 idle 2 walk 3 pursue 4 attack 5 hit 6 die 7 cast
//   A auto-cycle (every state of every enemy)   SPACE pause   drag/arrows turn
//   wheel / UP DOWN zoom   F face close-up   ESC quit
// Walk/pursue scroll the floor at the enemy's real speed (stride check); the mutant's
// throw releases a bolt at the release frame; deaths fall and hold at the settle frame.
enum { GS_IDLE, GS_WALK, GS_RUN, GS_ATTACK, GS_HIT, GS_DIE, GS_CAST, GS_N };
static const char *kGalState[GS_N]={"IDLE","WALK","PURSUE","ATTACK","HIT","DIE","CAST"};
static const int   kGalRole[GS_N]={CL_IDLE,CL_WALK,CL_RUN,CL_ATTACK,CL_HIT,CL_DEATH,CL_CAST};
static const char *kGalKind[]={"melee","hitscan","bolt thrower","raises the dead"};

static float GalFps(int type, int st){
    int k=kEnemy[type].kind;
    if (st==GS_ATTACK) return (k==AK_MELEE||k==AK_RAISE)?90.0f:(k==AK_BOLT?BOLT_THROW_FPS:30.0f);
    if (st==GS_DIE) return 60.0f;
    return 30.0f;
}

static int RunGallery(void){
    for (int t=0;t<EN_NTYPES;t++){ char p[96]; snprintf(p,sizeof p,"assets/%s.glb",kEnemy[t].name); LoadEnemyRig(&g_rig[t],p); }
    int type=0, st=GS_WALK, paused=0, autoMode=0, face=0;
    float frame=0, turn=25.0f, dist=3.6f, stT=0, hold=0;
    Vector3 boltP={0}; float boltT=-1;
    int galShots=getenv("CHERNOBYL_GALLERY_TEST")?atoi(getenv("CHERNOBYL_GALLERY_TEST")):0, galShot=0; float galClock=0;
    if (galShots>0){ autoMode=1; type=EN_MUTANT; st=GS_IDLE; }   // starts on the mutant: covers the bolt throw
    // dev: CHERNOBYL_GALLERY_POSE="name:state:f1,f2,..." -> one still per clip fraction, then exit
    float poseF[16]; int poseN=0, poseI=0;
    { const char *ps=getenv("CHERNOBYL_GALLERY_POSE");
      if (ps){ char nm[32]={0}; int stn=0; const char *c=strchr(ps,':');
        if (c && c-ps<31){ memcpy(nm,ps,c-ps); for (int t=0;t<EN_NTYPES;t++) if (!strcmp(nm,kEnemy[t].name)) type=t;
          stn=atoi(c+1); st=stn-1; const char *f=strchr(c+1,':');
          while (f && poseN<16){ poseF[poseN++]=(float)atof(f+1); f=strchr(f+1,','); } }
        paused=1; } }
    if (getenv("CHERNOBYL_GALLERY_TURN")) turn=(float)atof(getenv("CHERNOBYL_GALLERY_TURN"));   // dev: camera angle
    // CHERNOBYL_GALLERY_START="name[:clip]" (dev-enemies.sh gunner 4): open on that enemy / clip, playing
    { const char *ss=getenv("CHERNOBYL_GALLERY_START");
      if (ss && *ss){ char nm[32]={0}; const char *c=strchr(ss,':'); size_t n=c?(size_t)(c-ss):strlen(ss);
        if (n<sizeof nm){ memcpy(nm,ss,n); for (int t=0;t<EN_NTYPES;t++) if (!strcmp(nm,kEnemy[t].name)) type=t; }
        if (c){ int k=atoi(c+1); if (k>=1 && k<=GS_N) st=k-1; } } }
    EnableCursor();
    while (!WindowShouldClose()){
        float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;
        EnemyRig *rg=&g_rig[type];
        int prevType=type, prevSt=st;
        if (IsKeyPressed(KEY_TAB)||IsKeyPressed(KEY_RIGHT_BRACKET)) type=(type+1)%EN_NTYPES;
        if (IsKeyPressed(KEY_LEFT_BRACKET)) type=(type+EN_NTYPES-1)%EN_NTYPES;
        for (int k=0;k<GS_N;k++) if (IsKeyPressed(KEY_ONE+k)) st=k;
        if (IsKeyPressed(KEY_A)) autoMode=!autoMode;
        if (IsKeyPressed(KEY_SPACE)) paused=!paused;
        if (IsKeyPressed(KEY_F)) face=!face;
        if (IsKeyDown(KEY_LEFT))  turn-=90*dt;
        if (IsKeyDown(KEY_RIGHT)) turn+=90*dt;
        if (IsKeyDown(KEY_UP))    dist-=1.5f*dt;
        if (IsKeyDown(KEY_DOWN))  dist+=1.5f*dt;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) turn+=GetMouseDelta().x*0.4f;
        dist-=GetMouseWheelMove()*0.25f; if (dist<0.8f) dist=0.8f; if (dist>9) dist=9;
        if (st==GS_CAST && kEnemy[type].kind!=AK_RAISE) st=GS_ATTACK;          // only the priest casts
        if (type!=prevType || st!=prevSt){ frame=0; stT=0; hold=0; boltT=-1;
        }
        rg=&g_rig[type];
        if (!rg->ok || rg->animN<=0){ type=(type+1)%EN_NTYPES; continue; }
        ModelAnimation *an=&rg->anim[rg->clip[kGalRole[st]]];
        int nf=ANIM_FRAMES(*an); if (nf<1) nf=1;
        if (!paused){
            stT+=dt;
            float prev=frame; frame+=dt*GalFps(type,st);
            if (st==GS_DIE){ if (frame>=(float)rg->deathFrame){ frame=(float)rg->deathFrame; hold+=dt; } }
            else {
                if (st==GS_ATTACK && kEnemy[type].kind==AK_BOLT){           // the bolt leaves the hand
                    float rel=nf*BOLT_RELEASE_FRAC; if (prev<rel && frame>=rel){ boltT=0;
                        boltP=RigBoneWorld(type, CL_ATTACK, frame, "RightFingerBase", (Vector3){0,0,0}, ENEMY_YAW_OFFSET); }
                }
                if (frame>=nf) frame-=nf;
            }
            if (boltT>=0){ boltT+=dt; if (boltT>1.2f) boltT=-1; }
        }
        if (autoMode){                                                     // ~3 s a state, death held 1.5 s
            int done = (st==GS_DIE) ? (hold>1.5f) : (stT>3.0f);
            if (done){ do { st++; } while (st==GS_CAST && kEnemy[type].kind!=AK_RAISE);
                       if (st>=GS_N){ st=GS_IDLE; type=(type+1)%EN_NTYPES; }
                       frame=0; stT=0; hold=0; boltT=-1;                // fresh clock for the new state
                       rg=&g_rig[type]; if (!rg->ok || rg->animN<=0) continue;
                       an=&rg->anim[rg->clip[kGalRole[st]]]; nf=ANIM_FRAMES(*an); if (nf<1) nf=1; }
        }
        if (poseN>0) frame=poseF[poseI]*(nf-1);
        RigPose(rg,an,frame);
        float esc=ENEMY_SCALE*kEnemy[type].scale;
        float head=ViewerHeadY(rg)*kEnemy[type].scale, tall=head/0.92f;
        float d=face?0.95f*tall/1.75f:dist*tall/1.75f, ly=face?head+0.02f:1.05f*tall/1.75f;
        float yr=turn*DEG2RAD;
        Camera3D cam={ (Vector3){sinf(yr)*d, ly+(face?0.02f:0.15f), cosf(yr)*d}, (Vector3){0,ly,0}, (Vector3){0,1,0}, face?32.0f:42.0f, CAMERA_PERSPECTIVE };
        BeginDrawing();
        ClearBackground((Color){14,13,15,255});
        BeginMode3D(cam);
        DrawPlane((Vector3){0,-0.002f,0},(Vector2){30,30},(Color){34,32,30,255});
        // floor lines scroll under walk/pursue at the enemy's real speed (feet should "grip")
        float sp = (st==GS_RUN)?kEnemy[type].speed:((st==GS_WALK)?kEnemy[type].speed*0.45f:0.0f);
        float off=fmodf(GetTime()*sp,1.0f); if (paused) off=0;
        for (int z=-8;z<=8;z++){ float zz=z+off; DrawLine3D((Vector3){-8,0,zz},(Vector3){8,0,zz},(Color){60,56,52,255}); }
        for (int x=-8;x<=8;x++) DrawLine3D((Vector3){(float)x,0,-8},(Vector3){(float)x,0,8},(Color){48,45,42,255});
        ViewerLights(cam.position);
        DrawModelEx(rg->model,(Vector3){0,0,0},(Vector3){0,1,0},ENEMY_YAW_OFFSET,(Vector3){esc,esc,esc},WHITE);
        if (boltT>=0){                                                     // the radiation bolt, flying off "at the player"
            Vector3 bp={boltP.x, boltP.y-1.0f*boltT, boltP.z+13.0f*boltT};       // from the hand it left
            DrawSphere(bp,0.12f,(Color){140,255,90,255}); DrawSphereWires(bp,0.2f,6,6,(Color){90,220,60,120});
        }
        EndMode3D();
        int sw=GetScreenWidth(), sh=GetScreenHeight();
        DrawText(TextFormat("%s", kEnemy[type].name), 24, 20, 40, (Color){255,90,70,255});
        DrawText(TextFormat("%d/%d   %s   hp %.0f   speed %.1f m/s", type+1, EN_NTYPES, kGalKind[kEnemy[type].kind], kEnemy[type].hp, kEnemy[type].speed),
                 26, 66, 18, (Color){170,170,170,255});
        int x=26;
        for (int k=0;k<GS_N;k++){
            if (k==GS_CAST && kEnemy[type].kind!=AK_RAISE) continue;
            const char *lb=TextFormat("%d %s",k+1,kGalState[k]); int w=MeasureText(lb,22);
            Color c = (k==st)?(Color){255,220,90,255}:(Color){110,110,110,255};
            if (k==st) DrawRectangle(x-6,96,w+12,30,(Color){60,40,20,255});
            DrawText(lb,x,100,22,c); x+=w+26;
        }
        DrawText(TextFormat("frame %.0f/%d   %s%s", frame, nf, autoMode?"AUTO  ":"", paused?"PAUSED":""), 26, 134, 16, (Color){140,140,140,255});
        DrawText("TAB/[ ] enemy   1-7 state   A auto   SPACE pause   drag/arrows turn   wheel zoom   F face   ESC quit",
                 14, sh-28, 18, (Color){150,150,150,255});
        (void)sw;
        EndDrawing();
        if (poseN>0){ if (++galClock>=3){ galClock=0; TakeScreenshot(TextFormat("build/pose_%02d.png",poseI)); if (++poseI>=poseN) break; } }
        if (galShots>0){                                                   // dev self-test: CHERNOBYL_GALLERY_TEST=N
            galClock+=dt;
            if (galClock>=1.7f){ galClock=0; TakeScreenshot(TextFormat("build/gallery_%02d.png",galShot)); if (++galShot>=galShots) break; }
        }
    }
    return 0;
}
