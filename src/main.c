// CHERNOBYL 2 - a more-3D successor to the sprite-based Chernobyl FPS.
//
// Player runs around a real 3D room with an animated first-person rifle
// (assets/rifle.glb - an M16A3 arms+gun rig: 89-bone, skinning verified, fits
// the GPU limit). World geometry is generated procedurally in code; the rifle
// is the only loaded art asset. Built on raylib 6.0.
//
// Conventions carried over from Chernobyl:
//   - run.sh builds + runs.
//   - --debug emits newline-delimited JSON events to stderr.
//   - ASCII-only strings in DrawText (raylib's default font is ASCII-only).
//
// Controls: WASD move - MOUSE look - SHIFT sprint - SPACE jump - LMB fire -
//           R reload - ` or TAB = dev overlay - ESC quit.
// Viewmodel tuning (live): I/K=Y J/L=X U/O=Z  -/= scale  [/] yaw  ;/' pitch
//                          V = inspect (gun floats in world)  0 = reset
// ----------------------------------------------------------------------------

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  #include <OpenGL/gl3.h>          // glClear(GL_DEPTH_BUFFER_BIT) for the viewmodel pass
#elif defined(__EMSCRIPTEN__)
  #include <GLES3/gl3.h>
#endif

#include "mapload.h"   // SPIKE: --map loads idTech3 (.map) brush geometry as a Model

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
void MacSetDockIcon(const char *path);   // macicon.m: Dock / Cmd-Tab icon (SetWindowIcon is a no-op on macOS)
#endif

// raylib 6.0 redesigned skeletal animation; macro keeps it buildable on 5.5.
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
  #define ANIM_FRAMES(a)     ((a).keyframeCount)
  #define ANIM_APPLY(m,a,f)  UpdateModelAnimation((m),(a),(float)(f))
#else
  #define ANIM_FRAMES(a)     ((a).frameCount)
  #define ANIM_APPLY(m,a,f)  UpdateModelAnimation((m),(a),(int)(f))
#endif

// ---- Debug JSON logging -----------------------------------------------------
static int   g_debug     = 0;
static FILE *g_dbg       = NULL;
static int   g_maxFrames = 0;     // --frames N : timed run, then quit
static int   g_noEnemies = 0;     // --no-enemies / N key : empty arena for weapon tuning
static int   g_shotFrame = 0;     // --shot N : screenshot at frame N (0 = off)

static void DebugLog(const char *ev, const char *fmt, ...) {
    if (!g_debug || !g_dbg) return;
    fprintf(g_dbg, "{\"t\":%.3f,\"ev\":\"%s\"", GetTime(), ev);
    if (fmt && fmt[0]) { va_list ap; va_start(ap, fmt); fputc(',', g_dbg); vfprintf(g_dbg, fmt, ap); va_end(ap); }
    fprintf(g_dbg, "}\n"); fflush(g_dbg);
}
// raylib trace-log filter: passes everything through to the default handler
// EXCEPT the harmless "Indices data converted from u32 to u16" warning that the
// glTF loader spams for every multi-thousand-vert model. Keeps the console clean
// without hiding genuine warnings/errors.
static void FilteredTraceLog(int logLevel, const char *text, va_list args){
    if (text && strstr(text, "converted from u32 to u16")) return;
    char buf[512];
    vsnprintf(buf, sizeof buf, text, args);
    const char *tag = (logLevel>=LOG_ERROR)?"ERROR: ":(logLevel==LOG_WARNING)?"WARNING: ":
                      (logLevel==LOG_INFO)?"INFO: ":(logLevel==LOG_DEBUG)?"DEBUG: ":"";
    fprintf(logLevel>=LOG_ERROR?stderr:stdout, "%s%s\n", tag, buf);
}
static const char *JStr(const char *s){
    static char b[96]; size_t j=0; if(!s)s="";
    for(size_t i=0;s[i]&&j<sizeof(b)-2;i++){char c=s[i]; if(c=='"'||c=='\\'){b[j++]='\\';b[j++]=c;} else if((unsigned char)c>=0x20)b[j++]=c;}
    b[j]=0; return b;
}

// ---- World ------------------------------------------------------------------
#define ARENA   28.0f
#define WALL_H  6.0f
#define EYE_H   1.7f

typedef struct { Vector3 pos; Vector3 size; Color col; } Box;
#define NUM_CRATES 7
static Box  g_crates[NUM_CRATES];
static Texture2D g_floorTex, g_wallTex, g_crateTex;
static Model g_floor, g_wall, g_crate;
static Model g_map; static int g_hasMap=0; static const char *g_mapPath=NULL;  // SPIKE: --map <file.map>

// ---- Player -----------------------------------------------------------------
static Vector3 g_pos   = { 0, EYE_H, 6 };
static float   g_yaw   = 0.0f;
static float   g_pitch = 0.0f;
static float   g_vy    = 0.0f;
static int     g_grounded = 1;
static int     g_devOverlay = 1;
static float   g_bob = 0.0f;
static float   g_eyeSmooth = 0.0f;   // view-height lag that absorbs stair-step snaps, decays to 0

// ---- Weapon: animated M16A3 viewmodel (assets/rifle.glb) --------------------
// File animations: Take(51) Shoot(26) Reload(338) Watch(301) Hide(51). There's
// NO short idle clip - "Watch" is a 10s fidget that makes the gun wander. So we
// use the FINAL FRAME of "Take" (the drawn/ready stance) as a STATIC idle pose
// (held, not looped), and only PLAY Shoot on LMB / Reload on R, each returning
// to that held pose. Clip indices resolved BY NAME at load.
static Model           g_gun;
static ModelAnimation *g_gunAnim = NULL;
static int             g_gunAnimN = 0;
static int             g_hasGun = 0;
static int             g_aIdle=0, g_aShoot=1, g_aReload=2;  // idle = hold last frame of "Take"
static int             g_reloadSeq[6], g_reloadSeqN=0;  // active weapon's multi-phase reload clips
static int             g_reloadStep=-1;                 // -1 idle; >=0 index into g_reloadSeq
static int             g_reloading=0;                   // a reload (single or sequence) is in progress
static int             g_curAnim = 0;           // start in idle (Take, held at end)
static float           g_animT = 0.0f;          // frame cursor
static int             g_animOnce = 0;          // current clip is a one-shot (Reload)
static float           g_fireCd = 0.0f;
static float           g_recoil = 0.0f;         // 0..1 recoil kick on fire (decays); drives muzzle-up
static int             g_burstLeft = 0;         // rounds remaining in the current burst (burst weapons)
static float           g_spin = 0.0f;           // minigun barrel-spin rate 0..1
static float           g_mgSpinT = 0.0f;         // minigun: seconds the trigger's been held (spin-up timer)
static float           g_shake = 0.0f;           // 0..1 minigun "brain shake": screen jitter + edge vignette
#define MG_SPINUP 0.333f                          // barrel spins up for 1/3 s before bullets actually fire
static float           g_mgBarrelAngle = 0.0f;   // minigun barrel-spin angle: the barrel is a rigid (un-rigged) mesh,
                                                 // so it can't spin via the skeleton -- we rotate the geometry directly
#define MG_BARREL_MESH 1                          // minigun.glb mesh index of "barrels_minigun_0"
#define MG_IDLE_FRAME  0                          // 'allanims' frame to hold the arms at (a static grip)
static float           g_mgHeat = 0.0f;         // seconds of continuous minigun fire (5s -> overheat)
static int             g_mgLock = 0;            // minigun overheated: locked until trigger released
static int             g_mgFiring = 0;          // minigun was firing last frame (edge detect for cooldown sound)
static float           g_lmgPause = 0.0f;       // LMG: forced pause (s) after its fire sound ends
static int             g_lmgWasPlaying = 0;     // LMG: fire sound was playing last frame (edge detect)
static Camera3D        g_vmCam;
static int             g_inspect = 0;           // V: gun floats in the world
static float           g_savedMsg = 0.0f;       // >0: flash a "SAVED" confirmation
static float           g_spawnMsg = 0.0f;       // >0: flash a "SPAWN SET" confirmation
static int             g_menu = 0;              // ESC: options/pause menu open (frees cursor, pauses)
static int             g_paused = 0;            // P: pause the game (freezes the world, keeps mouse captured)
static int             g_fullscreen = 0;        // borderless monitor-fill on/off (set via the menu, persisted)
static int             g_quit = 0;              // menu Quit -> break the main loop
static Vector3         g_gunCentroid = { 0, 0, 0 };  // gun centroid (bind or posed), measured ONCE at load
static float           g_gunFitScale = 1.0f;         // inspect-view fit scale, measured ONCE at load
// Slots whose bind pose sits far from where the gun is actually drawn (posed):
// frame these from the POSED idle geometry. Most rigs (incl. every hand-tuned
// legacy weapon) have bind~=posed and stay on bind, so they're untouched. Set
// before the corresponding LoadWeapon call.
static int             g_posedBasis[16] = {0};

// This rig is BIG and off-origin (~2783u, centroid ~Y1390 Z849, barrel along
// model +Y). So: tiny scale, and recenter on the centroid so the gun lands at
// g_vmOff regardless of its authored origin. Orientation is a guess (the inspect
// view V confirms the model; tune yaw/pitch live to aim it down-range). 0=reset.
// Held-weapon framing: anchor the gun LOW and to the RIGHT, barrel reaching
// toward screen center (like a real FPS), bigger scale so it fills the lower
// third. These are best-guess defaults; tune live and 0 to reset.
// Defaults dialled in live and read off the dev overlay - held low-right,
// barrel down-range. vm_tune.txt (if present) overrides these at load.
#define VM_OFF0    ((Vector3){ -2.45f, -5.55f, 1.68f })
#define VM_SCALE0  0.0004f
#define VM_YAW0    178.0f
#define VM_PITCH0  -306.0f
#define VM_ROLL0   0.0f
static Vector3 g_vmOff   = { -2.45f, -5.55f, 1.68f };
static float   g_vmScale = VM_SCALE0;
static float   g_vmYaw   = VM_YAW0;
static float   g_vmPitch = VM_PITCH0;
static float   g_vmRoll  = VM_ROLL0;

// Persist the live-tuned viewmodel transform across runs so the player's
// dialed-in framing survives a rebuild. Saved to ./vm_tune.txt on quit, loaded
// at startup. 7 floats: offX offY offZ scale yaw pitch (one line).
// A weapon slot owns its model+clips+tuning; the hot-path render/fire code reads
// the active working-set globals above, which SwitchWeapon swaps in/out. Keys
// 1/2 select. Each weapon persists its own framing to its own tune file.
typedef struct {
    Model           model;
    ModelAnimation *anim;
    int             animN, has;
    int             aIdle, aShoot, aReload;
    int             reloadSeq[6], reloadSeqN;   // multi-phase reload clips, in play order
    Vector3         centroid;
    float           fitScale;
    Vector3         off;  float scale, yaw, pitch, roll;     // live, persisted
    Vector3         off0; float scale0, yaw0, pitch0, roll0; // defaults for 0=reset
    const char     *label;
    const char     *tunePath;
    Sound           fireSnd;                     // per-weapon fire report
    int             hasSnd;                      // fireSnd loaded OK
    int             sndLoop;                     // 1 = loop while trigger held (auto); 0 = one-shot per shot
    int             burst;                       // >0: rounds per trigger pull (burst fire)
    int             autoReload;                  // 1: play the reload anim after each shot (pump)
    int             spinUp;                      // 1: minigun -- 5s fire cap, then cooldown sound + lockout
    int             soundGated;                  // 1: LMG -- fires while its sound plays, then a fixed pause
    int             playShoot;                   // 1: play the model's Shot clip on each shot (AK has a good one)
    float           fireCd;                      // per-shot cooldown override (0 -> default)
    Sound           auxSnd;                      // secondary sound: minigun spin-down, shotgun cock-between-shots
    int             hasAuxSnd;
} Weapon;
static Weapon g_weapons[16];
static int    g_curWeapon = 0;
static int    g_numWeapons = 0;
static int    g_audio = 0;                       // audio device ready (sounds load/play only when set)
static Sound  g_deathSnd[8];                      // one is played at random on every enemy kill
static int    g_nDeathSnd = 0;
static Sound  g_attackSnd;                         // played when an enemy lunges into an attack
static int    g_hasAttackSnd = 0;

static void SaveTune(const char *path, Vector3 off, float scale, float yaw, float pitch, float roll){
    FILE *f=fopen(path,"w"); if(!f) return;
    fprintf(f,"%.5f %.5f %.5f %.6f %.2f %.2f %.2f\n", off.x,off.y,off.z,scale,yaw,pitch,roll);
    fclose(f);
}
static int LoadTune(const char *path, Vector3 *off, float *scale, float *yaw, float *pitch, float *roll){
    FILE *f=fopen(path,"r"); if(!f) return 0;
    float ox,oy,oz,sc,yw,pt,rl=0.0f;
    int n=fscanf(f,"%f %f %f %f %f %f %f",&ox,&oy,&oz,&sc,&yw,&pt,&rl);
    fclose(f);
    if(n>=6){ *off=(Vector3){ox,oy,oz}; *scale=sc; *yaw=yw; *pitch=pt; *roll=(n>=7)?rl:0.0f; return 1; }
    return 0;
}
// Custom player spawn (F2 saves your current pose; loaded at startup to override the
// map's spawn). Per-machine local content, like the weapon tune files.
static void SaveSpawn(Vector3 pos, float yaw, float pitch){
    FILE *f=fopen("spawn.txt","w"); if(!f) return;
    fprintf(f,"%.4f %.4f %.4f %.4f %.4f\n", pos.x,pos.y,pos.z,yaw,pitch); fclose(f);
}
static int LoadSpawn(Vector3 *pos, float *yaw, float *pitch){
    FILE *f=fopen("spawn.txt","r"); if(!f) return 0;
    float x,y,z,yw,pt; int n=fscanf(f,"%f %f %f %f %f",&x,&y,&z,&yw,&pt); fclose(f);
    if(n==5){ *pos=(Vector3){x,y,z}; *yaw=yw; *pitch=pt; return 1; }
    return 0;
}
// Copy the live working-set tuning back into the current slot (call before a switch/quit).
static void StashActiveTuning(void){
    if (g_numWeapons<=0) return;
    Weapon *w=&g_weapons[g_curWeapon];
    w->off=g_vmOff; w->scale=g_vmScale; w->yaw=g_vmYaw; w->pitch=g_vmPitch; w->roll=g_vmRoll;
}
// Make slot n the live weapon: copy its model/clips/tuning into the globals.
static void ActivateWeapon(int n){
    g_curWeapon=n;
    Weapon *w=&g_weapons[n];
    g_gun=w->model; g_gunAnim=w->anim; g_gunAnimN=w->animN; g_hasGun=w->has;
    g_aIdle=w->aIdle; g_aShoot=w->aShoot; g_aReload=w->aReload;
    g_reloadSeqN=w->reloadSeqN; for(int i=0;i<w->reloadSeqN;i++) g_reloadSeq[i]=w->reloadSeq[i];
    g_reloadStep=-1; g_reloading=0;
    g_gunCentroid=w->centroid; g_gunFitScale=w->fitScale;
    g_vmOff=w->off; g_vmScale=w->scale; g_vmYaw=w->yaw; g_vmPitch=w->pitch; g_vmRoll=w->roll;
    g_curAnim=g_aIdle; g_animOnce=0; g_animT=0.0f; g_recoil=0.0f;
    g_burstLeft=0; g_spin=0.0f; g_mgBarrelAngle=0.0f; g_mgSpinT=0.0f; g_shake=0.0f; g_mgHeat=0.0f; g_mgLock=0; g_mgFiring=0; g_lmgPause=0.0f; g_lmgWasPlaying=0;   // reset fire-mode state
}
static void SwitchWeapon(int n){
    if (n<0 || n>=g_numWeapons || n==g_curWeapon || !g_weapons[n].has) return;
    // silence the weapon we're holstering so its loop doesn't bleed past the swap
    if (g_audio && g_weapons[g_curWeapon].hasSnd && IsSoundPlaying(g_weapons[g_curWeapon].fireSnd))
        StopSound(g_weapons[g_curWeapon].fireSnd);
    if (g_audio && g_weapons[g_curWeapon].hasAuxSnd && IsSoundPlaying(g_weapons[g_curWeapon].auxSnd))
        StopSound(g_weapons[g_curWeapon].auxSnd);
    StashActiveTuning();
    ActivateWeapon(n);
    DebugLog("weapon","\"slot\":%d,\"label\":\"%s\"", n, g_weapons[n].label);
}
// Measure a model's bbox CENTRE + max extent from its current vertex data.
// useAnim picks mesh.animVertices (the posed buffer raylib actually draws) over
// the bind-pose mesh.vertices. Returns a unit fallback for empty/vertexless rigs.
static void MeasureBBox(Model m, int useAnim, Vector3 *centre, float *maxDim){
    float lo[3]={1e30f,1e30f,1e30f}, hi[3]={-1e30f,-1e30f,-1e30f}; int any=0;
    for(int mi=0;mi<m.meshCount;mi++){ Mesh*me=&m.meshes[mi];
        float*vv=(useAnim&&me->animVertices)?me->animVertices:me->vertices; if(!vv)continue;
        for(int v=0;v<me->vertexCount;v++){ any=1; for(int k=0;k<3;k++){float c=vv[v*3+k]; if(c<lo[k])lo[k]=c; if(c>hi[k])hi[k]=c;} } }
    if(!any){ *centre=(Vector3){0,0,0}; *maxDim=1.0f; return; }
    *centre=(Vector3){(lo[0]+hi[0])*0.5f,(lo[1]+hi[1])*0.5f,(lo[2]+hi[2])*0.5f};
    *maxDim=fmaxf(hi[0]-lo[0],fmaxf(hi[1]-lo[1],hi[2]-lo[2]));
}
// Load a glb into slot, resolve idle/shoot/reload clips by name, load saved tuning.
static void LoadWeapon(int slot, const char *path, const char *alt,
                       const char *label, const char *tunePath,
                       Vector3 off0, float scale0, float yaw0, float pitch0, float roll0){
    Weapon *w=&g_weapons[slot];
    w->label=label; w->tunePath=tunePath;
    w->off0=off0; w->scale0=scale0; w->yaw0=yaw0; w->pitch0=pitch0; w->roll0=roll0;
    w->off=off0; w->scale=scale0; w->yaw=yaw0; w->pitch=pitch0; w->roll=roll0;
    w->aIdle=0; w->aShoot=1; w->aReload=2; w->has=0; w->anim=NULL; w->animN=0;
    const char *fp=path; if(!FileExists(fp)) fp=alt;
    if(!FileExists(fp)){ DebugLog("weapon","\"slot\":%d,\"error\":\"missing\"",slot); return; }
    w->model=LoadModel(fp);
    w->anim=LoadModelAnimations(fp,&w->animN);
    w->has=(w->model.meshCount>0);
    // Resolve clips by name with priorities (first match wins per role): a real
    // "idle" beats take/draw/hold; "shot" counts as a fire clip; the plain
    // "reload" beats variants like reload_full. Handles e.g. AK_Idle/Shot/Reload.
    int rIdle=-1, rIdleAlt=-1, rShoot=-1, rReload=-1;
    for (int i=0;i<w->animN;i++){
        const char *nm=w->anim[i].name; char low[64]; int j=0;
        for (; nm[j] && j<63; j++){ char c=nm[j]; if(c>='A'&&c<='Z') c+=32; low[j]=c; }
        low[j]=0;
        if (strstr(low,"idle")){ if(rIdle<0) rIdle=i; }
        else if (strstr(low,"take")||strstr(low,"draw")||strstr(low,"weild")||strstr(low,"wield")||strstr(low,"hold")){ if(rIdleAlt<0) rIdleAlt=i; }
        if ((strstr(low,"shoot")||strstr(low,"fire")||strstr(low,"shot")) && rShoot<0) rShoot=i;
        if (strstr(low,"reload") && rReload<0) rReload=i;
    }
    w->aIdle  = (rIdle>=0)?rIdle:(rIdleAlt>=0?rIdleAlt:0);
    w->aShoot = (rShoot>=0)?rShoot:w->aIdle;
    w->aReload= (rReload>=0)?rReload:w->aIdle;
    // Multi-phase reload: if the rig has the named phases, play them in order
    // (e.g. shotgun pump: prep -> load shell -> load last -> recover). Falls
    // back to the single aReload clip when these aren't present.
    w->reloadSeqN=0;
    const char *phases[]={"priortoreload","reloadone","reloadlastone","postfire"};
    for (int p=0;p<4 && w->reloadSeqN<6;p++){
        for (int i=0;i<w->animN;i++){
            const char *nm=w->anim[i].name; char low[64]; int j=0;
            for (; nm[j] && j<63; j++){ char c=nm[j]; if(c>='A'&&c<='Z') c+=32; low[j]=c; }
            low[j]=0;
            if (strstr(low,phases[p])){ w->reloadSeq[w->reloadSeqN++]=i; break; }
        }
    }
    if (w->aShoot>=w->animN)  w->aShoot=w->aIdle;    // unresolved -> valid clip
    if (w->aReload>=w->animN) w->aReload=w->aIdle;   // (aReload==aIdle means "no reload clip")
    // Framing basis. raylib draws the POSED mesh (animVertices), but some FPS rigs
    // ship a bind pose that sits somewhere completely different (the MP5's bind
    // bbox is centred ~44k units off-origin, so recentering on it draws the gun
    // into empty space and it vanishes). For flagged slots, pose to idle frame 0
    // and frame from that; everyone else keeps the bind bbox they were tuned
    // against. Either way the recenter lands w->centroid at the viewmodel offset.
    Vector3 cc; float md;
    if (g_posedBasis[slot] && w->animN>0 && w->aIdle>=0 && w->aIdle<w->animN){
        UpdateModelAnimation(w->model, w->anim[w->aIdle], 0);
        MeasureBBox(w->model,1,&cc,&md);     // POSED idle geometry
    } else {
        MeasureBBox(w->model,0,&cc,&md);     // bind pose
    }
    w->centroid=cc;
    w->fitScale=(md>0.001f)?1.5f/md:1.0f;
    // Auto framing (passed scale0<=0): land the centroid dead ahead, slightly
    // down, scaled so the model is ~0.8 units (lower third). A saved tune overrides.
    if (scale0<=0.0f){
        float as=(md>0.001f)?0.8f/md:0.0004f;
        w->scale0=as; w->scale=as;
        w->off0=(Vector3){ 0.20f, -0.35f, -1.30f }; w->off=w->off0;
        w->yaw0=yaw0; w->pitch0=pitch0; w->roll0=roll0;  // orientation still a guess; user rotates
    }
    LoadTune(w->tunePath,&w->off,&w->scale,&w->yaw,&w->pitch,&w->roll);
    DebugLog("weapon","\"slot\":%d,\"label\":\"%s\",\"bones\":%d,\"anims\":%d,\"idle\":%d,\"shoot\":%d,\"reload\":%d,\"bbox\":%.1f,\"scale\":%.6f,\"centroid\":[%.1f,%.1f,%.1f]",
             slot,label,w->model.skeleton.boneCount,w->animN,w->aIdle,w->aShoot,w->aReload,md,w->scale,w->centroid.x,w->centroid.y,w->centroid.z);
}

// Attach a fire sound to a weapon slot. loop=1 -> sustained machine-gun fire
// (held in Update); loop=0 -> one blast per shot (played in Fire). No-op until
// the audio device is up, and silently skips a missing file (game still runs).
static void LoadWeaponSound(int slot, const char *path, const char *alt, int loop){
    if (!g_audio || slot<0 || slot>=16) return;
    Weapon *w=&g_weapons[slot];
    const char *fp=path; if(!FileExists(fp)) fp=alt;
    if(!FileExists(fp)){ DebugLog("wsound","\"slot\":%d,\"error\":\"missing\",\"path\":\"%s\"",slot,JStr(path)); return; }
    w->fireSnd=LoadSound(fp);
    w->hasSnd=(w->fireSnd.frameCount>0);
    w->sndLoop=loop;
    DebugLog("wsound","\"slot\":%d,\"path\":\"%s\",\"frames\":%u,\"loop\":%d",slot,JStr(fp),w->fireSnd.frameCount,loop);
}

// Secondary per-weapon sound (minigun spin-down, shotgun cock-between-shots).
static void LoadWeaponAux(int slot, const char *path, const char *alt){
    if (!g_audio || slot<0 || slot>=16) return;
    Weapon *w=&g_weapons[slot];
    const char *fp=path; if(!FileExists(fp)) fp=alt;
    if(!FileExists(fp)) return;
    w->auxSnd=LoadSound(fp); w->hasAuxSnd=(w->auxSnd.frameCount>0);
}

// ---- Tracers + impact sparks ------------------------------------------------
typedef struct { Vector3 a, b; float life; } Tracer;
typedef struct { Vector3 pos, vel; float life, life0, size, rest; Color col; int blood, lasting, pool; float ix, iz; unsigned int seed; } Spark; // rest=floor Y; blood=1 draws as a liquid streak; lasting=1 settles into spatter; pool=1 is a flat continuous stain at the body's feet; seed=stable jitter
#define MAX_TRACERS 32
#define MAX_SPARKS  4096
static Tracer g_tracers[MAX_TRACERS];
static Spark  g_sparks[MAX_SPARKS];

// Minigun barrel smoke/steam: soft camera-facing billboards that rise, expand and fade.
// hot=1 firing (darker, faster gunsmoke); hot=0 spin-down/cooling (white steam).
typedef struct { Vector3 pos, vel; float life, life0, sz0, sz1; int hot; } Smoke;
#define MAX_SMOKE 384
static Smoke      g_smoke[MAX_SMOKE];
static Texture2D  g_smokeTex;            // soft radial-gradient puff
static int        g_hasSmokeTex = 0;
static void SpawnSmoke(Vector3 o, int hot){
    for (int s=0;s<MAX_SMOKE;s++){
        if (g_smoke[s].life>0) continue;
        Smoke p={0};
        p.pos=o;
        p.pos.x += GetRandomValue(-2,2)/100.0f; p.pos.y += GetRandomValue(-2,2)/100.0f; p.pos.z += GetRandomValue(-2,2)/100.0f; // tight emission point
        p.vel=(Vector3){ GetRandomValue(-10,10)/100.0f, 0.30f+GetRandomValue(0,30)/100.0f, GetRandomValue(-10,10)/100.0f };     // finer column, less spread
        if (hot) p.vel.y += 0.30f;                              // gunsmoke puffs up faster
        p.life0 = (hot? 0.50f:1.00f) + GetRandomValue(0,55)/100.0f;
        p.life=p.life0;
        p.sz0 = 0.018f + GetRandomValue(0,2)/100.0f;            // small, fine particles at the muzzle
        p.sz1 = (hot?0.12f:0.20f) + GetRandomValue(0,8)/100.0f; // billow out modestly as they age
        p.hot = hot;
        g_smoke[s]=p; return;
    }
}
static void UpdateSmoke(float dt){
    for (int s=0;s<MAX_SMOKE;s++){ Smoke *p=&g_smoke[s]; if (p->life<=0) continue;
        p->life -= dt;
        p->vel.y += 0.5f*dt;                                   // buoyancy
        p->vel.x *= (1.0f-1.6f*dt); p->vel.z *= (1.0f-1.6f*dt);// drag
        p->pos = Vector3Add(p->pos, Vector3Scale(p->vel,dt));
    }
}
static void DrawSmoke(Camera3D cam){
    if (!g_hasSmokeTex) return;
    rlDisableDepthMask();                                      // particles blend, don't occlude each other
    for (int s=0;s<MAX_SMOKE;s++){ Smoke *p=&g_smoke[s]; if (p->life<=0) continue;
        float age=1.0f-p->life/p->life0;                       // 0..1
        float sz=p->sz0+(p->sz1-p->sz0)*age;
        float fade=p->life/p->life0; fade*=fade;               // ease-out fade
        unsigned char g=p->hot?90:205;                         // gunsmoke grey vs white steam
        Color tint={g,g,g,(unsigned char)(fade*(p->hot?150:120))};
        DrawBillboard(cam, g_smokeTex, p->pos, sz, tint);
    }
    rlEnableDepthMask();
}

// ---- Enemies (assets/enemy.glb - Mixamo walk rig, 37 bones, verified) -------
// One shared Model drawn many times; each enemy keeps its own animation cursor
// so they don't march in lockstep. Mixamo export is cm-scale (~185u tall) with
// feet at the origin facing +Z, so ENEMY_SCALE shrinks to ~1.85m and we yaw
// each to face the player. ENEMY_YAW_OFFSET flips facing if they moonwalk.
#define MAX_ENEMIES        12
#define ENEMY_SCALE        1.0000f      // FBX2glTF export is already upright meter-scale (~1.86m)
#define ENEMY_SPEED        3.4f
#define ENEMY_HP           100.0f
#define ENEMY_DMG_PER_SHOT 34.0f      // ~3 shots to kill
#define ENEMY_RADIUS       0.5f
#define ENEMY_MOVE_R       0.48f      // movement collision radius: kept just BELOW NAV_WALLR (fits everywhere nav routes) but near the visual width so the body does not poke through walls
#define ENEMY_HANDS_R      0.70f      // body+arms reach: a stuck/idling enemy eases back until walls are this far, so its animated hands stop poking through
                                      // fits everywhere the nav graph routes it (no jamming in tight cells / on stairs)
#define ENEMY_HEIGHT       1.85f
#define ENEMY_YAW_OFFSET   0.0f       // tune if they face the wrong way
#define ENEMY_TOUCH_DMG    18.0f      // player hp/sec when an enemy is in melee
#define ENEMY_ATTACK_RANGE  1.8f       // enters attack/melee within this range
// Headshot zone: a narrower box at the TOP of the enemy silhouette. A shot
// whose aim ray pierces it deals HEADSHOT_MULT x damage. Sized for the Mixamo
// humanoid (~1.85m): the head/upper-neck sits in the top ~0.32m, narrower than
// the 0.5 body radius. 3x of ENEMY_DMG_PER_SHOT (34) = 102 >= ENEMY_HP, so a
// clean headshot one-shot-kills a full-HP enemy (the classic FPS payoff).
#define HEAD_ZONE_H        0.32f      // vertical extent of the head box (down from the top)
#define HEAD_RADIUS        0.22f      // half-width of the head box (tighter than the body)
#define HEADSHOT_MULT      3.0f       // headshot damage multiplier
typedef struct { Vector3 pos; float hp; int state; int clip; float animT; float deathT; float hitT; float roarT; float drawY; float stuckT; float embedT; } Enemy; // state: 0 dead,1 alive,2 dying; roarT=attack-roar cooldown; drawY=smoothed render height; stuckT=time chasing-but-not-moving (->idle); embedT=time wedged inside a wall (->respawn)
static Enemy           g_enemies[MAX_ENEMIES];
#define MAX_CORPSES 32
typedef struct { Vector3 pos; float yaw; int active; } Corpse;   // lasting dead bodies, posed at the death-anim end
static Corpse          g_corpses[MAX_CORPSES];
static int             g_corpseNext = 0;
static Model           g_enemy;
static ModelAnimation *g_enemyAnim = NULL;
static int             g_enemyAnimN = 0;
static int g_eWalk=0, g_eIdle=0, g_eRun=0, g_eHit=0, g_eAttack=0, g_eDeath=0; // clips resolved by name at load
static int g_eDeathFrame=0;   // frame to freeze the death clip at: its LAST keyframe often loops back to the standing
                              // start pose, so corpses must settle on an earlier "body on the floor" frame instead
static int             g_hasEnemy = 0;
static int             g_kills = 0;
static int             g_headshots = 0;        // running headshot-kill tally (HUD)
static float           g_hsFlash = 0.0f;       // >0: flash "HEADSHOT!" near the crosshair
static float           g_playerHp = 100.0f;
static int             g_godMode = 1;           // dev stage: invulnerable (no death, no "YOU DIED"). G toggles.
static int             g_noclip = 0;            // map mode: F toggles free-fly / noclip vs FPS collision

static void EnemyArm(int i, Vector3 pos){       // common: bring enemy i to life at pos
    g_enemies[i].pos=pos; g_enemies[i].hp=ENEMY_HP; g_enemies[i].state=1;
    g_enemies[i].animT=(float)GetRandomValue(0,40); g_enemies[i].deathT=0;
    g_enemies[i].hitT=0; g_enemies[i].clip=g_eRun; g_enemies[i].roarT=0; g_enemies[i].drawY=pos.y;
    g_enemies[i].stuckT=0; g_enemies[i].embedT=0;
}
// A spawn spot is good only if it's near the player's level, inside the player's
// walkable nav region (not sealed off behind a wall), AND has body clearance so the
// enemy isn't jammed in a corner where it can't move (the random point can sit nearer
// a wall than the nav cell centre, so re-check the actual sphere here).
static int EnemySpawnOK(float x, float fy, float z){
    return fabsf(fy-(g_pos.y-EYE_H))<6.0f
        && MapNavReachable((Vector3){x,fy,z})
        && !MapSphereHitsWall((Vector3){x,fy+0.9f,z},ENEMY_MOVE_R);   // body fits (same radius it moves with)
}
// Map spawn: a ring around the player, dropped onto the floor under each spot.
static void SpawnEnemyMap(int i){
    for (int t=0;t<40;t++){
        float ang=(float)GetRandomValue(0,628)/100.0f;
        float dist=6.0f+GetRandomValue(0,90)/10.0f;          // 6..15 units out
        float x=g_pos.x+cosf(ang)*dist, z=g_pos.z+sinf(ang)*dist;
        float top=g_pos.y+8.0f;
        float fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},80.0f);
        if (fd>0){ float fy=top-fd; if (EnemySpawnOK(x,fy,z)){ EnemyArm(i,(Vector3){x,fy,z}); return; } }
    }
    // ring failed: scan outward from the player for any reachable, roomy cell.
    for (int rad=2; rad<14; rad++) for (int a=0;a<16;a++){
        float ang=a*0.3927f, x=g_pos.x+cosf(ang)*rad, z=g_pos.z+sinf(ang)*rad;
        float top=g_pos.y+8.0f, fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},80.0f);
        if (fd>0){ float fy=top-fd; if (EnemySpawnOK(x,fy,z)){ EnemyArm(i,(Vector3){x,fy,z}); return; } }
    }
    // nothing valid near the player (e.g. player is off the nav mesh in a tight nook):
    // drop the enemy on a RANDOM walkable nav cell -- a guaranteed collision-free,
    // navigable spot -- instead of blindly next to the player (which may be inside a wall).
    if (g_navOK){
        for (int t=0;t<400;t++){
            int cx=GetRandomValue(0,g_navNX-1), cz=GetRandomValue(0,g_navNZ-1), ci=cz*g_navNX+cx;
            if (g_navWalk[ci]){
                EnemyArm(i,(Vector3){ g_navMn.x+(cx+0.5f)*g_navCS, g_navFloor[ci], g_navMn.z+(cz+0.5f)*g_navCS });
                return;
            }
        }
    }
    EnemyArm(i,(Vector3){g_pos.x, g_pos.y-EYE_H, g_pos.z});        // absolute fallback: the player's own (occupied) spot
}
static void SpawnEnemy(int i){
    if (g_hasMap && g_mapCol.ready){ SpawnEnemyMap(i); return; }
    // arena: a random spot near a wall, away from the player
    float ang=(float)GetRandomValue(0,628)/100.0f;
    float dist=ARENA*0.6f + GetRandomValue(0,(int)(ARENA*0.3f));
    EnemyArm(i,(Vector3){ cosf(ang)*dist, 0, sinf(ang)*dist });
}

static Texture2D MakeChecker(int sz, Color a, Color b, int cells) {
    Image img = GenImageChecked(sz, sz, sz/cells, sz/cells, a, b);
    Texture2D t = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

static void SpawnImpact(Vector3 p, Vector3 n) {
    for (int i=0;i<14;i++) for (int s=0;s<MAX_SPARKS;s++){
        if (g_sparks[s].life>0) continue;
        Vector3 v={ n.x+GetRandomValue(-100,100)/100.0f, n.y+GetRandomValue(-20,120)/100.0f, n.z+GetRandomValue(-100,100)/100.0f };
        v=Vector3Scale(Vector3Normalize(v), 3.0f+GetRandomValue(0,300)/100.0f);
        float lf=0.4f+GetRandomValue(0,30)/100.0f;
        Spark sp={0}; sp.pos=p; sp.vel=v; sp.life=lf; sp.life0=lf; sp.size=0.04f; sp.rest=p.y; sp.blood=0;
        sp.col=(Color){255,(unsigned char)(200+GetRandomValue(0,55)),60,255};
        g_sparks[s]=sp;
        break;
    }
}

// Detailed blood burst: a spray of crimson droplets of varied size flung along
// the bullet's travel (dir) in a wide cone, plus heavier slow "gobs" every few
// particles. Gravity + fade are handled by the shared spark integrator/draw.
static void SpawnBlood(Vector3 p, Vector3 dir, float intensity, float floorY) {
    int n=(int)(200*intensity); if (n>900) n=900;          // headshots spray harder (capped)
    for (int i=0;i<n;i++) for (int s=0;s<MAX_SPARKS;s++){
        if (g_sparks[s].life>0) continue;
        float spread=3.6f;
        Vector3 v={ dir.x*4.5f + GetRandomValue(-100,100)/100.0f*spread,
                    dir.y*4.5f + GetRandomValue(-100,100)/100.0f*spread + 1.7f,
                    dir.z*4.5f + GetRandomValue(-100,100)/100.0f*spread };
        int gob=(i%5==0);                                  // heavy gobs: slower, redder, and LASTING (settle into spatter)
        if (gob){ v.x*=0.45f; v.y=v.y*0.45f+0.4f; v.z*=0.45f; }
        float lf = 1.1f + GetRandomValue(0,120)/100.0f;
        // small droplets: in flight they render as thin streaks, not balls. Settled
        // gobs leave only FINE spatter dots -- the continuous pool below carries the volume.
        float sz = gob ? (0.016f+GetRandomValue(0,10)/1000.0f) : (0.010f+GetRandomValue(0,8)/1000.0f);
        Spark sp={0}; sp.pos=p; sp.vel=v; sp.life=lf; sp.life0=lf; sp.size=sz; sp.rest=floorY; sp.blood=1; sp.lasting=gob;
        sp.seed=(unsigned int)GetRandomValue(1,1<<30);     // stable per-splat outline jitter
        sp.col=(Color){ (unsigned char)(150+GetRandomValue(0,105)), (unsigned char)GetRandomValue(0,28), (unsigned char)GetRandomValue(0,18), 255 };
        g_sparks[s]=sp;
        break;
    }
    // A flat continuous pool stain spreading from the body's feet -- this is what
    // reads as "liquid"; the flying gobs above just stipple fine spatter around it.
    for (int s=0;s<MAX_SPARKS;s++){
        if (g_sparks[s].life>0) continue;
        Spark sp={0};
        sp.pos=(Vector3){p.x, floorY+0.02f, p.z}; sp.vel=(Vector3){0,0,0};   // born settled on the floor
        sp.life=1e9f; sp.life0=1e9f; sp.rest=floorY;
        sp.blood=1; sp.lasting=1; sp.pool=1;
        sp.size=0.20f+0.11f*intensity;                     // pool radius; headshots pool wider
        sp.seed=(unsigned int)GetRandomValue(1,1<<30);
        sp.col=(Color){ (unsigned char)(105+GetRandomValue(0,35)), (unsigned char)GetRandomValue(0,12), (unsigned char)GetRandomValue(0,8), 255 };
        g_sparks[s]=sp;
        break;
    }
}

static int RaycastWorld(Vector3 ro, Vector3 rd, Vector3 *hit, Vector3 *nrm) {
    float best=1e9f; int got=0;
    if (rd.y<-1e-4f){ float t=-ro.y/rd.y; if(t>0&&t<best){best=t;*hit=Vector3Add(ro,Vector3Scale(rd,t));*nrm=(Vector3){0,1,0};got=1;} }
    if (fabsf(rd.x)>1e-4f) for(int s=0;s<2;s++){ float px=s?-ARENA:ARENA; float t=(px-ro.x)/rd.x;
        if(t>0&&t<best){Vector3 h=Vector3Add(ro,Vector3Scale(rd,t)); if(fabsf(h.z)<=ARENA&&h.y>=0&&h.y<=WALL_H){best=t;*hit=h;*nrm=(Vector3){s?1.f:-1.f,0,0};got=1;}}}
    if (fabsf(rd.z)>1e-4f) for(int s=0;s<2;s++){ float pz=s?-ARENA:ARENA; float t=(pz-ro.z)/rd.z;
        if(t>0&&t<best){Vector3 h=Vector3Add(ro,Vector3Scale(rd,t)); if(fabsf(h.x)<=ARENA&&h.y>=0&&h.y<=WALL_H){best=t;*hit=h;*nrm=(Vector3){0,0,s?1.f:-1.f};got=1;}}}
    for (int c=0;c<NUM_CRATES;c++){
        Vector3 mn=Vector3Subtract(g_crates[c].pos,Vector3Scale(g_crates[c].size,0.5f));
        Vector3 mx=Vector3Add(g_crates[c].pos,Vector3Scale(g_crates[c].size,0.5f));
        RayCollision rc=GetRayCollisionBox((Ray){ro,rd},(BoundingBox){mn,mx});
        if(rc.hit&&rc.distance>0&&rc.distance<best){best=rc.distance;*hit=rc.point;*nrm=rc.normal;got=1;}
    }
    return got;
}

// switch the weapon animation, restart its cursor. Retained for future weapon
// actions even though the reload/fire paths now set clips directly.
static void __attribute__((unused)) SetAnim(int idx, int once){
    g_curAnim=idx; g_animT=0.0f; g_animOnce=once;
}

// Test the aim ray against alive enemies (treated as a vertical box). Returns
// the nearest enemy index hit within maxDist, -1 if none; sets *outDist/*outPt.
// *outHead is set to 1 when that nearest hit also pierces the enemy's head box.
static int HitEnemy(Vector3 ro, Vector3 rd, float maxDist, float *outDist, Vector3 *outPt, int *outHead){
    int best=-1; float bd=maxDist; if (outHead) *outHead=0;
    for (int i=0;i<MAX_ENEMIES;i++){
        if (g_enemies[i].state!=1) continue;
        Vector3 c=g_enemies[i].pos;          // c.y = the enemy's floor (0 in the arena, map height on a map)
        BoundingBox bb={ (Vector3){c.x-ENEMY_RADIUS, c.y, c.z-ENEMY_RADIUS},
                         (Vector3){c.x+ENEMY_RADIUS, c.y+ENEMY_HEIGHT, c.z+ENEMY_RADIUS} };
        RayCollision rc=GetRayCollisionBox((Ray){ro,rd},bb);
        if (rc.hit && rc.distance>0 && rc.distance<bd){
            bd=rc.distance; best=i; *outDist=rc.distance; *outPt=rc.point;
            // The head box lives inside the body box's vertical span, so the
            // nearest-enemy pick above is unaffected; we just additionally ask
            // whether this same ray clips the (narrower) head of that enemy.
            BoundingBox hb={ (Vector3){c.x-HEAD_RADIUS, c.y+ENEMY_HEIGHT-HEAD_ZONE_H, c.z-HEAD_RADIUS},
                             (Vector3){c.x+HEAD_RADIUS, c.y+ENEMY_HEIGHT,             c.z+HEAD_RADIUS} };
            RayCollision hrc=GetRayCollisionBox((Ray){ro,rd},hb);
            if (outHead) *outHead = (hrc.hit && hrc.distance>0);
        }
    }
    return best;
}

static void Fire(Camera3D cam) {
    if (g_fireCd>0) return;
    Weapon *cw=&g_weapons[g_curWeapon];
    g_fireCd = (cw->fireCd>0.0f) ? cw->fireCd : 0.12f;   // per-weapon fire rate (slow shotguns etc.)
    // One-shot weapons (e.g. shotguns) sound their report here, per shot; auto
    // weapons loop in Update() instead.
    if (g_audio && cw->hasSnd && !cw->sndLoop) PlaySound(cw->fireSnd);
    Vector3 dir=Vector3Normalize(Vector3Subtract(cam.target,cam.position));
    Vector3 muzzle=Vector3Add(cam.position,Vector3Scale(dir,0.4f));
    Vector3 hit,nrm,end=Vector3Add(cam.position,Vector3Scale(dir,80.0f));
    float worldDist=80.0f;
    int worldHit=RaycastWorld(cam.position,dir,&hit,&nrm);
    if (worldHit){ end=hit; worldDist=Vector3Distance(cam.position,hit); }
    // enemy hit takes priority if it's closer than the world geometry
    float ed; Vector3 ep; int head=0;
    int ei=HitEnemy(cam.position,dir,worldDist,&ed,&ep,&head);
    if (ei>=0){
        end=ep;
        SpawnBlood(ep,dir, head?1.9f:1.0f, g_enemies[ei].pos.y);   // gobs pool on the enemy's floor
        g_enemies[ei].hp -= head ? ENEMY_DMG_PER_SHOT*HEADSHOT_MULT : ENEMY_DMG_PER_SHOT;
        if (head) g_hsFlash=1.1f;                         // flash "HEADSHOT!" on any head hit
        if (g_enemies[ei].hp<=0){
            g_enemies[ei].state=2; g_enemies[ei].deathT=0; g_enemies[ei].animT=0;
            g_enemies[ei].drawY=g_enemies[ei].pos.y;      // settle the render height so the corpse doesn't pop
            g_kills++; if (head) g_headshots++;           // tally a confirmed headshot kill
            if (g_audio && g_nDeathSnd>0) PlaySound(g_deathSnd[GetRandomValue(0,g_nDeathSnd-1)]);   // random death sound
        } else {
            g_enemies[ei].hitT=0.45f;                     // non-fatal hit -> flinch
        }
    } else if (worldHit){
        SpawnImpact(hit,nrm);
    }
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life<=0){ g_tracers[i]=(Tracer){muzzle,end,0.05f}; break; }
    // Code-driven recoil instead of the Shoot clip (that clip repositions the
    // gun out of the viewmodel frame -> "gun goes away"). Kick rises to 1, decays.
    g_recoil=1.0f;
    if (cw->playShoot && g_aShoot!=g_aIdle && g_aShoot<g_gunAnimN && !g_reloading){
        g_curAnim=g_aShoot; g_animOnce=1; g_animT=0.0f;    // play the model's Shot clip (AK)
    }
    DebugLog("fire","\"enemy\":%d,\"head\":%d,\"end\":[%.2f,%.2f,%.2f]", ei, head, end.x,end.y,end.z);
}

// Can the enemy stand at (x,z) coming from height curY? The spot must have a floor
// that's a sane STEP UP (<=ENEMY_STEP) or DROP DOWN (<=ENEMY_MAXDROP, ~a flight of
// stairs) -- NOT off a ledge into the void -- and be clear of walls. This single test
// drives all enemy movement, so they slide along walls, climb stairs, and crucially
// REFUSE to walk forward off a ledge (they'll follow the nav path around instead).
#define ENEMY_STEP    0.7f    // max riser an enemy can step up (matches the player's auto-step)
#define ENEMY_MAXDROP 1.6f    // max drop it will step down (== NAV_STEPDOWN); bigger = a ledge, don't walk off
static int EnemyMoveOK(float x, float z, float curY){
    if (!g_mapCol.ready) return 1;
    float top=curY+ENEMY_STEP, fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},40.0f);
    if (fd<=0) return 0;                                // no floor under the spot -> the void
    float ny=top-fd;                                   // destination floor height
    if (ny>curY+ENEMY_STEP)    return 0;               // riser too tall to step up
    if (ny<curY-ENEMY_MAXDROP) return 0;               // drop too big -> a ledge, don't walk off it
    return !MapSphereHitsWall((Vector3){x,ny+0.9f,z},ENEMY_MOVE_R);  // clear where it'd stand
}
// Move enemy i by (mx,mz), per-axis -- nothing walks through walls OR off ledges. Both
// the chase and the enemy-vs-enemy separation go through here.
static void EnemyNudge(int i, float mx, float mz){
    Enemy *e=&g_enemies[i];
    if (g_hasMap && g_mapCol.ready){
        if (EnemyMoveOK(e->pos.x+mx, e->pos.z, e->pos.y)) e->pos.x+=mx;
        if (EnemyMoveOK(e->pos.x, e->pos.z+mz, e->pos.y)) e->pos.z+=mz;
    } else { e->pos.x+=mx; e->pos.z+=mz; }
}

// Walk alive enemies toward the player; advance death timers; melee on contact.
static void UpdateEnemies(float dt){
    if (!g_hasEnemy) return;
    int prevCell = g_navPlayerCell;
    if (g_hasMap) MapNavUpdate(g_pos);                 // refresh the flow field if the player moved cells
    int flowChanged = (g_navPlayerCell != prevCell);   // player crossed into a new cell -> paths changed
    for (int i=0;i<MAX_ENEMIES;i++){
        Enemy *e=&g_enemies[i];
        if (e->state==1){
            float dx=g_pos.x-e->pos.x, dz=g_pos.z-e->pos.z;
            float d=sqrtf(dx*dx+dz*dz);
            if (e->hitT>0) e->hitT-=dt;
            if (e->roarT>0) e->roarT-=dt;
            // "Running in place" = it's trying to chase but the wall won't let it move at
            // all (an unreachable spot). stuckT accrues only while it tries-but-can't-move
            // (updated in the chase block below); a NAVIGATING enemy moves every frame even
            // when it curves around, so this never false-flags it. flowChanged resets it.
            if (flowChanged) e->stuckT=0.0f;
            int stuck = (g_hasMap && e->stuckT > 1.0f);
            // self-heal: if the body is wedged INSIDE wall geometry (spawned/pushed into an
            // impossible spot, e.g. clipping through a wall) it can never free itself ->
            // respawn it somewhere valid. Tracked independently of chase state, so it also
            // fires when the enemy is jammed in a wall right next to the player (attack range).
            if (g_mapCol.ready && MapSphereHitsWall((Vector3){e->pos.x, e->pos.y+0.9f, e->pos.z}, 0.35f)){
                e->embedT += dt;
                if (e->embedT > 0.8f){ SpawnEnemy(i); continue; }   // SpawnEnemy always picks a navigable cell now
            } else e->embedT = 0.0f;
            // clip priority: flinch > attack (in melee range) > idle-when-stuck > run (chasing)
            int want = (e->hitT>0) ? g_eHit : (d<=ENEMY_ATTACK_RANGE ? g_eAttack : (stuck ? g_eIdle : g_eRun));
            if (want!=e->clip){
                if (want==g_eAttack && e->roarT<=0 && g_audio && g_hasAttackSnd){   // lunging into an attack -> roar
                    PlaySound(g_attackSnd); e->roarT=2.5f;                          // cooldown so it doesn't spam at the range edge
                }
                e->clip=want; e->animT=0;
            }
            if (e->hitT<=0 && d>ENEMY_ATTACK_RANGE && !stuck){ // chase: follow the nav flow field, else beeline
                Vector3 sd; float mx=dx/d, mz=dz/d;
                if (g_hasMap && MapNavSteer(e->pos,&sd)){ mx=sd.x; mz=sd.z; }
                float step=ENEMY_SPEED*dt; Vector3 b4=e->pos;
                EnemyNudge(i, mx*step, mz*step);
                // wedged (the steer heads into a wall and per-axis collision blocks BOTH
                // axes)? sweep the heading outward to either side -- nearest angle first --
                // until one frees up, so the enemy slides out of corners instead of running
                // in place. Goes out to ±150° to escape concave pockets.
                if (g_hasMap && fabsf(e->pos.x-b4.x)<1e-5f && fabsf(e->pos.z-b4.z)<1e-5f){
                    const float ANG[10]={0.524f,-0.524f, 1.047f,-1.047f, 1.571f,-1.571f, 2.094f,-2.094f, 2.618f,-2.618f}; // ±30,60,90,120,150
                    for (int k=0;k<10;k++){
                        float c=cosf(ANG[k]), s=sinf(ANG[k]);
                        EnemyNudge(i, (mx*c-mz*s)*step, (mx*s+mz*c)*step);
                        if (fabsf(e->pos.x-b4.x)>1e-5f || fabsf(e->pos.z-b4.z)>1e-5f) break;
                    }
                }
                // tried to chase this frame: if it barely moved, it's grinding a wall; if it
                // moved at all (even sliding/curving), it's making its way -> not stuck.
                float mv=(e->pos.x-b4.x)*(e->pos.x-b4.x)+(e->pos.z-b4.z)*(e->pos.z-b4.z);
                if (mv < 0.0004f) e->stuckT += dt; else e->stuckT = 0.0f;   // 0.02 units/frame threshold
            }
            if (g_hasMap && stuck && e->hitT<=0 && d>ENEMY_ATTACK_RANGE){
                // stuck against a wall and idling -> ease back so the animated HANDS stop
                // poking through it. Drift toward whichever directions clear a hands-radius
                // sphere; settles once the body+arms no longer overlap the wall.
                float by=e->pos.y+0.9f;
                if (MapSphereHitsWall((Vector3){e->pos.x,by,e->pos.z},ENEMY_HANDS_R)){
                    float ox=0,oz=0;
                    const float A[8]={0.0f,0.785f,1.571f,2.356f,3.142f,3.927f,4.712f,5.498f};
                    for (int k=0;k<8;k++){ float cxx=cosf(A[k]),czz=sinf(A[k]);
                        if (!MapSphereHitsWall((Vector3){e->pos.x+cxx*0.3f,by,e->pos.z+czz*0.3f},ENEMY_HANDS_R)){ ox+=cxx; oz+=czz; } }
                    float l=sqrtf(ox*ox+oz*oz);
                    if (l>1e-4f) EnemyNudge(i, ox/l*ENEMY_SPEED*dt*0.6f, oz/l*ENEMY_SPEED*dt*0.6f);
                }
            }
            if (g_hasMap && g_mapCol.ready){                   // keep the enemy standing on the map floor
                // look down from only a step above the feet, not 4m up -- otherwise the
                // snap can grab an overhead landing and wedge the enemy under the stairs.
                float top=e->pos.y+ENEMY_STEP, fd=MapRayNearest((Vector3){e->pos.x,top,e->pos.z},(Vector3){0,-1,0},40.0f);
                if (fd>0) e->pos.y=top-fd;
            }
            // ease the RENDER height toward the real floor so stair steps ramp smoothly
            // instead of popping; snap on big jumps (spawn / falling off a ledge). Clamp
            // the climb side so the feet ride ON each step, never sunk under it.
            if (fabsf(e->pos.y-e->drawY)>1.5f) e->drawY=e->pos.y;
            else {
                e->drawY += (e->pos.y-e->drawY)*(1.0f-expf(-16.0f*dt));
                if (e->drawY < e->pos.y-0.03f) e->drawY = e->pos.y-0.03f;   // feet never sink into the stairs
            }
            if (e->clip==g_eAttack && e->hitT<=0 && !g_godMode) g_playerHp-=ENEMY_TOUCH_DMG*dt;
            if (g_enemyAnimN>0){                               // advance current clip (looping)
                int nf=ANIM_FRAMES(g_enemyAnim[e->clip]);
                float spd=(e->clip==g_eAttack)?90.0f:30.0f;    // attack plays much faster
                e->animT+=dt*spd;
                if (nf>0 && e->animT>=nf) e->animT-=nf;
            }
        } else if (e->state==2){                              // dying: play the death anim to its settle frame, then leave a corpse
            int nf = (g_enemyAnimN>0) ? g_eDeathFrame : 0;     // stop at the "on the floor" frame, not the spring-back last frame
            if (nf>0){ e->animT+=dt*60.0f; if (e->animT>(float)nf) e->animT=(float)nf; }
            e->deathT+=dt;
            // wait until the death anim has actually settled (body on the floor),
            // not a fixed timer -- otherwise the corpse is recorded mid-fall.
            if ((nf<=0 || e->animT>=(float)nf) && e->deathT>0.2f){
                float yaw=atan2f(g_pos.x-e->pos.x, g_pos.z-e->pos.z)*RAD2DEG + ENEMY_YAW_OFFSET;
                g_corpses[g_corpseNext]=(Corpse){ e->pos, yaw, 1 };   // lasting body, posed at the final death frame
                g_corpseNext=(g_corpseNext+1)%MAX_CORPSES;
                if (!g_noEnemies) SpawnEnemy(i); else e->state=0;
            }
        }
    }
    for (int a=0;a<MAX_ENEMIES;a++){            // keep enemies from overlapping each other
        if (g_enemies[a].state!=1) continue;
        for (int b=a+1;b<MAX_ENEMIES;b++){
            if (g_enemies[b].state!=1) continue;
            float dx=g_enemies[b].pos.x-g_enemies[a].pos.x;
            float dz=g_enemies[b].pos.z-g_enemies[a].pos.z;
            float d=sqrtf(dx*dx+dz*dz), mind=ENEMY_RADIUS*2.0f;
            if (d<mind && d>1e-4f){
                float push=(mind-d)*0.5f/d;
                EnemyNudge(a, -dx*push, -dz*push);    // wall-gated, so crowding can't shove them through walls
                EnemyNudge(b,  dx*push,  dz*push);
            }
        }
    }
    if (g_playerHp<0) g_playerHp=0;
}

static void Collide(void) {
    float r=0.4f;
    if (g_hasMap){          // map: walls/floor are handled in Update(); here just push out of enemies
        for (int e=0;e<MAX_ENEMIES;e++){
            if (g_enemies[e].state!=1) continue;
            if (fabsf((g_pos.y-EYE_H)-g_enemies[e].pos.y) > ENEMY_HEIGHT) continue;   // different floor -> ignore
            float dx=g_pos.x-g_enemies[e].pos.x, dz=g_pos.z-g_enemies[e].pos.z;
            float d=sqrtf(dx*dx+dz*dz), mind=r+ENEMY_RADIUS;
            if (d<mind && d>1e-4f){
                float nx=g_pos.x+dx*(mind-d)/d, nz=g_pos.z+dz*(mind-d)/d, by=g_pos.y-0.55f;
                if (!MapSphereHitsWall((Vector3){nx,by,g_pos.z},0.42f)) g_pos.x=nx;   // don't get shoved into a wall
                if (!MapSphereHitsWall((Vector3){g_pos.x,by,nz},0.42f)) g_pos.z=nz;
            }
        }
        return;
    }
    if (g_pos.x> ARENA-r) g_pos.x= ARENA-r;  if (g_pos.x<-ARENA+r) g_pos.x=-ARENA+r;
    if (g_pos.z> ARENA-r) g_pos.z= ARENA-r;  if (g_pos.z<-ARENA+r) g_pos.z=-ARENA+r;
    for (int c=0;c<NUM_CRATES;c++){
        Vector3 b=g_crates[c].pos, hs=Vector3Scale(g_crates[c].size,0.5f);
        float dx=g_pos.x-b.x, dz=g_pos.z-b.z;
        float ox=hs.x+r-fabsf(dx), oz=hs.z+r-fabsf(dz);
        if (ox>0&&oz>0){ if(ox<oz) g_pos.x+=(dx>0?ox:-ox); else g_pos.z+=(dz>0?oz:-oz); }
    }
    for (int e=0;e<MAX_ENEMIES;e++){            // don't let the player walk through enemies
        if (g_enemies[e].state!=1) continue;
        float dx=g_pos.x-g_enemies[e].pos.x, dz=g_pos.z-g_enemies[e].pos.z;
        float d=sqrtf(dx*dx+dz*dz), mind=r+ENEMY_RADIUS;
        if (d>1e-4f){ if (d<mind){ float push=(mind-d)/d; g_pos.x+=dx*push; g_pos.z+=dz*push; } }
        else g_pos.x+=mind;                      // exactly coincident: nudge out
    }
}

static Camera3D g_cam;

// Begin the active weapon's reload: multi-phase sequence if it has one, else the
// single reload clip, else nothing. Used by the R key and the shotgun's pump.
static void StartReload(void){
    if (g_reloading) return;
    if (g_reloadSeqN>0){ g_reloading=1; g_reloadStep=0; g_curAnim=g_reloadSeq[0]; g_animOnce=1; g_animT=0.0f; }
    else if (g_aReload!=g_aIdle && g_aReload<g_gunAnimN){ g_reloading=1; g_reloadStep=-1; g_curAnim=g_aReload; g_animOnce=1; g_animT=0.0f; }
}

// Options/pause menu: a vertical stack of buttons centred on screen. DrawMenu and
// the click hit-test (in Update) both call MenuRect, so the layout can't drift.
#define MENU_N 3
static Rectangle MenuRect(int i){
    int W=GetScreenWidth(), H=GetScreenHeight();
    float bw=360, bh=54, gap=16, total=MENU_N*bh+(MENU_N-1)*gap;
    return (Rectangle){ W/2.0f-bw/2.0f, H/2.0f-total/2.0f+24+i*(bh+gap), bw, bh };
}
// Persist the fullscreen choice across launches (per-machine, gitignored like the
// tune files). Default is windowed; the menu writes "fullscreen 1" once enabled.
static void LoadOptions(void){
    FILE *f=fopen("options.txt","r"); if(!f) return;
    int fs=0; if (fscanf(f,"fullscreen %d",&fs)==1) g_fullscreen=(fs!=0);
    fclose(f);
}
static void SaveOptions(void){
    FILE *f=fopen("options.txt","w"); if(!f) return;
    fprintf(f,"fullscreen %d\n", g_fullscreen?1:0); fclose(f);
}

static void Update(void) {
    float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;

    // ESC toggles the options/pause menu. While open the cursor is freed for
    // clicking and the whole gameplay update is skipped (early return), so the
    // world freezes behind the overlay.
    if (IsKeyPressed(KEY_ESCAPE)){
        g_menu=!g_menu;
        if (g_menu) EnableCursor(); else DisableCursor();
        DebugLog("menu","\"open\":%s", g_menu?"true":"false");
    }
    if (g_menu){
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){
            Vector2 mp=GetMousePosition();
            for (int i=0;i<MENU_N;i++) if (CheckCollisionPointRec(mp,MenuRect(i))){
                if (i==0){                                  // Fullscreen toggle
                    ToggleBorderlessWindowed(); g_fullscreen=!g_fullscreen; SaveOptions();
                    EnableCursor();                         // the toggle can re-grab; keep it free for the menu
                    DebugLog("options","\"fullscreen\":%s", g_fullscreen?"true":"false");
                } else if (i==1){ g_menu=0; DisableCursor(); }   // Resume
                else if (i==2) g_quit=1;                          // Quit
            }
        }
        return;
    }

    // P pauses/unpauses the game: the gameplay update is skipped (early return) so
    // the world freezes, but the cursor stays captured so play resumes seamlessly.
    if (IsKeyPressed(KEY_P)){
        g_paused=!g_paused;
        DebugLog("pause","\"paused\":%s", g_paused?"true":"false");
    }
    if (g_paused) return;

    // Keep the mouse captured the whole time we're playing. macOS releases the
    // cursor whenever the window loses focus (Cmd-Tab, notifications, etc.), so
    // re-grab it the instant we have focus again and it's become visible.
    if (IsWindowFocused() && !IsCursorHidden()) DisableCursor();

    if (IsWindowReady() && IsCursorHidden()){
        Vector2 md=GetMouseDelta();
        g_yaw-=md.x*0.0025f; g_pitch-=md.y*0.0025f;
        if (g_pitch>1.5f) g_pitch=1.5f;  if (g_pitch<-1.5f) g_pitch=-1.5f;
    }
    Vector3 fwd =Vector3Normalize((Vector3){ sinf(g_yaw)*cosf(g_pitch), sinf(g_pitch), cosf(g_yaw)*cosf(g_pitch) });
    Vector3 flat=Vector3Normalize((Vector3){ sinf(g_yaw), 0, cosf(g_yaw) });
    Vector3 right=Vector3Normalize(Vector3CrossProduct(flat,(Vector3){0,1,0}));

    float speed=IsKeyDown(KEY_LEFT_SHIFT)?9.0f:5.0f;
    Vector3 wish={0,0,0}; int moving=0;
    if (IsKeyDown(KEY_W)){wish=Vector3Add(wish,flat);moving=1;}
    if (IsKeyDown(KEY_S)){wish=Vector3Subtract(wish,flat);moving=1;}
    if (IsKeyDown(KEY_D)){wish=Vector3Add(wish,right);moving=1;}
    if (IsKeyDown(KEY_A)){wish=Vector3Subtract(wish,right);moving=1;}
    Vector3 mv={0,0,0};
    if (moving && Vector3Length(wish)>0.001f){
        wish=Vector3Scale(Vector3Normalize(wish),speed*dt);
        mv=wish; g_bob+=dt*(IsKeyDown(KEY_LEFT_SHIFT)?14.0f:10.0f);
    }
    if (g_hasMap && !g_noclip){                         // block into walls per-axis (so you slide, never drift)
        float by=g_pos.y-0.55f;
        if (!MapSphereHitsWall((Vector3){g_pos.x+mv.x, by, g_pos.z}, 0.42f)) g_pos.x+=mv.x;
        if (!MapSphereHitsWall((Vector3){g_pos.x, by, g_pos.z+mv.z}, 0.42f)) g_pos.z+=mv.z;
    } else { g_pos.x+=mv.x; g_pos.z+=mv.z; }
    if (g_hasMap && g_noclip){                         // F: free-fly / noclip (inspect or escape)
        if (IsKeyDown(KEY_SPACE))        g_pos.y += speed*dt;
        if (IsKeyDown(KEY_LEFT_CONTROL)) g_pos.y -= speed*dt;
        g_vy=0; g_grounded=1;
    } else if (g_hasMap){                               // FPS collision against the loaded map mesh
        if (g_grounded && IsKeyPressed(KEY_SPACE)){ g_vy=6.0f; g_grounded=0; }
        float dn=MapRayNearest(g_pos,(Vector3){0,-1,0},80.0f);  // eye -> floor distance
        float floorEye = (dn>0) ? g_pos.y-dn+EYE_H : -1e30f;    // eye height standing on that floor
        const float STEP=0.7f;                                  // auto climb/descend up to this height
        if (g_vy<=0.0f && dn>0 && (g_pos.y-floorEye)<=STEP){
            // grounded (or within a step of the floor): glue the feet to it,
            // but feed the height change into g_eyeSmooth so the CAMERA eases
            // over the step instead of popping (the actual stair-jitter fix).
            g_eyeSmooth += floorEye - g_pos.y;
            if (g_eyeSmooth> 1.0f) g_eyeSmooth= 1.0f;
            if (g_eyeSmooth<-1.0f) g_eyeSmooth=-1.0f;
            g_pos.y=floorEye; g_vy=0.0f; g_grounded=1;
        } else {                                                // airborne: ballistic
            g_vy-=18.0f*dt; g_pos.y+=g_vy*dt; g_grounded=0;
            if (dn>0 && g_pos.y<=floorEye){ g_pos.y=floorEye; g_vy=0.0f; g_grounded=1; }   // land
            if (g_vy>0.0f){ float up=MapRayNearest(g_pos,(Vector3){0,1,0},0.4f); if (up>0) g_vy=0.0f; }  // ceiling
        }
    } else {
        if (g_grounded && IsKeyPressed(KEY_SPACE)){ g_vy=5.0f; g_grounded=0; }
        g_vy-=16.0f*dt; g_pos.y+=g_vy*dt;
        if (g_pos.y<=EYE_H){ g_pos.y=EYE_H; g_vy=0; g_grounded=1; }
    }
    Collide();

    // ease the step-smoothing offset back to 0 (~0.15s), then view from the
    // smoothed eye height so stairs glide instead of popping
    if (g_eyeSmooth!=0.0f){ g_eyeSmooth -= g_eyeSmooth*fminf(1.0f,dt*14.0f); if (fabsf(g_eyeSmooth)<0.002f) g_eyeSmooth=0.0f; }
    Vector3 eye=g_pos; eye.y-=g_eyeSmooth;
    if (g_shake>0.001f){                                   // minigun brain-shake: jitter the head a touch
        float a=g_shake*0.055f;
        eye.x += GetRandomValue(-1000,1000)/1000.0f*a;
        eye.y += GetRandomValue(-1000,1000)/1000.0f*a;
        eye.z += GetRandomValue(-1000,1000)/1000.0f*a;
    }
    g_cam.position=eye; g_cam.target=Vector3Add(eye,fwd); g_cam.up=(Vector3){0,1,0};
    if (g_shake>0.001f){                                   // plus a little look-direction wobble
        float a=g_shake*0.03f;
        g_cam.target.x += GetRandomValue(-1000,1000)/1000.0f*a;
        g_cam.target.y += GetRandomValue(-1000,1000)/1000.0f*a;
    }

    // shooting / reload
    if (g_fireCd>0) g_fireCd-=dt;
    if (g_recoil>0) g_recoil=fmaxf(0.0f, g_recoil - dt*7.0f);   // recoil settles in ~0.14s
    if (g_hsFlash>0) g_hsFlash-=dt;                            // "HEADSHOT!" flash fades out
    Weapon *cw=&g_weapons[g_curWeapon];
    int lmb=IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    int firingNow=0;                                       // looped fire sound active this frame?

    if (IsKeyPressed(KEY_R)) StartReload();
    else if (!g_reloading){
        if (cw->burst>0){                                  // rifle: one 3-round burst per trigger pull
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g_burstLeft==0) g_burstLeft=cw->burst;
            if (g_burstLeft>0){ firingNow=1; if (g_fireCd<=0.0f){ Fire(g_cam); g_burstLeft--; } }
        } else if (!cw->spinUp && !cw->soundGated && lmb){ // full-auto / one-shot (shotgun, sawnoff)
            int willFire=(g_fireCd<=0.0f);
            firingNow=1; Fire(g_cam);
            if (willFire && cw->autoReload){               // shotgun: pump (reload anim) + cock sound between shots
                StartReload();
                if (g_audio && cw->hasAuxSnd) PlaySound(cw->auxSnd);
            }
        }
    }

    // minigun: hold the trigger -> the barrel SPINS UP for MG_SPINUP (1/3 s) with no
    // bullets, then once it's at speed it fires full-auto (hard 5s cap -> overheat +
    // cooldown sound). Firing also drives g_shake (screen jitter + edge vignette).
    if (cw->spinUp){
        if (!lmb) g_mgLock=0;                              // releasing clears the overheat lock
        int trigger = lmb && !g_reloading && !g_mgLock;
        if (trigger){
            g_mgSpinT += dt;
            g_spin = fminf(1.0f, g_mgSpinT/MG_SPINUP);     // barrel accelerates, reaching full at MG_SPINUP
            if (g_audio && cw->hasAuxSnd && IsSoundPlaying(cw->auxSnd)) StopSound(cw->auxSnd);  // revving -> cut cooldown
            if (g_mgSpinT >= MG_SPINUP){                   // AT SPEED -> actually shoot bullets
                firingNow=1; Fire(g_cam); g_mgHeat+=dt;
                if (g_mgHeat>=5.0f) g_mgLock=1;            // overheated after 5s of fire
            }
            g_shake = fminf(1.0f, g_shake + dt*3.5f);      // brain-shake builds as it revs + fires
        } else {
            if (g_mgFiring && g_spin>0.3f && g_audio && cw->hasAuxSnd) PlaySound(cw->auxSnd);   // just stopped -> cooldown sound
            g_mgSpinT=0.0f;
            g_spin=fmaxf(0.0f, g_spin-dt*0.5f);            // barrel spins down (~2s to stop)
            if (g_spin<=0.0f && g_audio && cw->hasAuxSnd && IsSoundPlaying(cw->auxSnd)) StopSound(cw->auxSnd);  // stopped -> cut sound
            g_mgHeat=0.0f;
            g_shake=fmaxf(0.0f, g_shake-dt*4.0f);
        }
        g_mgFiring = (trigger && g_mgSpinT>=MG_SPINUP);    // "firing" = actually shooting (drives the cooldown-sound edge)
        // smoke/steam out of the spinning barrels: thick gunsmoke while firing, lighter
        // steam while the barrel spins down and cools. Emitted at the muzzle in world space.
        if (g_spin>0.02f){
            Vector3 upv=Vector3CrossProduct(right,fwd);                          // camera up
            Vector3 muzzle=Vector3Add(g_pos, Vector3Add(Vector3Scale(fwd,1.1f), Vector3Scale(upv,-0.08f))); // dead-centre of the barrel, a hair low
            if (g_mgFiring){ for(int q=0;q<4;q++) SpawnSmoke(muzzle,1); }        // firing: a dense fine stream
            else if (GetRandomValue(0,1)==0) SpawnSmoke(muzzle,0);              // spin-down/cooling: light steam
        }
    } else {
        g_shake=fmaxf(0.0f, g_shake-dt*4.0f);              // any other weapon -> shake fades out
    }

    // LMG: fires while its (one-shot) sound plays, then a fixed pause before the
    // next burst -- "when the sound ends, stop for 3/4 s, then it can shoot again".
    if (cw->soundGated){
        int playing = g_audio && cw->hasSnd && IsSoundPlaying(cw->fireSnd);
        if (g_lmgPause>0.0f) g_lmgPause-=dt;               // forced pause -> no fire
        else if (lmb && !g_reloading){
            if (playing) Fire(g_cam);                      // shooting during the burst
            else if (g_audio && cw->hasSnd) PlaySound(cw->fireSnd);   // start a burst (plays the whole sound once)
        }
        if (g_lmgWasPlaying && !playing && g_lmgPause<=0.0f) g_lmgPause=0.75f;   // sound ended -> 3/4 s pause
        g_lmgWasPlaying=playing;
    }

    // looped fire sound (rifle burst / minigun) follows the actual firing
    if (g_audio && cw->hasSnd && cw->sndLoop && !cw->soundGated){
        if (firingNow){ if (!IsSoundPlaying(cw->fireSnd)) PlaySound(cw->fireSnd); }
        else if (IsSoundPlaying(cw->fireSnd)) StopSound(cw->fireSnd);
    }

    UpdateEnemies(dt);
    UpdateSmoke(dt);

    // advance weapon animation. idle = HOLD the last frame of Take (static ready
    // pose, no wandering). One-shot clips (Shoot/Reload) play through, then snap
    // back to the held idle.
    if (g_gunAnimN>0 && g_curAnim<g_gunAnimN){
        int nf=ANIM_FRAMES(g_gunAnim[g_curAnim]);
        if (g_weapons[g_curWeapon].spinUp && g_curAnim==g_aIdle && !g_animOnce){
            // minigun: the barrel mesh is rigid (not skinned) and 'allanims' only
            // animates the arms -- so scrubbing it just flails the hands. Instead
            // HOLD the arms at a static grip and spin the barrel geometrically
            // (DrawViewmodel rotates the barrel mesh by g_mgBarrelAngle).
            g_animT = (nf>0) ? (float)MG_IDLE_FRAME : 0.0f;
            g_mgBarrelAngle += dt*g_spin*42.0f;            // ~6.7 rev/s at full spin-up
            if (g_mgBarrelAngle>2.0f*PI) g_mgBarrelAngle-=2.0f*PI;
        } else if (g_curAnim==g_aIdle && !g_animOnce){
            g_animT = nf>0 ? (float)(nf-1) : 0.0f;   // freeze on the drawn/ready frame
        } else {
            g_animT += dt*30.0f;
            // nf<=0 (empty/malformed clip) counts as instantly finished, so a
            // zero-frame reload phase can't wedge g_reloading=1 forever.
            if (nf<=0 || g_animT>=nf){
                if (g_animOnce){
                    if (g_reloadStep>=0 && g_reloadStep+1<g_reloadSeqN){
                        g_reloadStep++;                  // next reload phase
                        g_curAnim=g_reloadSeq[g_reloadStep]; g_animT=0.0f;
                    } else {                             // sequence (or one-shot) done
                        g_reloadStep=-1; g_reloading=0; g_curAnim=g_aIdle; g_animOnce=0; g_animT=0.0f;
                    }
                } else g_animT = nf>0 ? g_animT-nf : 0.0f;
            }
        }
    }

    // overlay + inspect + reset
    if (IsKeyPressed(KEY_GRAVE) || IsKeyPressed(KEY_TAB)) g_devOverlay=!g_devOverlay;
    if (IsKeyPressed(KEY_V)) g_inspect=!g_inspect;
    if (IsKeyPressed(KEY_G)){ g_godMode=!g_godMode; if (g_godMode) g_playerHp=100.0f;   // refill on re-enable
        DebugLog("mode","\"godMode\":%s", g_godMode?"true":"false"); }
    if (IsKeyPressed(KEY_F) && g_hasMap){ g_noclip=!g_noclip; g_vy=0.0f;   // map: fly/noclip <-> walk
        DebugLog("mode","\"noclip\":%s", g_noclip?"true":"false"); }
    if (IsKeyPressed(KEY_N)){                          // N: toggle orient mode (no enemies, weapon-aim tuning)
        g_noEnemies=!g_noEnemies;
        for (int i=0;i<MAX_ENEMIES;i++) g_enemies[i].state=0;   // clear the field
        for (int c=0;c<MAX_CORPSES;c++) g_corpses[c].active=0;  // and clear corpses
        g_playerHp=100.0f;                             // full health (clears a leftover YOU DIED)
        if (!g_noEnemies) for (int i=0;i<5;i++) SpawnEnemy(i);  // bring the wave back
        DebugLog("mode","\"noEnemies\":%s", g_noEnemies?"true":"false");
    }
    if (IsKeyPressed(KEY_ONE)) SwitchWeapon(0);
    if (IsKeyPressed(KEY_TWO)) SwitchWeapon(1);
    if (IsKeyPressed(KEY_THREE)) SwitchWeapon(2);
    if (IsKeyPressed(KEY_FOUR)) SwitchWeapon(3);
    if (IsKeyPressed(KEY_FIVE)) SwitchWeapon(4);
    if (IsKeyPressed(KEY_SIX)) SwitchWeapon(5);
    if (IsKeyPressed(KEY_SEVEN)) SwitchWeapon(6);
    if (IsKeyPressed(KEY_EIGHT)) SwitchWeapon(7);
    if (IsKeyPressed(KEY_NINE)) SwitchWeapon(8);
    if (IsKeyPressed(KEY_ZERO)){ Weapon *w=&g_weapons[g_curWeapon]; g_vmOff=w->off0; g_vmScale=w->scale0; g_vmYaw=w->yaw0; g_vmPitch=w->pitch0; g_vmRoll=w->roll0; }
    // Save: ENTER (or F5). On Mac F5 is a system key (dictation/keyboard light)
    // and gets eaten by the OS, so ENTER is the reliable bind. g_savedMsg flashes
    // an on-screen confirmation so you KNOW it wrote.
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_F5)){
        StashActiveTuning();
        SaveTune(g_weapons[g_curWeapon].tunePath,g_vmOff,g_vmScale,g_vmYaw,g_vmPitch,g_vmRoll);
        g_savedMsg=2.0f;   // seconds to show "SAVED"
        DebugLog("vmsave","\"slot\":%d,\"label\":\"%s\",\"path\":\"%s\",\"saved\":true",
                 g_curWeapon, g_weapons[g_curWeapon].label, g_weapons[g_curWeapon].tunePath);
    }
    if (g_savedMsg>0) g_savedMsg-=GetFrameTime();
    // H ("home"): remember the current spot+facing as the player spawn (written to
    // spawn.txt, loaded at startup). Press it where you want to start next launch.
    // (F2 also works, but macOS eats the function keys, so H is the real bind.)
    if (IsKeyPressed(KEY_H) || IsKeyPressed(KEY_F2)){
        SaveSpawn(g_pos,g_yaw,g_pitch); g_spawnMsg=2.0f;
        DebugLog("spawnsave","\"pos\":[%.2f,%.2f,%.2f],\"yaw\":%.3f,\"pitch\":%.3f", g_pos.x,g_pos.y,g_pos.z,g_yaw,g_pitch);
    }
    if (g_spawnMsg>0) g_spawnMsg-=GetFrameTime();

    // live viewmodel tuning (faster in orient mode so a mispositioned weapon is
    // easy to sweep back into frame)
    float ns=dt*(g_noEnemies?3.0f:0.5f);
    float rs=dt*(g_noEnemies?120.0f:60.0f);
    float ss=g_noEnemies?3.0f:1.0f;
    if (IsKeyDown(KEY_I)) g_vmOff.y+=ns;  if (IsKeyDown(KEY_K)) g_vmOff.y-=ns;
    if (IsKeyDown(KEY_L)) g_vmOff.x+=ns;  if (IsKeyDown(KEY_J)) g_vmOff.x-=ns;
    if (IsKeyDown(KEY_O)) g_vmOff.z+=ns;  if (IsKeyDown(KEY_U)) g_vmOff.z-=ns;
    if (IsKeyDown(KEY_EQUAL)) g_vmScale*=(1.0f+dt*ss);  if (IsKeyDown(KEY_MINUS)) g_vmScale*=(1.0f-dt*ss);
    if (IsKeyDown(KEY_RIGHT_BRACKET)) g_vmYaw+=rs;  if (IsKeyDown(KEY_LEFT_BRACKET)) g_vmYaw-=rs;
    if (IsKeyDown(KEY_APOSTROPHE)) g_vmPitch+=rs;   if (IsKeyDown(KEY_SEMICOLON)) g_vmPitch-=rs;
    if (IsKeyDown(KEY_PERIOD)) g_vmRoll+=rs;        if (IsKeyDown(KEY_COMMA)) g_vmRoll-=rs;
    if (IsKeyPressed(KEY_P))
        DebugLog("vmxform","\"off\":[%.3f,%.3f,%.3f],\"scale\":%.4f,\"yaw\":%.1f,\"pitch\":%.1f,\"roll\":%.1f",
                 g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch,g_vmRoll);
    if (g_noEnemies){   // orient mode: log the selected weapon + orientation whenever it changes
        static float lo[7]={1e9f,0,0,0,0,0,0};
        float cur[7]={g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch,g_vmRoll};
        int changed=0; for(int k=0;k<7;k++) if(fabsf(cur[k]-lo[k])>1e-4f) changed=1;
        if (changed){ for(int k=0;k<7;k++) lo[k]=cur[k];
            DebugLog("orient","\"weapon\":\"%s\",\"off\":[%.3f,%.3f,%.3f],\"scale\":%.5f,\"yaw\":%.1f,\"pitch\":%.1f,\"roll\":%.1f",
                     g_weapons[g_curWeapon].label,g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch,g_vmRoll);
        }
    }

    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life>0) g_tracers[i].life-=dt;
    for (int i=0;i<MAX_SPARKS;i++) if (g_sparks[i].life>0){
        Spark *sp=&g_sparks[i];
        int resting=(sp->vel.x==0 && sp->vel.y==0 && sp->vel.z==0 && sp->pos.y<=sp->rest+0.03f);
        if (resting){
            if (!sp->lasting) sp->life-=dt;        // glints + fine spray fade; lasting blood puddles stay put
        } else {
            sp->life-=dt;
            sp->vel.y-=12.0f*dt;
            sp->pos=Vector3Add(sp->pos,Vector3Scale(sp->vel,dt));
            if (sp->pos.y<=sp->rest){              // hit the floor -> settle
                float hl=sqrtf(sp->vel.x*sp->vel.x+sp->vel.z*sp->vel.z);  // keep the horizontal heading for a directional splat
                if (hl>0.001f){ sp->ix=sp->vel.x/hl; sp->iz=sp->vel.z/hl; }
                sp->pos.y=sp->rest+0.015f; sp->vel=(Vector3){0,0,0};
                if (sp->lasting){ sp->life=1e9f; sp->life0=1e9f; }   // lasting blood puddle (full opacity)
                else if (sp->life<0.5f) sp->life=0.5f;             // brief impact glow
            }
        }
    }
}

static void DrawWorld(void) {
    if (g_hasMap){                          // SPIKE: draw the loaded .map (no collision yet)
        rlDisableBackfaceCulling();         // brush winding flips under the Z->Y axis swap
        DrawModel(g_map,(Vector3){0,0,0},1.0f,WHITE);
        rlEnableBackfaceCulling();
    } else {
        DrawModelEx(g_floor,(Vector3){0,0,0},(Vector3){0,1,0},0,(Vector3){ARENA*2,1,ARENA*2},WHITE);
        DrawModelEx(g_wall,(Vector3){0,WALL_H/2, ARENA},(Vector3){0,1,0}, 0,(Vector3){ARENA*2,WALL_H,1},WHITE);
        DrawModelEx(g_wall,(Vector3){0,WALL_H/2,-ARENA},(Vector3){0,1,0}, 0,(Vector3){ARENA*2,WALL_H,1},WHITE);
        DrawModelEx(g_wall,(Vector3){ ARENA,WALL_H/2,0},(Vector3){0,1,0},90,(Vector3){ARENA*2,WALL_H,1},WHITE);
        DrawModelEx(g_wall,(Vector3){-ARENA,WALL_H/2,0},(Vector3){0,1,0},90,(Vector3){ARENA*2,WALL_H,1},WHITE);
        for (int c=0;c<NUM_CRATES;c++)
            DrawModelEx(g_crate,g_crates[c].pos,(Vector3){0,1,0},0,g_crates[c].size,g_crates[c].col);
    }
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life>0)
        DrawLine3D(g_tracers[i].a,g_tracers[i].b,(Color){255,240,150,255});
    for (int i=0;i<MAX_SPARKS;i++) if (g_sparks[i].life>0){
        Spark *sp=&g_sparks[i];
        int settled=(sp->vel.x==0 && sp->vel.y==0 && sp->vel.z==0);
        if (sp->pool){                                     // flat continuous pool stain: many small overlapping flat discs
            float R=sp->size, y0=sp->rest+0.006f, y1=sp->rest+0.012f;  // filling a disc -> reads as one wet liquid pool
            unsigned int s=sp->seed?sp->seed:1u;
            for (int b=0;b<26;b++){
                unsigned int h=s*2654435761u + (unsigned int)b*2654435789u; h^=h>>13; h*=0x9E3779B1u;
                float t1=(float)((h>>3)&1023)/1023.0f, t2=(float)((h>>13)&1023)/1023.0f, t3=(float)((h>>23)&255)/255.0f;
                float a=t1*6.2831853f, rr=sqrtf(t2)*R;      // sqrt -> evenly fill the disc area
                float ox=cosf(a)*rr, oz=sinf(a)*rr;
                float dr=R*(0.34f+0.16f*t3)*(1.0f-0.45f*(rr/R));   // solid centre, smaller discs toward the ragged edge
                DrawCylinderEx((Vector3){sp->pos.x+ox,y0,sp->pos.z+oz},
                               (Vector3){sp->pos.x+ox,y1,sp->pos.z+oz}, dr, dr, 10, sp->col);
            }
        } else if (sp->lasting && settled){                // settled gob -> a small lumpy spatter dot (a couple offset discs)
            float ax=sp->ix, az=sp->iz; if (ax==0.0f && az==0.0f){ ax=1.0f; az=0.0f; }
            float y0=sp->rest+0.011f, y1=sp->rest+0.018f;   // a thin slab lying on the floor, not a ball
            unsigned int s=sp->seed?sp->seed:1u;
            for (int b=0;b<2;b++){                          // offset discs break the circular outline
                unsigned int h=s*2654435761u + (unsigned int)b*2246822519u;
                float t1=(float)((h>>3)&255)/255.0f, t2=(float)((h>>11)&255)/255.0f, t3=(float)((h>>19)&255)/255.0f;
                float along=(t1*1.4f-0.2f)*sp->size;        // mostly forward of impact (the spatter trails ahead)
                float side =(t2-0.5f)*1.2f*sp->size;        // lateral scatter
                float ox=ax*along - az*side, oz=az*along + ax*side;
                float r=sp->size*(0.30f+0.30f*t3);          // small, varied
                DrawCylinderEx((Vector3){sp->pos.x+ox,y0,sp->pos.z+oz},
                               (Vector3){sp->pos.x+ox,y1,sp->pos.z+oz}, r, r, 8, sp->col);
            }
        } else if (sp->blood){                             // in-flight blood -> thin streak stretched along travel
            float k=(sp->life0>0)?sp->life/sp->life0:1.0f;
            Color c=sp->col; c.a=(unsigned char)(k*255.0f);
            float spd=Vector3Length(sp->vel);
            float r=(sp->size>0.03f?0.03f:sp->size)*0.55f; // capped so even gobs stay droplet-sized, never grapefruits
            if (spd>0.5f){                                 // moving: a tapered teardrop pointing the way it flies
                Vector3 dirn=Vector3Scale(sp->vel,1.0f/spd);
                float len=sp->size*1.5f+spd*0.025f;
                Vector3 tail=Vector3Subtract(sp->pos,Vector3Scale(dirn,len));
                DrawCylinderEx(sp->pos, tail, r, r*0.25f, 6, c);
            } else {                                       // nearly stopped: a small droplet
                DrawSphereEx(sp->pos, r, 5, 5, c);
            }
        } else {                                           // impact glints: tiny bright spark
            float k=(sp->life0>0)?sp->life/sp->life0:1.0f;
            float sz=sp->size*(0.45f+0.55f*k);
            Color c=sp->col; c.a=(unsigned char)(k*255.0f);
            DrawSphereEx(sp->pos, sz*0.6f, 6, 6, c);
        }
    }
}

// Draw all alive/dying enemies. The shared model is posed to each enemy's own
// animation cursor right before drawing, so they animate independently.
static void DrawEnemies(void){
    if (!g_hasEnemy) return;
    for (int i=0;i<MAX_ENEMIES;i++){
        Enemy *e=&g_enemies[i];
        if (e->state==0) continue;
        // Occlusion cull: if the map blocks the camera's view of the enemy's feet, chest
        // AND head, it's fully behind a wall/stairs -> don't draw it, so no part of the
        // model pokes through the geometry to the player's side. Any point visible -> draw
        // (so an enemy peeking around cover still shows).
        if (g_hasMap && g_mapCol.ready){
            const float hy[3]={0.15f,0.9f,1.7f}; int vis=0;
            for (int p=0;p<3;p++){
                Vector3 pt={e->pos.x, e->drawY+hy[p], e->pos.z};
                Vector3 to=Vector3Subtract(pt,g_cam.position); float dist=Vector3Length(to);
                if (dist<0.5f){ vis=1; break; }                      // basically on top of us -> visible
                float hit=MapRayNearest(g_cam.position, Vector3Scale(to,1.0f/dist), dist);
                if (hit<=0.0f || hit>=dist-0.15f){ vis=1; break; }   // nothing blocks this point
            }
            if (!vis) continue;
        }
        float dx=g_pos.x-e->pos.x, dz=g_pos.z-e->pos.z;       // face the player (yaw about +Y)
        float yaw=atan2f(dx,dz)*RAD2DEG + ENEMY_YAW_OFFSET;
        if (g_enemyAnimN>0) ANIM_APPLY(g_enemy, g_enemyAnim[e->state==2?g_eDeath:e->clip], e->animT);
        Vector3 dpos={e->pos.x, e->drawY, e->pos.z};          // smoothed height -> no stair-step jitter
        DrawModelEx(g_enemy, dpos, (Vector3){0,1,0}, yaw,     // no sink: dying body falls onto the floor, not through
                    (Vector3){ENEMY_SCALE,ENEMY_SCALE,ENEMY_SCALE}, WHITE);
        if (e->state==1 && e->hp<ENEMY_HP){                   // HP bar above damaged enemies
            Vector3 hp={e->pos.x, e->pos.y+ENEMY_HEIGHT+0.3f, e->pos.z};
            DrawCube(hp, 0.6f*(e->hp/ENEMY_HP), 0.08f, 0.02f, (Color){230,60,60,255});
        }
    }
    // lasting corpses: the shared model posed at the death clip's settle frame, lying where each enemy fell
    if (g_enemyAnimN>0){
        ANIM_APPLY(g_enemy, g_enemyAnim[g_eDeath], (float)g_eDeathFrame);
        for (int c=0;c<MAX_CORPSES;c++) if (g_corpses[c].active)
            DrawModelEx(g_enemy, g_corpses[c].pos, (Vector3){0,1,0}, g_corpses[c].yaw,
                        (Vector3){ENEMY_SCALE,ENEMY_SCALE,ENEMY_SCALE}, WHITE);
    }
}

static void PoseGun(void){
    if (g_gunAnimN>0 && g_curAnim<g_gunAnimN) ANIM_APPLY(g_gun, g_gunAnim[g_curAnim], g_animT);
}

// INSPECT (V): gun floats ~2.5m ahead, auto-fit, spinning, in the world camera.
// Can't-fail view - if the model loaded, you WILL see it here.
static void DrawInspect(void){
    if (!g_hasGun) return;
    PoseGun();
    // Use the centroid + scale measured ONCE at load (NOT per-frame bbox - that
    // shifts as the animation moves the geometry, making the gun wander).
    float s=g_gunFitScale;
    Vector3 dir=Vector3Normalize(Vector3Subtract(g_cam.target,g_cam.position));
    Vector3 at=Vector3Add(g_cam.position,Vector3Scale(dir,2.5f));
    Matrix rot=MatrixRotateY(DEG2RAD*(float)GetTime()*40.0f);
    Vector3 pos=Vector3Subtract(at,Vector3Scale(Vector3Transform(g_gunCentroid,rot),s));
    BeginMode3D(g_cam);
        g_gun.transform=rot;
        DrawModelEx(g_gun,pos,(Vector3){0,1,0},0,(Vector3){s,s,s},WHITE);
    EndMode3D();
    g_gun.transform=MatrixIdentity();
}

// Draw a weapon model but spin ONE rigid mesh about its local Z axis. The minigun
// barrel cluster isn't rigged, so we rotate its geometry directly. Mirrors how
// DrawModelEx composes its transform so every other mesh draws byte-identically.
static void DrawGunSpinMesh(Model model, Vector3 pos, float scale, int spinMesh, float angle){
    Matrix mt = MatrixMultiply(model.transform, MatrixMultiply(MatrixScale(scale,scale,scale), MatrixTranslate(pos.x,pos.y,pos.z)));
    Matrix spun = MatrixMultiply(MatrixRotateZ(angle), mt);   // spin in local space (barrels run along local Z, centred on X=Y=0)
    for (int i=0;i<model.meshCount;i++)
        DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], (i==spinMesh)?spun:mt);
}

static void DrawViewmodel(void){
    // In orient mode the live first-person viewmodel must always show so tuning
    // is visible; inspect (which ignores the transform) is suppressed there.
    if (!g_hasGun || (g_inspect && !g_noEnemies)) return;
    PoseGun();
    float bobX=sinf(g_bob)*0.012f, bobY=fabsf(cosf(g_bob))*0.010f;
    // recoil: muzzle kicks UP (pitch) and the gun shoves slightly up+back, then
    // settles. r is the live kick amount (0..1) driven by Fire().
    float r=g_recoil;
    Vector3 target={ g_vmOff.x+bobX, g_vmOff.y+bobY+r*0.012f, g_vmOff.z+r*0.03f };
    Matrix rot=MatrixMultiply(MatrixRotateZ(DEG2RAD*g_vmRoll), MatrixMultiply(MatrixRotateX(DEG2RAD*(g_vmPitch - r*4.0f)), MatrixRotateY(DEG2RAD*g_vmYaw)));
    // recenter: place the model's centroid AT target (model is far off-origin)
    Vector3 vpos=Vector3Subtract(target, Vector3Scale(Vector3Transform(g_gunCentroid,rot), g_vmScale));
    rlDrawRenderBatchActive();
#if !defined(_WIN32)
    glClear(GL_DEPTH_BUFFER_BIT);   // weapon always on top
#endif
    BeginMode3D(g_vmCam);
        g_gun.transform=rot;
        if (g_weapons[g_curWeapon].spinUp)               // minigun: spin the barrel mesh, hands held static
            DrawGunSpinMesh(g_gun, vpos, g_vmScale, MG_BARREL_MESH, g_mgBarrelAngle);
        else
            DrawModelEx(g_gun,vpos,(Vector3){0,1,0},0,(Vector3){g_vmScale,g_vmScale,g_vmScale},WHITE);
    EndMode3D();
    g_gun.transform=MatrixIdentity();
}

// Drop-shadowed text: a near-black copy offset down-right, then the bright text
// on top. The raylib default font is a small bitmap that turns muddy against the
// 3D scene; the shadow gives every glyph a hard edge so it stays readable.
static void TextSh(const char *t, int x, int y, int sz, Color c){
    DrawText(t, x+2, y+2, sz, (Color){0,0,0,200});
    DrawText(t, x,   y,   sz, c);
}

static void DrawHUD(void) {
    int W=GetScreenWidth(), H=GetScreenHeight();
    Color cc={0,255,120,220};
    DrawLine(W/2-10,H/2,W/2-3,H/2,cc); DrawLine(W/2+3,H/2,W/2+10,H/2,cc);
    DrawLine(W/2,H/2-10,W/2,H/2-3,cc); DrawLine(W/2,H/2+3,W/2,H/2+10,cc);
    DrawFPS(W-90,10);
    // kills + headshots + player health
    DrawText(TextFormat("KILLS %d", g_kills), W-160, 36, 22, (Color){255,230,120,255});
    DrawText(TextFormat("HEADSHOTS %d", g_headshots), W-160, 60, 18, (Color){255,140,50,255});
    if (g_spawnMsg>0){                                 // F2 confirmation
        const char *t="SPAWN SET"; int fs=26, tw=MeasureText(t,fs);
        DrawText(t, W/2-tw/2, H/2+70, fs, (Color){120,220,255,255});
    }
    // "HEADSHOT!" punch near the crosshair: full for ~0.65s, then fades over 0.45s
    if (g_hsFlash>0){
        const char *t="HEADSHOT!"; int fs=36, tw=MeasureText(t,fs), x=W/2-tw/2, y=H/2-74;
        float k=fminf(1.0f, g_hsFlash/0.45f);
        DrawText(t, x+2, y+2, fs, (Color){0,0,0,(unsigned char)(180*k)});
        DrawText(t, x,   y,   fs, (Color){255,70,40,(unsigned char)(255*k)});
    }
    DrawRectangle(20, H-44, 224, 24, (Color){0,0,0,150});
    DrawRectangle(22, H-42, (int)(220*g_playerHp/100.0f), 20, (Color){200,40,40,255});
    DrawText(TextFormat("HP %d", (int)g_playerHp), 28, H-40, 16, RAYWHITE);
    if (g_godMode) DrawText("GOD", 112, H-40, 16, (Color){120,220,255,255});   // invulnerable (dev)
    if (g_playerHp<=0 && !g_noEnemies && !g_godMode) DrawText("YOU DIED - press ESC", W/2-120, H/2+30, 24, (Color){255,80,80,255});
    // Big unmistakable mode banner so "floating" can be diagnosed: INSPECT mode
    // intentionally floats the gun in front of you; press V to get back to FP.
    if (g_inspect)
        DrawText("INSPECT MODE - press V for first-person", W/2-220, 30, 24, (Color){255,180,40,255});
    if (g_devOverlay){
        DrawRectangle(0,0,380,90,(Color){0,0,0,150});
        DrawText("CHERNOBYL 2  -  M16A3 (LMB fire, R reload)",6,6,12,GRAY);
        DrawText(TextFormat("%s  [%s]  1-9=weapon N=mode V=inspect G=god 0=reset", g_inspect?"INSPECT":"FP", g_weapons[g_curWeapon].label),8,22,16,LIME);
        if (g_noEnemies){   // orient mode panel - bigger + drop-shadowed for legibility
            DrawRectangle(6,96,600,392,(Color){0,0,0,215});
            DrawRectangleLines(6,96,600,392,(Color){255,210,60,255});
            TextSh("ORIENT MODE  (N = back to play)",18,104,28,YELLOW);
            TextSh(TextFormat("weapon:  %s",g_weapons[g_curWeapon].label),18,142,24,(Color){120,230,255,255});
            TextSh(TextFormat("off [%.2f %.2f %.2f]  scale %.5f",g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale),18,176,20,RAYWHITE);
            TextSh(TextFormat("yaw %.1f  pitch %.1f  roll %.1f",g_vmYaw,g_vmPitch,g_vmRoll),18,202,20,RAYWHITE);
            Color kc=(Color){235,235,140,255};
            TextSh("J / L      move left / right",18,236,20,kc);
            TextSh("K / I      move down / up",18,262,20,kc);
            TextSh("U / O      move back / forward",18,288,20,kc);
            TextSh("- / =      scale  smaller / bigger",18,314,20,kc);
            TextSh("[ / ]      yaw    left / right",18,340,20,kc);
            TextSh("; / '      pitch  down / up",18,366,20,kc);
            TextSh(", / .      roll   twist left / right",18,392,20,kc);
            TextSh("0 reset      ENTER save  (or F5)",18,422,20,(Color){170,255,170,255});
            if (g_savedMsg>0) TextSh(TextFormat("SAVED  ->  %s",g_weapons[g_curWeapon].tunePath),18,448,20,(Color){90,255,90,255});
        }
        DrawText(TextFormat("vm off %.2f %.2f %.2f  scale %.5f  yaw %.0f pit %.0f",
                 g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch),8,42,13,RAYWHITE);
        DrawText("IJKL/UO move  -/= scale  [/] yaw  ;/' pitch  P log",8,60,12,GRAY);
    }
}

// Sunset-gradient sky behind the world. The .map's sky surfaces (skies/*) are
// skipped by the loader, so those openings reveal this. A vertical gradient
// needs no yaw response; we just slide the horizon with pitch (look up -> more
// sky). Stands in for the named distant_sunset cubemap (its images weren't
// fetchable); swap in a real cubemap later if wanted.
static void DrawSky(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    float hy=H*0.5f + g_pitch*(float)H*0.55f; if (hy<0) hy=0; if (hy>H) hy=H;
    Color zenith={48,58,104,255}, horizon={232,138,70,255}, ground={42,34,40,255};
    if (hy>0) DrawRectangleGradientV(0,0,W,(int)hy, zenith, horizon);            // sky: blue -> sunset
    if (hy<H) DrawRectangleGradientV(0,(int)hy,W,H-(int)hy, horizon, ground);    // horizon haze -> dark
}

// Options/pause overlay drawn on top of the frozen world. Hover-highlighted
// buttons; clicks are handled in Update against the same MenuRect layout.
static void DrawMenu(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    DrawRectangle(0,0,W,H,(Color){0,0,0,165});
    const char *title="OPTIONS"; int ts=46, tw=MeasureText(title,ts);
    TextSh(title, W/2-tw/2, (int)MenuRect(0).y-78, ts, (Color){255,210,60,255});
    Vector2 mp=GetMousePosition();
    char fsl[40]; snprintf(fsl,sizeof fsl,"Fullscreen:  %s", g_fullscreen?"ON":"OFF");
    const char *labels[MENU_N]={fsl,"Resume","Quit"};
    for (int i=0;i<MENU_N;i++){
        Rectangle r=MenuRect(i); int hov=CheckCollisionPointRec(mp,r);
        DrawRectangleRec(r, hov?(Color){58,82,120,240}:(Color){22,30,44,225});
        DrawRectangleLinesEx(r,2, hov?(Color){255,210,60,255}:(Color){120,140,170,255});
        int fs=28, lw=MeasureText(labels[i],fs);
        TextSh(labels[i], (int)(r.x+r.width/2-lw/2), (int)(r.y+r.height/2-fs/2), fs, RAYWHITE);
    }
    Rectangle last=MenuRect(MENU_N-1);
    TextSh("ESC to close", W/2-72, (int)(last.y+last.height+24), 18, GRAY);
}

// Minimal "PAUSED" overlay (P-pause). Unlike the options menu this keeps the
// cursor captured, so it's just a dimmer + centred label over the frozen world.
static void DrawPaused(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    DrawRectangle(0,0,W,H,(Color){0,0,0,120});
    const char *t="PAUSED"; int ts=64, tw=MeasureText(t,ts);
    TextSh(t, W/2-tw/2, H/2-ts/2, ts, (Color){255,210,60,255});
    const char *h="press P to resume"; int hs=22, hw=MeasureText(h,hs);
    TextSh(h, W/2-hw/2, H/2+ts/2+12, hs, RAYWHITE);
}

// Edge vignette: black bands fading inward on all four sides (corners darken twice ->
// a tunnel-vision look). k=0..1 drives the darkness; used for the minigun brain-shake.
static void DrawVignette(float k){
    if (k<=0.001f) return;
    int W=GetScreenWidth(), H=GetScreenHeight();
    unsigned char a=(unsigned char)(fminf(1.0f,k)*105.0f);   // peripheral darkness (half the original 210)
    Color edge={0,0,0,a}, clear={0,0,0,0};
    int bw=W/6, bh=H/6;                            // narrow edge bands -> only the periphery darkens, centre stays clear
    DrawRectangleGradientH(0,0,bw,H, edge, clear);          // left
    DrawRectangleGradientH(W-bw,0,bw,H, clear, edge);       // right
    DrawRectangleGradientV(0,0,W,bh, edge, clear);          // top
    DrawRectangleGradientV(0,H-bh,W,bh, clear, edge);       // bottom
}

static void Frame(void) {
    static int frameNo=0;
    Update();
    BeginDrawing();
        ClearBackground((Color){70,90,110,255});
        if (g_hasMap) DrawSky();                  // gradient sky shows through the map's sky openings
        BeginMode3D(g_cam);
            DrawWorld();
            DrawEnemies();
            DrawSmoke(g_cam);                     // minigun muzzle smoke/steam (billboards, drawn last)
        EndMode3D();
        if (g_inspect && !g_noEnemies) DrawInspect();
        DrawViewmodel();
        DrawVignette(g_shake);                    // minigun: blurred/darkened edges while it rips
        DrawHUD();
        if (g_menu) DrawMenu();
        else if (g_paused) DrawPaused();
    EndDrawing();
    if (g_debug && (frameNo%30==0))
        DebugLog("frame","\"n\":%d,\"fps\":%d,\"anim\":%d,\"pos\":[%.1f,%.1f,%.1f]",
                 frameNo,GetFPS(),g_curAnim,g_pos.x,g_pos.y,g_pos.z);
    if (g_shotFrame>0 && frameNo==g_shotFrame){
        TakeScreenshot("chernobyl2-shot.png");
        DebugLog("shot","\"file\":\"chernobyl2-shot.png\",\"frame\":%d", frameNo);
    }
    frameNo++;
}

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

int main(int argc, char **argv) {
    int vmWeapon=-1; float vmOv[7]={0}; int vmHas=0;          // dev: --weapon / --vm framing overrides
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--debug")) g_debug=1;
        else if (!strcmp(argv[i],"--frames") && i+1<argc) g_maxFrames=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--shot") && i+1<argc) g_shotFrame=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--no-enemies")) g_noEnemies=1;
        else if (!strcmp(argv[i],"--map") && i+1<argc) g_mapPath=argv[++i];   // SPIKE: load a .map
        else if (!strcmp(argv[i],"--weapon") && i+1<argc) vmWeapon=atoi(argv[++i]);   // dev: start on weapon N
        else if (!strcmp(argv[i],"--vm") && i+1<argc) vmHas=(sscanf(argv[++i],"%f %f %f %f %f %f %f",&vmOv[0],&vmOv[1],&vmOv[2],&vmOv[3],&vmOv[4],&vmOv[5],&vmOv[6])==7);   // dev: override viewmodel "x y z scale yaw pitch roll"
    }
    // Send the JSON event stream straight to a log file (not stdout/stderr, so
    // it stays clean of raylib's own warnings). Fresh file each run.
    if (g_debug){
        g_dbg=fopen("chernobyl2-debug.log","w");
        if (!g_dbg) g_dbg=stderr;   // fall back if the file can't be opened
    }

    SetTraceLogLevel(LOG_WARNING);
    SetTraceLogCallback(FilteredTraceLog);   // drop raylib's harmless u32->u16 index spam
    InitWindow(1280,720,"Chernobyl 2");
    SetExitKey(KEY_NULL);              // ESC opens the options menu instead of quitting
    int ready=IsWindowReady();
    if (ready){                        // soft radial puff for the minigun smoke billboards
        Image si=GenImageGradientRadial(64,64,0.25f,(Color){255,255,255,255},(Color){255,255,255,0});
        g_smokeTex=LoadTextureFromImage(si); UnloadImage(si);
        SetTextureFilter(g_smokeTex,TEXTURE_FILTER_BILINEAR); g_hasSmokeTex=(g_smokeTex.id!=0);
    }
    DebugLog("boot","\"windowReady\":%s,\"raylib\":\"%s\"", ready?"true":"false", RAYLIB_VERSION);

    const char *gunPath="assets/rifle.glb";
    if (!FileExists(gunPath)) gunPath="../assets/rifle.glb";
    DebugLog("model","\"path\":\"%s\"", JStr(gunPath));

    if (!ready){   // headless: nothing to render without a GL context
        DebugLog("shutdown","\"reason\":\"no-gl-context\"");
        return 0;
    }

    // Windowed (1280x720) by default. Fullscreen is opt-in via the in-game options
    // menu (ESC), which uses borderless windowed mode (resizes to the monitor's
    // resolution) and persists the choice; re-apply it here if it was left on.
    // Skipped under --shot so headless captures stay a deterministic 1280x720.
    // Everything renders off GetScreenWidth/Height + the framebuffer aspect, so
    // the HUD and 3D views adapt to whatever resolution we land on.
    LoadOptions();
    if (g_fullscreen && g_shotFrame<=0){
        ToggleBorderlessWindowed();
        DebugLog("window","\"borderless\":true,\"w\":%d,\"h\":%d,\"monitor\":%d",
                 GetScreenWidth(), GetScreenHeight(), GetCurrentMonitor());
    }

    SetTargetFPS(120);   // 120 Hz target (ProMotion etc.); all motion is dt-scaled so physics is unchanged
    DisableCursor();

    // window + Dock/Cmd-Tab icon
    {
        const char *ip="assets/icon.png"; if (!FileExists(ip)) ip="../assets/icon.png";
        if (FileExists(ip)){
            Image ic=LoadImage(ip);
            if (ic.data){ ImageFormat(&ic,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8); SetWindowIcon(ic); UnloadImage(ic); }
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
            MacSetDockIcon(ip);            // SetWindowIcon is ignored on macOS; set the Dock image directly
#endif
        }
    }

    InitAudioDevice();                 // weapon fire sounds; harmless if it fails
    g_audio = IsAudioDeviceReady();
    DebugLog("audio","\"ready\":%s", g_audio?"true":"false");

    if (g_audio){                      // a random one of these plays on every enemy kill
        const char *deaths[]={ "enemy_death1.mp3","enemy_death2.mp3","enemy_death3.mp3","enemy_death4.mp3" };
        for (int d=0; d<(int)(sizeof deaths/sizeof deaths[0]); d++){
            char p1[64],p2[64]; snprintf(p1,sizeof p1,"assets/%s",deaths[d]); snprintf(p2,sizeof p2,"../assets/%s",deaths[d]);
            const char *sp=FileExists(p1)?p1:(FileExists(p2)?p2:NULL);
            if (sp){ Sound s=LoadSound(sp); if (s.frameCount>0) g_deathSnd[g_nDeathSnd++]=s; }
        }
        const char *ap="assets/enemy_attack.mp3";
        if (!FileExists(ap)) ap="../assets/enemy_attack.mp3";
        if (FileExists(ap)){ g_attackSnd=LoadSound(ap); g_hasAttackSnd=(g_attackSnd.frameCount>0); }
        DebugLog("enemysnd","\"deaths\":%d,\"attack\":%s", g_nDeathSnd, g_hasAttackSnd?"true":"false");
    }

    if (g_mapPath){                    // SPIKE: load a Xonotic .map's brush geometry
        g_map=LoadQ3MapModel(g_mapPath,&g_hasMap);
        int tt=0; for(int mi=0;mi<g_map.meshCount;mi++) tt+=g_map.meshes[mi].triangleCount;
        DebugLog("map","\"path\":\"%s\",\"ok\":%s,\"textures\":%d,\"tris\":%d,\"lights\":%d,\"bake_sec\":%.2f",
                 JStr(g_mapPath), g_hasMap?"true":"false", g_map.meshCount, tt, g_nMapLights, g_mapAOsec);
        if (!g_hasMap) TraceLog(LOG_WARNING,"map: failed to load %s", g_mapPath);
    }
    if (g_hasMap){                                    // start at a map spawn point, drop to the floor
        g_pos = g_hasSpawn ? (Vector3){g_mapSpawn.x, g_mapSpawn.y+2.0f, g_mapSpawn.z} : (Vector3){0,12,0};
        g_yaw=0.0f; g_pitch=0.0f; g_vy=0.0f; g_grounded=0;
        Vector3 sp; float sy,sp2;                     // a saved spawn (F2) overrides the map's start
        if (LoadSpawn(&sp,&sy,&sp2)){ g_pos=sp; g_yaw=sy; g_pitch=sp2;
            DebugLog("spawn","\"custom\":true,\"pos\":[%.1f,%.1f,%.1f]", sp.x,sp.y,sp.z); }
        MapNavBuild(g_pos);                           // flood the walkable nav grid from the start point
        DebugLog("nav","\"grid\":[%d,%d],\"walkable\":%d,\"ok\":%d", g_navNX, g_navNZ, g_navCount, g_navOK);
    }

    g_floorTex=MakeChecker(512,(Color){60,64,70,255},(Color){44,48,54,255},16);
    g_wallTex =MakeChecker(256,(Color){80,72,64,255},(Color){64,58,52,255},8);
    g_crateTex=MakeChecker(128,(Color){120,90,55,255},(Color){95,70,42,255},4);
    g_floor=LoadModelFromMesh(GenMeshPlane(1,1,1,1));
    g_wall =LoadModelFromMesh(GenMeshCube(1,1,1));
    g_crate=LoadModelFromMesh(GenMeshCube(1,1,1));
    g_floor.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture=g_floorTex;
    g_wall.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =g_wallTex;
    g_crate.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture=g_crateTex;

    for (int c=0;c<NUM_CRATES;c++){
        float gx=GetRandomValue(-(int)ARENA+4,(int)ARENA-4);
        float gz=GetRandomValue(-(int)ARENA+4,(int)ARENA-4);
        float h=1.0f+GetRandomValue(0,20)/10.0f;
        g_crates[c]=(Box){ (Vector3){gx,h/2,gz}, (Vector3){1.6f,h,1.6f}, WHITE };
    }

    g_cam  =(Camera3D){ g_pos, Vector3Add(g_pos,(Vector3){0,0,-1}), (Vector3){0,1,0}, 70.0f, CAMERA_PERSPECTIVE };
    g_vmCam=(Camera3D){ (Vector3){0,0,0}, (Vector3){0,0,-1}, (Vector3){0,1,0}, 55.0f, CAMERA_PERSPECTIVE };

    LoadWeapon(0, "assets/rifle.glb", "../assets/rifle.glb", "M16A3 Rifle", "vm_tune.txt",
               VM_OFF0, VM_SCALE0, VM_YAW0, VM_PITCH0, 0.0f);
    LoadWeapon(1, "assets/shotgun.glb", "../assets/shotgun.glb", "Shotgun", "vm_tune_shotgun.txt",
               (Vector3){ -0.00216f, -0.19537f, -0.58625f }, 0.000068f, 106.89f, -18.45f, 88.0f);  // dialed in
    LoadWeapon(2, "assets/minigun.glb", "../assets/minigun.glb", "Minigun", "vm_tune_minigun.txt",
               (Vector3){ 0.098f, -0.143f, -0.637f }, 0.96758f, 184.1f, -6.1f, 0.0f);  // dialed in
    LoadWeapon(3, "assets/lmg.glb", "../assets/lmg.glb", "LMG", "vm_tune_lmg.txt",
               (Vector3){ -2.45f, -5.55f, 1.68f }, -1.0f, 180.0f, 0.0f, 0.0f);
    LoadWeapon(4, "assets/ak74.glb", "../assets/ak74.glb", "AK-74M", "vm_tune_ak.txt",
               (Vector3){ 0.200f, -0.218f, -0.642f }, 0.290106f, -176.0f, 88.1f, 0.0f);  // dialed in
    // new weapons (auto-framed; tune each live with N, ENTER saves). MP5 + Benelli
    // are clean rigs that animate fire+reload; the flamethrower/knife are model-only
    // for now (they hitscan -- no flame/melee mechanic yet).
    g_posedBasis[5]=1;   // MP5's bind pose is ~44k units off-origin; frame from the posed idle pose
    LoadWeapon(5, "assets/mp5.glb",          "../assets/mp5.glb",          "MP5",          "vm_tune_mp5.txt",     (Vector3){0.33f,-0.49f,-1.04f}, 0.000026f, 106.0f, -90.0f, 0.0f);
    LoadWeapon(6, "assets/benelli.glb",      "../assets/benelli.glb",      "Benelli M4",   "vm_tune_benelli.txt", (Vector3){0.30f,-0.40f,-1.08f}, 0.000021f, 104.0f, 90.0f, 0.0f);
    LoadWeapon(7, "assets/flamethrower.glb", "../assets/flamethrower.glb", "Flamethrower", "vm_tune_flame.txt",   (Vector3){0.05045f,-0.37347f,-0.43725f}, 0.021775f, -330.04f, 52.32f, 134.41f);
    LoadWeapon(8, "assets/knife.glb",        "../assets/knife.glb",        "Knife",        "vm_tune_knife.txt",   (Vector3){0,0,0}, -1.0f, 0.0f, 0.0f, 0.0f);
    g_numWeapons=9;
    ActivateWeapon( (vmWeapon>=0)?vmWeapon:0 );   // park on the rifle (or --weapon N for tuning)
    if (vmHas){ g_vmOff=(Vector3){vmOv[0],vmOv[1],vmOv[2]}; g_vmScale=vmOv[3]; g_vmYaw=vmOv[4]; g_vmPitch=vmOv[5]; g_vmRoll=vmOv[6]; }

    // Per-weapon fire sounds. The two automatics loop while the trigger is held;
    // the shotgun fires one blast per shot.
    LoadWeaponSound(0, "assets/rifle_fire.mp3",   "../assets/rifle_fire.mp3",   1);  // M16A3 (machine gun)
    LoadWeaponSound(1, "assets/shotgun_fire.mp3", "../assets/shotgun_fire.mp3", 0);  // shotgun: one-shot
    LoadWeaponSound(2, "assets/minigun_fire.mp3", "../assets/minigun_fire.mp3", 1);  // minigun
    LoadWeaponSound(3, "assets/lmg_fire.mp3",     "../assets/lmg_fire.mp3",     1);  // LMG (biggun)
    LoadWeaponSound(4, "assets/ak74_fire.mp3",    "../assets/ak74_fire.mp3",    0);  // AK-74M: one-shot per bullet
    LoadWeaponSound(5, "assets/rifle_fire.mp3",   "../assets/rifle_fire.mp3",   1);  // MP5: reuse the MG loop
    LoadWeaponSound(6, "assets/shotgun_fire.mp3", "../assets/shotgun_fire.mp3", 0);  // Benelli M4: one-shot per shot
    LoadWeaponAux(1, "assets/shotgun_cock.mp3",     "../assets/shotgun_cock.mp3");     // shotgun cock between shots
    LoadWeaponAux(2, "assets/minigun_cooldown.mp3", "../assets/minigun_cooldown.mp3"); // minigun spin-down
    if (g_weapons[2].hasAuxSnd) SetSoundVolume(g_weapons[2].auxSnd, 0.4f);  // spin-down was too loud/abrupt -> soften it
    g_weapons[0].burst=3;        // rifle:   3-round burst per trigger pull
    g_weapons[1].autoReload=1;   // shotgun: pump (reload anim) + cock sound after each shot
    g_weapons[2].spinUp=1;       // minigun: 5s fire cap -> cooldown sound + lockout
    g_weapons[3].soundGated=1;   // LMG:     fire while the sound plays, then a 0.75s pause
    g_weapons[4].playShoot=1;    // AK-74M:  plays its Shot clip on fire; R plays AK_Reload
    g_weapons[5].playShoot=1;    // MP5:     plays its Shoot clip on fire; R plays Reload
    g_weapons[6].playShoot=1;    // Benelli: plays its Fire clip on fire; R plays Reload

    // Load the enemy (Mixamo walk rig) and spawn a starting wave.
    const char *enemyPath="assets/enemy.glb";
    if (!FileExists(enemyPath)) enemyPath="../assets/enemy.glb";
    if (FileExists(enemyPath)){
        g_enemy=LoadModel(enemyPath);
        g_enemyAnim=LoadModelAnimations(enemyPath,&g_enemyAnimN);
        for (int ci=0; ci<g_enemyAnimN; ci++){
            const char *nm=g_enemyAnim[ci].name;
            if      (strstr(nm,"walk"))   g_eWalk=ci;
            else if (strstr(nm,"idle"))   g_eIdle=ci;
            else if (strstr(nm,"run"))    g_eRun=ci;
            else if (strstr(nm,"hit"))    g_eHit=ci;
            else if (strstr(nm,"attack")) g_eAttack=ci;
            else if (strstr(nm,"death"))  g_eDeath=ci;
        }
        TraceLog(LOG_INFO,"enemy clips walk=%d idle=%d run=%d hit=%d attack=%d death=%d",g_eWalk,g_eIdle,g_eRun,g_eHit,g_eAttack,g_eDeath);
        // Pick the death clip's "settled on the floor" frame. Many death anims end
        // with a trailing keyframe that snaps back to the standing start pose, so
        // freezing the corpse at the literal last frame leaves it standing. Walk
        // the root bone's vertical track and take the LATEST frame still near its
        // lowest point -- that's the body lying down, just before any spring-back.
        g_eDeathFrame = (g_enemyAnimN>0) ? ANIM_FRAMES(g_enemyAnim[g_eDeath])-1 : 0;
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
        if (g_enemyAnimN>0 && g_enemy.skeleton.boneCount>0){
            ModelAnimation *d=&g_enemyAnim[g_eDeath];
            int nf=ANIM_FRAMES(*d), root=0;
            for (int b=0;b<g_enemy.skeleton.boneCount;b++)
                if (strstr(g_enemy.skeleton.bones[b].name,"Hips")||strstr(g_enemy.skeleton.bones[b].name,"hips")){ root=b; break; }
            float minY=1e30f, maxY=-1e30f;
            for (int f=0;f<nf;f++){ float y=d->keyframePoses[f][root].translation.y; if(y<minY)minY=y; if(y>maxY)maxY=y; }
            float thresh=minY+(maxY-minY)*0.2f;            // "near the floor" band
            for (int f=nf-1;f>=0;f--)                      // latest frame still in that band
                if (d->keyframePoses[f][root].translation.y<=thresh){ g_eDeathFrame=f; break; }
        }
#endif
        DebugLog("enemydeath","\"frames\":%d,\"settleFrame\":%d", g_enemyAnimN>0?ANIM_FRAMES(g_enemyAnim[g_eDeath]):0, g_eDeathFrame);
        g_hasEnemy=(g_enemy.meshCount>0);
        BoundingBox eb=GetModelBoundingBox(g_enemy);
        DebugLog("enemy","\"meshes\":%d,\"bones\":%d,\"anims\":%d,\"size\":[%.1f,%.1f,%.1f]",
                 g_enemy.meshCount, g_enemy.skeleton.boneCount, g_enemyAnimN,
                 eb.max.x-eb.min.x, eb.max.y-eb.min.y, eb.max.z-eb.min.z);
        if (!g_noEnemies) for (int i=0;i<5;i++) SpawnEnemy(i);   // arena or map: SpawnEnemy picks placement
    } else DebugLog("enemy","\"error\":\"assets/enemy.glb not found\"");

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(Frame,0,1);
#else
    int fc=0;
    while (!WindowShouldClose() && !g_quit){ Frame(); if (g_maxFrames>0 && ++fc>=g_maxFrames) break; }
#endif

    StashActiveTuning();
    for (int i=0;i<g_numWeapons;i++) SaveTune(g_weapons[i].tunePath,g_weapons[i].off,g_weapons[i].scale,g_weapons[i].yaw,g_weapons[i].pitch,g_weapons[i].roll);
    DebugLog("shutdown","\"ok\":true");
    for (int i=0;i<g_numWeapons;i++) if (g_weapons[i].has){
        if (g_weapons[i].anim) UnloadModelAnimations(g_weapons[i].anim,g_weapons[i].animN);
        UnloadModel(g_weapons[i].model);
    }
    if (g_hasEnemy){ if (g_enemyAnim) UnloadModelAnimations(g_enemyAnim,g_enemyAnimN); UnloadModel(g_enemy); }
    UnloadModel(g_floor); UnloadModel(g_wall); UnloadModel(g_crate);
    if (g_hasMap) UnloadModel(g_map);
    UnloadTexture(g_floorTex); UnloadTexture(g_wallTex); UnloadTexture(g_crateTex);
    if (g_audio){
        for (int i=0;i<g_numWeapons;i++){ if (g_weapons[i].hasSnd) UnloadSound(g_weapons[i].fireSnd);
                                          if (g_weapons[i].hasAuxSnd) UnloadSound(g_weapons[i].auxSnd); }
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
