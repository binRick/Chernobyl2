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
    struct { int role; float t, turn; int face; const char *name; } shots[16]={
        {CL_IDLE,0.6f,0,0,"front"}, {CL_IDLE,0.6f,35,0,"three_quarter"}, {CL_IDLE,0.6f,0,1,"face"},
        {CL_IDLE,0.6f,40,1,"face_side"}, {CL_ATTACK,0.25f,20,0,"attack"}, {CL_WALK,0.4f,-30,0,"walk"} };
    int nshots=6, shot=0, settle=0;
    if (getenv("CHERNOBYL_VIEW_GAIT")){                      // dev: a stride strip instead -- walk and run, front three-quarter and side (legs vs apron / robe)
        static const char *gn[16]={"walk_a0","walk_a1","walk_a2","walk_a3","walk_s0","walk_s1","walk_s2","walk_s3","run_a0","run_a1","run_a2","run_a3","run_s0","run_s1","run_s2","run_s3"};
        nshots=16;
        for (int k=0;k<16;k++){ shots[k].role=(k<8)?CL_WALK:CL_RUN; shots[k].t=(k%4)*((k<8)?0.37f:0.18f)+0.05f; shots[k].turn=((k/4)%2)?80:30; shots[k].face=0; shots[k].name=gn[k]; }
    }
    RenderTexture2D rtA={0}, rtB={0}; int rtOk=0;
    // each model's eye line from its rig's Head bone (bind pose), not the bounding box --
    // hats, toques and tentacles would throw that off
    float hA=ViewerHeadY(&A), hB=two?ViewerHeadY(&B):hA;
    while (!WindowShouldClose()){
        if (IsKeyPressed(KEY_ESCAPE)) break;                 // ESC leaves the dev mode (the game turns raylib's own exit key off)
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

// Enemy picker: a column of buttons down the right edge, one per enemy -- a small full-body
// picture (idle pose, thighs up, rendered once at startup, lit like the stage) and the name.
#define GAL_THUMB_W 224     // rendered at 2x the button size: sharp on HiDPI
#define GAL_THUMB_H 224
static RenderTexture2D g_galThumb[EN_NTYPES];

static void GalThumbs(void){
    for (int t=0;t<EN_NTYPES;t++){
        EnemyRig *rg=&g_rig[t];
        g_galThumb[t]=LoadRenderTexture(GAL_THUMB_W,GAL_THUMB_H);
        SetTextureFilter(g_galThumb[t].texture,TEXTURE_FILTER_BILINEAR);
        BeginTextureMode(g_galThumb[t]);
        ClearBackground((Color){26,24,27,255});
        if (rg->ok && rg->animN>0){
            ModelAnimation *an=&rg->anim[rg->clip[CL_IDLE]];
            RigPose(rg,an,0);
            float esc=ENEMY_SCALE*kEnemy[t].scale, tall=ViewerHeadY(rg)*kEnemy[t].scale/0.92f;
            float yr=25.0f*DEG2RAD, d=1.55f*tall/1.75f, ly=0.70f*tall;   // thighs up: faces and weapons read
            Camera3D cam={ (Vector3){sinf(yr)*d, ly+0.1f, cosf(yr)*d}, (Vector3){0,ly,0}, (Vector3){0,1,0}, 40.0f, CAMERA_PERSPECTIVE };
            BeginMode3D(cam);
            ViewerLights(cam.position);
            DrawModelEx(rg->model,(Vector3){0,0,0},(Vector3){0,1,0},ENEMY_YAW_OFFSET,(Vector3){esc,esc,esc},WHITE);
            EndMode3D();
        }
        EndTextureMode();
    }
}

// The gallery lists every enemy type, then the boss variants that are not a type of their own:
// the Archimandrite (wave 2's boss) is a priest half as tall again. g_galArch = that entry is up.
#define GAL_N (EN_NTYPES+1)
static int g_galArch=0;
#define GAL_BSC (g_galArch?ARCH_SCALE:1.0f)
// Sounds: with S on (the default) the selected enemy's own voice plays as each state starts --
// pursue = charge, attack, hit = hurt, die = death, cast = the raise -- falling back to whatever
// the game would use for his kind (shared roar, his gunshot, the bolt, a shared death cry, the
// boss themes). The VOICE panel on the left shows each slot and plays it on a click.
static int g_galSnd=1; static Sound g_galLast; static int g_galHasLast=0;
static void GalPlay(Sound sd){ if (!g_audio || sd.frameCount==0) return; if (g_galHasLast) StopSound(g_galLast);
    SetSoundPan(sd,0.0f); SetSoundPitch(sd,1.0f); SetSoundVolume(sd,1.0f); PlaySound(sd); g_galLast=sd; g_galHasLast=1; }
// What a cue plays for this type, and where it comes from: 0 nothing, 1 his own, 2 his family's, 3 a shared stand-in.
static int GalCueSound(int type, int cue, Sound *out, const char **from){
    int o=EnemyVoiceOwner(type,cue);
    if (o>=0){ *out=g_eCue[o][cue][GetRandomValue(0,g_hasECue[o][cue]-1)]; *from=(o==type)?"own":kEnemy[o].name; return o==type?1:2; }
    if (cue==EC_ATTACK){
        if (kEnemy[type].kind==AK_HITSCAN && g_hasEShot){ *out=g_eShotSnd; *from="shared gunshot"; return 3; }
        if (kEnemy[type].kind==AK_BOLT && g_hasEBolt){ *out=g_eBoltSnd; *from="shared bolt"; return 3; }
        if (kEnemy[type].kind==AK_RAISE && g_hasERaise){ *out=g_eRaiseSnd; *from="shared raise"; return 3; }
        if (g_hasAttackSnd){ *out=g_attackSnd; *from="shared roar"; return 3; } }
    if (cue==EC_DEATH && g_nDeathSnd>0){ *out=g_deathSnd[GetRandomValue(0,g_nDeathSnd-1)]; *from="shared death cries"; return 3; }
    *from="missing"; return 0;
}
static void GalCue(int type, int st){
    int cue = st==GS_RUN?EC_CHARGE : st==GS_ATTACK?EC_ATTACK : st==GS_HIT?EC_HURT : st==GS_DIE?EC_DEATH : -1;
    if (st==GS_CAST && g_hasERaise){ GalPlay(g_eRaiseSnd); return; }
    if (cue<0) return;
    int boss=(type==EN_HEADCHEF || g_galArch);
    if (cue==EC_DEATH && boss && g_hasBossDeadSnd && EnemyVoiceOwner(type,cue)!=type){ GalPlay(g_bossDeadSnd); return; }   // a boss with no death cry of his own goes down to the theme
    if (cue==EC_CHARGE && boss && g_hasBossSnd && EnemyVoiceOwner(type,cue)<0){ GalPlay(g_bossSnd); return; }   // ...and arrives to it
    Sound sd; const char *from; if (GalCueSound(type,cue,&sd,&from)) GalPlay(sd);
}
static Rectangle GalVoiceRow(int c){ return (Rectangle){ 22, 178+c*26.0f, 430, 24 }; }
// screen rectangle of enemy button k (column down the right edge)
static Rectangle GalButton(int k, int sw, int sh){
    float top=20, bottom=sh-40.0f, rowH=(bottom-top)/GAL_N;
    if (rowH>72) rowH=72;
    float w=200;
    return (Rectangle){ sw-w-12, top+k*rowH, w, rowH-4 };
}

// ---- weapon range: the game's own guns (viewmodel, sounds, cadence, tracers, blood, flinch,
// death) fired at the gallery enemy from RANGE_DIST away. Bottom bar: [WEAPON ON/OFF] + one
// button per gun, in the game's number-key order. Left click fires, right-drag aims, R reloads,
// ammo never runs out. The target flinches, dies, and stands back up 2 s later.
#define RANGE_DIST 5.0f
static const int kRangeSlot[4]={2,5,0,1};              // = the game's kSlotForKey: minigun first
static Rectangle GalWeaponButton(int k, int sh){        // k=0: the ON/OFF toggle, 1..6: guns
    float w=(k==0)?150.0f:132.0f, x=14.0f+(k>0?150.0f+6.0f+(k-1)*(132.0f+6.0f):0.0f);
    return (Rectangle){ x, sh-28.0f-44.0f, w, 36.0f };
}
static void RangeTargetReset(int type){
    for (int i=0;i<MAX_ENEMIES;i++) g_enemies[i].state=0;
    Enemy *e=&g_enemies[0]; memset(e,0,sizeof *e);
    e->type=type; e->state=1; e->hp=e->maxHp=g_galArch?ARCH_HP:kEnemy[type].hp; e->pos=(Vector3){0,0,0};
    e->sc=GAL_BSC; e->boss=g_galArch;
    for (int i=0;i<MAX_SPARKS;i++) g_sparks[i].life=0;  // wipe the last body's blood
}
static void RangeAimHome(int type){                     // look at the chest
    g_yaw=PI; g_pitch=atan2f(1.15f*kEnemy[type].scale*GAL_BSC-EYE_H, RANGE_DIST);
}
static void RangeSilence(void){
    Weapon *w=&g_weapons[g_curWeapon];
    if (g_audio && w->hasSnd && IsSoundPlaying(w->fireSnd)) StopSound(w->fireSnd);
    if (g_audio && w->hasAuxSnd && IsSoundPlaying(w->auxSnd)) StopSound(w->auxSnd);
}

static int RunGallery(void){
    for (int t=0;t<EN_NTYPES;t++) if (!g_rig[t].ok){ char p[96]; snprintf(p,sizeof p,"assets/%s.glb",kEnemy[t].name); LoadEnemyRig(&g_rig[t],p); }
    int type=0, st=GS_WALK, paused=0, autoMode=0, face=0;
    float frame=0, turn=25.0f, dist=3.6f, stT=0, hold=0;
    Vector3 boltP={0}; float boltT=-1;
    int range=0, armed=0; float deadT=0;
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
    g_galArch=0;
    { const char *ss=getenv("CHERNOBYL_GALLERY_START"), *ps=getenv("CHERNOBYL_GALLERY_POSE");          // "archimandrite" names the boss entry
      if ((ss && !strncmp(ss,"archimandrite",13)) || (ps && !strncmp(ps,"archimandrite",13))){ type=EN_PRIEST; g_galArch=1; } }
    // CHERNOBYL_GALLERY_START="name[:clip]" (dev-enemies.sh gunner 4): open on that enemy / clip, playing
    { const char *ss=getenv("CHERNOBYL_GALLERY_START");
      if (ss && *ss){ char nm[32]={0}; const char *c=strchr(ss,':'); size_t n=c?(size_t)(c-ss):strlen(ss);
        if (n<sizeof nm){ memcpy(nm,ss,n); for (int t=0;t<EN_NTYPES;t++) if (!strcmp(nm,kEnemy[t].name)) type=t; }
        if (c){ int k=atoi(c+1); if (k>=1 && k<=GS_N) st=k-1; } } }
    // dev: CHERNOBYL_GALLERY_RANGE="slot[:seconds]" -> start in weapon mode with that gun (g_weapons
    // index); with seconds, hold the trigger that long, screenshot build/range_NN.png every 0.5 s, exit
    float rangeTest=0, rangeClock=0; int rangeShot=0;
    { const char *rs=getenv("CHERNOBYL_GALLERY_RANGE");
      if (rs && *rs){ int sl=atoi(rs); if (sl>=0 && sl<g_numWeapons && g_weapons[sl].has){ range=1; ActivateWeapon(sl); }
        const char *c=strchr(rs,':'); if (c) rangeTest=(float)atof(c+1); } }
    for (int c=0;c<NUM_CRATES;c++) g_crates[c].pos.y=-100.0f;   // the arena's invisible crates would eat bullets
    if (range){ RangeTargetReset(type); RangeAimHome(type); }
    float aimDY=0, aimDP=0;                                                // dev: CHERNOBYL_GALLERY_AIM="yaw:pitch" degrees off the chest
    { const char *ae=getenv("CHERNOBYL_GALLERY_AIM"); if (ae && *ae){ aimDY=(float)atof(ae); const char *c=strchr(ae,':'); if (c) aimDP=(float)atof(c+1); } }
    if (range){ g_yaw+=aimDY*DEG2RAD; g_pitch+=aimDP*DEG2RAD; }
    GalThumbs();
    EnableCursor();
    while (!WindowShouldClose()){
        if (IsKeyPressed(KEY_ESCAPE)) break;                 // ESC leaves the dev mode (the game turns raylib's own exit key off)
        float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;
        EnemyRig *rg=&g_rig[type];
        int prevType=type, prevSt=st, prevRange=range, prevArch=g_galArch;
        { int pick=g_galArch?EN_NTYPES:type, was=pick;                    // the list position: the types, then the Archimandrite
          if (IsKeyPressed(KEY_TAB)||IsKeyPressed(KEY_RIGHT_BRACKET)) pick=(pick+1)%GAL_N;
          if (IsKeyPressed(KEY_LEFT_BRACKET)) pick=(pick+GAL_N-1)%GAL_N;
          if (pick!=was){ g_galArch=(pick==EN_NTYPES); type=g_galArch?EN_PRIEST:pick; } }
        if (IsKeyPressed(KEY_S)) g_galSnd=!g_galSnd;
        for (int k=0;k<GS_N;k++) if (IsKeyPressed(KEY_ONE+k)) st=k;
        if (IsKeyPressed(KEY_A)) autoMode=!autoMode;
        if (IsKeyPressed(KEY_SPACE)) paused=!paused;
        if (IsKeyPressed(KEY_W)) range=!range;
        if (!range){
            if (IsKeyPressed(KEY_F)) face=!face;
            if (IsKeyDown(KEY_LEFT))  turn-=90*dt;
            if (IsKeyDown(KEY_RIGHT)) turn+=90*dt;
            if (IsKeyDown(KEY_UP))    dist-=1.5f*dt;
            if (IsKeyDown(KEY_DOWN))  dist+=1.5f*dt;
        }
        int sw0=GetScreenWidth(), sh0=GetScreenHeight();
        int overPick=-1, overW=-1, overV=-1;                               // buttons: click to pick
        { Vector2 mp=GetMousePosition();
          for (int k=0;k<GAL_N;k++) if (CheckCollisionPointRec(mp,GalButton(k,sw0,sh0))) overPick=k;
          if (!range) for (int c=0;c<EC_N;c++) if (CheckCollisionPointRec(mp,GalVoiceRow(c))) overV=c;
          for (int k=0;k<=4;k++) if (CheckCollisionPointRec(mp,GalWeaponButton(k,sh0))) overW=k; }
        static int dragging=0;                                             // a drag that starts on a button doesn't turn
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){
            armed=0; dragging=0;
            if (overPick>=0){ g_galArch=(overPick==EN_NTYPES); type=g_galArch?EN_PRIEST:overPick; autoMode=0; }
            else if (overV>=0){ Sound sd; const char *from; if (GalCueSound(type,overV,&sd,&from)) GalPlay(sd); }
            else if (overW==0) range=!range;
            else if (overW>0){ int sl=kRangeSlot[overW-1];
                if (g_weapons[sl].has){ if (!range){ range=1; ActivateWeapon(sl); } else if (sl!=g_curWeapon) SwitchWeapon(sl); } }
            else if (range) armed=1;                                       // the trigger
            else dragging=1;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)){ dragging=0; armed=0; }
        if (dragging) turn+=GetMouseDelta().x*0.4f;
        if (!range){ dist-=GetMouseWheelMove()*0.25f; if (dist<0.8f) dist=0.8f; if (dist>9) dist=9; }
        if (st==GS_CAST && kEnemy[type].kind!=AK_RAISE) st=GS_ATTACK;          // only the priest casts
        if (type!=prevType || st!=prevSt){ frame=0; stT=0; hold=0; boltT=-1;
        }
        if (range && !prevRange){ autoMode=0; RangeTargetReset(type); RangeAimHome(type); g_vmRaise=1.0f; }
        if (!range && prevRange){ RangeSilence(); g_autoFire=0; for (int i=0;i<MAX_ENEMIES;i++) g_enemies[i].state=0; }
        if (range && (type!=prevType || g_galArch!=prevArch)){ RangeTargetReset(type); RangeAimHome(type); deadT=0; }
        { static int vSt=-1, vType=-1, vArch=-1;                          // a state (or an enemy) just came up: his sound for it
          if (st!=vSt || type!=vType || g_galArch!=vArch){ if (vSt>=0 && g_galSnd && !range) GalCue(type,st); vSt=st; vType=type; vArch=g_galArch; } }
        rg=&g_rig[type];
        if (!rg->ok || rg->animN<=0){ type=(type+1)%EN_NTYPES; continue; }
        ModelAnimation *an=&rg->anim[rg->clip[kGalRole[st]]];
        float galRate=GalFps(type,st);
        if ((st==GS_WALK || st==GS_RUN) && rg->gaitN>0){                    // the gait and cadence the game picks for this pace
            float spd=(st==GS_RUN)?kEnemy[type].speed:kEnemy[type].speed*0.45f, gsc=kEnemy[type].scale*GAL_BSC, best=1e9f; int role=kGalRole[st];
            for (int g=0;g<rg->gaitN;g++){ float m=rg->mps[rg->gait[g]]*gsc; if (m<0.1f) continue;
                float d=fabsf(logf(spd/m)); if (d<best){ best=d; role=rg->gait[g]; } }
            float m=rg->mps[role]*gsc;
            an=&rg->anim[rg->clip[role]]; if (m>0.1f) galRate=30.0f*Clamp(spd/m,0.5f,1.7f);
        }
        int nf=ANIM_FRAMES(*an); if (nf<1) nf=1;
        if (!paused){
            stT+=dt;
            float prev=frame; frame+=dt*galRate;
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
                       if (st>=GS_N){ st=GS_IDLE; int pick=((g_galArch?EN_NTYPES:type)+1)%GAL_N; g_galArch=(pick==EN_NTYPES); type=g_galArch?EN_PRIEST:pick; }
                       frame=0; stT=0; hold=0; boltT=-1;                // fresh clock for the new state
                       rg=&g_rig[type]; if (!rg->ok || rg->animN<=0) continue;
                       an=&rg->anim[rg->clip[kGalRole[st]]]; nf=ANIM_FRAMES(*an); if (nf<1) nf=1; }
        }
        { const char *fc=getenv("CHERNOBYL_GALLERY_CLIP");               // dev: force a clip by glb name (stills)
          if (fc && *fc) for (int ci=0;ci<rg->animN;ci++) if (!strcmp(rg->anim[ci].name,fc)){
              an=&rg->anim[ci]; nf=ANIM_FRAMES(*an); if (nf<1) nf=1; if (frame>=nf) frame=0; } }
        if (poseN>0) frame=poseF[poseI]*(nf-1);

        // ---- weapon range: aim, trigger, the target's flinch / death / stand-up
        Enemy *tg=&g_enemies[0];
        float drawFrame=frame; ModelAnimation *drawAn=an;
        Vector3 fwd={0,0,-1};
        if (range){
            Vector2 md=GetMouseDelta();
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)){ g_yaw-=md.x*0.0025f; g_pitch-=md.y*0.0025f; g_vmLook.x+=md.x; g_vmLook.y+=md.y; }
            if (IsKeyDown(KEY_LEFT))  g_yaw+=1.2f*dt;
            if (IsKeyDown(KEY_RIGHT)) g_yaw-=1.2f*dt;
            if (IsKeyDown(KEY_UP))    g_pitch+=1.0f*dt;
            if (IsKeyDown(KEY_DOWN))  g_pitch-=1.0f*dt;
            if (IsKeyPressed(KEY_C)) RangeAimHome(type);
            if (g_pitch>1.2f) g_pitch=1.2f; if (g_pitch<-1.2f) g_pitch=-1.2f;
            g_pos=(Vector3){0,EYE_H,RANGE_DIST};
            fwd=Vector3Normalize((Vector3){ sinf(g_yaw)*cosf(g_pitch), sinf(g_pitch), cosf(g_yaw)*cosf(g_pitch) });
            g_cam=(Camera3D){ g_pos, Vector3Add(g_pos,fwd), (Vector3){0,1,0}, 70.0f, CAMERA_PERSPECTIVE };
            if (rangeTest>0) g_autoFire=(rangeClock<rangeTest);
            int held=armed && IsMouseButtonDown(MOUSE_BUTTON_LEFT), pressed=armed && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
            WeaponTrigger(dt, fwd, held, pressed);
            WeaponAnimate(dt, 0);
            EffectsUpdate(dt); UpdateSmoke(dt); UpdateGrenades(dt);
            Weapon *cw=&g_weapons[g_curWeapon];
            if (cw->magSize>0 && cw->reserve<cw->magSize) cw->reserve=cw->reserveStart;   // the range never runs dry
            if (tg->state==0) RangeTargetReset(type);
            static float limpClock=0;
            int dRole=tg->deathRole?tg->deathRole:CL_DEATH;
            if (tg->state==2){                                             // shot dead: fall (by cause), lie there, stand back up
                deadT+=dt;
                drawAn=&rg->anim[rg->clip[dRole]];
                drawFrame=fminf(deadT*60.0f,(float)rg->settle[dRole]);
                if (deadT>(float)rg->settle[dRole]/60.0f+2.0f){ RangeTargetReset(type); deadT=0; }
            } else if (tg->knockT>0){                                      // flattened: the knockdown, then back up
                drawAn=&rg->anim[rg->clip[CL_KNOCK]];
                int kf=ANIM_FRAMES(*drawAn); drawFrame=fminf((kf/60.0f-tg->knockT)*60.0f,(float)(kf>0?kf-1:0));
                tg->knockT-=dt; if (tg->hitT>0) tg->hitT-=dt; tg->pushV=(Vector3){0,0,0};
            } else if (tg->hitT>0){                                        // flinch
                drawAn=&rg->anim[rg->clip[tg->hitRole>=CL_HIT_HEAD?tg->hitRole:CL_HIT]];
                int hf=ANIM_FRAMES(*drawAn); drawFrame=fminf((0.45f-tg->hitT)*30.0f,(float)(hf>0?hf-1:0));
                tg->hitT-=dt;
            } else if (tg->limpT>0 && (st==GS_WALK || st==GS_RUN)){        // walking on a shot leg
                drawAn=&rg->anim[rg->clip[CL_LIMP]];
                int lf=ANIM_FRAMES(*drawAn); limpClock+=dt*30.0f*LIMP_SPEED/LIMP_CLIP_MPS;
                if (lf>0) while (limpClock>=lf) limpClock-=lf;
                drawFrame=limpClock;
            }
            if (tg->limpT>0) tg->limpT-=dt;
        }
        RigPose(rg,drawAn,drawFrame);
        float esc=ENEMY_SCALE*kEnemy[type].scale*GAL_BSC;
        float head=ViewerHeadY(rg)*kEnemy[type].scale*GAL_BSC, tall=head/0.92f;
        float d=face?0.95f*tall/1.75f:dist*tall/1.75f, ly=face?head+0.02f:1.05f*tall/1.75f;
        float yr=turn*DEG2RAD;
        Camera3D cam={ (Vector3){sinf(yr)*d, ly+(face?0.02f:0.15f), cosf(yr)*d}, (Vector3){0,ly,0}, (Vector3){0,1,0}, face?32.0f:42.0f, CAMERA_PERSPECTIVE };
        if (range) cam=g_cam;
        BeginDrawing();
        ClearBackground((Color){14,13,15,255});
        BeginMode3D(cam);
        DrawPlane((Vector3){0,-0.002f,0},(Vector2){30,30},(Color){34,32,30,255});
        // floor lines scroll under walk/pursue at the enemy's real speed (feet should "grip")
        float sp = (st==GS_RUN)?kEnemy[type].speed:((st==GS_WALK)?kEnemy[type].speed*0.45f:0.0f);
        if (range && tg->state!=1) sp=0;
        if (range && tg->state==1 && tg->knockT>0) sp=0;
        if (range && tg->state==1 && tg->limpT>0 && sp>0) sp=LIMP_SPEED;
        float off=fmodf(GetTime()*sp,1.0f); if (paused) off=0;
        for (int z=-8;z<=8;z++){ float zz=z+off; DrawLine3D((Vector3){-8,0,zz},(Vector3){8,0,zz},(Color){60,56,52,255}); }
        for (int x=-8;x<=8;x++) DrawLine3D((Vector3){(float)x,0,-8},(Vector3){(float)x,0,8},(Color){48,45,42,255});
        ViewerLights(cam.position);
        float drawYawG=(range && tg->state==2)?tg->deathYaw:ENEMY_YAW_OFFSET;
        DrawModelEx(rg->model,(Vector3){0,0,0},(Vector3){0,1,0},drawYawG,(Vector3){esc,esc,esc},WHITE);
        if (range){ tg->drawAn=drawAn; tg->drawFrame=drawFrame; tg->drawYaw=drawYawG;     // wounds go on the pose shown
            g_runT+=GetFrameTime();                                    // (the gallery has no run: this is only the clock wounds bleed by)
            if (tg->wN>0) DrawWounds(drawAn, drawFrame, (Vector3){0,0,0}, drawYawG, esc, tg->wN, tg->wBone, tg->wOff, tg->wSz, tg->wDir, tg->wT0); }
        if (boltT>=0 && !(range && tg->state!=1)){                         // the radiation bolt, flying off "at the player"
            Vector3 bp={boltP.x, boltP.y-1.0f*boltT, boltP.z+13.0f*boltT};       // from the hand it left
            DrawSphere(bp,0.12f,(Color){140,255,90,255}); DrawSphereWires(bp,0.2f,6,6,(Color){90,220,60,120});
        }
        if (range){
            if (tg->burnN>0) DrawBurnMarks((Vector3){0,0,0}, tg->state==2?0.30f:1.0f, tg->burnY, tg->burnR, tg->burnN);
            DrawEffects();
            DrawSmoke(g_cam);
        }
        EndMode3D();
        int sw=GetScreenWidth(), sh=GetScreenHeight();
        if (range){
            DrawViewmodel();
            DrawVignette(g_shake);
            int cx=sw/2, cy=sh/2; Color ch=(Color){235,235,235,220};       // crosshair
            DrawRectangle(cx-12,cy-1,8,2,ch); DrawRectangle(cx+4,cy-1,8,2,ch);
            DrawRectangle(cx-1,cy-12,2,8,ch); DrawRectangle(cx-1,cy+4,2,8,ch);
            Weapon *cw=&g_weapons[g_curWeapon];
            const char *ammo = cw->flame ? TextFormat("fuel %d", cw->mag) : (cw->magSize>0 ? TextFormat("%d / %d", cw->mag, cw->reserve) : "");
            DrawText(TextFormat("%s   %s%s", cw->label, ammo, g_reloading?"   RELOADING":""), sw/2-220, sh-28-44-34, 22, (Color){235,220,170,255});
            float hpk=tg->maxHp>0?fmaxf(0.0f,tg->hp)/tg->maxHp:0;          // the target's health
            DrawRectangle(26,160,240,12,(Color){40,20,20,220}); DrawRectangle(26,160,(int)(240*hpk),12,(Color){210,50,40,255});
            DrawText(tg->state==2?"DOWN -- back up in a moment":TextFormat("hp %.0f / %.0f", fmaxf(0.0f,tg->hp), tg->maxHp), 274, 157, 16, (Color){200,200,200,255});
            if (g_hsFlash>0) DrawText("HEADSHOT!", sw/2-MeasureText("HEADSHOT!",40)/2, sh/2-90, 40, (Color){255,70,50,(unsigned char)fminf(255.0f,g_hsFlash*400.0f)});
        }
        { const char *nm=g_galArch?"archimandrite":kEnemy[type].name; int nw=MeasureText(nm,40);
          DrawText(nm, 24, 20, 40, (Color){255,90,70,255});
          if (g_galArch || type==EN_HEADCHEF) DrawText(g_galArch?"BOSS  -  ends wave 2":"BOSS  -  ends every other wave", 24+nw+16, 36, 20, (Color){255,200,80,255}); }
        DrawText(TextFormat("%d/%d   %s   hp %.0f   speed %.1f m/s", (g_galArch?EN_NTYPES:type)+1, GAL_N, kGalKind[kEnemy[type].kind], g_galArch?ARCH_HP:kEnemy[type].hp, kEnemy[type].speed),
                 26, 66, 18, (Color){170,170,170,255});
        if (!range){                                                       // VOICE: his four slots, each playable
            DrawText(TextFormat("VOICE    click a row to play    S: play with each state  [%s]", g_galSnd?"ON":"OFF"), 26, 154, 16, (Color){255,200,80,255});
            for (int c=0;c<EC_N;c++){ Rectangle r=GalVoiceRow(c); Sound sd; const char *from; int k=GalCueSound(type,c,&sd,&from);
                if (c==overV) DrawRectangleRec(r,(Color){44,40,40,235});
                Color tc = k==1?(Color){110,200,120,255} : k==0?(Color){230,80,70,255} : (Color){240,180,60,255};
                DrawText(kECue[c],(int)r.x+6,(int)r.y+4,16,(Color){225,225,230,255});
                const char *t = k==1 ? TextFormat("OK   %d take%s", g_hasECue[type][c], g_hasECue[type][c]==1?"":"s")
                              : k==2 ? TextFormat("BORROWED   the %s's", from) : k==3 ? TextFormat("BORROWED   %s", from)
                              : TextFormat("MISSING   wants %s", g_eCuePath[type][c]);
                DrawText(t,(int)r.x+84,(int)r.y+4,16,tc); }
        }
        int x=26;
        for (int k=0;k<GS_N;k++){
            if (k==GS_CAST && kEnemy[type].kind!=AK_RAISE) continue;
            const char *lb=TextFormat("%d %s",k+1,kGalState[k]); int w=MeasureText(lb,22);
            Color c = (k==st)?(Color){255,220,90,255}:(Color){110,110,110,255};
            if (k==st) DrawRectangle(x-6,96,w+12,30,(Color){60,40,20,255});
            DrawText(lb,x,100,22,c); x+=w+26;
        }
        DrawText(TextFormat("frame %.0f/%d   %s%s", frame, nf, autoMode?"AUTO  ":"", paused?"PAUSED":""), 26, 134, 16, (Color){140,140,140,255});
        DrawText(range ? "click fire   right-drag/arrows aim   C re-aim   R reload   W weapon off   click/TAB enemy   1-7 state   ESC quit"
                       : "click/TAB/[ ] enemy   1-7 state   W weapon   A auto   SPACE pause   drag/arrows turn   wheel zoom   F face   ESC quit",
                 14, sh-28, 18, (Color){150,150,150,255});
        for (int k=0;k<GAL_N;k++){
            Rectangle r=GalButton(k,sw,sh);
            int kt=(k<EN_NTYPES)?k:EN_PRIEST, sel=(k<EN_NTYPES)?(k==type && !g_galArch):g_galArch, hov=(k==overPick);
            DrawRectangleRec(r, sel?(Color){60,40,20,235}:(hov?(Color){44,40,40,235}:(Color){24,22,25,220}));
            float th=r.height-6, tw=th*GAL_THUMB_W/(float)GAL_THUMB_H;
            DrawTexturePro(g_galThumb[kt].texture,(Rectangle){0,0,GAL_THUMB_W,-GAL_THUMB_H},   // render textures are upside down
                           (Rectangle){r.x+3,r.y+3,tw,th},(Vector2){0,0},0,WHITE);
            int fs=r.height>=48?20:16;
            int bossRow=(k==EN_HEADCHEF || k==EN_NTYPES);
            DrawText(k<EN_NTYPES?kEnemy[k].name:"archimandrite",(int)(r.x+tw+12),(int)(r.y+r.height/2-fs/2)-(bossRow?6:0),k<EN_NTYPES?fs:(fs>16?16:fs),
                     sel?(Color){255,220,90,255}:(hov?(Color){230,230,230,255}:(Color){160,160,160,255}));
            if (bossRow) DrawText("BOSS",(int)(r.x+tw+12),(int)(r.y+r.height/2+fs/2-5),12,(Color){255,90,70,255});
            if (sel) DrawRectangleLinesEx(r,2,(Color){255,90,70,255});
        }
        for (int k=0;k<=4;k++){                                            // weapon bar
            Rectangle r=GalWeaponButton(k,sh);
            int sl=(k>0)?kRangeSlot[k-1]:-1, hov=(k==overW);
            int on = (k==0) ? range : (range && sl==g_curWeapon);
            int usable = (k==0) || g_weapons[sl].has;
            DrawRectangleRec(r, on?(Color){60,40,20,235}:(hov&&usable?(Color){44,40,40,235}:(Color){24,22,25,220}));
            const char *lb = (k==0) ? (range?"WEAPON: ON":"WEAPON: OFF") : g_weapons[sl].label;
            int fs=18; while (fs>12 && MeasureText(lb,fs)>r.width-12) fs--;
            Color tc = !usable?(Color){80,80,80,255}:(on?(Color){255,220,90,255}:(hov?(Color){230,230,230,255}:(Color){160,160,160,255}));
            DrawText(lb,(int)(r.x+r.width/2-MeasureText(lb,fs)/2),(int)(r.y+r.height/2-fs/2),fs,tc);
            if (on) DrawRectangleLinesEx(r,2,(Color){255,90,70,255});
        }
        SetMouseCursor((overPick>=0||overW>=0)?MOUSE_CURSOR_POINTING_HAND:(range?MOUSE_CURSOR_CROSSHAIR:MOUSE_CURSOR_DEFAULT));
        EndDrawing();
        if (poseN>0){ if (++galClock>=3){ galClock=0; TakeScreenshot(TextFormat("build/pose_%02d.png",poseI)); if (++poseI>=poseN) break; } }
        if (galShots>0){                                                   // dev self-test: CHERNOBYL_GALLERY_TEST=N
            galClock+=dt;
            if (galClock>=1.7f){ galClock=0; TakeScreenshot(TextFormat("build/gallery_%02d.png",galShot)); if (++galShot>=galShots) break; }
        }
        if (rangeTest>0){                                                  // dev self-test: CHERNOBYL_GALLERY_RANGE=slot:seconds
            float pc=rangeClock; rangeClock+=dt;
            if (floorf(rangeClock/0.5f)>floorf(pc/0.5f)){
                TakeScreenshot(TextFormat("build/range_%02d.png",rangeShot++));
                DebugLog("range","\"t\":%.2f,\"slot\":%d,\"hp\":%.1f,\"state\":%d,\"hitT\":%.2f,\"hitRole\":%d,\"mag\":%d",
                         rangeClock,g_curWeapon,tg->hp,tg->state,tg->hitT,tg->hitRole,g_weapons[g_curWeapon].mag); }
            if (rangeClock>rangeTest+1.5f) break;
        }
    }
    RangeSilence();
    for (int t=0;t<EN_NTYPES;t++) UnloadRenderTexture(g_galThumb[t]);
    return 0;
}
