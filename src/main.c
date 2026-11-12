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
#include <dirent.h>      // map-select menu: scan maps/*.map at startup

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
static int g_mapPathOwned=0;   // 1 if g_mapPath was malloc'd by the picker (free on map switch); 0 if it's argv
static Shader g_scrollShader; static int g_scrollLoc=-1; static int g_hasScrollShader=0;  // the WORLD shader: UV scroll (liquids/warpzones) + dynamic lights + fog
static int g_wLoc[6]={-1,-1,-1,-1,-1,-1};   // dPos dCol dR dN viewPos fog
// ---- Atmosphere: fog + dynamic lights --------------------------------------
// The arena's lamps are baked into vertex colour; what MOVES is lit here. A dynamic light is a
// short-lived point light (muzzle flash, explosion) or one re-added every frame by something
// that glows (flame stream, radiation bolt, the priest's lantern: hold=1, steady while fed).
// The world shader takes the 8 nearest the camera, each enemy the 3 nearest him.
static const float kFog[4]={0.060f,0.070f,0.080f,0.010f};   // fog colour + density (per metre)
typedef struct { Vector3 pos, col; float R, life, life0; int hold; } DynLight;
#define MAX_DYN 24
static DynLight g_dyn[MAX_DYN];
static void DynLightAdd(Vector3 p, Vector3 col, float R, float life, int hold){
    int freeS=-1, weak=0;
    for (int i=0;i<MAX_DYN;i++){ DynLight *d=&g_dyn[i];
        if (d->life>0 && d->hold==hold && fabsf(d->R-R)<0.5f && Vector3DistanceSqr(d->pos,p)<1.6f*1.6f){   // same source, still lit: feed it
            d->pos=p; d->col=col; d->life=d->life0=life; return; }
        if (d->life<=0){ if (freeS<0) freeS=i; } else if (d->life<g_dyn[weak].life || g_dyn[weak].life<=0) weak=i; }
    g_dyn[freeS>=0?freeS:weak]=(DynLight){p,col,R,life,life,hold};
}
static void DynLightsUpdate(float dt){ for (int i=0;i<MAX_DYN;i++) if (g_dyn[i].life>0) g_dyn[i].life-=dt; }
// The up-to-maxN lights that matter most at `at` (pos/col as xyz triples, colour pre-faded). Returns the count.
static int DynLightsNear(Vector3 at, int maxN, float reach, float *pos, float *col, float *rad){
    int pick[8]; float pd[8]; int n=0; if (maxN>8) maxN=8;
    for (int i=0;i<MAX_DYN;i++){ if (g_dyn[i].life<=0) continue;
        float d=Vector3Distance(g_dyn[i].pos,at)-g_dyn[i].R; if (d>reach) continue;
        int k=n; while (k>0 && pd[k-1]>d) k--;
        if (k>=maxN) continue;
        int last=(n<maxN)?n:maxN-1;
        for (int j=last;j>k;j--){ pick[j]=pick[j-1]; pd[j]=pd[j-1]; }
        pick[k]=i; pd[k]=d; if (n<maxN) n++; }
    for (int j=0;j<n;j++){ const DynLight *d=&g_dyn[pick[j]]; float k=d->hold?1.0f:(d->life/d->life0);
        pos[j*3]=d->pos.x; pos[j*3+1]=d->pos.y; pos[j*3+2]=d->pos.z;
        col[j*3]=d->col.x*k; col[j*3+1]=d->col.y*k; col[j*3+2]=d->col.z*k; rad[j]=d->R; }
    return n;
}

// ---- Player -----------------------------------------------------------------
static Vector3 g_pos   = { 0, EYE_H, 6 };
static float   g_yaw   = 0.0f;
static float   g_pitch = 0.0f;
static int     g_lookSet = 0;            // --look <yaw> <pitch>: aim the spawn camera (dev/map inspection)
static float   g_lookYaw = 0.0f, g_lookPitch = 0.0f;
static float   g_vy    = 0.0f;
static int     g_grounded = 1;
static int     g_devOverlay = 1;
static float   g_bob = 0.0f;
// Procedural viewmodel motion (view-space, decaying): sway lags the mouse, bob
// follows the stride, and the raise/lower offset drives weapon switches.
static Vector2 g_vmSway = {0,0};     // smoothed mouse delta (pixels/frame), the gun lags the look
static Vector2 g_vmLook = {0,0};     // this frame's raw mouse delta
static float   g_vmRaise = 0.0f;     // 1 = fully lowered (just switched), eases to 0
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
static int             g_aIdle=0, g_aShoot=1, g_aReload=2, g_aWalk=-1;  // idle = hold last frame of "Take"; aWalk=-1 = no walk clip
static int             g_reloadSeq[6], g_reloadSeqN=0;  // active weapon's multi-phase reload clips
static int             g_reloadStep=-1;                 // -1 idle; >=0 index into g_reloadSeq
static int             g_reloading=0;                   // a reload (single or sequence) is in progress
static int             g_drawTopUp=0;                   // a weapon was just drawn: reload it if it is not full
static int g_glPhase=0, g_glCut=0, g_glFire=0;   // 1 opening, 2 feeding, 3 shutting; cut = trigger pulled mid-reload; fire = shoot as it shuts
static int             g_curAnim = 0;           // start in idle (Take, held at end)
static float           g_animT = 0.0f;          // frame cursor
static int             g_animOnce = 0;          // current clip is a one-shot (Reload)
static float           g_fireCd = 0.0f;
static float           g_recoil = 0.0f;         // 0..1 recoil kick on fire (decays); drives muzzle-up
static int             g_burstLeft = 0;         // rounds remaining in the current burst (burst weapons)
static float           g_spin = 0.0f;           // minigun barrel-spin rate 0..1
static float           g_mgSpinT = 0.0f;         // minigun: seconds the trigger's been held (spin-up timer)
static float           g_shake = 0.0f;           // 0..1 minigun "brain shake": screen jitter + edge vignette
static float           g_flameHeat = 0.0f;        // flamethrower warm-up: seconds the trigger's been held; below FLAME_WARMUP it spits raw fuel, above it ignites
#define FLAME_WARMUP 0.55f                         // warm-up before the flame catches: spray unignited liquid until then
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
static Camera3D        g_cam;           // world camera (set per-map in StartSelectedMap)
static Camera3D        g_vmCam;
static int             g_inspect = 0;           // V: gun floats in the world
static float           g_savedMsg = 0.0f;       // >0: flash a "SAVED" confirmation
static int             g_tuneTarget = 0;        // tuning focus: 0 = whole viewmodel, 1 = gun-on-hand grip (B toggles, pinned guns only)
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
// Shared viewmodel placement per weapon class. Every gun is measured at load into a
// canonical frame (ComputeVmFrame), then drawn at its class's slot: the gun's centre
// at `pos` (view space, camera at origin looking -Z), scaled to `len` view units,
// barrel converging on the crosshair `conv` units ahead (far: barrels point straight forward,
// parallel to the view -- a near point tipped them up and to the left). One table -> every weapon
// sits in the same place at the same size; per-weapon trims (N mode) ride on top.
enum { VM_RIFLE=0, VM_HEAVY=1, VM_NCLASS };
static struct { float len; Vector3 pos; float conv; } kVmClass[VM_NCLASS]={   // VMCLASS0..1="len x y z conv" env overrides (dev)
    { 0.70f, { 0.200f, -0.160f, -0.50f }, 60.0f },  // rifles, SMGs, shotguns, launcher
    { 0.80f, { 0.200f, -0.220f, -0.75f }, 60.0f },  // minigun / LMG / flamethrower: held out at the hip, barrel near-level, whole gun in view
};
typedef struct {
    Model           model;
    ModelAnimation *anim;
    int             animN, has;
    int             aIdle, aShoot, aReload, aWalk;
    int             idleHold;                    // ready-pose anim frame to HOLD (-1 = default last frame); Z/X scrub it, saved to the tune file -- so the hands grip the gun instead of T-posing
    int             reloadSeq[6], reloadSeqN;   // multi-phase reload clips, in play order
    Vector3         centroid;
    float           fitScale;
    Vector3         off;  float scale, yaw, pitch, roll;     // live, persisted
    Vector3         off0; float scale0, yaw0, pitch0, roll0; // defaults for 0=reset
    const char     *label;
    const char     *tunePath;                    // legacy absolute tune file: now only read for idleHold (+ the .pin sibling)
    char            trimPath[96];                // vm_trim_*.txt: off/scale/yaw/pitch/roll TRIM on top of the auto frame
    Matrix          vmAlign;                     // model -> canonical gun frame: centred, barrel -Z, up +Y, length 1 (ComputeVmFrame)
    float           vmBase;                      // canonical -> view-space scale (class length); model units * vmBase*trim = view units
    float           gunLen;                      // measured gun length in model units (diagnostic)
    int             vmClass;                     // VM_RIFLE / VM_HEAVY: picks the shared placement
    int             gunBone;                     // main gun-body bone (ComputeVmFrame), -1 = none / pinned
    Vector3         gunC;                        // gun centre at the rest pose, model space
    float           reloadSteady;                // 0..1: how much of the gun-body's reload roll/swing to cancel (VmSteady)
    Sound           fireSnd;                     // per-weapon fire report
    int             hasSnd;                      // fireSnd loaded OK
    int             sndLoop;                     // 1 = loop while trigger held (auto); 0 = one-shot per shot
    int             burst;                       // >0: rounds per trigger pull (burst fire)
    int             autoReload;                  // 1: play the reload anim after each shot (pump)
    int             spinUp;                      // 1: minigun -- 5s fire cap, then cooldown sound + lockout
    int             soundGated;                  // 1: LMG -- fires while its sound plays, then a fixed pause
    int             flame;                       // 1: flamethrower -- continuous fire cone while the trigger is held
    int             melee;                       // 1: knife -- short-range arc swing on each click
    int             grenade;                     // 1: grenade launcher -- lobs an arcing grenade that explodes on impact
    int             pinGun;                      // 1: gun mesh is STATIC & detached from the arms -> draw it rigidly attached to handBone each frame
    int             handBone, handBoneL;         // right / left hand bones; the gun RIDES the right, -1 = not found
    Vector3         gunBindC;                    // bind-space centre of the static gun meshes
    Vector3         pinSeat;                     // bind-space point the gun centre is seated at: the PALM MIDPOINT (so it spans both hands), else the right palm
    Matrix          pinAlign;                    // auto-rotation: gun's principal (long) axis -> the palm-to-palm axis
    Vector3         pinOff; float pinScale, pinYaw, pinPitch, pinRoll;  // grip trim on top of the auto-seat, in hand-bind space (B-mode tunes it; saved to <tune>.pin)
    Vector3         armsROff;                    // extra nudge applied to RIGHT-arm vertices only (baked into mesh.vertices; B-mode)
    Vector3         armsLOff;                    // same for the LEFT arm -- each hand seats independently
    unsigned long long gunMask;                  // bit k = mesh k is a GUN part (unskinned OR rigid-bound to one bone) vs blended-skinned arms
    int             pinAuthored;                 // 1: raylib's bake reproduces the file's authored gun-in-hands placement -> keep it (no re-seat), just ride the hand
    int             playShoot;                   // 1: play the model's Shot clip on each shot (AK has a good one)
    float           fireCd;                      // per-shot cooldown override (0 -> default)
    Sound           auxSnd;                      // secondary sound: minigun spin-down, shotgun cock-between-shots
    int             hasAuxSnd;
    // -- combat identity (M1). Live ammo lives HERE in the slot and is only ever
    // read through g_weapons[g_curWeapon] -- never mirrored into the working-set
    // globals -- so the ActivateWeapon/StashActiveTuning swap can't corrupt it.
    float           dmg;                         // damage per bullet/pellet (0 -> legacy ENEMY_DMG_PER_SHOT)
    int             pellets;                     // >1: pellet volley per shot (shotguns)
    float           spread;                      // aim cone half-angle per pellet, degrees
    float           recoilKick;                  // camera pitch kick per shot, degrees
    int             shellReload;                 // 1: reload clip inserts ONE shell -> loop it until full (benelli)
    float           shellPump, shellRackAt;      // the clip's closing pump: where it starts, and the frame the action racks
    float           shellFirst, shellCycle; int shellCount;   // >0: ONE reload clip inserting shellCount shells,
                                                 // the first at frame shellFirst then every shellCycle (remington)
    int             magSize;                     // rounds per mag / fuel units per tank (0 = no ammo: knife)
    int             reserveStart;                // reserve a fresh run starts with
    int             mag, reserve;                // live counts
} Weapon;
static Weapon g_weapons[16];
static int    g_curWeapon = 0;
static int    g_numWeapons = 0;
static int    g_audio = 0;                       // audio device ready (sounds load/play only when set)
static Sound  g_deathSnd[8];                      // one is played at random on every enemy kill
static int    g_nDeathSnd = 0;
static Sound  g_attackSnd;                         // played when an enemy lunges into an attack
static int    g_hasAttackSnd = 0;
static Sound  g_grenSnd;  static int g_hasGrenSnd  = 0;   // grenade explosion
static Sound  g_hurtSnd;  static int g_hasHurtSnd  = 0;   // player took a real hit
static Sound  g_heartSnd; static int g_hasHeartSnd = 0;   // low-health heartbeat (re-triggered while low)
static Sound  g_appearSnd, g_dieSnd, g_shellSnd; static int g_hasAppearSnd=0, g_hasDieSnd=0, g_hasShellSnd=0;   // hostiles come into view / the player's death scream / a shell pushed into the tube
static int    g_seenNow = 0, g_seenPrev = 0; static float g_appearCd = 0.0f;   // hostiles on screen this frame / last frame; sting cooldown
#define WAVE_BG_VOL 0.42f
static Music  g_waveBg[4]; static int g_nWaveBg = 0; static float g_waveBgVol[4]={0,0,0,0};   // each wave's background track (looped, cross-faded)
static Sound  g_glSnd[3]; static int g_hasGlSnd[3]={0,0,0};      // grenade launcher reload: drum open, a grenade in, drum shut (cuts of one recording)
static Sound  g_drySnd; static int g_hasDrySnd = 0;              // trigger on an empty gun
static Sound  g_fleshSnd; static int g_hasFleshSnd = 0; static float g_fleshSndCd = 0.0f;   // a round striking a body (rate-limited like the wall hit)
static Sound  g_wallSnd; static int g_hasWallSnd = 0; static float g_wallSndCd = 0.0f;   // a round striking masonry (rate-limited: the minigun lands 17 a second)
static Sound  g_runSnd; static int g_hasRunSnd = 0; static float g_runVol = 0.0f;   // boots pounding toward the player: follows the nearest runner
static Sound  g_gibSnd; static int g_hasGibSnd = 0;              // a body blown apart
static Sound  g_knockSnd; static int g_hasKnockSnd = 0;          // a body flattened
static Sound  g_grenBounceSnd; static int g_hasGrenBounceSnd = 0;   // a grenade skipping off a wall or the floor
static Sound  g_spinUpSnd; static int g_hasSpinUpSnd = 0;       // minigun: the first second of a minigun recording, as the trigger is pulled
static Sound  g_sgDeathSnd; static int g_hasSgDeathSnd = 0;     // some of the men the shotgun kills go out with this scream
static Sound  g_bossDeadSnd; static int g_hasBossDeadSnd = 0;   // the boss goes down (wave 1's track; every boss until the others have their own)
static Sound  g_bossSnd; static int g_hasBossSnd = 0;     // the head chef arrives: 15 s of dread over his entrance
static Sound  g_mobSnd; static int g_hasMobSnd = 0; static float g_mobCd = 0.0f;   // a pack bearing down on the player: the angry crowd
static Sound  g_crowdSnd; static int g_hasCrowdSnd = 0;   // several hostiles killed by ONE grenade: they go together
static Sound  g_painSnd[2]; static int g_nPainSnd = 0;    // the player's own voice when he is being torn up (assets/pain_*.mp3)
static float  g_painAcc = 0.0f, g_painCd = 0.0f;          // damage taken lately (drains) / seconds until he may cry out again
// ---- Sound design: synthesized cues, ambience, and where things are ----------
// No sample library to lean on, so the small cues are built at start-up from sines and
// noise: the tick of a round landing, the thud that confirms a kill, brass on concrete,
// the player's own boots. The ambience is two seamless loops -- a low drone with wind and a
// Geiger counter ticking over, and a tension layer (a heavy pulse under a dissonant hum, the
// counter chattering) whose volume follows how many hostiles are up.
enum { SY_HIT, SY_KILL, SY_CLINK, SY_STEP, SY_JUMP, SY_LAND, SY_N };
static Sound g_sy[SY_N]; static int g_hasSy=0;
static Music g_amb[2]; static int g_hasAmb=0; static float g_ambTense=0.0f;
static unsigned int g_syRng=0x1234567u;
static void SfxAt(Sound snd, Vector3 at, float gain);
static float SyNoise(void){ g_syRng=g_syRng*1664525u+1013904223u; return (float)((g_syRng>>8)&0xFFFF)/32768.0f-1.0f; }
static void SynthInit(void){
    const int SR=44100;
    const float len[SY_N]={0.050f,0.200f,0.140f,0.120f,0.130f,0.200f};
    for (int k=0;k<SY_N;k++){
        int n=(int)(len[k]*SR); short *d=(short*)MemAlloc(n*sizeof(short)); float lp=0;
        for (int i=0;i<n;i++){ float t=(float)i/SR, v=0;
            if (k==SY_HIT)   v=sinf(t*6.2832f*1850.0f)*expf(-t*95.0f)*0.7f + SyNoise()*expf(-t*400.0f)*0.5f;
            if (k==SY_KILL)  v=sinf(t*6.2832f*(540.0f-t*900.0f))*expf(-t*22.0f)*0.55f + sinf(t*6.2832f*88.0f)*expf(-t*18.0f)*0.6f + SyNoise()*expf(-t*300.0f)*0.3f;
            if (k==SY_CLINK) v=(sinf(t*6.2832f*4150.0f)+0.6f*sinf(t*6.2832f*6230.0f)+0.4f*sinf(t*6.2832f*9100.0f))*expf(-t*42.0f)*0.4f;
            if (k==SY_STEP){ lp+=(SyNoise()-lp)*0.08f; v=lp*expf(-t*38.0f)*1.6f + sinf(t*6.2832f*(74.0f-t*120.0f))*expf(-t*30.0f)*0.7f; }
            if (k==SY_JUMP){ lp+=(SyNoise()-lp)*0.20f; float e=sinf(3.14159f*t/len[k]); v=lp*e*e*1.5f + sinf(t*6.2832f*(95.0f+t*260.0f))*expf(-t*26.0f)*0.35f; }   // boots leaving the floor: a scuff and a push
            if (k==SY_LAND){ lp+=(SyNoise()-lp)*0.06f; v=lp*expf(-t*20.0f)*2.2f + sinf(t*6.2832f*(62.0f-t*90.0f))*expf(-t*17.0f)*0.95f; }                        // both feet coming down: heavier than a step
            if (v>1) v=1; if (v<-1) v=-1; d[i]=(short)(v*30000.0f); }
        Wave w={ .frameCount=(unsigned int)n, .sampleRate=SR, .sampleSize=16, .channels=1, .data=d };
        g_sy[k]=LoadSoundFromWave(w); MemFree(d);
    }
    g_hasSy=1;
    // ambience: two 12 s loops. Every tone completes a whole number of cycles in 12 s and the
    // noise is cross-faded end-to-start, so the loop point is silent.
    const int AR=22050, AN=AR*12;
    for (int L=0;L<2;L++){
        int bytes=44+AN*2; unsigned char *buf=(unsigned char*)MemAlloc(bytes); short *d=(short*)(buf+44);
        float *f=(float*)MemAlloc(AN*sizeof(float)); float lp=0, lp2=0;
        for (int i=0;i<AN;i++){ float t=(float)i/AR, v;
            lp+=(SyNoise()-lp)*0.012f; lp2+=(lp-lp2)*0.02f;
            if (L==0){ float gust=0.55f+0.45f*sinf(t*6.2832f/12.0f*2.0f+1.0f);            // drone + wind
                v=sinf(t*6.2832f*55.0f)*0.20f + sinf(t*6.2832f*55.25f)*0.18f + sinf(t*6.2832f*82.5f)*0.09f + lp2*7.0f*gust; }
            else { float ph=fmodf(t,0.75f), th=expf(-ph*9.0f)+0.6f*expf(-fmaxf(0.0f,ph-0.22f)*11.0f)*(ph>0.22f);   // the pulse: a double beat
                float trem=0.6f+0.4f*sinf(t*6.2832f*4.0f/12.0f*7.0f);
                v=sinf(t*6.2832f*41.0f)*th*0.55f + (sinf(t*6.2832f*233.0f)+sinf(t*6.2832f*247.0f))*0.045f*trem + lp*1.2f; }
            f[i]=v; }
        int xf=AR/40; for (int i=0;i<xf;i++){ float w=(float)i/xf; f[AN-1-i]*=w; f[i]*=w; }   // 25 ms in and out: the noise has no seam
        int clicks=(L==0)?26:150;                                                          // the Geiger counter
        for (int c=0;c<clicks;c++){ g_syRng=g_syRng*1664525u+1013904223u; int at=(int)((g_syRng>>4)%(unsigned)(AN-400));
            for (int i=0;i<120;i++) f[at+i]+=SyNoise()*expf(-i*0.09f)*((L==0)?0.16f:0.20f); }
        for (int i=0;i<AN;i++){ float v=f[i]; if (v>1) v=1; if (v<-1) v=-1; d[i]=(short)(v*26000.0f); }
        MemFree(f);
        unsigned int u; unsigned short h;                                                  // a WAV header in front of it
        memcpy(buf,"RIFF",4); u=bytes-8; memcpy(buf+4,&u,4); memcpy(buf+8,"WAVEfmt ",8); u=16; memcpy(buf+16,&u,4);
        h=1; memcpy(buf+20,&h,2); h=1; memcpy(buf+22,&h,2); u=AR; memcpy(buf+24,&u,4); u=AR*2; memcpy(buf+28,&u,4);
        h=2; memcpy(buf+32,&h,2); h=16; memcpy(buf+34,&h,2); memcpy(buf+36,"data",4); u=AN*2; memcpy(buf+40,&u,4);
        g_amb[L]=LoadMusicStreamFromMemory(".wav",buf,bytes);                              // (buf stays alive: the stream reads from it)
        if (!IsMusicValid(g_amb[L])) return;
        g_amb[L].looping=true; SetMusicVolume(g_amb[L], L==0?0.55f:0.0f); PlayMusicStream(g_amb[L]);
    }
    g_hasAmb=1;
}
static Sound  g_waveSnd;  static int g_hasWaveSnd  = 0;   // wave-start sting
static Sound  g_clearSnd; static int g_hasClearSnd = 0;   // wave-clear sting

static void SaveTune(const char *path, Vector3 off, float scale, float yaw, float pitch, float roll, int idleHold){
    FILE *f=fopen(path,"w"); if(!f) return;
    fprintf(f,"%.5f %.5f %.5f %.6f %.2f %.2f %.2f %d\n", off.x,off.y,off.z,scale,yaw,pitch,roll,idleHold);
    fclose(f);
}
static int LoadTune(const char *path, Vector3 *off, float *scale, float *yaw, float *pitch, float *roll, int *idleHold){
    FILE *f=fopen(path,"r"); if(!f) return 0;
    float ox,oy,oz,sc,yw,pt,rl=0.0f; int ih=-1;
    int n=fscanf(f,"%f %f %f %f %f %f %f %d",&ox,&oy,&oz,&sc,&yw,&pt,&rl,&ih);
    fclose(f);
    if (idleHold) *idleHold = (n>=8)?ih:-1;   // 8th value optional: old 7-value tunes keep the default
    if(n>=6){ *off=(Vector3){ox,oy,oz}; *scale=sc; *yaw=yw; *pitch=pt; *roll=(n>=7)?rl:0.0f; return 1; }
    return 0;
}
// Gun-pin grip tune (gun pose relative to the hand bone) -- kept in a sibling "<tune>.pin"
// file so it's separate from the whole-viewmodel transform.
static void SavePin(const char *tunePath, Vector3 off, float scale, float yaw, float pitch, float roll, Vector3 rOff, Vector3 lOff){
    char p[280]; snprintf(p,sizeof p,"%s.pin",tunePath);
    FILE *f=fopen(p,"w"); if(!f) return;
    fprintf(f,"%.5f %.5f %.5f %.5f %.2f %.2f %.2f %.5f %.5f %.5f %.5f %.5f %.5f\n",
            off.x,off.y,off.z,scale,yaw,pitch,roll,rOff.x,rOff.y,rOff.z,lOff.x,lOff.y,lOff.z); fclose(f);
}
static void LoadPin(const char *tunePath, Vector3 *off, float *scale, float *yaw, float *pitch, float *roll, Vector3 *rOff, Vector3 *lOff){
    char p[280]; snprintf(p,sizeof p,"%s.pin",tunePath);
    FILE *f=fopen(p,"r"); if(!f) return;
    float ox,oy,oz,sc,yw,pt,rl,rx=0,ry=0,rz=0,lx=0,ly=0,lz=0;
    int n=fscanf(f,"%f %f %f %f %f %f %f %f %f %f %f %f %f",&ox,&oy,&oz,&sc,&yw,&pt,&rl,&rx,&ry,&rz,&lx,&ly,&lz); fclose(f);
    if(n>=7){ *off=(Vector3){ox,oy,oz}; *scale=sc; *yaw=yw; *pitch=pt; *roll=rl; }
    if(rOff) *rOff = (n>=10) ? (Vector3){rx,ry,rz} : (Vector3){0,0,0};   // optional per-hand nudges (older pins keep zero)
    if(lOff) *lOff = (n>=13) ? (Vector3){lx,ly,lz} : (Vector3){0,0,0};
}
// Custom player spawn (F2 saves your current pose; loaded at startup to override the
// map's spawn). Per-machine local content, like the weapon tune files. The save is
// keyed to the map's basename (spawn_<map>.txt) so a saved spawn for one map never
// leaks onto another -- the bind-mode pose for afterslime would otherwise dump the
// player into the void on every other .map. The no-map (built-in arena) case keeps
// the legacy spawn.txt name.
static const char *SpawnPath(char *buf, int sz){
    if (!g_mapPath){ snprintf(buf,sz,"spawn.txt"); return buf; }
    const char *b=strrchr(g_mapPath,'/'); b=b?b+1:g_mapPath;   // basename
    char stem[160]; snprintf(stem,sizeof stem,"%s",b);
    char *dot=strrchr(stem,'.'); if (dot) *dot=0;              // drop ".map"
    snprintf(buf,sz,"spawn_%s.txt",stem); return buf;
}
static void SaveSpawn(Vector3 pos, float yaw, float pitch){
    char path[224]; FILE *f=fopen(SpawnPath(path,sizeof path),"w"); if(!f) return;
    fprintf(f,"%.4f %.4f %.4f %.4f %.4f\n", pos.x,pos.y,pos.z,yaw,pitch); fclose(f);
}
static int LoadSpawn(Vector3 *pos, float *yaw, float *pitch){
    char path[224]; FILE *f=fopen(SpawnPath(path,sizeof path),"r"); if(!f) return 0;
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
// Which side of the body does bone b belong to? Walk ancestry until a name carries a
// side prefix (handles unprefixed bones like the minigun's left 'palm_016', whose parent
// chain is L_*). +1 right, -1 left, 0 neutral (root/torso).
static int BoneSide(Model *m, int b){
    while (b>=0 && b<m->skeleton.boneCount){
        const char *bn=m->skeleton.bones[b].name; char low[40]; int j=0;
        for(;bn[j]&&j<39;j++){char c=bn[j]; low[j]=(c>='A'&&c<='Z')?c+32:c;} low[j]=0;
        if (strstr(low,"l_")==low || strstr(low,"left"))  return -1;
        if (strstr(low,"r_")==low || strstr(low,"right")) return  1;
        b=m->skeleton.bones[b].parent;
    }
    return 0;
}
// Shift ONE side's arm vertices by dv (side: +1 right, -1 left; model space, baked
// straight into mesh.vertices -- the per-frame skinning pass re-uploads from there,
// so the edit shows immediately). Each vertex moves PROPORTIONALLY to its summed
// skin weight on that side's bones: full inside the hand, tapering smoothly at
// blend boundaries -- an all-or-nothing dominant-bone cutoff tears the mesh
// (stretched fingers) wherever neighbours land on opposite sides of the cut.
static void ApplyArmsSideDelta(Weapon *w, Vector3 dv, int side){
    if (fabsf(dv.x)+fabsf(dv.y)+fabsf(dv.z) < 1e-9f) return;
    int nb=w->model.skeleton.boneCount; if (nb<=0 || nb>256) return;
    signed char sideOf[256];
    for (int b=0;b<nb;b++) sideOf[b]=(signed char)BoneSide(&w->model,b);
    for (int k=0;k<w->model.meshCount && k<64;k++){
        Mesh *ms=&w->model.meshes[k];
        if ((w->gunMask>>k)&1ULL) continue;                       // arms meshes only
        if (!ms->boneIndices || !ms->boneWeights || !ms->vertices) continue;
        for (int v=0;v<ms->vertexCount;v++){
            float ws=0.0f;                                        // weight mass on the requested side
            for (int c=0;c<4;c++){
                int b=(int)ms->boneIndices[v*4+c];
                if (b<nb && sideOf[b]==side) ws+=ms->boneWeights[v*4+c];
            }
            if (ws<=0.001f) continue;
            ms->vertices[v*3]+=dv.x*ws; ms->vertices[v*3+1]+=dv.y*ws; ms->vertices[v*3+2]+=dv.z*ws;
        }
    }
}
static Matrix XformToMatrix(Transform t);      // fwd decl (defined near DrawPinnedGun)
// Convert a view-axis nudge into bind space (the verts live PRE-skin) by counter-
// rotating with the given hand bone's skin transform at the currently held frame.
// Identity-frame rigs (minigun) pass through unchanged.
static Vector3 NudgeToBindSpace(Weapon *w, int bone, Vector3 dv){
    if (bone>=0 && g_gunAnimN>0 && g_curAnim<g_gunAnimN && g_gunAnim[g_curAnim].keyframePoses){
        ModelAnimation *an=&g_gunAnim[g_curAnim];
        int kf=(int)g_animT; int nf=ANIM_FRAMES(*an); if(kf<0)kf=0; if(nf>0&&kf>=nf)kf=nf-1;
        Matrix skin=MatrixMultiply(MatrixInvert(XformToMatrix(w->model.skeleton.bindPose[bone])),
                                   XformToMatrix(an->keyframePoses[kf][bone]));
        Matrix invs=MatrixInvert(skin);
        Vector3 o=Vector3Transform((Vector3){0,0,0},invs);
        dv=Vector3Subtract(Vector3Transform(dv,invs),o);          // direction only (no translation)
    }
    return dv;
}

// One-shot diagnostic for the pinned-gun path: after posing the model at the held
// frame, log (a) where raylib's OWN skinning matrix sends the wrist bind point,
// (b) where MY keyframe-derived skin matrix sends it, (c) where the most wrist-bound
// ARM VERTEX actually landed (mesh.animVertices = the rendered ground truth). If
// (a)==(b)==(c), gun and hand must coincide on screen; whichever differs is the lie.
static void DebugPinProbe(Weapon *w){
    if (!w->pinGun) return;                                       // only instrumented weapons
    if (w->handBone<0 || w->animN<=0 || !w->model.boneMatrices){  // log WHY it can't probe -- a NULL boneMatrices means arms never animate at all
        DebugLog("pinprobe","\"skip\":true,\"handBone\":%d,\"animN\":%d,\"boneMatricesNull\":%d",
                 w->handBone, w->animN, w->model.boneMatrices==NULL);
        return;
    }
    int hb=w->handBone;
    float hold=(w->idleHold>=0)?(float)w->idleHold:0.0f;
    ANIM_APPLY(w->model, w->anim[0], hold);                       // pose arms + boneMatrices at the held frame
    int kf=(int)hold; int nf=ANIM_FRAMES(w->anim[0]); if (kf<0) kf=0; if (nf>0 && kf>=nf) kf=nf-1;
    Vector3 pw=w->model.skeleton.bindPose[hb].translation;        // the wrist, bind space
    Vector3 raylibSays=Vector3Transform(pw, w->model.boneMatrices[hb]);
    Matrix mySkin=MatrixMultiply(MatrixInvert(XformToMatrix(w->model.skeleton.bindPose[hb])),
                                 XformToMatrix(w->anim[0].keyframePoses[kf][hb]));
    Vector3 mineSays=Vector3Transform(pw, mySkin);
    Vector3 posedKey=w->anim[0].keyframePoses[kf][hb].translation;
    // ground truth: the arm vertex most bound to the wrist, baked vs rendered
    Vector3 armBaked={0,0,0}, armPosed={0,0,0}; float bestW=0;
    for (int k=0;k<w->model.meshCount && k<64;k++){
        Mesh *ms=&w->model.meshes[k];
        if ((w->gunMask>>k)&1ULL) continue;                       // arms only
        if (!ms->boneWeights||!ms->boneIndices||!ms->animVertices) continue;
        for (int v=0;v<ms->vertexCount;v++)
            for (int c=0;c<4;c++)
                if ((int)ms->boneIndices[v*4+c]==hb && ms->boneWeights[v*4+c]>bestW){
                    bestW=ms->boneWeights[v*4+c];
                    armBaked=(Vector3){ms->vertices[v*3],ms->vertices[v*3+1],ms->vertices[v*3+2]};
                    armPosed=(Vector3){ms->animVertices[v*3],ms->animVertices[v*3+1],ms->animVertices[v*3+2]};
                }
    }
    DebugLog("pinprobe","\"hold\":%d,\"bindW\":[%.2f,%.2f,%.2f],\"keyPose\":[%.2f,%.2f,%.2f],\"raylibSkin\":[%.2f,%.2f,%.2f],\"mySkin\":[%.2f,%.2f,%.2f],\"armW\":%.2f,\"armBaked\":[%.2f,%.2f,%.2f],\"armPosed\":[%.2f,%.2f,%.2f]",
             kf, pw.x,pw.y,pw.z, posedKey.x,posedKey.y,posedKey.z, raylibSays.x,raylibSays.y,raylibSays.z,
             mineSays.x,mineSays.y,mineSays.z, bestW, armBaked.x,armBaked.y,armBaked.z, armPosed.x,armPosed.y,armPosed.z);
}

// Make slot n the live weapon: copy its model/clips/tuning into the globals.
static void ActivateWeapon(int n){
    g_curWeapon=n;
    Weapon *w=&g_weapons[n];
    g_gun=w->model; g_gunAnim=w->anim; g_gunAnimN=w->animN; g_hasGun=w->has;
    g_aIdle=w->aIdle; g_aShoot=w->aShoot; g_aReload=w->aReload; g_aWalk=w->aWalk;
    g_reloadSeqN=w->reloadSeqN; for(int i=0;i<w->reloadSeqN;i++) g_reloadSeq[i]=w->reloadSeq[i];
    g_reloadStep=-1; g_reloading=0; g_glPhase=0; g_glCut=0; g_glFire=0;
    g_gunCentroid=w->centroid; g_gunFitScale=w->fitScale;
    g_vmOff=w->off; g_vmScale=w->scale; g_vmYaw=w->yaw; g_vmPitch=w->pitch; g_vmRoll=w->roll;
    g_curAnim=g_aIdle; g_animOnce=0; g_animT=0.0f; g_recoil=0.0f;
    g_burstLeft=0; g_spin=0.0f; g_mgBarrelAngle=0.0f; g_mgSpinT=0.0f; g_shake=0.0f; g_mgHeat=0.0f; g_mgLock=0; g_mgFiring=0; g_lmgPause=0.0f; g_lmgWasPlaying=0; g_flameHeat=0.0f;   // reset fire-mode state
    DebugPinProbe(w);    // pinned guns: log raylib-vs-mine wrist placement (no-op otherwise)
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
    g_drawTopUp=1;                                     // drawn: top it up if it isn't full (WeaponTrigger, once it is raised)
    g_vmRaise=1.0f;                // new gun comes up from below the frame
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
// glTF column-major float[16] -> raylib Matrix (raylib names the math element at
// row r, column c as m[c*4+r], which is exactly the glTF array index).
static Matrix MatFromColMajor(const float *a){
    Matrix m={0};
    m.m0=a[0];  m.m1=a[1];  m.m2=a[2];  m.m3=a[3];
    m.m4=a[4];  m.m5=a[5];  m.m6=a[6];  m.m7=a[7];
    m.m8=a[8];  m.m9=a[9];  m.m10=a[10]; m.m11=a[11];
    m.m12=a[12]; m.m13=a[13]; m.m14=a[14]; m.m15=a[15];
    return m;
}
// raylib builds a skinned model's bind pose from the joints' REST transforms and
// ignores the skin's inverseBindMatrices. Most of our FPS arm rigs were exported
// with bind != rest, so raylib skins every vertex against the wrong reference:
// the AK's hands close on air beside the gun, the MP5's arms vanish entirely.
// tools/gen_ibm.py extracts the true IBMs into <model>.glb.ibm; this swaps them in
// as the bind pose (bind = inverse(IBM)). Bones are matched by name, so a sidecar
// that drifts from the asset fails loudly in the log instead of mis-skinning.
static int ApplyIbmBindPose(Model *m, const char *glbPath, int slot){
    char p[300]; snprintf(p,sizeof p,"%s.ibm",glbPath);
    FILE *f=fopen(p,"r"); if (!f) return 0;
    int ver=0, n=0; float mw[16]; char tag[32];
    if (fscanf(f,"%31s %d",tag,&ver)!=2 || strcmp(tag,"ibm")){ fclose(f); return 0; }
    if (fscanf(f,"%31s",tag)!=1 || strcmp(tag,"meshworld")){ fclose(f); return 0; }
    for (int i=0;i<16;i++) if (fscanf(f,"%f",&mw[i])!=1){ fclose(f); return 0; }
    if (fscanf(f,"%31s %d",tag,&n)!=2 || strcmp(tag,"joints")){ fclose(f); return 0; }
    int mode=1; { const char *e=getenv("IBMMODE"); if (e) mode=atoi(e); }   // 1 = meshworld*inv(IBM): raylib bakes the mesh node transform into the verts (verified on the AK). dev A/B: -1 off, 0 inv(IBM) only
    if (mode<0){ fclose(f); return 0; }
    Matrix MW=MatFromColMajor(mw);
    int applied=0, missing=0;
    for (int k=0;k<n;k++){
        char name[128]; float a[16];
        if (fscanf(f,"%127s",name)!=1) break;
        for (int i=0;i<16;i++) if (fscanf(f,"%f",&a[i])!=1){ fclose(f); return applied; }
        int b=-1;
        if (k<m->skeleton.boneCount && !strncmp(m->skeleton.bones[k].name,name,sizeof m->skeleton.bones[k].name-1)) b=k;   // fast path: skin order
        else for (int q=0;q<m->skeleton.boneCount;q++) if (!strncmp(m->skeleton.bones[q].name,name,sizeof m->skeleton.bones[q].name-1)){ b=q; break; }
        if (b<0){ missing++; continue; }
        Matrix bind=MatrixInvert(MatFromColMajor(a));
        if (mode==1) bind=MatrixMultiply(bind,MW);
        Vector3 t,sc; Quaternion r; MatrixDecompose(bind,&t,&r,&sc);
        m->skeleton.bindPose[b]=(Transform){t,r,sc};
        applied++;
    }
    fclose(f);
    DebugLog("ibm","\"slot\":%d,\"file\":\"%s\",\"mode\":%d,\"applied\":%d,\"missing\":%d,\"bones\":%d",slot,p,mode,applied,missing,m->skeleton.boneCount);
    return applied;
}
// Load a glb into slot, resolve idle/shoot/reload clips by name, load saved tuning.
static void LoadWeapon(int slot, const char *path, const char *alt,
                       const char *label, const char *tunePath, int vmClass){
    Weapon *w=&g_weapons[slot];
    w->label=label; w->tunePath=tunePath; w->vmClass=vmClass;
    // Live tuning is a TRIM on top of the auto-measured frame (ComputeVmFrame), so
    // the neutral trim is the default: no offset, unit scale, no extra rotation.
    w->off0=(Vector3){0,0,0}; w->scale0=1.0f; w->yaw0=0.0f; w->pitch0=0.0f; w->roll0=0.0f;
    w->off=w->off0; w->scale=1.0f; w->yaw=0.0f; w->pitch=0.0f; w->roll=0.0f;
    w->vmAlign=MatrixIdentity(); w->vmBase=1.0f; w->gunLen=1.0f;
    w->gunBone=-1; w->gunC=(Vector3){0,0,0}; w->reloadSteady=0.85f;
    { const char *b=strstr(tunePath,"vm_tune"); // vm_tune_ak.txt -> vm_trim_ak.txt
      if (b) snprintf(w->trimPath,sizeof w->trimPath,"%.*svm_trim%s",(int)(b-tunePath),tunePath,b+7);
      else   snprintf(w->trimPath,sizeof w->trimPath,"trim_%s",tunePath); }
    w->aIdle=0; w->aShoot=1; w->aReload=2; w->has=0; w->anim=NULL; w->animN=0;
    const char *fp=path; if(!FileExists(fp)) fp=alt;
    if(!FileExists(fp)){ DebugLog("weapon","\"slot\":%d,\"error\":\"missing\"",slot); return; }
    w->model=LoadModel(fp);
    ApplyIbmBindPose(&w->model, fp, slot);           // true bind pose (raylib skins against the rest pose)
    w->anim=LoadModelAnimations(fp,&w->animN);
    w->has=(w->model.meshCount>0);
    // Cull stray morph-target / blend-shape helper meshes: those ship with NO diffuse
    // texture AND the default white material, so raylib draws them as a white untextured
    // blob over the gun (the M16A3's "shape_pose" mesh -> "rifle looks unskinned"). Only
    // hide meshes that are BOTH untextured AND pure white -- legit flat-colour parts (the
    // flamethrower's tinted glass / status lights have real baseColorFactor) are kept.
    for (int k=0;k<w->model.meshCount;k++){
        int mi=w->model.meshMaterial[k]; if (mi<0) continue;
        MaterialMap dm=w->model.materials[mi].maps[MATERIAL_MAP_DIFFUSE];
        if (dm.texture.id==rlGetTextureIdDefault() && dm.color.r==255 && dm.color.g==255 && dm.color.b==255 && dm.color.a==255){
            w->model.meshes[k].vertexCount=0; w->model.meshes[k].triangleCount=0;   // draw nothing for this mesh
            DebugLog("weapon","\"slot\":%d,\"culledUntexturedMesh\":%d",slot,k);
        }
    }
    // Gun-pin: locate the right-hand bone so a detached static gun mesh can ride it
    // (set per-slot via pinGun). Prefer the RIGHT wrist/hand/palm; fall back to any.
    w->handBone=-1; w->handBoneL=-1; w->pinScale=1.0f; w->pinOff=(Vector3){0,0,0}; w->pinYaw=0; w->pinPitch=0; w->pinRoll=0;
    w->pinAlign=MatrixIdentity(); w->pinSeat=(Vector3){0,0,0};
    for (int b=0;b<w->model.skeleton.boneCount;b++){
        const char *bn=w->model.skeleton.bones[b].name; char low[40]; int j=0;
        for(;bn[j]&&j<39;j++){char c=bn[j]; low[j]=(c>='A'&&c<='Z')?c+32:c;} low[j]=0;
        if (!(strstr(low,"wrist")||strstr(low,"hand")||strstr(low,"palm"))) continue;
        int isL=(strstr(low,"l_")==low)||strstr(low,"left");
        int isR=(strstr(low,"r_")==low)||strstr(low,"right");
        if (isL){ if (w->handBoneL<0) w->handBoneL=b; }
        else if (isR){ if (w->handBone<0) w->handBone=b; }
        else if (w->handBone<0) w->handBone=b;            // unsided "hand" -> usable as the ride bone
    }
    // Which meshes are GUN parts? This raylib build gives EVERY mesh of a rigged model
    // bone data (static meshes get rigid-bound to a bone), so "boneCount==0" finds
    // nothing (the first pin attempt classified all 15 meshes as arms -> the pin was a
    // no-op). Real test: a gun part is unskinned OR rigid (every vertex 100%% on the
    // same single bone); the arms are BLENDED across many bones.
    w->gunMask=0ULL;
    for (int k=0;k<w->model.meshCount && k<64;k++){
        Mesh *ms=&w->model.meshes[k];
        if (ms->vertexCount<=0 || !ms->vertices) continue;
        int isGun=0;
        if (!ms->boneIndices || !ms->boneWeights) isGun=1;            // no skin data at all -> static gun part
        else {
            int rigid=1, rb=-2;                                       // rb: -2 unset, -1 unweighted, >=0 the single bone
            for (int v=0;v<ms->vertexCount && rigid;v++){
                int best=-1; float bw=0.0f;
                for (int c=0;c<4;c++){ float wt=ms->boneWeights[v*4+c]; if (wt>bw){ bw=wt; best=(int)ms->boneIndices[v*4+c]; } }
                int vb = (bw>=0.99f) ? best : ((bw<=0.01f) ? -1 : -3); // -3 = genuinely blended vertex
                if (vb==-3){ rigid=0; break; }
                if (rb==-2) rb=vb; else if (vb!=rb) rigid=0;          // spans multiple bones -> organic arms
            }
            isGun=rigid;
        }
        if (isGun) w->gunMask |= (1ULL<<k);
    }
    // Bind-space centre of the gun meshes. The pinned draw seats this point at the
    // palms, so the gun starts in the hands instead of wherever the artist parked it;
    // the .pin grip trim then nudges from there.
    w->gunBindC=(Vector3){0,0,0};
    { int nstat=0; Vector3 acc={0,0,0};
      for (int k=0;k<w->model.meshCount && k<64;k++){
        Mesh *ms=&w->model.meshes[k];
        if (!((w->gunMask>>k)&1ULL)) continue;
        Vector3 mn={1e9f,1e9f,1e9f}, mx={-1e9f,-1e9f,-1e9f};
        for (int v=0;v<ms->vertexCount;v++){
            float X=ms->vertices[v*3], Y=ms->vertices[v*3+1], Z=ms->vertices[v*3+2];
            if(X<mn.x)mn.x=X; if(Y<mn.y)mn.y=Y; if(Z<mn.z)mn.z=Z;
            if(X>mx.x)mx.x=X; if(Y>mx.y)mx.y=Y; if(Z>mx.z)mx.z=Z;
        }
        acc.x+=(mn.x+mx.x)*0.5f; acc.y+=(mn.y+mx.y)*0.5f; acc.z+=(mn.z+mx.z)*0.5f; nstat++;
      }
      if (nstat>0) w->gunBindC=(Vector3){acc.x/nstat,acc.y/nstat,acc.z/nstat};
      // Auto-seat + auto-align: the bind data shows the palms in their hold pose, so by
      // default seat the gun's CENTRE at the palm MIDPOINT and rotate its principal
      // (long) axis onto the palm-to-palm axis -- the gun then spans both hands like a
      // real two-handed hold instead of hanging centred on one palm.
      if (w->handBone>=0 && w->model.skeleton.bindPose){
        Vector3 R=w->model.skeleton.bindPose[w->handBone].translation;
        w->pinSeat=R;
        if (nstat>0 && w->handBoneL>=0){
            Vector3 Lp=w->model.skeleton.bindPose[w->handBoneL].translation;
            w->pinSeat=(Vector3){(R.x+Lp.x)*0.5f,(R.y+Lp.y)*0.5f,(R.z+Lp.z)*0.5f};
            double C[3][3]={{0,0,0},{0,0,0},{0,0,0}}; long nv=0;   // covariance of the gun verts
            for (int k=0;k<w->model.meshCount && k<64;k++){
                Mesh *ms=&w->model.meshes[k];
                if (!((w->gunMask>>k)&1ULL)) continue;
                for (int v=0;v<ms->vertexCount;v++){
                    double dx=ms->vertices[v*3]-w->gunBindC.x, dy=ms->vertices[v*3+1]-w->gunBindC.y, dz=ms->vertices[v*3+2]-w->gunBindC.z;
                    C[0][0]+=dx*dx; C[0][1]+=dx*dy; C[0][2]+=dx*dz;
                    C[1][1]+=dy*dy; C[1][2]+=dy*dz; C[2][2]+=dz*dz; nv++;
                }
            }
            C[1][0]=C[0][1]; C[2][0]=C[0][2]; C[2][1]=C[1][2];
            if (nv>0){
                double ax=1.0,ay=0.31,az=0.17;                      // power iteration -> dominant eigenvector = the gun's long axis
                for (int it=0;it<32;it++){
                    double nx=C[0][0]*ax+C[0][1]*ay+C[0][2]*az;
                    double ny=C[1][0]*ax+C[1][1]*ay+C[1][2]*az;
                    double nz=C[2][0]*ax+C[2][1]*ay+C[2][2]*az;
                    double L2=sqrt(nx*nx+ny*ny+nz*nz); if (L2<1e-12) break;
                    ax=nx/L2; ay=ny/L2; az=nz/L2;
                }
                Vector3 u={(float)ax,(float)ay,(float)az};
                Vector3 vdir=Vector3Normalize(Vector3Subtract(Lp,R));   // rear (right) palm -> front (left) palm
                if (Vector3DotProduct(u,vdir)<0.0f) u=Vector3Negate(u);
                Vector3 axv=Vector3CrossProduct(u,vdir); float al=Vector3Length(axv);
                float dot=Vector3DotProduct(u,vdir); if(dot>1)dot=1; if(dot<-1)dot=-1;
                if (al>1e-4f) w->pinAlign=MatrixRotate(Vector3Scale(axv,1.0f/al), acosf(dot));
            }
        }
      }
      // Measured from grenadelauncher.glb itself: the AUTHORED scene already has the
      // launcher in the hands (gun centre [0.0,11.0,19.9] -- 9.3 units from the palm
      // midpoint, the exact grip the hand animation was authored around). If raylib's
      // bake lands the gun there, the authored placement IS the grip: keep it verbatim
      // and just ride it on the hand bone, instead of re-seating by centroid/PCA.
      { Vector3 authoredC={0.02f,11.0f,19.88f};
        w->pinAuthored = (nstat>0 && w->handBone>=0 && Vector3Distance(w->gunBindC,authoredC)<3.0f); }
      DebugLog("gunpin","\"slot\":%d,\"gunMeshes\":%d,\"mask\":%llu,\"handR\":%d,\"handL\":%d,\"authored\":%d,\"gunC\":[%.1f,%.1f,%.1f],\"seat\":[%.1f,%.1f,%.1f]",
               slot, nstat, w->gunMask, w->handBone, w->handBoneL, w->pinAuthored, w->gunBindC.x, w->gunBindC.y, w->gunBindC.z, w->pinSeat.x, w->pinSeat.y, w->pinSeat.z);
    }
    LoadPin(w->tunePath,&w->pinOff,&w->pinScale,&w->pinYaw,&w->pinPitch,&w->pinRoll,&w->armsROff,&w->armsLOff);
    ApplyArmsSideDelta(w, w->armsROff, 1);    // bake the saved per-hand nudges into the arm verts (no-op when zero)
    ApplyArmsSideDelta(w, w->armsLOff, -1);
    // Resolve clips by name with priorities (first match wins per role): a real
    // "idle" beats take/draw/hold; "shot" counts as a fire clip; the plain
    // "reload" beats variants like reload_full. Handles e.g. AK_Idle/Shot/Reload.
    int rIdle=-1, rIdleAlt=-1, rShoot=-1, rReload=-1, rWalk=-1;
    for (int i=0;i<w->animN;i++){
        const char *nm=w->anim[i].name; char low[64]; int j=0;
        for (; nm[j] && j<63; j++){ char c=nm[j]; if(c>='A'&&c<='Z') c+=32; low[j]=c; }
        low[j]=0;
        if (strstr(low,"idle")){ if(rIdle<0) rIdle=i; }
        else if (strstr(low,"take")||strstr(low,"draw")||strstr(low,"weild")||strstr(low,"wield")||strstr(low,"hold")){ if(rIdleAlt<0) rIdleAlt=i; }
        if ((strstr(low,"shoot")||strstr(low,"fire")||strstr(low,"shot")) && rShoot<0) rShoot=i;
        if (strstr(low,"reload") && rReload<0) rReload=i;
        if ((strstr(low,"walk")||strstr(low,"run")) && rWalk<0) rWalk=i;
    }
    w->aWalk = (rWalk>=0)?rWalk:-1;   // -1 = no walk clip (most guns); set below after aIdle resolves
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
    // idleHold (the grip frame) still lives in the legacy tune file; its absolute
    // transform is ignored -- the auto frame replaced it. A trim file, once saved,
    // carries idleHold too and wins.
    w->idleHold=-1;
    { Vector3 o; float a,b,c,d; LoadTune(w->tunePath,&o,&a,&b,&c,&d,&w->idleHold); }
    { int ih=w->idleHold; if (LoadTune(w->trimPath,&w->off,&w->scale,&w->yaw,&w->pitch,&w->roll,&ih)) w->idleHold=(ih>=0)?ih:w->idleHold; }
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

// Next free particle, searched from a moving cursor (a full array costs one pass, not one per particle).
static int g_sparkCur=0;
static Spark *SparkAlloc(void){
    for (int n=0;n<MAX_SPARKS;n++){
        int s=g_sparkCur; g_sparkCur=(g_sparkCur+1)%MAX_SPARKS;
        if (g_sparks[s].life<=0) return &g_sparks[s];
    }
    return NULL;
}

// ---- Settled blood -----------------------------------------------------------
// Blood that has landed never moves again, so it is baked once into a GPU mesh and drawn with a
// single call. It used to be re-sent every frame as thousands of small cylinders, which cost more
// than the rest of the frame once a fight got going, and it filled the particle array for good.
// Two rings share the mesh: fine spatter gobs and the larger pools. A full ring overwrites its
// oldest stain, so the floor keeps taking new blood for the whole run.
#define DISC_MAXSIDES 12
static float g_discCos[DISC_MAXSIDES+1][DISC_MAXSIDES+1], g_discSin[DISC_MAXSIDES+1][DISC_MAXSIDES+1];
static void DiscTable(void){
    static int built=0; if (built) return; built=1;
    for (int n=3;n<=DISC_MAXSIDES;n++) for (int i=0;i<=n;i++){
        float a=(float)i*6.2831853f/(float)n; g_discCos[n][i]=cosf(a); g_discSin[n][i]=sinf(a); }
}
#define STAIN_GOBS    4096
#define STAIN_GOB_V   48                                  // 2 discs x 8 triangles
#define STAIN_POOLS   256
#define STAIN_POOL_N  26
#define STAIN_POOL_V  (STAIN_POOL_N*10*3)                 // 26 discs x 10 triangles
#define STAIN_POOL0   (STAIN_GOBS*STAIN_GOB_V)
#define STAIN_VERTS   (STAIN_POOL0 + STAIN_POOLS*STAIN_POOL_V)
static Mesh     g_stainMesh;
static Material g_stainMat;
static int g_stainReady=0, g_stainTried=0, g_stainColBuf=-1, g_stainAny=0;
static int g_stainGobNext=0, g_stainPoolNext=0;
static int g_stainLo[2]={STAIN_VERTS,STAIN_VERTS}, g_stainHi[2]={0,0};   // vertex ranges waiting for upload (gobs, pools)

static int StainInit(void){
    if (g_stainReady) return 1;
    if (g_stainTried || !IsWindowReady()) return 0;
    g_stainTried=1; DiscTable();
    g_stainMesh=(Mesh){0};
    g_stainMesh.vertexCount=STAIN_VERTS; g_stainMesh.triangleCount=STAIN_VERTS/3;
    g_stainMesh.vertices=(float*)MemAlloc(STAIN_VERTS*3*sizeof(float));          // zeroed: unused slots are empty triangles
    g_stainMesh.colors=(unsigned char*)MemAlloc(STAIN_VERTS*4);
    UploadMesh(&g_stainMesh, true);
    for (int i=1;i<7;i++) if (g_stainMesh.vboId && g_stainMesh.vboId[i]!=0){ g_stainColBuf=i; break; }   // the colour buffer's slot
    g_stainMat=LoadMaterialDefault();
    g_stainReady=(g_stainMesh.vboId && g_stainMesh.vboId[0]!=0 && g_stainColBuf>0);
    return g_stainReady;
}
// One face-up disc into the mesh at vertex *vi (centre, later rim point, earlier rim point = facing up).
static void StainDisc(int *vi, float x, float y, float z, float r, int sides, Color c){
    float *v=g_stainMesh.vertices+(*vi)*3; unsigned char *k=g_stainMesh.colors+(*vi)*4;
    for (int i=0;i<sides;i++){
        v[0]=x; v[1]=y; v[2]=z;
        v[3]=x+g_discCos[sides][i+1]*r; v[4]=y; v[5]=z+g_discSin[sides][i+1]*r;
        v[6]=x+g_discCos[sides][i]*r;   v[7]=y; v[8]=z+g_discSin[sides][i]*r;
        for (int q=0;q<3;q++){ k[q*4]=c.r; k[q*4+1]=c.g; k[q*4+2]=c.b; k[q*4+3]=c.a; }
        v+=9; k+=12;
    }
    *vi+=sides*3;
}
static void StainMark(int ring, int lo, int hi){
    if (lo<g_stainLo[ring]) g_stainLo[ring]=lo;
    if (hi>g_stainHi[ring]) g_stainHi[ring]=hi;
    g_stainAny=1;
}
// A settled gob: a small lumpy spatter dot, two offset discs trailing ahead of the impact.
static void StainAddGob(const Spark *sp){
    if (!StainInit()) return;
    int slot=g_stainGobNext; g_stainGobNext=(slot+1)%STAIN_GOBS;
    int vi=slot*STAIN_GOB_V, v0=vi;
    float ax=sp->ix, az=sp->iz; if (ax==0.0f && az==0.0f){ ax=1.0f; az=0.0f; }
    float y=sp->rest+0.016f+(float)(slot&7)*0.001f;       // a thin film on the floor; stepped so overlapping dots never fight for depth
    unsigned int s=sp->seed?sp->seed:1u;
    Color col={ (unsigned char)(sp->col.r*0.62f), (unsigned char)(sp->col.g*0.5f), (unsigned char)(sp->col.b*0.5f), sp->col.a };   // landed blood is darker than the spray: unlit bright red glowed on a dim floor
    for (int b=0;b<2;b++){                                // offset discs break the circular outline
        unsigned int h=s*2654435761u + (unsigned int)b*2246822519u;
        float t1=(float)((h>>3)&255)/255.0f, t2=(float)((h>>11)&255)/255.0f, t3=(float)((h>>19)&255)/255.0f;
        float along=(t1*1.4f-0.2f)*sp->size;              // mostly forward of impact (the spatter trails ahead)
        float side =(t2-0.5f)*1.2f*sp->size;              // lateral scatter
        float ox=ax*along - az*side, oz=az*along + ax*side;
        float r=sp->size*(0.30f+0.30f*t3);                // small, varied
        StainDisc(&vi, sp->pos.x+ox, y, sp->pos.z+oz, r, 8, col);
    }
    StainMark(0, v0, vi);
}
// A pool: many small overlapping discs filling a ragged circle, which reads as one wet stain.
static void StainAddPool(float x, float floorY, float z, float R, unsigned int seed, Color col){
    if (!StainInit()) return;
    int slot=g_stainPoolNext; g_stainPoolNext=(slot+1)%STAIN_POOLS;
    int vi=STAIN_POOL0+slot*STAIN_POOL_V, v0=vi;
    float y=floorY+0.007f+(float)(slot&7)*0.001f;         // under the spatter dots, stepped like them
    unsigned int s=seed?seed:1u;
    for (int b=0;b<STAIN_POOL_N;b++){
        unsigned int h=s*2654435761u + (unsigned int)b*2654435789u; h^=h>>13; h*=0x9E3779B1u;
        float t1=(float)((h>>3)&1023)/1023.0f, t2=(float)((h>>13)&1023)/1023.0f, t3=(float)((h>>23)&255)/255.0f;
        float a=t1*6.2831853f, rr=sqrtf(t2)*R;            // sqrt -> evenly fill the disc area
        float dr=R*(0.34f+0.16f*t3)*(1.0f-0.45f*(rr/R));  // solid centre, smaller discs toward the ragged edge
        StainDisc(&vi, x+cosf(a)*rr, y, z+sinf(a)*rr, dr, 10, col);
    }
    StainMark(1, v0, vi);
}
// Send whatever was added since the last call to the GPU: at most one range per ring.
static void StainFlush(void){
    if (!g_stainReady) return;
    for (int r=0;r<2;r++) if (g_stainHi[r]>g_stainLo[r]){
        int lo=g_stainLo[r], n=g_stainHi[r]-lo;
        UpdateMeshBuffer(g_stainMesh, 0, g_stainMesh.vertices+lo*3, n*3*(int)sizeof(float), lo*3*(int)sizeof(float));
        UpdateMeshBuffer(g_stainMesh, g_stainColBuf, g_stainMesh.colors+lo*4, n*4, lo*4);
        g_stainLo[r]=STAIN_VERTS; g_stainHi[r]=0;
    }
}
static void StainDraw(void){
    if (!g_stainReady || !g_stainAny) return;
    StainFlush();
    DrawMesh(g_stainMesh, g_stainMat, MatrixIdentity());
}
// New map: the old floor's blood goes with it.
static void StainReset(void){
    memset(g_sparks,0,sizeof g_sparks);
    if (!g_stainReady) return;
    memset(g_stainMesh.vertices,0,STAIN_VERTS*3*sizeof(float));
    g_stainGobNext=0; g_stainPoolNext=0;
    g_stainLo[0]=0; g_stainHi[0]=STAIN_POOL0; g_stainLo[1]=STAIN_POOL0; g_stainHi[1]=STAIN_VERTS;
    StainFlush(); g_stainAny=0;
}

// Scorch decals: persistent soot marks the flamethrower burns onto whatever it hits.
// Each is a cluster of flat dark discs lying on the surface (oriented to its normal).
typedef struct { Vector3 pos, n; float r; unsigned int seed; int active; } Scorch;
#define MAX_SCORCH 192
static Scorch g_scorch[MAX_SCORCH];
static int    g_scorchNext=0;
static float  g_scorchT=0.0f;            // throttle so the flame lays marks at a steady rate, not every frame
static void AddScorch(Vector3 p, Vector3 n, float r){
    g_scorch[g_scorchNext]=(Scorch){ p, Vector3Normalize(n), r, (unsigned int)GetRandomValue(1,1<<30), 1 };
    g_scorchNext=(g_scorchNext+1)%MAX_SCORCH;
}
// Bullet holes: every round that hits the arena leaves a dark pit with a pale chipped rim.
// A ring buffer -- the oldest give way once a few hundred are on the walls.
typedef struct { Vector3 pos, n; float r; int active; } Hole;
#define MAX_HOLES 320
static Hole g_holes[MAX_HOLES]; static int g_holeNext=0;
static void AddHole(Vector3 p, Vector3 n){
    g_holes[g_holeNext]=(Hole){ p, Vector3Normalize(n), 0.022f+GetRandomValue(0,14)/1000.0f, 1 };
    g_holeNext=(g_holeNext+1)%MAX_HOLES;
}
// Spent brass: thrown out of the action to the right, tumbles, bounces once or twice, lies there.
typedef struct { Vector3 pos, vel, ax; float ang, spin, life, floorY, len, rad; Color col; int bounces; } Casing;
#define MAX_CASINGS 64
static Casing g_casings[MAX_CASINGS]; static int g_casingNext=0;
static void CasingsUpdate(float dt){
    for (int i=0;i<MAX_CASINGS;i++){ Casing *c=&g_casings[i]; if (c->life<=0) continue;
        c->life-=dt;
        if (c->bounces>=3) continue;                           // at rest
        c->vel.y-=14.0f*dt; c->pos=Vector3Add(c->pos,Vector3Scale(c->vel,dt)); c->ang+=c->spin*dt;
        if (c->pos.y<=c->floorY+c->rad && c->vel.y<0){
            c->pos.y=c->floorY+c->rad; c->bounces++;
            if (c->bounces==1 && g_hasSy){ SetSoundPitch(g_sy[SY_CLINK], (c->rad>0.008f?0.55f:0.9f)+GetRandomValue(0,25)/100.0f); SfxAt(g_sy[SY_CLINK], c->pos, 0.22f); }
            if (c->bounces>=3 || fabsf(c->vel.y)<0.9f){ c->bounces=3; c->vel=(Vector3){0,0,0}; c->ax=(Vector3){0,1,0}; }   // settles lying flat
            else { c->vel.y*=-0.38f; c->vel.x*=0.55f; c->vel.z*=0.55f; c->spin*=0.6f; }
        }
    }
}

// Minigun barrel smoke/steam: soft camera-facing billboards that rise, expand and fade.
// hot=1 firing (darker, faster gunsmoke); hot=0 spin-down/cooling (white steam).
typedef struct { Vector3 pos, vel; float life, life0, sz0, sz1; int hot; int flame; int liquid; } Smoke;
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
// Flamethrower jet: bright billboards shot FORWARD along the aim, with spread, that
// decelerate and curl up as they age. Reuses the smoke pool/draw (flame flag picks
// the additive fire gradient). o=muzzle, fwd=aim direction.
static void SpawnFlame(Vector3 o, Vector3 fwd){
    for (int s=0;s<MAX_SMOKE;s++){
        if (g_smoke[s].life>0) continue;
        Smoke p={0};
        p.pos=o;
        float spd=8.0f+GetRandomValue(0,50)/10.0f;
        Vector3 jit={GetRandomValue(-30,30)/100.0f,GetRandomValue(-30,30)/100.0f,GetRandomValue(-30,30)/100.0f};
        p.vel=Vector3Add(Vector3Scale(fwd,spd), Vector3Scale(jit,9.0f));   // strong lateral spread -> a fat, fanning gout
        p.vel.y += 0.6f;                                       // the jet licks upward as it travels
        p.life0=0.34f+GetRandomValue(0,22)/100.0f; p.life=p.life0;  // short-lived: a lick of fire, not lingering smoke
        p.sz0=0.16f+GetRandomValue(0,8)/100.0f;
        p.sz1=1.05f+GetRandomValue(0,55)/100.0f;               // billow large as it burns out -> a wide wall of fire
        p.hot=1; p.flame=1;
        g_smoke[s]=p; return;
    }
}
// Enemy gunfire: a two-frame burst of flame at the barrel and a wisp of smoke, so a shot
// has a visible source and not just a tracer appearing from his chest.
static void SpawnMuzzleFlash(Vector3 o, Vector3 dir){
    int made=0;
    for (int s=0;s<MAX_SMOKE && made<2;s++){
        if (g_smoke[s].life>0) continue;
        Smoke p={0};
        p.pos=Vector3Add(o, Vector3Scale(dir, 0.10f+0.14f*made));
        p.vel=Vector3Scale(dir, 2.5f);
        p.life0=0.055f+GetRandomValue(0,25)/1000.0f; p.life=p.life0;
        p.sz0=(made?0.16f:0.30f)+GetRandomValue(0,8)/100.0f; p.sz1=p.sz0*1.35f;
        p.hot=1; p.flame=1;
        g_smoke[s]=p; made++;
    }
    SpawnSmoke(Vector3Add(o, Vector3Scale(dir,0.15f)), 1);
    DynLightAdd(Vector3Add(o, Vector3Scale(dir,0.3f)), (Vector3){1.5f,1.0f,0.50f}, 9.0f, 0.07f, 0);
}
// Unignited fuel: the flamethrower's warm-up spit -- a pale liquid jet that arcs DOWN
// under gravity (heavy, not buoyant) and does no damage. Same nozzle/aim as the flame.
static void SpawnLiquid(Vector3 o, Vector3 fwd){
    for (int s=0;s<MAX_SMOKE;s++){
        if (g_smoke[s].life>0) continue;
        Smoke p={0};
        p.pos=o;
        float spd=6.0f+GetRandomValue(0,30)/10.0f;             // a bit slower/heavier than lit flame
        Vector3 jit={GetRandomValue(-12,12)/100.0f,GetRandomValue(-12,12)/100.0f,GetRandomValue(-12,12)/100.0f};
        p.vel=Vector3Add(Vector3Scale(fwd,spd), Vector3Scale(jit,3.0f));
        p.life0=0.45f+GetRandomValue(0,25)/100.0f; p.life=p.life0;  // lives a touch longer (it falls, doesn't burn up)
        p.sz0=0.05f+GetRandomValue(0,4)/100.0f;
        p.sz1=0.16f+GetRandomValue(0,10)/100.0f;               // a fine mist, doesn't billow like fire
        p.hot=0; p.liquid=1;
        g_smoke[s]=p; return;
    }
}
static void UpdateSmoke(float dt){
    for (int s=0;s<MAX_SMOKE;s++){ Smoke *p=&g_smoke[s]; if (p->life<=0) continue;
        p->life -= dt;
        if (p->liquid){                                        // unignited fuel: heavy, falls + air drag
            p->vel.y -= 9.0f*dt;
            p->vel = Vector3Scale(p->vel, 1.0f-2.4f*dt);
        } else {
            p->vel.y += 0.5f*dt;                               // smoke/flame: buoyancy
            if (p->flame) p->vel = Vector3Scale(p->vel, 1.0f-3.2f*dt);  // the fire jet decelerates fast (air drag)
            else { p->vel.x *= (1.0f-1.6f*dt); p->vel.z *= (1.0f-1.6f*dt); } // smoke: horizontal drag only
        }
        p->pos = Vector3Add(p->pos, Vector3Scale(p->vel,dt));
    }
}
// Contact shadows: a soft dark disc on the floor under every standing enemy. Without one a
// figure floats -- nothing ties the feet to the ground. Queued while the enemies draw, then
// blended in one batch (depth-tested, no depth write) so walls and bodies still cover them.
#define MAX_BLOBS 96
static Vector3 g_blobPos[MAX_BLOBS]; static float g_blobR[MAX_BLOBS], g_blobA[MAX_BLOBS]; static int g_blobN=0;
static Texture2D g_blobTex={0};
static void GroundBlob(Vector3 at, float r, float a){
    if (g_blobN<MAX_BLOBS){ g_blobPos[g_blobN]=at; g_blobR[g_blobN]=r; g_blobA[g_blobN]=a; g_blobN++; }
}
static void DrawGroundBlobs(void){
    if (g_blobN<=0) return;
    if (!g_blobTex.id){
        Image im=GenImageGradientRadial(64,64,0.0f,(Color){0,0,0,255},(Color){0,0,0,0});
        g_blobTex=LoadTextureFromImage(im); UnloadImage(im); SetTextureFilter(g_blobTex,TEXTURE_FILTER_BILINEAR);
    }
    rlDrawRenderBatchActive(); rlDisableDepthMask();
    rlSetTexture(g_blobTex.id);
    rlBegin(RL_QUADS);
    for (int i=0;i<g_blobN;i++){
        float x=g_blobPos[i].x, y=g_blobPos[i].y+0.025f, z=g_blobPos[i].z, r=g_blobR[i];
        rlColor4ub(255,255,255,(unsigned char)(Clamp(g_blobA[i],0.0f,1.0f)*255.0f)); rlNormal3f(0,1,0);
        rlTexCoord2f(0,0); rlVertex3f(x-r,y,z-r);
        rlTexCoord2f(0,1); rlVertex3f(x-r,y,z+r);
        rlTexCoord2f(1,1); rlVertex3f(x+r,y,z+r);
        rlTexCoord2f(1,0); rlVertex3f(x+r,y,z-r);
    }
    rlEnd(); rlSetTexture(0);
    rlDrawRenderBatchActive(); rlEnableDepthMask();
    g_blobN=0;
}
static void DrawSmoke(Camera3D cam){
    if (!g_hasSmokeTex) return;
    rlDisableDepthMask();                                      // particles blend, don't occlude each other
    for (int s=0;s<MAX_SMOKE;s++){ Smoke *p=&g_smoke[s]; if (p->life<=0 || p->flame) continue;
        float age=1.0f-p->life/p->life0;                       // 0..1
        float sz=p->sz0+(p->sz1-p->sz0)*age;
        float fade=p->life/p->life0; fade*=fade;               // ease-out fade
        Color tint;
        if (p->liquid) tint=(Color){210,200,150,(unsigned char)(fade*150)};  // pale amber atomised fuel
        else { unsigned char g=p->hot?90:205; tint=(Color){g,g,g,(unsigned char)(fade*(p->hot?150:120))}; } // gunsmoke grey vs white steam
        DrawBillboard(cam, g_smokeTex, p->pos, sz, tint);
    }
    // Flame pass: additive so overlapping licks build into a bright hot core, and a
    // young->old colour ramp (white-yellow -> orange -> deep red) that reads as fire.
    BeginBlendMode(BLEND_ADDITIVE);
    for (int s=0;s<MAX_SMOKE;s++){ Smoke *p=&g_smoke[s]; if (p->life<=0 || !p->flame) continue;
        float age=1.0f-p->life/p->life0;                       // 0..1
        float sz=p->sz0+(p->sz1-p->sz0)*age;
        float fade=p->life/p->life0;                           // linear fade-out
        unsigned char a=(unsigned char)(fade*200.0f);
        Color tint = (age<0.25f) ? (Color){255,240,180,a}     // ignition: hot white-yellow
                   : (age<0.60f) ? (Color){255,140,40,a}      // body: orange
                                 : (Color){170,35,20,a};       // dying: deep red
        DrawBillboard(cam, g_smokeTex, p->pos, sz, tint);
    }
    EndBlendMode();
    rlEnableDepthMask();
}
// Soot decals: each scorch is a ragged cluster of flat near-black discs lying ON the
// burned surface (built in the surface's tangent plane, oriented to its normal), so
// overlapping marks build up grime. Depth-tested with the world so walls occlude them.
static void DrawScorch(void){
    DiscTable();
    rlBegin(RL_TRIANGLES);
    for (int i=0;i<MAX_SCORCH;i++){
        Scorch *s=&g_scorch[i]; if (!s->active) continue;
        Vector3 n=s->n;
        Vector3 up = (fabsf(n.y)>0.9f) ? (Vector3){1,0,0} : (Vector3){0,1,0};
        Vector3 T=Vector3Normalize(Vector3CrossProduct(up,n));
        Vector3 B=Vector3CrossProduct(n,T);
        unsigned int sd=s->seed?s->seed:1u;
        for (int b=0;b<7;b++){                                  // overlapping discs -> a sooty, irregular smudge
            unsigned int h=sd*2654435761u + (unsigned int)b*2246822519u; h^=h>>13; h*=0x9E3779B1u;
            float t1=(float)((h>>3)&1023)/1023.0f, t2=(float)((h>>13)&1023)/1023.0f, t3=(float)((h>>23)&255)/255.0f;
            float a=t1*6.2831853f, rr=sqrtf(t2)*s->r;
            Vector3 c=Vector3Add(s->pos, Vector3Add(Vector3Scale(T,cosf(a)*rr), Vector3Scale(B,sinf(a)*rr)));
            float dr=s->r*(0.30f+0.22f*t3)*(1.0f-0.4f*(rr/s->r));   // solid centre, smaller flecks toward the edge
            Vector3 p1=Vector3Add(c, Vector3Scale(n, 0.028f));     // float just off the surface (no z-fight)
            rlColor4ub(18,15,13,(unsigned char)(110+(int)(t3*60)));
            for (int q=0;q<10;q++){                                // one flat fan facing out of the surface
                float c0=g_discCos[10][q]*dr, s0=g_discSin[10][q]*dr, c1=g_discCos[10][q+1]*dr, s1=g_discSin[10][q+1]*dr;
                rlVertex3f(p1.x,p1.y,p1.z);
                rlVertex3f(p1.x+T.x*c0+B.x*s0, p1.y+T.y*c0+B.y*s0, p1.z+T.z*c0+B.z*s0);
                rlVertex3f(p1.x+T.x*c1+B.x*s1, p1.y+T.y*c1+B.y*s1, p1.z+T.z*c1+B.z*s1);
            }
        }
    }
    rlEnd();
}

// ---- Enemies (assets/<type>.glb -- MakeHuman + CMU mocap, built by tools/chef/build_enemy.py) ---
// One shared Model drawn many times; each enemy keeps its own animation cursor
// so they don't march in lockstep. Mixamo export is cm-scale (~185u tall) with
// feet at the origin facing +Z, so ENEMY_SCALE shrinks to ~1.85m and we yaw
// each to face the player. ENEMY_YAW_OFFSET flips facing if they moonwalk.
#define MAX_ENEMIES        24     // hard cap (M2 perf-gated); the wave director's alive-cap stays below it
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
#define ENEMY_TOUCH_DMG    18.0f      // player hp/sec in melee -- LEGACY untimed drain, kept for A/B via --melee-drain
#define ENEMY_MELEE_DMG    16.0f      // hp per landed swing (timed attack window -- the default)
#define ATK_STRIKE_FRAC    0.45f      // fraction through the attack clip where the swing lands (the dodge window is everything before it)
#define ENEMY_BLEND        0.15f      // seconds of crossfade on every clip change (no pose snaps)
#define DUAL_STRIKE_A      0.28f      // two-weapon attack (synth_dual in the builder): right blade lands...
#define DUAL_STRIKE_B      0.78f      // ...then the left
#define DUAL_DMG           0.6f       // each blade: 60% of a single chop
#define ATK_COOLDOWN       1.2f       // min seconds between swings that can hurt
#define ENEMY_ATTACK_RANGE  1.8f       // enters attack/melee within this range
// Headshot zone: a narrower box at the TOP of the enemy silhouette. A shot
// whose aim ray pierces it deals HEADSHOT_MULT x damage. Sized for the Mixamo
// humanoid (~1.85m): the head/upper-neck sits in the top ~0.32m, narrower than
// the 0.5 body radius. 3x of ENEMY_DMG_PER_SHOT (34) = 102 >= ENEMY_HP, so a
// clean headshot one-shot-kills a full-HP enemy (the classic FPS payoff).
#define HEAD_ZONE_H        0.32f      // vertical extent of the head box (down from the top)
#define HEAD_RADIUS        0.22f      // half-width of the head box (tighter than the body)
#define HEADSHOT_MULT      3.0f       // headshot damage multiplier
// Flamethrower (slot 5): a continuous cone of fire. Burns any enemy inside the
// cone + range while the trigger is held, no per-shot cooldown.
#define FLAME_RANGE        8.5f       // reach of the fire cone (units)
#define FLAME_COS          0.80f      // cos of the half-angle (~37deg): a broad billowing gout, not a thin jet
#define FLAME_DPS          85.0f      // damage/sec to an enemy fully in the cone (~1.2s to kill at 100hp)
// Melee (no weapon uses it since the knife was cut; kept for a future melee rig): a short, wide melee swing on each click.
#define KNIFE_RANGE        2.6f       // lunge reach (units)
#define KNIFE_COS          0.55f      // cos half-angle (~57deg): a wide slash, forgiving to aim
#define KNIFE_DMG          75.0f      // per swing; two slashes (or a clean one on a softened enemy) kills
#define KNIFE_CD           0.42f      // seconds between swings (the slash cadence)
#define FLAME_FUEL_PER_SEC 9.0f       // fuel units burned per second while the stream is lit (tank = magSize)
#define MAX_BURN 8       // localised flame-char marks per body (height up the body + how dark/wide that spot is)
// Wave enemy types (M2): tint+scale variants of the one Mixamo rig. cost = wave-
// budget points. The NAV/collision radii (NAV_WALLR/ENEMY_MOVE_R) stay IDENTICAL
// for every type -- that tuning is fragile and afterslime-calibrated -- only the
// drawn model and the hitscan hitbox scale per type.
typedef enum { EN_COOK=0, EN_LINECOOK=1, EN_BUTCHER=2, EN_LIQUIDATOR=3, EN_HEADCHEF=4,
               EN_RIFLEMAN=5, EN_GUNNER=6, EN_MUTANT=7, EN_PRIEST=8, EN_REVENANT=9, EN_FIEND=10, EN_NTYPES } EnemyKind;
// attack kinds: melee swing on the clip's strike frame / hitscan shots / slow dodgeable
// radiation bolt / raise the dead (priest). range+rate+burst only matter for ranged kinds.
enum { AK_MELEE=0, AK_HITSCAN=1, AK_BOLT=2, AK_RAISE=3 };
// Each type draws its own model, assets/<name>.glb (tools/chef/build_all.sh). Enemies are
// lit per pixel by the arena lamps (EnemyLightUpload), so tints stay near white.
static const struct { int cost; float hp, speed, meleeDmg, scale; Color tint; const char *name;
                     int kind; float range, rate, shotDmg; int burst; } kEnemy[EN_NTYPES]={
    //  cost   hp    spd  melee  scale  tint               name          kind        range rate  dmg  burst
    {  2,  80.0f, 4.6f, 16.0f, 1.00f, {238,236,232,255}, "cook",       AK_MELEE,    0, 0,    0, 0 },   // canteen cook: the wave-1 staple
    {  3,  50.0f, 6.4f, 10.0f, 1.00f, {238,236,232,255}, "linecook",   AK_MELEE,    0, 0,    0, 0 },   // wiry, knife, outruns a backpedal
    {  6, 260.0f, 2.6f, 30.0f, 1.05f, {238,236,232,255}, "butcher",    AK_MELEE,    0, 0,    0, 0 },   // slow wall of meat, hits like a truck
    {  4, 140.0f, 3.8f, 22.0f, 1.00f, {238,236,232,255}, "liquidator", AK_MELEE,    0, 0,    0, 0 },   // gas mask + shovel: tough mid-liner
    { 14, 800.0f, 3.2f, 40.0f, 1.35f, {238,236,232,255}, "headchef",   AK_MELEE,    0, 0,    0, 0 },   // the giant: between-waves BOSS
    {  4, 110.0f, 4.2f, 12.0f, 1.00f, {238,236,232,255}, "rifleman",   AK_HITSCAN, 26, 1.6f, 12, 1 },   // militia rifleman: aimed single shots
    {  6, 140.0f, 3.6f, 14.0f, 1.00f, {238,236,232,255}, "gunner",     AK_HITSCAN, 22, 2.8f,  6, 5 },   // machine-gunner: 5-round bursts
    {  4, 100.0f, 4.6f, 14.0f, 1.00f, {238,236,232,255}, "mutant",     AK_BOLT,    24, 2.2f, 16, 1 },   // glowing mutant: slow radiation bolts
    {  8, 220.0f, 3.0f, 10.0f, 1.00f, {238,236,232,255}, "priest",     AK_RAISE,   15, 7.0f,  0, 0 },   // irradiated priest: raises the dead
    {  5, 120.0f, 6.8f, 24.0f, 1.00f, {238,236,232,255}, "revenant",   AK_MELEE,    0, 0,    0, 0 },   // burned firefighter: sprints in with a fire axe
    {  8, 340.0f, 3.4f, 28.0f, 1.06f, {238,236,232,255}, "fiend",      AK_MELEE,  2.8f, 0,   0, 0 },   // tentacled liquidator: tanky, tentacles out-reach a swing
};
static int EnemyDualWield(int t){ return t==EN_COOK || t==EN_BUTCHER || t==EN_HEADCHEF; }
// melee reach: the tentacled fiend lashes from further out (range doubles as reach for AK_MELEE)
#define ENEMY_REACH(t) ((kEnemy[t].kind==AK_MELEE && kEnemy[t].range>0) ? kEnemy[t].range : ENEMY_ATTACK_RANGE)
#define MAX_WOUNDS  10      // bullet wounds kept per body (oldest replaced)
#define ENEMY_MAX_BONES 64  // skinning palette size; also each enemy's stored pose
#define LOCO_BLEND 0.22f       // seconds to fade between gaits (idle / walk / run / limp)
#define LIMP_CLIP_MPS_D 0.52f  // limp take's walking speed if it can't be measured
#define FLAME_FLINCH_GAP 1.5  // seconds between flame-caused flinches (each tick re-flinching froze him)
typedef struct { Vector3 pos; float hp; int state; int clip; float animT; float deathT; float hitT; float roarT; float drawY; float stuckT; float embedT; float fallV; float atkCd; int type; float burnY[MAX_BURN]; float burnR[MAX_BURN]; int burnN; float maxHp; float spd; int boss; float fireT, losT; int los, burstLeft, raiseC; float chanT; int swing, prevClip; float prevT, blendT; int hitRole; double flameFlinchAt;
                 int deathRole; float deathYaw, limpT, knockT, riseT; int charged; float sc, sumT, boltT; int phase; Vector3 pushV;
                 int wN, wNext; int wBone[MAX_WOUNDS]; Vector3 wOff[MAX_WOUNDS]; float wSz[MAX_WOUNDS];   // bullet wounds, pinned to bones
                 struct ModelAnimation *drawAn; float drawFrame, drawYaw;
                 // --- motion (EnemyPose / UpdateEnemies): he has momentum, turns at a rate, and his legs and torso are posed apart
                 Vector3 vel, mv, lastPos;        // vel: commanded ground velocity (accel-limited, takes hit impulses); mv: measured, smoothed
                 float yaw, yawRate, twist, headTw, lean, pitch;   // body facing (deg) + torso/head turned to the player + lean into turns/accel
                 int loco, locoPrev, back; float locoT, locoPrevT, locoBlend, legW;   // lower-body clip (idle/walk/run/limp) + crossfade; legW: legs from loco (1) or the action clip (0)
                 float flank, atkRest, atkRate, stagT, hitAcc, hitAccT, aimT, modeT, lostT; int mode, side, shots, rehit;   // approach side, post-swing rest, stagger, ranged stance
                 int poseOK, fromOK, blendAll; Transform pose[ENEMY_MAX_BONES], from[ENEMY_MAX_BONES]; } Enemy;   // the pose last drawn (wounds are placed on it) // state: 0 dead,1 alive,2 dying; burnY/burnR/burnN = soot marks at the body heights the flame actually touched (NOT a whole-body tint); roarT=attack-roar cooldown; drawY=smoothed render height; stuckT=time chasing-but-not-moving (->idle); embedT=time wedged inside a wall (->respawn); fallV=render-only fall velocity while dropping off a ledge
static Enemy           g_enemies[MAX_ENEMIES];
#define MAX_CORPSES 32
typedef struct { Vector3 pos; float yaw; int active; int type; float burnY[MAX_BURN]; float burnR[MAX_BURN]; int burnN; int role;
                 int wN; int wBone[MAX_WOUNDS]; Vector3 wOff[MAX_WOUNDS]; float wSz[MAX_WOUNDS]; } Corpse;   // lasting dead bodies, posed at the death-anim end; carries the soot marks from when it was alive
static Corpse          g_corpses[MAX_CORPSES];
static int             g_corpseNext = 0;
// Enemy rigs. Clips are addressed by ROLE (CL_*), each rig maps a role to its own
// animation index, so models with different clip orders/frame counts coexist.
enum { CL_WALK=0, CL_IDLE, CL_RUN, CL_HIT, CL_ATTACK, CL_DEATH, CL_CAST,
       CL_HIT_HEAD, CL_HIT_LEFT, CL_HIT_RIGHT, CL_HIT_LEG,
       CL_DEATH_HEAD, CL_DEATH_LEG, CL_DEATH_BLAST, CL_DEATH_RIDDLED, CL_KNOCK, CL_LIMP,
       CL_STRIDE, CL_JOG, CL_NROLES };   // CL_STRIDE / CL_JOG: the gaits between the slow walk and the run (newer models)
       // deaths by cause (headshot drop / legs shot out / thrown by a blast / riddled by a minigun) fall
       // back to CL_DEATH; CL_KNOCK (flattened, gets up) to CL_HIT; CL_LIMP (after a leg hit) to CL_RUN   // CL_HIT = centre mass; the
       // zone flinches (headshot / his left or right side / legs) fall back to CL_HIT on older models   // CL_CAST: spell clip (priest raising the dead); falls back to attack
typedef struct { Model model; ModelAnimation *anim; int animN; int clip[CL_NROLES]; int deathFrame; int settle[CL_NROLES]; int ok;
                 int gpu; Matrix *invBind, *bones;
                 Model lod; int hasLod;
                 // layered posing (RigLayerInit): which bones are torso/arms/head, how much of the chest twist and
                 // head turn each takes, and how fast each gait clip really walks (so feet keep pace with the ground)
                 int layer, bHips, bLB, bNeck, bHandR; unsigned char up[ENEMY_MAX_BONES]; float tw[ENEMY_MAX_BONES], hd[ENEMY_MAX_BONES], mps[CL_NROLES];
                 unsigned found; int gaitN, gait[4]; } EnemyRig;   // found: roles the file really has (bit per role); gait[]: its walking gaits, slowest first
                   // lod: <name>_lod.glb, ~30% triangles, drawn past ENEMY_LOD_DIST   // gpu: skinned in the vertex shader (EnemySkinInit)
static EnemyRig        g_rig[EN_NTYPES];
static EnemyRig *RigOf(int type){ return g_rig[type].ok ? &g_rig[type] : &g_rig[EN_COOK]; }   // missing model -> the cook stands in
static ModelAnimation *RigClip(int type, int role){ EnemyRig *r=RigOf(type); return &r->anim[r->clip[role]]; }
// ---- GPU skinning for enemies ------------------------------------------------
// raylib's CPU skinning re-skins a model and re-uploads its vertex buffer on EVERY
// UpdateModelAnimation. All enemies of a type share one model, so with a full field
// that buffer was rewritten ~24x a frame between draws -- each rewrite forcing a GPU
// sync (profiled: 72% of the frame stalled in glBufferSubData). Instead the bind-pose
// verts stay on the GPU untouched and a vertex shader skins them from a per-draw bone
// matrix array (<= ENEMY_MAX_BONES). raylib's own GPU path is compiled out of this build.
static Shader g_skinShader; static int g_skinBonesLoc=-1, g_skinReady=-1;   // -1 untried, 0 failed, 1 ok
static int g_skinLoc[7]={-1,-1,-1,-1,-1,-1,-1};   // lPos lCol lR lN ambient viewPos fog
static int g_fogOn=0;                          // the arena run fogs enemies; the gallery / menus don't
static int g_skinEmiLoc=-1;
static void EnemyGlowDim(float dim){ if (g_skinReady==1 && g_skinEmiLoc>=0) SetShaderValue(g_skinShader,g_skinEmiLoc,&dim,SHADER_UNIFORM_FLOAT); }   // 0 = eyes lit, 1 = out
static int EnemySkinInit(void){
    if (g_skinReady>=0) return g_skinReady;
    // Skinned + LIT: position and normal skinned by the bone palette, then per-pixel
    // lighting with the same falloff the map bakes into its vertices (ambient +
    // colour * N.L * (1-d/R)) from the 4 strongest unoccluded arena lamps, a soft fill
    // from the player (enemies stay readable in dark corners, as in ../chernobyl) and
    // a faint rim so silhouettes separate from the walls.
    const char *vs="#version 330\n"
        "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor; in vec3 vertexNormal;\n"
        "in vec4 vertexBoneIndices; in vec4 vertexBoneWeights; in vec4 vertexTangent;\n"
        "uniform mat4 mvp; uniform mat4 matModel; uniform mat4 boneMatrices[64];\n"
        "out vec2 fragTexCoord; out vec4 fragColor; out vec3 fragPos; out vec3 fragNormal; out vec4 fragTangent;\n"
        "void main(){\n"
        "  vec4 p=vec4(vertexPosition,1.0), s=vec4(0.0); vec3 n=vec3(0.0), t=vec3(0.0); float wsum=0.0;\n"
        "  for (int i=0;i<4;i++){ float w=vertexBoneWeights[i]; if (w>0.0){ mat4 B=boneMatrices[int(vertexBoneIndices[i])];\n"
        "      s+=w*(B*p); n+=w*(mat3(B)*vertexNormal); t+=w*(mat3(B)*vertexTangent.xyz); wsum+=w; } }\n"
        "  if (wsum<=0.0){ s=p; n=vertexNormal; t=vertexTangent.xyz; } else s/=wsum;\n"
        "  vec4 wp=matModel*vec4(s.xyz,1.0);\n"
        "  fragPos=wp.xyz; fragNormal=normalize(mat3(matModel)*n);\n"
        "  fragTangent=vec4(mat3(matModel)*t, vertexTangent.w);\n"
        "  fragTexCoord=vertexTexCoord; fragColor=vertexColor; gl_Position=mvp*vec4(s.xyz,1.0);\n"
        "}\n";
    // Surface detail (per material, neutral defaults when a model has none -- EnemyRigGPU):
    //   texture2 = tangent-space normal map; texOrm = packed R occlusion, G roughness, B specular;
    //   texEmi = emission (glTF emissiveTexture), added unlit.
    // Occlusion darkens the ambient fully and the lamps by half; specular is a Blinn-Phong
    // highlight per lamp whose tightness follows roughness (wet blood / sweat glint, canvas doesn't).
    const char *fs="#version 330\n"
        "in vec2 fragTexCoord; in vec4 fragColor; in vec3 fragPos; in vec3 fragNormal; in vec4 fragTangent;\n"
        "uniform sampler2D texture0; uniform sampler2D texture2; uniform sampler2D texOrm; uniform sampler2D texEmi; uniform float emiDim; uniform vec4 colDiffuse;\n"
        "uniform vec3 lPos[8]; uniform vec3 lCol[8]; uniform float lR[8]; uniform int lN;\n"
        "uniform vec3 ambient; uniform vec3 viewPos; uniform vec4 fog;\n"
        "out vec4 finalColor;\n"
        "void main(){\n"
        "  vec4 t=texture(texture0, fragTexCoord); if (t.a<0.35) discard;\n"
        "  vec3 Ng=normalize(fragNormal); vec3 V=normalize(viewPos-fragPos);\n"
        "  float side=dot(Ng,V)<0.0?-1.0:1.0; Ng*=side;\n"
        "  vec3 N=Ng; vec3 T=fragTangent.xyz-Ng*dot(Ng,fragTangent.xyz);\n"
        "  if (dot(T,T)>1e-8){ T=normalize(T); vec3 B=cross(Ng,T)*(fragTangent.w<0.0?-1.0:1.0);\n"
        "    vec3 m=texture(texture2,fragTexCoord).xyz*2.0-1.0; m.xy*=side; N=normalize(mat3(T,B,Ng)*m); }\n"
        "  vec4 orm=texture(texOrm,fragTexCoord); float ao=orm.r, rough=orm.g, spi=orm.b, sss=1.0-orm.a;\n"
        "  float shin=mix(96.0,6.0,rough), ndv=max(dot(N,V),0.0);\n"
        "  float fres=0.65+0.35*pow(1.0-ndv,5.0)*4.0;\n"   // wet surfaces flare at grazing angles
        "  vec3 dif=vec3(0.0), spe=vec3(0.0), sub=vec3(0.0);\n"
        "  for (int i=0;i<8;i++){ if (i>=lN) break; vec3 dl=lPos[i]-fragPos; float d=length(dl);\n"
        "    if (d<lR[i]){ vec3 L=dl/max(d,1e-4); float att=(1.0-d/lR[i]); float nl=dot(N,L); float ndl=max(nl,0.0);\n"
        "      dif+=lCol[i]*ndl*att*1.05;\n"
        "      float wrap=max((nl+0.45)/1.45,0.0);\n"   // skin: light bleeds red past the shadow edge
        "      sub+=lCol[i]*max(wrap-ndl,0.0)*att*vec3(1.0,0.32,0.22);\n"
        "      spe+=lCol[i]*pow(max(dot(N,normalize(L+V)),0.0),shin)*att*ndl*(shin+8.0)/40.0; } }\n"
        "  float rim=pow(1.0-ndv,3.0);\n"
        "  float sheen=pow(1.0-ndv,4.0)*clamp((rough-0.78)*4.5,0.0,1.0)*0.30;\n"   // fabric fuzz
        "  vec3 lit=ambient*ao + dif*mix(1.0,ao,0.5) + sub*sss*0.6 + rim*0.22*vec3(0.75,0.8,0.9)*ao + sheen*ao;\n"
        "  spe*=fres;\n"
        "  vec3 c=t.rgb*colDiffuse.rgb*fragColor.rgb*clamp(lit,0.0,1.6) + spe*spi*mix(1.0,ao,0.5);\n"
        "  c+=texture(texEmi,fragTexCoord).rgb*(1.0-emiDim)*1.9;\n"   // self-lit: irradiated irises, mask lenses, embers; emiDim puts it out as he dies
        "  c=mix(c, fog.rgb, (1.0-exp(-length(viewPos-fragPos)*fog.a))*0.85);\n"   // (emission burns through a little)
        "  finalColor=vec4(c, t.a*colDiffuse.a);\n"
        "}\n";
    g_skinShader=LoadShaderFromMemory(vs,fs);
    g_skinReady=0;
    if (g_skinShader.id>0 && g_skinShader.id!=rlGetShaderIdDefault()){
        g_skinBonesLoc=GetShaderLocation(g_skinShader,"boneMatrices");
        g_skinLoc[0]=GetShaderLocation(g_skinShader,"lPos");  g_skinLoc[1]=GetShaderLocation(g_skinShader,"lCol");
        g_skinLoc[2]=GetShaderLocation(g_skinShader,"lR");    g_skinLoc[3]=GetShaderLocation(g_skinShader,"lN");
        g_skinLoc[4]=GetShaderLocation(g_skinShader,"ambient"); g_skinLoc[5]=GetShaderLocation(g_skinShader,"viewPos"); g_skinLoc[6]=GetShaderLocation(g_skinShader,"fog");
        g_skinShader.locs[SHADER_LOC_MAP_NORMAL]=GetShaderLocation(g_skinShader,"texture2");
        g_skinShader.locs[SHADER_LOC_MAP_OCCLUSION]=GetShaderLocation(g_skinShader,"texOrm");
        g_skinShader.locs[SHADER_LOC_MAP_EMISSION]=GetShaderLocation(g_skinShader,"texEmi");
        g_skinEmiLoc=GetShaderLocation(g_skinShader,"emiDim");
        g_skinShader.locs[SHADER_LOC_VERTEX_TANGENT]=GetShaderLocationAttrib(g_skinShader,"vertexTangent");
        if (g_skinBonesLoc>=0) g_skinReady=1;
    }
    DebugLog("gpuskin","\"ok\":%d,\"loc\":%d",g_skinReady,g_skinBonesLoc);
    return g_skinReady;
}
static Matrix XformMat(Transform t){
    return MatrixMultiply(MatrixMultiply(MatrixScale(t.scale.x,t.scale.y,t.scale.z), QuaternionToMatrix(t.rotation)),
                          MatrixTranslate(t.translation.x,t.translation.y,t.translation.z));
}
static void EnemyMeshBonesGPU(Mesh *ms){
    rlEnableVertexArray(ms->vaoId);
    rlLoadVertexBuffer(ms->boneIndices, ms->vertexCount*4*(int)sizeof(unsigned char), false);
    rlSetVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES, 4, RL_UNSIGNED_BYTE, false, 0, 0);
    rlEnableVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES);
    rlLoadVertexBuffer(ms->boneWeights, ms->vertexCount*4*(int)sizeof(float), false);
    rlSetVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEWEIGHTS, 4, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEWEIGHTS);
    rlDisableVertexArray();
}
// Distance LOD: <path>_lod.glb is the same figure decimated (mesh + skin only). It borrows
// the full model's materials and bone palette, so it is posed by the same RigPose call.
// The builder keeps one mesh per material in the same order; anything else -> no LOD.
static void EnemyLodLoad(EnemyRig *rg, const char *path){
    if (!rg->gpu) return;
    char lp[200]; snprintf(lp,sizeof lp,"%s",path);
    char *dot=strstr(lp,".glb"); if (!dot) return; snprintf(dot,sizeof lp-(dot-lp),"_lod.glb");
    if (!FileExists(lp)) return;
    Model L=LoadModel(lp);
    Model *M=&rg->model;
    if (L.meshCount!=M->meshCount || L.skeleton.boneCount!=M->skeleton.boneCount){
        DebugLog("enemy","\"lod\":\"%s\",\"rejected\":\"meshes %d vs %d, bones %d vs %d\"",lp,L.meshCount,M->meshCount,L.skeleton.boneCount,M->skeleton.boneCount);
        UnloadModel(L); return;
    }
    for (int i=0;i<L.meshCount;i++){ Mesh *ms=&L.meshes[i];
        if (!ms->boneIndices || !ms->boneWeights){ UnloadModel(L); return; }
        if (!ms->tangents && ms->texcoords && ms->normals) GenMeshTangents(ms);
        EnemyMeshBonesGPU(ms);
        L.meshMaterial[i]=M->meshMaterial[i];
    }
    for (int i=0;i<L.materialCount;i++) UnloadMaterial(L.materials[i]);
    RL_FREE(L.materials); L.materials=M->materials; L.materialCount=M->materialCount;   // shared (never unloaded twice)
    RL_FREE(L.boneMatrices); L.boneMatrices=M->boneMatrices;                            // shared pose
    int lv=0, fv=0; for (int i=0;i<L.meshCount;i++){ lv+=L.meshes[i].vertexCount; fv+=M->meshes[i].vertexCount; }
    rg->lod=L; rg->hasLod=1;
    DebugLog("enemy","\"lod\":\"%s\",\"verts\":%d,\"full\":%d",lp,lv,fv);
}
// Hand the rig's meshes their bone attributes (locations 7/8) and swap its materials to
// the skinning shader. The position VBO keeps the bind pose raylib uploaded at load.
static void EnemyRigGPU(EnemyRig *rg){
    Model *m=&rg->model; int nb=m->skeleton.boneCount;
    if (EnemySkinInit()!=1 || nb<=0 || nb>ENEMY_MAX_BONES || !m->skeleton.bindPose || !m->boneMatrices) return;
    for (int i=0;i<m->meshCount;i++){ Mesh *ms=&m->meshes[i];
        if (!ms->boneIndices || !ms->boneWeights || ms->vertexCount<=0 || !ms->vaoId) return;   // all-or-nothing
    }
    for (int i=0;i<m->meshCount;i++) EnemyMeshBonesGPU(&m->meshes[i]);
    // neutral detail maps for materials that ship none (older models / plain props):
    // flat normal, full light, matte, no highlight -> they render exactly as before
    static Texture2D flatN={0}, defOrm={0}, noEmi={0};
    if (!flatN.id){
        Image a=GenImageColor(1,1,(Color){128,128,255,255}); flatN=LoadTextureFromImage(a); UnloadImage(a);
        Image b=GenImageColor(1,1,(Color){255,204,0,255});   defOrm=LoadTextureFromImage(b); UnloadImage(b);
        Image c=GenImageColor(1,1,(Color){0,0,0,255});       noEmi=LoadTextureFromImage(c);  UnloadImage(c);
    }
    for (int i=0;i<m->meshCount;i++){ Mesh *ms=&m->meshes[i];
        if (!ms->tangents && ms->texcoords && ms->normals) GenMeshTangents(ms);   // the glTF exporter leaves them out
    }
    for (int i=0;i<m->materialCount;i++){
        m->materials[i].shader=g_skinShader;
        MaterialMap *mm=m->materials[i].maps;
        if (mm[MATERIAL_MAP_NORMAL].texture.id==0)    mm[MATERIAL_MAP_NORMAL].texture=flatN;
        if (mm[MATERIAL_MAP_OCCLUSION].texture.id==0) mm[MATERIAL_MAP_OCCLUSION].texture=defOrm;
        if (mm[MATERIAL_MAP_EMISSION].texture.id==0 || mm[MATERIAL_MAP_EMISSION].texture.id==rlGetTextureIdDefault()) mm[MATERIAL_MAP_EMISSION].texture=noEmi;
        int ids[4]={MATERIAL_MAP_DIFFUSE,MATERIAL_MAP_NORMAL,MATERIAL_MAP_OCCLUSION,MATERIAL_MAP_EMISSION};
        for (int k=0;k<4;k++){ Texture2D *t=&mm[ids[k]].texture;   // raylib loads POINT-filtered: jaggy up close
            if (t->id>0 && t->width>1 && t->id!=rlGetTextureIdDefault() && t->mipmaps<=1){ GenTextureMipmaps(t); SetTextureFilter(*t, TEXTURE_FILTER_TRILINEAR); } }
    }
    rg->invBind=(Matrix*)MemAlloc(sizeof(Matrix)*nb); rg->bones=NULL;   // the pose lives in model.boneMatrices
    for (int b=0;b<nb;b++) rg->invBind[b]=MatrixInvert(XformMat(m->skeleton.bindPose[b]));   // once, not per frame
    rg->gpu=1;
}
// Light an enemy for this draw: the 4 strongest arena lamps that actually reach its
// chest (one shadow ray each, so lamps behind walls don't bleed through), plus a soft
// fill from the player. Uploaded to the skinning shader's light uniforms.
static void EnemyLightUpload(Vector3 at){
    if (g_skinReady!=1) return;
    Vector3 P=(Vector3){at.x, at.y+1.1f, at.z};
    int best[4]={-1,-1,-1,-1}; float bw[4]={0,0,0,0};
    for (int l=0;l<g_nMapLights;l++){
        MapLight *L=&g_mapLights[l];
        Vector3 dl=Vector3Subtract(L->pos,P); float d=Vector3Length(dl);
        if (d>=L->R) continue;
        float w=(1.0f-d/L->R)*(L->color.x+L->color.y+L->color.z);
        int k=-1; for (int j=0;j<4;j++) if (best[j]<0 || w>bw[j]){ k=j; break; }
        if (k<0) continue;
        if (g_hasMap && g_mapCol.ready && d>0.3f){
            float hit=MapRayNearest(P, Vector3Scale(dl,1.0f/d), d-0.2f);
            if (hit>0.0f && hit<d-0.3f) continue;              // a wall between enemy and lamp
        }
        for (int j=3;j>k;j--){ best[j]=best[j-1]; bw[j]=bw[j-1]; }
        best[k]=l; bw[k]=w;
    }
    float pos[24], col[24], rad[8]; int n=0;
    for (int j=0;j<4;j++) if (best[j]>=0){ MapLight *L=&g_mapLights[best[j]];
        pos[n*3]=L->pos.x; pos[n*3+1]=L->pos.y; pos[n*3+2]=L->pos.z;
        col[n*3]=L->color.x; col[n*3+1]=L->color.y; col[n*3+2]=L->color.z; rad[n]=L->R; n++; }
    // player fill: a dim, neutral light riding just above the camera
    pos[n*3]=g_cam.position.x; pos[n*3+1]=g_cam.position.y+0.6f; pos[n*3+2]=g_cam.position.z;
    col[n*3]=0.42f; col[n*3+1]=0.42f; col[n*3+2]=0.45f; rad[n]=16.0f; n++;
    n+=DynLightsNear(P, 3, 0.0f, pos+n*3, col+n*3, rad+n);     // muzzle flashes, fire, bolts, the lantern
    { float fg[4]={kFog[0],kFog[1],kFog[2], g_fogOn?kFog[3]:0.0f}; SetShaderValue(g_skinShader,g_skinLoc[6],fg,SHADER_UNIFORM_VEC4); }
    float amb[3]={0.30f,0.29f,0.31f}, vp[3]={g_cam.position.x,g_cam.position.y,g_cam.position.z};
    SetShaderValueV(g_skinShader,g_skinLoc[0],pos,SHADER_UNIFORM_VEC3,n);
    SetShaderValueV(g_skinShader,g_skinLoc[1],col,SHADER_UNIFORM_VEC3,n);
    SetShaderValueV(g_skinShader,g_skinLoc[2],rad,SHADER_UNIFORM_FLOAT,n);
    SetShaderValue(g_skinShader,g_skinLoc[3],&n,SHADER_UNIFORM_INT);
    SetShaderValue(g_skinShader,g_skinLoc[4],amb,SHADER_UNIFORM_VEC3);
    SetShaderValue(g_skinShader,g_skinLoc[5],vp,SHADER_UNIFORM_VEC3);
}
// Pose a rig for drawing: GPU rigs compute their bone palette (frame-interpolated, as
// raylib does) and upload it to the skinning shader; anything else falls back to raylib.
static void RigPose(EnemyRig *rg, ModelAnimation *an, float frame){
    if (!rg->gpu){ ANIM_APPLY(rg->model, *an, frame); return; }
    int nf=an->keyframeCount; if (nf<=0 || !an->keyframePoses) return;
    int f0=(int)frame; float bl=frame-(float)f0; if (bl<0) bl=0; if (bl>1) bl=1;
    f0%=nf; if (f0<0) f0+=nf; int f1=(f0+1)%nf;
    int nb=rg->model.skeleton.boneCount;
    for (int b=0;b<nb;b++){
        Transform a=an->keyframePoses[f0][b], c=an->keyframePoses[f1][b], t;
        t.translation=Vector3Lerp(a.translation,c.translation,bl);
        t.rotation=QuaternionSlerp(a.rotation,c.rotation,bl);
        t.scale=Vector3Lerp(a.scale,c.scale,bl);
        rg->model.boneMatrices[b]=MatrixMultiply(rg->invBind[b], XformMat(t));
    }
    // No manual upload: DrawModelEx already sends model.boneMatrices to the shader's
    // "boneMatrices" uniform (rmodels.c) -- an upload here would just be overwritten.
}
// Crossfade: pose blended from clip A at frame fa (weight 1-w) into clip B at fb (w).
static Transform RigSample(ModelAnimation *an, float frame, int b){
    int nf=an->keyframeCount; int f0=(int)frame; float bl=frame-(float)f0; if (bl<0) bl=0; if (bl>1) bl=1;
    f0%=nf; if (f0<0) f0+=nf; int f1=(f0+1)%nf;
    Transform a=an->keyframePoses[f0][b], c=an->keyframePoses[f1][b], t;
    t.translation=Vector3Lerp(a.translation,c.translation,bl);
    t.rotation=QuaternionSlerp(a.rotation,c.rotation,bl);
    t.scale=Vector3Lerp(a.scale,c.scale,bl);
    return t;
}
static void RigPoseBlend(EnemyRig *rg, ModelAnimation *A, float fa, ModelAnimation *B, float fb, float w){
    if (!rg->gpu || w>=1.0f || A->keyframeCount<=0 || !A->keyframePoses){ RigPose(rg,B,fb); return; }
    if (B->keyframeCount<=0 || !B->keyframePoses) return;
    if (w<0) w=0;
    int nb=rg->model.skeleton.boneCount;
    for (int b=0;b<nb;b++){
        Transform a=RigSample(A,fa,b), c=RigSample(B,fb,b), t;
        t.translation=Vector3Lerp(a.translation,c.translation,w);
        t.rotation=QuaternionSlerp(a.rotation,c.rotation,w);
        t.scale=Vector3Lerp(a.scale,c.scale,w);
        rg->model.boneMatrices[b]=MatrixMultiply(rg->invBind[b], XformMat(t));
    }
}
// ---- Layered posing: legs from a locomotion clip, torso from the action ------------------
static Transform XfLerp(Transform a, Transform c, float w){
    Transform t; t.translation=Vector3Lerp(a.translation,c.translation,w);
    t.rotation=QuaternionSlerp(a.rotation,c.rotation,w); t.scale=Vector3Lerp(a.scale,c.scale,w); return t;
}
static float AngDiff(float a, float b){ float d=fmodf(a-b,360.0f); if (d>180.0f) d-=360.0f; if (d<-180.0f) d+=360.0f; return d; }   // a-b in (-180,180]
static int RigBoneNamed(const EnemyRig *rg, const char *name){
    for (int i=0;i<rg->model.skeleton.boneCount;i++) if (!strcmp(rg->model.skeleton.bones[i].name,name)) return i;
    return -1;
}
// How fast a gait clip walks, m/s at scale 1: the hips' speed over whichever foot is planted.
static float ClipGroundSpeed(const EnemyRig *rg, int role){
    const ModelAnimation *an=&rg->anim[rg->clip[role]]; int nf=an->keyframeCount;
    int lf=RigBoneNamed(rg,"LeftFoot"), rf=RigBoneNamed(rg,"RightFoot"), hp=rg->bHips;
    if (nf<4 || lf<0 || rf<0 || hp<0 || !an->keyframePoses) return 0.0f;
    float sum=0; int n=0;
    for (int f=0;f+1<nf;f++){
        const Transform *a=an->keyframePoses[f], *c=an->keyframePoses[f+1];
        int st=(a[lf].translation.y+c[lf].translation.y < a[rf].translation.y+c[rf].translation.y) ? lf : rf;   // the planted foot
        float hx=(c[hp].translation.x-a[hp].translation.x)-(c[st].translation.x-a[st].translation.x);
        float hz=(c[hp].translation.z-a[hp].translation.z)-(c[st].translation.z-a[st].translation.z);
        sum+=sqrtf(hx*hx+hz*hz); n++;
    }
    return n>0 ? sum/n*30.0f : 0.0f;                       // clips are sampled at 30 Hz
}
static void RigLayerInit(EnemyRig *rg){
    int nb=rg->model.skeleton.boneCount; const BoneInfo *bi=rg->model.skeleton.bones;
    rg->layer=0; if (nb<=0 || nb>ENEMY_MAX_BONES || rg->animN<=0) return;
    rg->bHips=RigBoneNamed(rg,"Hips"); rg->bLB=RigBoneNamed(rg,"LowerBack"); rg->bNeck=RigBoneNamed(rg,"Neck");
    rg->bHandR=RigBoneNamed(rg,"RightFingerBase");
    if (rg->bHips<0 || rg->bLB<0) return;
    for (int b=0;b<nb;b++){
        rg->up[b]=0; rg->tw[b]=0; rg->hd[b]=0;
        for (int a=b,g=0; a>=0 && g<64; a=bi[a].parent,g++){          // nearest spine ancestor decides the share
            const char *n=bi[a].name;
            if (!rg->hd[b]){ if (!strcmp(n,"Head")) rg->hd[b]=1.0f; else if (!strcmp(n,"Neck1")) rg->hd[b]=0.65f; else if (!strcmp(n,"Neck")) rg->hd[b]=0.3f; }
            if (!strcmp(n,"Spine1")){ rg->up[b]=1; rg->tw[b]=1.0f; break; }
            if (!strcmp(n,"Spine")){ rg->up[b]=1; rg->tw[b]=0.62f; break; }
            if (!strcmp(n,"LowerBack")){ rg->up[b]=1; rg->tw[b]=0.3f; break; }
        }
    }
    // Gait speeds: the builder's own figure (<enemy>.gait, read in LoadEnemyRig) or, without one, measured off the feet
    static const int gait[5]={CL_WALK,CL_STRIDE,CL_JOG,CL_RUN,CL_LIMP}; static const float fb[5]={1.3f,1.3f,2.6f,3.6f,LIMP_CLIP_MPS_D};
    float est[5];
    for (int k=0;k<5;k++){ est[k]=ClipGroundSpeed(rg,gait[k]);
        if (!(rg->mps[gait[k]]>0.05f)) rg->mps[gait[k]]=(est[k]>0.2f && est[k]<9.0f)?est[k]:fb[k]; }
    // the ladder: every gait the file has, slowest first, skipping ones that walk no faster than the last
    rg->gaitN=0;
    for (int k=0;k<4;k++){ int r=gait[k];
        if (r!=CL_WALK && !(rg->found&(1u<<r))) continue;
        if (rg->gaitN>0 && rg->mps[r] < rg->mps[rg->gait[rg->gaitN-1]]*1.12f) continue;
        rg->gait[rg->gaitN++]=r; }
    rg->layer=1;
    DebugLog("riglayer","\"walk\":%.2f,\"stride\":%.2f,\"jog\":%.2f,\"run\":%.2f,\"limp\":%.2f,\"est\":[%.2f,%.2f,%.2f,%.2f,%.2f],\"gaits\":%d,\"bones\":%d",
             rg->mps[CL_WALK],rg->mps[CL_STRIDE],rg->mps[CL_JOG],rg->mps[CL_RUN],rg->mps[CL_LIMP],est[0],est[1],est[2],est[3],est[4],rg->gaitN,nb);
}
// Load an enemy model + clips into a rig, resolving clip ROLES by name and the
// death clip's settle frame. Returns 1 if the model has geometry.
static int LoadEnemyRig(EnemyRig *rg, const char *path){
    memset(rg,0,sizeof *rg);
    char alt[160]; snprintf(alt,sizeof alt,"../%s",path);
    const char *fp=FileExists(path)?path:(FileExists(alt)?alt:NULL);
    if (!fp){ DebugLog("enemy","\"error\":\"%s not found\"",path); return 0; }
    rg->model=LoadModel(fp);
    rg->anim=LoadModelAnimations(fp,&rg->animN);
    static const char *roleName[CL_NROLES]={"walk","idle","run","hit","attack","death","cast",
                                            "hit_head","hit_left","hit_right","hit_leg",
                                            "death_head","death_leg","death_blast","death_riddled","knockdown","limp","stride","jog"};
    for (int r=0;r<CL_NROLES;r++){ rg->clip[r]=-1; rg->mps[r]=0; }
    rg->found=0;
    for (int ci=0; ci<rg->animN; ci++){
        const char *nm=rg->anim[ci].name; int m=-1;
        for (int r=0;r<CL_NROLES;r++) if (!strcmp(nm,roleName[r])) m=r;              // exact name first ("hit" vs "hit_head")
        if (m<0) for (int r=0;r<=CL_CAST;r++) if (strstr(nm,roleName[r])){ m=r; break; }   // legacy: names with a prefix
        if (m>=0 && (rg->clip[m]<0 || !strcmp(nm,roleName[m]))){ rg->clip[m]=ci; rg->found|=1u<<m; }
    }
    {   // <enemy>.gait beside the model: "role metres-per-second" per line, how fast each gait loop walks
        char gp[200]; snprintf(gp,sizeof gp,"%s",fp); char *dot=strrchr(gp,'.');
        if (dot && (size_t)(dot-gp)+6<sizeof gp){ strcpy(dot,".gait");
            FILE *gf=fopen(gp,"r");
            if (gf){ char rn[32]; float v;
                while (fscanf(gf,"%31s %f",rn,&v)==2)
                    for (int r=0;r<CL_NROLES;r++) if (!strcmp(rn,roleName[r]) && v>0.05f && v<12.0f) rg->mps[r]=v;
                fclose(gf); } }
    }
    for (int r=0;r<CL_NROLES;r++) if (rg->clip[r]<0){
        int fb = (r>=CL_HIT_HEAD && r<=CL_HIT_LEG) ? CL_HIT : (r>=CL_DEATH_HEAD && r<=CL_DEATH_RIDDLED) ? CL_DEATH
               : (r==CL_KNOCK) ? CL_HIT : (r==CL_LIMP) ? CL_RUN : (r==CL_CAST) ? CL_ATTACK : (r==CL_STRIDE) ? CL_WALK : (r==CL_JOG) ? CL_RUN : -1;
        rg->clip[r] = (fb>=0 && rg->clip[fb]>=0) ? rg->clip[fb] : 0;              // older models: the plain clip
    }
    // Pick the death clip's "settled on the floor" frame. Many death anims end
    // with a trailing keyframe that snaps back to the standing start pose, so
    // freezing the corpse at the literal last frame leaves it standing. Walk
    // the root bone's vertical track and take the LATEST frame still near its
    // lowest point -- that's the body lying down, just before any spring-back.
    static const int deathRoles[5]={CL_DEATH,CL_DEATH_HEAD,CL_DEATH_LEG,CL_DEATH_BLAST,CL_DEATH_RIDDLED};
    for (int dr=0; dr<5; dr++){ int role=deathRoles[dr];
    rg->deathFrame = (rg->animN>0) ? ANIM_FRAMES(rg->anim[rg->clip[role]])-1 : 0;
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
    if (rg->animN>0 && rg->model.skeleton.boneCount>0){
        ModelAnimation *d=&rg->anim[rg->clip[role]];
        int nf=ANIM_FRAMES(*d), root=0;
        for (int b=0;b<rg->model.skeleton.boneCount;b++)
            if (strstr(rg->model.skeleton.bones[b].name,"Hips")||strstr(rg->model.skeleton.bones[b].name,"hips")){ root=b; break; }
        float minY=1e30f, maxY=-1e30f;
        for (int f=0;f<nf;f++){ float y=d->keyframePoses[f][root].translation.y; if(y<minY)minY=y; if(y>maxY)maxY=y; }
        float thresh=minY+(maxY-minY)*0.2f;            // "near the floor" band
        for (int f=nf-1;f>=0;f--)                      // latest frame still in that band
            if (d->keyframePoses[f][root].translation.y<=thresh){ rg->deathFrame=f; break; }
    }
#endif
    rg->settle[role]=rg->deathFrame; }
    rg->deathFrame=rg->settle[CL_DEATH];                // (the plain death's, as before)
    rg->ok=(rg->model.meshCount>0);
    if (rg->ok && rg->animN>0) EnemyRigGPU(rg);
    if (rg->ok && rg->gpu) RigLayerInit(rg);
    if (rg->ok) EnemyLodLoad(rg, fp);
    BoundingBox eb=GetModelBoundingBox(rg->model);
    DebugLog("enemy","\"file\":\"%s\",\"gpu\":%d,\"meshes\":%d,\"bones\":%d,\"anims\":%d,\"clips\":[%d,%d,%d,%d,%d,%d],\"settleFrame\":%d,\"size\":[%.2f,%.2f,%.2f]",
             fp, rg->gpu, rg->model.meshCount, rg->model.skeleton.boneCount, rg->animN,
             rg->clip[0],rg->clip[1],rg->clip[2],rg->clip[3],rg->clip[4],rg->clip[5], rg->deathFrame,
             eb.max.x-eb.min.x, eb.max.y-eb.min.y, eb.max.z-eb.min.z);
    return rg->ok;
}
#define g_eWalk CL_WALK      // legacy names for the clip ROLES (e->clip holds a role, not an anim index)
#define g_eIdle CL_IDLE
#define g_eRun CL_RUN
#define g_eHit CL_HIT
#define g_eAttack CL_ATTACK
#define g_eDeath CL_DEATH
                              // start pose, so corpses must settle on an earlier "body on the floor" frame instead
// Build the pose an enemy is drawn in (model space, kept in e->pose). Legs come from his locomotion
// clip, played at the pace he is really covering ground; torso, arms and head come from the action
// clip (swing / aim / cast / flinch) hung on those hips, then the chest twists and the head turns
// toward the player and the whole body leans into turns. Clip changes ease in from a snapshot of the
// last pose. Knockdowns and deaths take the whole body.
#define EM_ADVANCE 0
#define EM_AIM     1
#define EM_MOVE    2
static int EnemyActing(int clip){ return !(clip==CL_RUN || clip==CL_IDLE || clip==CL_WALK || clip==CL_LIMP || clip==CL_STRIDE || clip==CL_JOG); }
static void EnemyPoseBuild(Enemy *e){
    EnemyRig *rg=RigOf(e->type); int nb=rg->model.skeleton.boneCount;
    if (!rg->layer || rg->animN<=0){ e->poseOK=0; return; }
    Transform T[ENEMY_MAX_BONES];
    float twist=0, head=0, lean=0, pitch=0;
    if (e->state==2){
        ModelAnimation *d=RigClip(e->type, e->deathRole?e->deathRole:CL_DEATH);
        for (int b=0;b<nb;b++) T[b]=RigSample(d,e->animT,b);
    } else {
        ModelAnimation *L=RigClip(e->type,e->loco);
        for (int b=0;b<nb;b++) T[b]=RigSample(L,e->locoT,b);
        if (e->locoBlend>0){                                  // gait change: fade the old gait out
            ModelAnimation *Lp=RigClip(e->type,e->locoPrev); float w=1.0f-e->locoBlend/LOCO_BLEND; w=w*w*(3.0f-2.0f*w);
            for (int b=0;b<nb;b++) T[b]=XfLerp(RigSample(Lp,e->locoPrevT,b),T[b],w);
        }
        if (EnemyActing(e->clip)){
            ModelAnimation *A=RigClip(e->type,e->clip); float lw=e->legW;
            Vector3 aP=RigSample(A,e->animT,rg->bLB).translation, P=Vector3Lerp(aP,T[rg->bLB].translation,lw);
            for (int b=0;b<nb;b++){
                Transform a=RigSample(A,e->animT,b);
                if (!rg->up[b]) T[b]=XfLerp(a,T[b],lw);
                else { T[b]=a; T[b].translation=Vector3Add(P,Vector3Subtract(a.translation,aP)); }
            }
        }
        if (e->clip!=CL_KNOCK){ twist=e->twist; head=e->headTw; lean=e->lean; pitch=e->pitch; }
    }
    if (fabsf(twist)>0.05f){                                   // chest turned toward the player, shared up the spine
        Vector3 P=T[rg->bLB].translation;
        for (int b=0;b<nb;b++) if (rg->up[b]){
            Quaternion q=QuaternionFromAxisAngle((Vector3){0,1,0}, twist*rg->tw[b]*DEG2RAD);
            T[b].translation=Vector3Add(P,Vector3RotateByQuaternion(Vector3Subtract(T[b].translation,P),q));
            T[b].rotation=QuaternionMultiply(q,T[b].rotation);
        }
    }
    if (fabsf(head)>0.05f && rg->bNeck>=0){                    // ...and the head the rest of the way
        Vector3 P=T[rg->bNeck].translation;
        for (int b=0;b<nb;b++) if (rg->hd[b]>0){
            Quaternion q=QuaternionFromAxisAngle((Vector3){0,1,0}, head*rg->hd[b]*DEG2RAD);
            T[b].translation=Vector3Add(P,Vector3RotateByQuaternion(Vector3Subtract(T[b].translation,P),q));
            T[b].rotation=QuaternionMultiply(q,T[b].rotation);
        }
    }
    if (fabsf(lean)>0.05f || fabsf(pitch)>0.05f){              // leaning into a turn / a hard start, about the feet
        Quaternion q=QuaternionMultiply(QuaternionFromAxisAngle((Vector3){0,0,1},lean*DEG2RAD), QuaternionFromAxisAngle((Vector3){1,0,0},pitch*DEG2RAD));
        for (int b=0;b<nb;b++){ T[b].translation=Vector3RotateByQuaternion(T[b].translation,q); T[b].rotation=QuaternionMultiply(q,T[b].rotation); }
    }
    if (e->blendT>0 && e->fromOK){                             // ease in from the pose he was in when the clip changed
        float w=1.0f-e->blendT/ENEMY_BLEND; if (w<0) w=0; w=w*w*(3.0f-2.0f*w);
        for (int b=0;b<nb;b++) if (e->blendAll || rg->up[b]) T[b]=XfLerp(e->from[b],T[b],w);
    }
    memcpy(e->pose,T,sizeof(Transform)*nb); e->poseOK=1;
}
static void EnemyPoseSnap(Enemy *e, int all){                 // call just before changing his clip
    if (e->poseOK){ memcpy(e->from,e->pose,sizeof e->from); e->fromOK=1; } else e->fromOK=0;
    e->blendT=ENEMY_BLEND; e->blendAll=all;
}
// Upload a stored pose to the rig's bone palette for drawing.
static void RigPoseStored(EnemyRig *rg, const Transform *pose){
    int nb=rg->model.skeleton.boneCount;
    for (int b=0;b<nb;b++) rg->model.boneMatrices[b]=MatrixMultiply(rg->invBind[b], XformMat(pose[b]));
}
static int             g_hasEnemy = 0;
static int             g_kills = 0;
static int             g_headshots = 0;        // running headshot-kill tally (HUD)
static float           g_hsFlash = 0.0f;       // >0: flash "HEADSHOT!" near the crosshair
static float           g_hitMark = 0.0f, g_killMark = 0.0f;   // >0: hit marker (white X) / kill marker (red, wider) at the crosshair
static float           g_recoilRet = 0.0f;     // camera recoil still owed back (radians of pitch)
static float           g_playerHp = 100.0f;
static int             g_godMode = 0;           // invulnerable cheat; G toggles (tags the run as cheated)
static int             g_cheatUsed = 0;         // god mode was enabled this run -> the results get tagged
static int             g_meleeDrain = 0;        // --melee-drain: legacy untimed contact damage, kept for A/B
static int             g_reloadAt = 0;          // --reload-at N: force a reload at update frame N (dev: inspect reload anims)
static int             g_kite = 0;              // --kite: dev autopilot -- face the nearest enemy and backpedal (repro chase glitches)
static int             g_shotEvery = 0;         // --shot-every K: numbered screenshot every K frames (dev)
static int             g_devCorpses = 0;        // --corpses N: lay out N cook corpses at run start (dev: test the priest)
static int             g_devBoss = 0;           // --boss: jump straight to wave 1's boss interlude (dev)
static int             g_vmHoldDev = -1;        // --vm-hold N: freeze the grenade launcher's clip on frame N (dev: read its animation)
static int             g_soundboard = 0;        // --sounds
static int             g_mute = 0;              // --mute (or CHERNOBYL_MUTE=1): master volume 0 -- headless tests stay silent
static int             g_autoFire = 0;          // --autofire: hold the trigger every frame (dev: screenshot muzzle FX)
static int             g_cycleTest = 0;         // --cycle: soak walks all 9 weapon slots twice, logging ammo state
static float           g_flameFuel = 0.0f;      // flamethrower: fractional fuel-unit accumulator (whole units leave the mag)
// ---- Wave director (M2) -----------------------------------------------------
static int             g_wave         = 0;      // current wave, 1-based (0 = run not started yet)  [fwd-declared above EnemyArm]
static int             g_bossThisWave = 0;      // the between-waves boss has been spawned for this wave
static int             g_waveBudget   = 0;      // spawn points left to spend this wave
static float           g_waveBreather = 0.0f;   // >0: pause between waves, counting down
static float           g_spawnTick    = 0.0f;   // trickle-spawn timer
static int             g_aliveCap     = 0;      // max simultaneous enemies this wave
static float           g_runT         = 0.0f;   // run clock (results screen)
static int             g_stress       = 0;      // --stress: keep the field at MAX_ENEMIES cooks, no waves (perf gate)
static int             g_dead = 0;              // player death state: world frozen, waiting on a respawn input
static float           g_deadT = 0.0f;          // seconds since death (brief delay before respawn is allowed)
static float           g_hurt = 0.0f;           // 0..1 red damage flash, spikes on hp loss then decays
static float           g_prevHp = 100.0f;       // last frame's hp, to detect a drop -> trigger the hurt flash
static int             g_noclip = 0;            // map mode: F toggles free-fly / noclip vs FPS collision
static Vector3         g_spawnResetPos = {0,0,0};  // where to drop the player back to (fell off the map / new map)
static float           g_voidY = -1e9f;            // below this Y the player has fallen out of the world
#define DEFAULT_MAP "maps/chernobyl.map"            // THE arena (tools/grid2map.py from ../chernobyl level 0)
static int             g_returnToMenu = 0;         // ESC menu "Restart Run" / death -> break the game loop and reload

static int g_wave;
static void EnemyArm(int i, Vector3 pos, int type){   // common: bring enemy i to life at pos
    int wv=(g_wave>1)?g_wave-1:0;                      // ../chernobyl scaling: +12% HP, +0.18 m/s per wave past the first
    g_enemies[i].maxHp=kEnemy[type].hp*(1.0f+0.12f*wv); g_enemies[i].spd=kEnemy[type].speed+0.18f*wv; g_enemies[i].boss=0;
    g_enemies[i].pos=pos; g_enemies[i].hp=g_enemies[i].maxHp; g_enemies[i].type=type; g_enemies[i].state=1;
    g_enemies[i].animT=(float)GetRandomValue(0,40); g_enemies[i].deathT=0;
    g_enemies[i].hitT=0; g_enemies[i].clip=g_eRun; g_enemies[i].roarT=0; g_enemies[i].drawY=pos.y;
    g_enemies[i].stuckT=0; g_enemies[i].embedT=0; g_enemies[i].fallV=0.0f; g_enemies[i].atkCd=0.0f; g_enemies[i].burnN=0;
    g_enemies[i].fireT=kEnemy[type].rate*0.5f; g_enemies[i].losT=0; g_enemies[i].los=0; g_enemies[i].burstLeft=0;
    g_enemies[i].raiseC=-1; g_enemies[i].chanT=0;
    g_enemies[i].swing=0; g_enemies[i].blendT=0;
    g_enemies[i].hitRole=CL_HIT; g_enemies[i].deathRole=CL_DEATH; g_enemies[i].limpT=0; g_enemies[i].knockT=0; g_enemies[i].flameFlinchAt=-100.0;   // a reused slot starts clean
    g_enemies[i].pushV=(Vector3){0,0,0}; g_enemies[i].riseT=0; g_enemies[i].charged=0; g_enemies[i].sc=1.0f; g_enemies[i].sumT=g_enemies[i].boltT=0; g_enemies[i].phase=0;
    g_enemies[i].wN=0; g_enemies[i].wNext=0; g_enemies[i].drawAn=NULL;
    { Enemy *e=&g_enemies[i];                              // motion state: standing, turned to the player, his own pace and approach side
      e->spd*=1.0f+GetRandomValue(-8,8)/100.0f;
      e->vel=e->mv=(Vector3){0,0,0}; e->lastPos=pos;
      e->yaw=atan2f(g_pos.x-pos.x, g_pos.z-pos.z)*RAD2DEG + ENEMY_YAW_OFFSET; e->yawRate=e->twist=e->headTw=e->lean=e->pitch=0;
      e->loco=e->locoPrev=CL_IDLE; e->back=0; e->locoT=(float)GetRandomValue(0,40); e->locoPrevT=0; e->locoBlend=0; e->legW=1;
      e->flank=GetRandomValue(-100,100)/100.0f; e->atkRest=0; e->atkRate=1; e->stagT=0; e->hitAcc=e->hitAccT=0; e->aimT=e->modeT=e->lostT=0;
      e->mode=EM_ADVANCE; e->side=GetRandomValue(0,1)?1:-1; e->shots=0; e->rehit=0; e->poseOK=e->fromOK=e->blendAll=0; }
}
// A spawn spot is good only if it's near the player's level, inside the player's
// walkable nav region (not sealed off behind a wall), AND has body clearance so the
// enemy isn't jammed in a corner where it can't move (the random point can sit nearer
// a wall than the nav cell centre, so re-check the actual sphere here).
// Where to start the downward floor-probe for a spawn at (x,z): up to 8 above the
// player, but never above a ceiling -- probe UP from just over the player's feet first.
// Without this, an arena with a roof (maps/chernobyl.map) dropped enemies onto the
// underside of the ceiling: they walked the nav mesh at 5.1 m and were culled as
// "behind the ceiling" -- invisible, but still landing hits.
static float SpawnProbeTop(float x, float z){
    float top=g_pos.y+8.0f;
    Vector3 o={x, g_pos.y-EYE_H+0.6f, z};
    float up=MapRayNearest(o,(Vector3){0,1,0},top-o.y);
    if (up>0.0f) top=o.y+up-0.05f;
    return top;
}
static int EnemySpawnOK(float x, float fy, float z){
    return fabsf(fy-(g_pos.y-EYE_H))<6.0f
        && MapNavReachable((Vector3){x,fy,z})
        && !MapSphereHitsWall((Vector3){x,fy+0.9f,z},ENEMY_MOVE_R);   // body fits (same radius it moves with)
}
// Map spawn: a ring around the player, dropped onto the floor under each spot.
// Biased toward where the player is FACING so enemies appear in front -- unless
// `behind` is set, which inverts the bias so a slice of every wave flanks (the
// anti-kiting rule: backpedalling is never a safe direction).
static void SpawnEnemyMap(int i, int type, int behind){
    float ahead = 1.5708f - g_yaw;                       // player's look direction in the ring's angle space
    if (behind) ahead += 3.14159f;                       // flank: spawn arc centred BEHIND the player
    for (int t=0;t<40;t++){
        float ang = ahead + GetRandomValue(-115,115)/100.0f; // within ~±66 deg of the arc centre
        float dist=6.0f+GetRandomValue(0,90)/10.0f;          // 6..15 units out
        float x=g_pos.x+cosf(ang)*dist, z=g_pos.z+sinf(ang)*dist;
        float top=SpawnProbeTop(x,z);
        float fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},80.0f);
        if (fd>0){ float fy=top-fd; if (EnemySpawnOK(x,fy,z)){ EnemyArm(i,(Vector3){x,fy,z},type); return; } }
    }
    // ring failed: scan outward from the player for any reachable, roomy cell.
    for (int rad=2; rad<14; rad++) for (int a=0;a<16;a++){
        float ang=a*0.3927f, x=g_pos.x+cosf(ang)*rad, z=g_pos.z+sinf(ang)*rad;
        float top=SpawnProbeTop(x,z), fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},80.0f);
        if (fd>0){ float fy=top-fd; if (EnemySpawnOK(x,fy,z)){ EnemyArm(i,(Vector3){x,fy,z},type); return; } }
    }
    // nothing valid near the player (e.g. player is off the nav mesh in a tight nook):
    // drop the enemy on a RANDOM walkable nav cell -- a guaranteed collision-free,
    // navigable spot -- instead of blindly next to the player (which may be inside a wall).
    if (g_navOK){
        for (int t=0;t<400;t++){
            int cx=GetRandomValue(0,g_navNX-1), cz=GetRandomValue(0,g_navNZ-1), ci=cz*g_navNX+cx;
            if (g_navNL[ci]>0){
                int l=GetRandomValue(0,g_navNL[ci]-1);   // any storey in this column is a valid, navigable spot
                EnemyArm(i,(Vector3){ g_navMn.x+(cx+0.5f)*g_navCS, g_navFloor[ci*NAV_MAXL+l], g_navMn.z+(cz+0.5f)*g_navCS },type);
                return;
            }
        }
    }
    EnemyArm(i,(Vector3){g_pos.x, g_pos.y-EYE_H, g_pos.z},type);   // absolute fallback: the player's own (occupied) spot
}
// Entrance: nobody just appears. He comes up off the floor where the ground has gone bad --
// flat on his back in a pool of green light and vapour, then onto his feet (the get-up half
// of the knockdown clip). He can be shot while he rises, and can't act until he is up.
static void EnemyRiseIn(int i){
    Enemy *e=&g_enemies[i]; EnemyRig *rg=RigOf(e->type);
    if (e->state!=1 || rg->animN<=0) return;
    int nf=ANIM_FRAMES(rg->anim[rg->clip[CL_KNOCK]]); float start=50.0f;      // lying still, just before the get-up
    if (nf<start+30.0f) return;
    e->clip=CL_KNOCK; e->animT=start; e->knockT=(nf-start)/60.0f; e->hitT=e->knockT; e->riseT=e->knockT; e->legW=0;
    for (int q=0;q<5;q++) SpawnSmoke((Vector3){e->pos.x+GetRandomValue(-40,40)/100.0f, e->pos.y+0.15f, e->pos.z+GetRandomValue(-40,40)/100.0f}, 0);
    DebugLog("rise","\"i\":%d,\"t\":%.2f", i, e->riseT);
}
static void SpawnEnemyT(int i, int type, int behind){
    if (g_hasMap && g_mapCol.ready){ SpawnEnemyMap(i,type,behind); EnemyRiseIn(i); return; }
    // arena: a random spot near a wall, away from the player
    float ang=(float)GetRandomValue(0,628)/100.0f;
    float dist=ARENA*0.6f + GetRandomValue(0,(int)(ARENA*0.3f));
    EnemyArm(i,(Vector3){ cosf(ang)*dist, 0, sinf(ang)*dist },type);
}

// ---- Bosses ---------------------------------------------------------------
// Wave 2 ends with THE ARCHIMANDRITE instead of the head chef: a priest half as tall again who
// never comes to you. He hangs back and makes the fight about his dead -- raising the corpses the
// wave left (faster and from much further than a common priest), and calling fresh ones up out
// of the floor round himself when there are none. At half health his lantern turns red and he
// starts throwing radiation bolts as well, and the dead come faster.
#define ARCH_SCALE   1.5f
#define ARCH_HP      1000.0f
#define ARCH_MINIONS 5         // he keeps about this many of his dead on their feet (7 in the second phase)
#define ARCH_BOLT_DMG 14.0f
static int BossTypeForWave(int w){ return (w==2) ? EN_PRIEST : EN_HEADCHEF; }
static const char *BossName(int type){ return type==EN_PRIEST ? "THE ARCHIMANDRITE" : "THE HEAD CHEF"; }

// ---- Wave director (M2): budgeted, trickled, cleared, breathered ------------
// Wave 1 already fields nearly the whole roster (as ../Chernobyl does: its first wave is a mix,
// not one type), so the budget starts where one of each fits and grows from there.
static int WaveBudgetFor(int w){ return 42 + w*8; }                // 50 points wave 1 (~13 hostiles) -> 82 by wave 5
static int g_roster[EN_NTYPES], g_rosterN = -1;                    // wave 1's guaranteed line-up, cheapest first (-1 = not dealt yet)
static int WaveAliveCapFor(int w){ int c=7+w; return c>16?16:c; }  // trickle cap, well under MAX_ENEMIES
static void StartWave(int w){
    g_wave=w; g_waveBudget=WaveBudgetFor(w); g_aliveCap=WaveAliveCapFor(w);
    g_spawnTick=0.0f; g_waveBreather=0.0f; g_bossThisWave=0; g_rosterN=-1;
    { void SeedWavePickups(void); SeedWavePickups(); }
    if (g_audio && g_hasWaveSnd) PlaySound(g_waveSnd);
    DebugLog("wave","\"start\":%d,\"budget\":%d,\"cap\":%d", w, g_waveBudget, g_aliveCap);
}
// What to spend budget on. Every type but the head chef (the between-waves BOSS) can turn up from
// wave 1: cooks stay the staple and the rest weigh heavier as waves rise. Wave 1 opens with one of
// each type (all but the boss and the fiend, who joins the mix from wave 2), cheapest first, so the
// first wave shows the roster; what budget is left after that goes on the weighted mix. Falls
// back to a cook when the budget can't afford the pick.
static int g_forceEnemy = -1;   // --enemy NAME: every spawn is this type (demo / testing a single enemy); -2 = "all"
static int WavePickType(void){
    if (g_forceEnemy==-2){ if (g_waveBudget<=0) return -1; int t; do t=GetRandomValue(0,EN_NTYPES-1); while (t==EN_HEADCHEF && GetRandomValue(0,2)); return t; }   // --enemy all (boss rarer)
    if (g_forceEnemy>=0) return (kEnemy[g_forceEnemy].cost<=g_waveBudget) ? g_forceEnemy : -1;
    if (g_wave==1 && g_rosterN<0){                     // deal wave 1's line-up
        g_rosterN=0;
        for (int k=0;k<EN_NTYPES;k++) if (k!=EN_HEADCHEF && k!=EN_FIEND) g_roster[g_rosterN++]=k;
        for (int i=1;i<g_rosterN;i++){                 // dearest LAST in the array (dealt from the front): ties in random order
            int v=g_roster[i], key=kEnemy[v].cost*8+GetRandomValue(0,7), j=i-1;
            while (j>=0 && kEnemy[g_roster[j]].cost*8+4>key){ g_roster[j+1]=g_roster[j]; j--; }
            g_roster[j+1]=v; }
    }
    if (g_wave==1 && g_rosterN>0){
        int t=g_roster[0];
        if (kEnemy[t].cost<=g_waveBudget){ for (int i=1;i<g_rosterN;i++) g_roster[i-1]=g_roster[i]; g_rosterN--; return t; }
        g_rosterN=0;
    }
    int w=g_wave, wt[EN_NTYPES]={0}, sum=0, t=EN_COOK;
    wt[EN_COOK]=40;         wt[EN_LINECOOK]=12+2*w; wt[EN_LIQUIDATOR]=8+2*w; wt[EN_RIFLEMAN]=7+2*w; wt[EN_MUTANT]=7+2*w;
    wt[EN_REVENANT]=5+2*w;  wt[EN_GUNNER]=4+2*w;    wt[EN_BUTCHER]=4+2*w;    wt[EN_PRIEST]=2+w;     wt[EN_FIEND]=(w>=2)?1+2*w:0;
    for (int k=0;k<EN_NTYPES;k++) sum+=wt[k];
    int r=GetRandomValue(0,sum-1);
    for (int k=0;k<EN_NTYPES;k++){ if (r<wt[k]){ t=k; break; } r-=wt[k]; }
    if (kEnemy[t].cost>g_waveBudget) t=EN_COOK;
    if (kEnemy[t].cost>g_waveBudget) return -1;        // can't even afford a cook -> budget is spent
    return t;
}

// ---- Pickups + power-ups (../chernobyl) -------------------------------------
// Health +25, mega +50 (to 150), ammo (+1 mag every gun), QUAD (4x damage 30 s), SPEED
// (1.5x move 30 s). Dropped by kills (bosses always drop mega + quad) and seeded on the
// map each wave. Spin + bob in the world; expire after 25 s, blinking for the last 5.
enum { PK_HEALTH=0, PK_MEGA, PK_AMMO, PK_QUAD, PK_SPEED, PK_N };
typedef struct { Vector3 pos; int type, active; float life, t; } Pickup;
#define MAX_PICKUPS 32
static Pickup g_pk[MAX_PICKUPS];
static float  g_quadT=0.0f, g_speedT=0.0f, g_pkMsgT=0.0f;
static char   g_pkMsg[48]="";
static Sound  g_pkSnd; static int g_hasPkSnd=0;
static Sound  g_pkAmmoSnd; static int g_hasPkAmmoSnd=0;      // an ammo box picked up
static Sound  g_menuSelSnd; static int g_hasMenuSelSnd=0;     // ESC menu: a row clicked
static Sound  g_surrSnd; static int g_hasSurrSnd=0; static float g_surrCd=0.0f;   // the player, hemmed in on every side
static Sound  g_menuSnd; static int g_hasMenuSnd=0; static int g_menuHover=-1;   // ESC menu: the pointer rolls onto a row
static void PickupSpawn(Vector3 pos, int type){
    int slot=-1; float oldest=1e9f;
    for (int i=0;i<MAX_PICKUPS;i++){ if (!g_pk[i].active){ slot=i; break; } if (g_pk[i].life<oldest){ oldest=g_pk[i].life; slot=i; } }
    g_pk[slot]=(Pickup){ (Vector3){pos.x, pos.y, pos.z}, type, 1, 25.0f, (float)GetRandomValue(0,100)/10.0f };
}
static void BoltsReset(void);
static void PickupsReset(void){ memset(g_pk,0,sizeof g_pk); g_quadT=g_speedT=g_pkMsgT=0.0f; BoltsReset(); }
// a random reachable floor spot (nav grid, BFS-reached from the player) -- for wave seeding
static int PickupRandomSpot(Vector3 *out){
    if (!g_navOK || g_navNX<=0) return 0;
    for (int tries=0;tries<400;tries++){
        int cx=GetRandomValue(0,g_navNX-1), cz=GetRandomValue(0,g_navNZ-1), ci=cz*g_navNX+cx;
        if (g_navNL[ci]<=0) continue;
        int l=GetRandomValue(0,g_navNL[ci]-1); long node=(long)ci*NAV_MAXL+l;
        if (g_navDist && g_navDist[node]<0) continue;
        Vector3 p={ g_navMn.x+(cx+0.5f)*g_navCS, g_navFloor[node], g_navMn.z+(cz+0.5f)*g_navCS };
        if (Vector3Distance(p,g_pos)<6.0f) continue;
        *out=p; return 1;
    }
    return 0;
}
static void PickupOnKill(Vector3 at, int boss){
    if (boss){ PickupSpawn(at,PK_MEGA); PickupSpawn(Vector3Add(at,(Vector3){1.2f,0,0}),PK_QUAD); return; }
    int r=GetRandomValue(0,99);
    if (r>=22) return;                                   // ~22% of kills drop something
    int q=GetRandomValue(0,99);
    PickupSpawn(at, q<50?PK_HEALTH : q<85?PK_AMMO : q<93?PK_SPEED : PK_QUAD);
}
static void PickupsUpdate(float dt){
    if (g_quadT>0) g_quadT-=dt; if (g_speedT>0) g_speedT-=dt; if (g_pkMsgT>0) g_pkMsgT-=dt;
    for (int i=0;i<MAX_PICKUPS;i++){ Pickup *k=&g_pk[i]; if (!k->active) continue;
        k->t+=dt; k->life-=dt; if (k->life<=0){ k->active=0; continue; }
        Vector3 d=Vector3Subtract(k->pos,(Vector3){g_pos.x,g_pos.y-EYE_H,g_pos.z});
        if (d.x*d.x+d.z*d.z>1.3f*1.3f || fabsf(d.y)>1.6f) continue;
        const char *msg=NULL;
        switch (k->type){
            case PK_HEALTH: if (g_playerHp>=100.0f) continue; g_playerHp=fminf(100.0f,g_playerHp+25.0f); msg="+25 HEALTH"; break;
            case PK_MEGA:   if (g_playerHp>=150.0f) continue; g_playerHp=fminf(150.0f,g_playerHp+50.0f); msg="+50 MEGA HEALTH"; break;
            case PK_AMMO:   for (int w=0;w<g_numWeapons;w++){ Weapon *wp=&g_weapons[w]; if (wp->magSize>0){
                                wp->reserve+=wp->magSize; int cap=wp->reserveStart*2; if (wp->reserve>cap) wp->reserve=cap; } }
                            msg="AMMO"; break;
            case PK_QUAD:   g_quadT=30.0f;  msg="QUAD DAMAGE!"; break;
            case PK_SPEED:  g_speedT=30.0f; msg="SPEED BOOST!"; break;
        }
        k->active=0;
        if (msg){ snprintf(g_pkMsg,sizeof g_pkMsg,"%s",msg); g_pkMsgT=1.6f; }
        if (g_audio && k->type==PK_AMMO && g_hasPkAmmoSnd){ SetSoundVolume(g_pkAmmoSnd,0.7f); PlaySound(g_pkAmmoSnd); }
        else if (g_audio && g_hasPkSnd) PlaySound(g_pkSnd);
        DebugLog("pickup","\"type\":%d,\"hp\":%.0f",k->type,g_playerHp);
    }
}
static void DrawPickups(void){
    float tm=(float)GetTime();
    for (int i=0;i<MAX_PICKUPS;i++){ Pickup *k=&g_pk[i]; if (!k->active) continue;
        if (k->life<5.0f && fmodf(tm*6.0f,1.0f)<0.4f) continue;          // blink before expiring
        Vector3 c={k->pos.x, k->pos.y+0.45f+0.09f*sinf(k->t*2.6f), k->pos.z};
        rlPushMatrix(); rlTranslatef(c.x,c.y,c.z); rlRotatef(k->t*90.0f,0,1,0);
        switch (k->type){
            case PK_HEALTH: case PK_MEGA: {
                float s=(k->type==PK_MEGA)?0.55f:0.42f;
                DrawCube((Vector3){0,0,0},s,s,s,(Color){235,235,230,255}); DrawCubeWires((Vector3){0,0,0},s,s,s,(Color){120,120,120,255});
                Color red=(k->type==PK_MEGA)?(Color){40,110,255,255}:(Color){210,25,25,255};
                DrawCube((Vector3){0,0,0},s*1.02f,s*0.22f,s*0.62f,red); DrawCube((Vector3){0,0,0},s*1.02f,s*0.62f,s*0.22f,red);
                DrawCube((Vector3){0,0,0},s*0.22f,s*0.62f,s*1.02f,red);
            } break;
            case PK_AMMO:
                DrawCube((Vector3){0,0,0},0.62f,0.34f,0.4f,(Color){74,82,48,255}); DrawCubeWires((Vector3){0,0,0},0.62f,0.34f,0.4f,(Color){30,34,20,255});
                DrawCube((Vector3){0,0.05f,0},0.64f,0.07f,0.42f,(Color){220,180,40,255});
                break;
            case PK_QUAD:
                rlRotatef(45,1,0,1);
                DrawCube((Vector3){0,0,0},0.42f,0.42f,0.42f,(Color){200,40,230,255});
                DrawSphereWires((Vector3){0,0,0},0.5f+0.05f*sinf(tm*5),8,10,(Color){255,90,255,180});
                break;
            case PK_SPEED:
                DrawCylinder((Vector3){0,-0.25f,0},0.0f,0.26f,0.25f,8,(Color){40,220,240,255});
                DrawCylinder((Vector3){0,0,0},0.26f,0.0f,0.25f,8,(Color){40,220,240,255});
                DrawSphereWires((Vector3){0,0,0},0.45f+0.05f*sinf(tm*5),8,10,(Color){120,255,255,170});
                break;
        }
        rlPopMatrix();
    }
}
void PickupOnKillAt(int ei);
void PickupOnKillAt(int ei){ PickupOnKill(g_enemies[ei].pos, g_enemies[ei].boss); }
void SeedWavePickups(void){
    Vector3 p;
    if (PickupRandomSpot(&p)) PickupSpawn(p,PK_HEALTH);
    if (PickupRandomSpot(&p)) PickupSpawn(p,PK_AMMO);
    if (g_wave>=3 && PickupRandomSpot(&p)) PickupSpawn(p, GetRandomValue(0,1)?PK_SPEED:PK_QUAD);
}
static void UpdateWaves(float dt){
    if (!g_hasEnemy || g_noEnemies) return;
    int alive=0; for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state!=0) alive++;  // dying bodies still hold the field
    if (g_stress){                                     // perf gate: saturate the field, no wave logic
        for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state==0) SpawnEnemyT(i,(g_forceEnemy>=0)?g_forceEnemy:(g_forceEnemy==-2?GetRandomValue(0,EN_NTYPES-1):EN_COOK),i&1);
        return;
    }
    if (g_waveBreather>0.0f){
        g_waveBreather-=dt;
        if (g_waveBreather<=0.0f) StartWave(g_wave+1);
        return;
    }
    if (g_wave==0 && g_devCorpses>0){                 // --corpses: scatter test bodies for the priest
        for (int n=0;n<g_devCorpses && n<MAX_CORPSES;n++){ Vector3 p; if (!PickupRandomSpot(&p)) break;
            Corpse *cp=&g_corpses[g_corpseNext]; *cp=(Corpse){0}; cp->pos=p; cp->yaw=(float)GetRandomValue(0,359); cp->active=1; cp->type=EN_COOK;
            g_corpseNext=(g_corpseNext+1)%MAX_CORPSES; }
        g_devCorpses=0;
    }
    if (g_wave==0 && g_devBoss){ StartWave(g_devBoss); g_waveBudget=0; return; }   // --boss / --boss-wave N: that wave, emptied -> its boss next frame   // --boss: empty wave -> boss next frame
    if (g_wave==0){ g_waveBreather=3.0f; return; }     // run just started: a 3 s grace, then wave 1
    if (g_waveBudget>0){
        g_spawnTick-=dt;
        if (alive<g_aliveCap && g_spawnTick<=0.0f){
            int t=WavePickType();
            if (t<0){ g_waveBudget=0; return; }
            int slot=-1; for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state==0){ slot=i; break; }
            if (slot>=0){
                int behind = GetRandomValue(0,99)<35;  // flank bias: ~a third of every wave comes from behind
                SpawnEnemyT(slot,t,behind);
                g_waveBudget-=(g_forceEnemy==-2)?2:kEnemy[t].cost;   // --enemy all: flat cost so a wave holds the whole roster
                float tick=0.9f-g_wave*0.045f; if (tick<0.45f) tick=0.45f;   // trickle speeds up with the waves
                g_spawnTick=tick;
                DebugLog("spawn","\"wave\":%d,\"type\":\"%s\",\"behind\":%d,\"budget\":%d", g_wave, kEnemy[t].name, behind, g_waveBudget);
            }
        }
    } else if (alive==0 && !g_bossThisWave && g_forceEnemy<=-1){  // field clear -> BOSS interlude (../chernobyl); not in single-type demos
        g_bossThisWave=1;
        int slot=-1; for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state==0){ slot=i; break; }
        if (slot>=0){
            int bt=BossTypeForWave(g_wave);
            SpawnEnemyT(slot,bt,0);
            g_enemies[slot].boss=1;                     // HUD bar, always drops mega + quad
            if (bt==EN_PRIEST){ Enemy *b=&g_enemies[slot];                    // the Archimandrite: a giant who commands the dead
                b->sc=ARCH_SCALE; b->maxHp=ARCH_HP*(1.0f+0.12f*(g_wave>1?g_wave-1:0)); b->hp=b->maxHp; b->sumT=3.5f; b->boltT=2.0f; }
            if (g_audio && g_hasBossSnd){ SetSoundVolume(g_bossSnd,0.80f); PlaySound(g_bossSnd); }   // his own entrance (it used to borrow the wave sting)
            else if (g_audio && g_hasWaveSnd) PlaySound(g_waveSnd);
            DebugLog("boss","\"wave\":%d,\"hp\":%.0f", g_wave, g_enemies[slot].maxHp);
        }
    } else if (alive==0){                              // boss down + field clear -> breather, then next wave
        g_waveBreather=6.0f;
        if (g_audio && g_hasClearSnd) PlaySound(g_clearSnd);
        DebugLog("wave","\"clear\":%d,\"kills\":%d", g_wave, g_kills);
    }
}

static Texture2D MakeChecker(int sz, Color a, Color b, int cells) {
    Image img = GenImageChecked(sz, sz, sz/cells, sz/cells, a, b);
    Texture2D t = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

static void SpawnImpact(Vector3 p, Vector3 n) {
    for (int i=0;i<14;i++){
        Spark *slot=SparkAlloc(); if (!slot) break;
        Vector3 v={ n.x+GetRandomValue(-100,100)/100.0f, n.y+GetRandomValue(-20,120)/100.0f, n.z+GetRandomValue(-100,100)/100.0f };
        v=Vector3Scale(Vector3Normalize(v), 3.0f+GetRandomValue(0,300)/100.0f);
        float lf=0.4f+GetRandomValue(0,30)/100.0f;
        Spark sp={0}; sp.pos=p; sp.vel=v; sp.life=lf; sp.life0=lf; sp.size=0.04f; sp.rest=p.y; sp.blood=0;
        sp.col=(Color){255,(unsigned char)(200+GetRandomValue(0,55)),60,255};
        *slot=sp;
    }
}

// Detailed blood burst: a spray of crimson droplets of varied size flung along
// the bullet's travel (dir) in a wide cone, plus heavier slow "gobs" every few
// particles. Gravity + fade are handled by the shared spark integrator/draw.
static void SpawnBlood(Vector3 p, Vector3 dir, float intensity, float floorY) {
    int n=(int)(200*intensity); if (n>900) n=900;          // headshots spray harder (capped)
    for (int i=0;i<n;i++){
        Spark *slot=SparkAlloc(); if (!slot) break;
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
        *slot=sp;
    }
    // A flat continuous pool stain spreading from the body's feet -- this is what
    // reads as "liquid"; the flying gobs above just stipple fine spatter around it.
    { Color pc={ (unsigned char)(105+GetRandomValue(0,35)), (unsigned char)GetRandomValue(0,12), (unsigned char)GetRandomValue(0,8), 255 };
      unsigned int seed=(unsigned int)GetRandomValue(1,1<<30);
      StainAddPool(p.x, floorY, p.z, 0.20f+0.11f*intensity, seed, pc); }   // pool radius; headshots pool wider
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
        float sc=kEnemy[g_enemies[i].type].scale*g_enemies[i].sc;    // hitbox tracks the drawn size (head chef looms)
        float er=ENEMY_RADIUS*sc, eh=ENEMY_HEIGHT*sc;
        BoundingBox bb={ (Vector3){c.x-er, c.y, c.z-er},
                         (Vector3){c.x+er, c.y+eh, c.z+er} };
        RayCollision rc=GetRayCollisionBox((Ray){ro,rd},bb);
        if (rc.hit && rc.distance>0 && rc.distance<bd){
            bd=rc.distance; best=i; *outDist=rc.distance; *outPt=rc.point;
            // The head box lives inside the body box's vertical span, so the
            // nearest-enemy pick above is unaffected; we just additionally ask
            // whether this same ray clips the (narrower) head of that enemy.
            // A headshot is judged where the shot passes the body's CENTRE LINE: its height there must
            // be in the head zone and it must pass within a head's width of the axis. (It used to be
            // "the ray clips the head box anywhere" -- and the body box is over a metre deep on a boss,
            // so a rising shot into the chest came out through the back of the head box: a headshot.)
            if (outHead){
                float hx=rd.x, hz=rd.z, h2=hx*hx+hz*hz, ty=rc.point.y, miss=0.0f;
                if (h2>1e-6f){ float t=((c.x-ro.x)*hx+(c.z-ro.z)*hz)/h2; if (t<0) t=0;
                    ty=ro.y+rd.y*t; float px=ro.x+rd.x*t-c.x, pz=ro.z+rd.z*t-c.z; miss=sqrtf(px*px+pz*pz); }
                *outHead = (ty>=c.y+eh-HEAD_ZONE_H*sc && ty<=c.y+eh+0.05f && miss<=HEAD_RADIUS*sc);
            }
        }
    }
    return best;
}

// Apply damage to alive enemy ei (dmg already includes any headshot multiplier).
// Shared by every weapon: bullets, the flame cone, and the knife. Handles the
// flinch, the kill -> dying state + tally + death sound. blood>0 spurts blood at
// hitPt along dir; head>0 also pops the "HEADSHOT!" flash + counts a headshot kill.
// Which flinch a shot earns, from where it landed: a headshot snaps the head back, the
// lower legs buckle a knee, a shot wide of centre spins that shoulder back (the enemy
// faces its shooter, so the shooter's right is the enemy's left), anything else is centre mass.
// ---- Wounds that stay: each bullet leaves a blood stain pinned to the bone it struck, at the point
// where the shot's path meets that body part (the hit box is wider than the body, so the raw hit
// point would float in front of it). Re-posed every frame from the bone, so it rides the animation
// and lies on the corpse.
static float WoundBoneRadius(const char *n){                       // rough body-part radius, metres
    if (strstr(n,"Head")) return 0.095f;  if (strstr(n,"Neck")) return 0.055f;
    if (strstr(n,"Spine")||strstr(n,"Hips")) return 0.14f;
    if (strstr(n,"ForeArm")) return 0.045f;  if (strstr(n,"Shoulder")) return 0.07f;  if (strstr(n,"Arm")) return 0.055f;
    if (strstr(n,"UpLeg")) return 0.075f;  if (strstr(n,"Leg")) return 0.055f;
    return 0.04f;
}
static int WoundBoneOK(const char *n){
    return !(strstr(n,"Finger")||strstr(n,"Thumb")||strstr(n,"Toe")||strstr(n,"Tent")||strstr(n,"End")||strstr(n,"Eye")||strstr(n,"jaw")||strstr(n,"Hand")||strstr(n,"Foot"));
}
static Vector3 WoundWorld(ModelAnimation *an, float frame, int b, Vector3 L, Vector3 base, float yaw, float esc){
    Transform t=RigSample(an,frame,b);
    Vector3 m=Vector3Add(t.translation, Vector3RotateByQuaternion(L,t.rotation));
    return Vector3Add(base, Vector3RotateByAxisAngle(Vector3Scale(m,esc),(Vector3){0,1,0},yaw*DEG2RAD));
}
#define WSAMPLE(b) (ps ? ps[b] : RigSample(an,fr,b))          /* the pose he's drawn in, else the clip */
static void EnemyAddWound(Enemy *e, Vector3 hitPt, Vector3 dir){
    EnemyRig *rg=RigOf(e->type); if (rg->animN<=0) return;
    const Transform *ps=e->poseOK ? e->pose : NULL;
    ModelAnimation *an=e->drawAn ? e->drawAn : RigClip(e->type,e->clip);
    float fr=e->drawAn ? e->drawFrame : e->animT;
    float yaw=ps ? e->yaw : e->drawAn ? e->drawYaw : atan2f(g_pos.x-e->pos.x, g_pos.z-e->pos.z)*RAD2DEG + ENEMY_YAW_OFFSET;
    float esc=ENEMY_SCALE*(kEnemy[e->type].scale*e->sc); if (esc<=0) return;
    Vector3 base={e->pos.x, e->drawY, e->pos.z};
    Vector3 o=Vector3Scale(Vector3RotateByAxisAngle(Vector3Subtract(hitPt,base),(Vector3){0,1,0},-yaw*DEG2RAD),1.0f/esc);   // model space
    Vector3 u=Vector3Normalize(Vector3RotateByAxisAngle(dir,(Vector3){0,1,0},-yaw*DEG2RAD));
    const BoneInfo *bi=rg->model.skeleton.bones; int nb=rg->model.skeleton.boneCount;
    int best=-1; float bestScore=1e9f, bestS=0, bestT=0, bestR=0; Vector3 bJ={0}, bC={0};
    for (int b=0;b<nb;b++){
        if (!WoundBoneOK(bi[b].name)) continue;
        Vector3 J=WSAMPLE(b).translation, C=J;
        for (int c=0;c<nb;c++) if (bi[c].parent==b){ C=WSAMPLE(c).translation; break; }
        Vector3 v=Vector3Subtract(C,J), w0=Vector3Subtract(o,J);   // closest approach: ray o+s*u vs segment J+t*v
        float bb=Vector3DotProduct(u,v), cc=Vector3DotProduct(v,v), dd=Vector3DotProduct(u,w0), ee=Vector3DotProduct(v,w0);
        float t = (cc>1e-8f) ? (ee - bb*(-dd))/cc : 0.0f;          // first guess with s = -dd (ray dir is unit)
        float den=cc-bb*bb, sv;
        if (den>1e-8f){ sv=(bb*ee-cc*dd)/den; t=(ee+bb*sv)/cc; } else sv=-dd;
        if (t<0) t=0; if (t>1) t=1;
        Vector3 Q=Vector3Add(J,Vector3Scale(v,t)); sv=Vector3DotProduct(Vector3Subtract(Q,o),u);
        Vector3 P=Vector3Add(o,Vector3Scale(u,sv));
        float dist=Vector3Distance(P,Q), r=WoundBoneRadius(bi[b].name)/esc;
        float score=dist-r;
        if (score<bestScore){ bestScore=score; best=b; bestS=sv; bestT=t; bestR=r; bJ=J; bC=C; }
    }
    if (best<0 || bestScore>0.06f/esc) return;                    // the shot clipped the box, not the body
    Vector3 Q=Vector3Add(bJ,Vector3Scale(Vector3Subtract(bC,bJ),bestT)), P=Vector3Add(o,Vector3Scale(u,bestS));
    float dist=Vector3Distance(P,Q); Vector3 S;
    if (dist<bestR){ float back=sqrtf(bestR*bestR-dist*dist); S=Vector3Add(o,Vector3Scale(u,bestS-back)); }   // where the path enters
    else S=Vector3Add(Q,Vector3Scale(Vector3Normalize(Vector3Subtract(P,Q)),bestR));                          // a graze
    Transform tb=WSAMPLE(best);
    int k=e->wNext; e->wNext=(e->wNext+1)%MAX_WOUNDS; if (e->wN<MAX_WOUNDS) e->wN++;
    e->wBone[k]=best; e->wOff[k]=Vector3RotateByQuaternion(Vector3Subtract(S,tb.translation),QuaternionInvert(tb.rotation));
    e->wSz[k]=0.055f+GetRandomValue(0,30)/1000.0f;
}
static const Transform *g_woundPose=NULL;                      // set around DrawWounds to place them on a stored pose
static void DrawWounds(ModelAnimation *an, float frame, Vector3 base, float yaw, float esc,
                       int n, const int *wb, const Vector3 *wo, const float *ws){
    if (!g_hasSmokeTex || (!an && !g_woundPose)) return;
    for (int k=0;k<n;k++){
        Vector3 p;
        if (g_woundPose){ Transform t=g_woundPose[wb[k]]; Vector3 m=Vector3Add(t.translation, Vector3RotateByQuaternion(wo[k],t.rotation));
            p=Vector3Add(base, Vector3RotateByAxisAngle(Vector3Scale(m,esc),(Vector3){0,1,0},yaw*DEG2RAD)); }
        else p=WoundWorld(an,frame,wb[k],wo[k],base,yaw,esc);
        Vector3 tc=Vector3Subtract(g_cam.position,p); float L=Vector3Length(tc);
        if (L>1e-3f) p=Vector3Add(p,Vector3Scale(tc,0.012f/L));   // just proud of the skin
        DrawBillboard(g_cam, g_smokeTex, p, ws[k]*1.6f, (Color){110,6,6,215});    // the stain
        DrawBillboard(g_cam, g_smokeTex, p, ws[k]*0.55f, (Color){28,0,0,255});    // the hole
    }
}

// What the damage came from, for the death/knockdown pick: callers set it around HurtEnemy.
enum { HC_BULLET=0, HC_BLAST, HC_FLAME, HC_MELEE };
#define PAIN_DMG 28.0f          // this much health lost inside ~2.5 s may draw a cry of pain...
#define PAIN_GAP 5.0f           // ...at most once in this many seconds
#define CHARGE_VOICE_RANGE 14.0f
#define RUN_SND_SPEED 3.2f      // m/s: faster than this is running
#define RUN_SND_RANGE 15.0f
#define SURR_N 4                // this many hostiles inside SURR_RANGE, on both sides of him = surrounded
#define SURR_RANGE 5.5f
#define MOB_N 5                 // this many hostiles on their feet within MOB_RANGE of the player = a charging pack
#define MOB_RANGE 16.0f
#define MULTIKILL_N 3           // one grenade killing this many sets off the crowd scream
#define GIB_BLAST_DMG 100.0f     // a grenade doing this much to the round that kills him blows him apart
static int g_hurtCause=HC_BULLET; static Vector3 g_hurtFrom;   // g_hurtFrom: the blast centre
#define LIMP_TIME     6.0f      // seconds of limping after a leg hit
#define LIMP_SPEED    0.85f     // m/s while limping (the clip plays faster to keep the feet planted)
#define LIMP_CLIP_MPS 0.52f     // the limp take's own walking speed
#define KNOCK_SHOT_DMG 50.0f    // one shotgun blast doing this much (within KNOCK_SHOT_DIST) flattens him
#define KNOCK_SHOT_DIST 7.0f
#define KNOCK_BLAST_DMG 20.0f   // a grenade doing this much
#define HIT_ACC_WINDOW 0.15f    // hits landing within this of each other add up (a shotgun's pellets, a burst)...
#define STAGGER_DMG    35.0f    // ...and this much at once (or 30% of his max HP if more) staggers him
#define STAGGER_TIME   0.55f    // seconds off balance, stepping back
#define STAGGER_PUSH   3.2f     // m/s shove at mass 1 (a cook is 0.8, the head chef 4)
#define HIT_SLOW       0.40f    // his pace while flinching from a light hit
#define ENEMY_ACCEL    14.0f    // m/s^2 getting up to speed...
#define ENEMY_BRAKE    22.0f    // ...and pulling up
#define FLANK_DIST     11.0f    // inside this, in sight, chasers fan out to their own side of the player
#define FLANK_GAIN     0.55f
#define SEP_DIST       1.8f     // chasers steer apart inside this
#define SEP_GAIN       0.9f
#define LUNGE_MAX      1.0f     // a running enemy starts his swing up to this far outside his reach
#define AIM_SETTLE     0.45f    // a shooter holds his aim on the player this long before he fires
#define LOST_TIME      0.7f     // ...and gives the position up after this long without a sight of him
#define BACKOFF_FRAC   0.30f    // shooters give ground inside this fraction of their range
#define RELOCATE_PCT   65       // chance a shooter shifts position after a shot / burst
#define TWIST_MAX      75.0f    // chest turned off the hips, degrees
#define HEAD_MAX       35.0f    // head turned further still
static float EnemyYawAway(const Enemy *e, Vector3 from){        // a yaw whose "forward" points away from `from`
    float ax=e->pos.x-from.x, az=e->pos.z-from.z;
    return atan2f(ax,az)*RAD2DEG + ENEMY_YAW_OFFSET;
}
// Flatten an alive enemy: the knockdown clip (fall, lie there, get up) gates his AI the way a
// flinch does (hitT), and he's shoved along `push` (m/s, decays; walls stop it).
static void EnemyKnock(int ei, Vector3 push){
    Enemy *e=&g_enemies[ei]; if (e->state!=1 || e->knockT>0) return;
    EnemyRig *rg=RigOf(e->type); if (rg->animN<=0) return;
    float dur=ANIM_FRAMES(rg->anim[rg->clip[CL_KNOCK]])/60.0f;   // baked at 30 samples/s on a 60 fps timeline
    e->knockT=dur; if (e->hitT<dur) e->hitT=dur;
    e->pushV=(Vector3){push.x,0,push.z};
    if (g_audio && g_hasKnockSnd){ SetSoundPitch(g_knockSnd,0.92f+GetRandomValue(0,16)/100.0f); SfxAt(g_knockSnd, e->pos, 0.9f); }
    DebugLog("knock","\"i\":%d,\"dur\":%.2f,\"push\":%.2f", ei, dur, Vector3Length(push));
}
static int HitZoneRole(const Enemy *e, int head, Vector3 dir, Vector3 hitPt){
    if (head) return CL_HIT_HEAD;
    float sc=(kEnemy[e->type].scale*e->sc);
    float h=(hitPt.y-e->pos.y)/(ENEMY_HEIGHT*sc);
    if (h<0.42f) return CL_HIT_LEG;
    Vector3 r=Vector3CrossProduct(dir,(Vector3){0,1,0});
    float rl=Vector3Length(r); if (rl<1e-3f) return CL_HIT;              // straight down (a blast): centre
    float s=((hitPt.x-e->pos.x)*r.x + (hitPt.z-e->pos.z)*r.z)/rl/(ENEMY_RADIUS*sc);
    if (s> 0.35f) return CL_HIT_LEFT;
    if (s<-0.35f) return CL_HIT_RIGHT;
    return CL_HIT;
}
// ---- Enemy voices: every type has its own slot for each cue -----------------
// Drop a file at assets/voice/<enemy>_<cue>.(wav|mp3) -- cue = charge | attack | hurt | death --
// and that enemy uses it; nothing else to wire. A type without its own file borrows its
// family's (the kitchen staff from the cook, the gunner from the rifleman, the fiend from the
// liquidator); with neither, the game falls back to the shared sound for that cue (attack roar,
// death cries) or stays silent (charge, hurt). The sound board (--sounds) lists all 44 slots.
enum { EC_CHARGE=0, EC_ATTACK, EC_HURT, EC_DEATH, EC_N };
#define EC_TAKES 10                                       // a slot can hold several takes: <enemy>_<cue>.wav, <enemy>_<cue>2.wav ... one at random
static const char *kECue[EC_N]={"charge","attack","hurt","death"};
static const int kEVoiceKin[EN_NTYPES]={ -1, EN_COOK, -1, -1, EN_COOK, -1, EN_RIFLEMAN, -1, -1, -1, EN_LIQUIDATOR };   // whose voice he borrows (head chef + line cook: the cook)
static Sound g_eCue[EN_NTYPES][EC_N][EC_TAKES]; static int g_hasECue[EN_NTYPES][EC_N]; static float g_eCueCd[EN_NTYPES][EC_N];   // g_hasECue = takes loaded
static char  g_eCuePath[EN_NTYPES][EC_N][64];
static void EnemyVoiceLoad(void){
    for (int t=0;t<EN_NTYPES;t++) for (int c=0;c<EC_N;c++){
        const char *ext[2]={"wav","mp3"}; g_hasECue[t][c]=0;
        snprintf(g_eCuePath[t][c],sizeof g_eCuePath[t][c],"assets/voice/%s_%s.mp3",kEnemy[t].name,kECue[c]);
        for (int v=0;v<EC_TAKES;v++){ int got=0;
            for (int x=0;x<2 && !got;x++){ char p[64], num[4]=""; if (v>0) snprintf(num,sizeof num,"%d",v+1);
                snprintf(p,sizeof p,"assets/voice/%s_%s%s.%s",kEnemy[t].name,kECue[c],num,ext[x]);
                if (FileExists(p)){ Sound sd=LoadSound(p); if (sd.frameCount>0){ g_eCue[t][c][g_hasECue[t][c]++]=sd; got=1;
                    if (v==0) snprintf(g_eCuePath[t][c],sizeof g_eCuePath[t][c],"%s",p); } } }
            if (!got && v>0) break; }                         // takes are numbered without gaps
    }
}
static int EnemyVoiceOwner(int type, int cue){            // the type whose file(s) this type plays for the cue, or -1
    if (g_hasECue[type][cue]>0) return type;
    int k=kEVoiceKin[type]; return (k>=0 && g_hasECue[k][cue]>0) ? k : -1;
}
static int EnemyVoice(const Enemy *e, int cue, float gain, float gap){   // 1 = this type has a voice for the cue (played unless still cooling down)
    if (!g_audio) return 0;
    int o=EnemyVoiceOwner(e->type,cue); if (o<0) return 0;
    if (g_eCueCd[o][cue]>0.0f) return 1;
    Sound sd=g_eCue[o][cue][GetRandomValue(0,g_hasECue[o][cue]-1)];
    SetSoundPitch(sd, 0.94f+GetRandomValue(0,12)/100.0f);
    SfxAt(sd, e->pos, gain); g_eCueCd[o][cue]=gap;
    return 1;
}
static void EnemyVoiceStopAll(void){ for (int t=0;t<EN_NTYPES;t++) for (int c=0;c<EC_N;c++) for (int v=0;v<g_hasECue[t][c];v++) StopSound(g_eCue[t][c][v]); }

// Gibs: a body caught at the heart of a blast, or by a shotgun at arm's length, comes apart --
// chunks of meat and bone thrown out, bouncing and leaving blood where they land. No corpse.
typedef struct { Vector3 pos, vel; float life, floorY, size; int bounces; Color col; } Gib;
#define MAX_GIBS 96
static Gib g_gibs[MAX_GIBS]; static int g_gibNext=0;
static void SpawnGibs(Vector3 c, Vector3 away, float floorY, float scale){
    for (int k=0;k<13;k++){
        Gib g={0}; float a=GetRandomValue(0,628)/100.0f, sp=2.0f+GetRandomValue(0,450)/100.0f;
        g.pos=(Vector3){c.x+cosf(a)*0.15f, c.y+GetRandomValue(-40,50)/100.0f*scale, c.z+sinf(a)*0.15f};
        g.vel=(Vector3){cosf(a)*sp+away.x*2.6f, 2.5f+GetRandomValue(0,450)/100.0f, sinf(a)*sp+away.z*2.6f};
        g.size=(0.045f+GetRandomValue(0,60)/1000.0f)*scale; g.life=7.0f+GetRandomValue(0,300)/100.0f; g.floorY=floorY;
        g.col=(k%4==3) ? (Color){170,160,140,255} : (Color){(unsigned char)(90+GetRandomValue(0,40)),12,10,255};   // bone among the meat
        g_gibs[g_gibNext]=g; g_gibNext=(g_gibNext+1)%MAX_GIBS;
    }
    for (int k=0;k<3;k++) SpawnBlood(c,(Vector3){away.x*0.5f+GetRandomValue(-60,60)/100.0f, 0.5f, away.z*0.5f+GetRandomValue(-60,60)/100.0f}, 2.2f, floorY);
}
static void GibsUpdate(float dt){
    for (int i=0;i<MAX_GIBS;i++){ Gib *g=&g_gibs[i]; if (g->life<=0) continue;
        g->life-=dt; if (g->bounces>=3) continue;
        g->vel.y-=16.0f*dt;
        Vector3 st=Vector3Scale(g->vel,dt); float sl=Vector3Length(st);
        if (g_hasMap && g_mapCol.ready && sl>1e-5f){              // walls stop them
            float w=MapRayNearest(g->pos,Vector3Scale(st,1.0f/sl),sl+g->size);
            if (w>0.0f && fabsf(st.y)<sl*0.9f){ g->vel.x*=-0.3f; g->vel.z*=-0.3f; st=(Vector3){0,st.y,0}; }
        }
        g->pos=Vector3Add(g->pos,st);
        if (g->pos.y<=g->floorY+g->size*0.6f && g->vel.y<0){
            g->pos.y=g->floorY+g->size*0.6f; g->bounces++;
            SpawnBlood(g->pos,(Vector3){0,0.2f,0},0.05f,g->floorY);                 // a wet mark where it lands
            if (g->bounces>=3 || fabsf(g->vel.y)<1.2f){ g->bounces=3; g->vel=(Vector3){0,0,0}; }
            else { g->vel.y*=-0.30f; g->vel.x*=0.5f; g->vel.z*=0.5f; }
        }
    }
}
static void DrawGibs(void){
    for (int i=0;i<MAX_GIBS;i++){ Gib *g=&g_gibs[i]; if (g->life<=0) continue;
        Color c=g->col; if (g->life<1.0f) c.a=(unsigned char)(255*g->life);
        DrawSphereEx(g->pos, g->size, 4, 6, c); }
}
static void HurtEnemy(int ei, float dmg, int head, Vector3 dir, Vector3 hitPt, int blood){
    if (ei<0 || g_enemies[ei].state!=1) return;
    if (blood) SpawnBlood(hitPt, dir, head?1.9f:1.0f, g_enemies[ei].pos.y);
    if (blood && g_hurtCause==HC_BULLET) EnemyAddWound(&g_enemies[ei], hitPt, dir);
    if (g_quadT>0.0f) dmg*=4.0f;                          // QUAD DAMAGE power-up
    g_enemies[ei].hp -= dmg;
    if (head) g_hsFlash=1.1f;
    if (g_enemies[ei].hp<=0){
        g_enemies[ei].prevClip=g_enemies[ei].clip; g_enemies[ei].prevT=g_enemies[ei].animT; g_enemies[ei].blendT=ENEMY_BLEND;
        EnemyPoseSnap(&g_enemies[ei],1);                      // the fall eases out of whatever he was doing
        g_enemies[ei].state=2; g_enemies[ei].deathT=0; g_enemies[ei].animT=0; g_killMark=0.32f;
        { Enemy *e=&g_enemies[ei]; const Weapon *cw=&g_weapons[g_curWeapon];   // how he dies: by cause, then where it hit
          e->deathYaw=atan2f(g_pos.x-e->pos.x, g_pos.z-e->pos.z)*RAD2DEG + ENEMY_YAW_OFFSET;   // facing the shooter
          e->pushV=(Vector3){0,0,0};
          if (g_hurtCause==HC_BLAST){ e->deathRole=CL_DEATH_BLAST; e->deathYaw=EnemyYawAway(e,g_hurtFrom);
              Vector3 a={e->pos.x-g_hurtFrom.x,0,e->pos.z-g_hurtFrom.z}; float al=Vector3Length(a);
              if (al>1e-3f) e->pushV=Vector3Scale(a, fminf(6.0f, 2.0f+dmg*0.03f)/al); }        // thrown clear of it
          else if (g_hurtCause==HC_BULLET && head) e->deathRole=CL_DEATH_HEAD;
          else if (g_hurtCause==HC_BULLET && HitZoneRole(e,0,dir,hitPt)==CL_HIT_LEG) e->deathRole=CL_DEATH_LEG;
          else if (g_hurtCause==HC_BULLET && (cw->spinUp || cw->soundGated)) e->deathRole=CL_DEATH_RIDDLED;
          else e->deathRole=CL_DEATH;
          if (g_hurtCause==HC_BULLET){                         // the round that kills him carries the body with it
              Vector3 pd={dir.x,0,dir.z}; float pl=Vector3Length(pd), mass=fmaxf(0.8f, kEnemy[e->type].hp/100.0f);
              float shove=fminf(3.8f, (cw->pellets>1?2.6f:1.7f)+dmg*0.02f);          // thrown back off his feet, not dropped where he stood
              if (e->type==EN_RIFLEMAN || e->type==EN_GUNNER) shove*=1.45f;         // the soldiers go down hardest: shot DOWN
              if (pl>1e-3f) e->pushV=Vector3Scale(pd, shove/(pl*mass)); }
          { float pd2=Vector3Distance(e->pos,g_pos);           // torn apart: the heart of a blast, or buckshot at arm's length
            int gib = !e->boss && e->type!=EN_HEADCHEF &&
                      ((g_hurtCause==HC_BLAST && dmg>=GIB_BLAST_DMG && GetRandomValue(0,99)<65) || (g_hurtCause==HC_BULLET && cw->pellets>1 && pd2<3.2f && GetRandomValue(0,99)<45));
            if (gib){
                Vector3 c={e->pos.x, e->pos.y+ENEMY_HEIGHT*0.55f, e->pos.z}, aw=(g_hurtCause==HC_BLAST)?(Vector3){e->pos.x-g_hurtFrom.x,0,e->pos.z-g_hurtFrom.z}:(Vector3){dir.x,0,dir.z};
                float al=Vector3Length(aw); aw=(al>1e-3f)?Vector3Scale(aw,1.0f/al):(Vector3){0,0,0};
                SpawnGibs(c, aw, e->pos.y, (kEnemy[e->type].scale*e->sc));
                if (g_audio && g_hasGibSnd){ SetSoundPitch(g_gibSnd,0.92f+GetRandomValue(0,16)/100.0f); SfxAt(g_gibSnd, c, 1.0f); }
                e->state=0;                                    // nothing left to fall over
                DebugLog("gib","\"i\":%d,\"cause\":%d", ei, g_hurtCause); } }
          DebugLog("edeath","\"i\":%d,\"role\":%d,\"cause\":%d", ei, e->deathRole, g_hurtCause); }
        g_enemies[ei].drawY=g_enemies[ei].pos.y;          // settle the render height so the corpse doesn't pop
        if (g_enemies[ei].boss && g_audio && g_hasBossDeadSnd){                      // the boss is down
            if (g_hasBossSnd) StopSound(g_bossSnd);
            SetSoundVolume(g_bossDeadSnd,0.85f); PlaySound(g_bossDeadSnd); DebugLog("bossdead","\"wave\":%d", g_wave); }
        g_kills++; if (head) g_headshots++;
        if (EnemyVoice(&g_enemies[ei], EC_DEATH, 1.0f, 0.0f)){}                       // his kind's own death, when it has one
        else if (g_audio && g_hasSgDeathSnd && g_hurtCause==HC_BULLET && g_weapons[g_curWeapon].pellets>1 && GetRandomValue(0,99)<45)
            SfxAt(g_sgDeathSnd, g_enemies[ei].pos, 1.0f);                             // killed by buckshot: sometimes this scream instead
        else if (g_audio && g_nDeathSnd>0){ SfxAt(g_deathSnd[GetRandomValue(0,g_nDeathSnd-1)], g_enemies[ei].pos, 1.0f); }
        if (g_audio && g_hasSy){ SetSoundPitch(g_sy[SY_KILL], 0.92f+GetRandomValue(0,16)/100.0f); SetSoundVolume(g_sy[SY_KILL],0.55f); PlaySound(g_sy[SY_KILL]); }   // the kill, confirmed
        { void PickupOnKillAt(int ei); PickupOnKillAt(ei); }
    } else {
        // flame ticks every frame: re-flinching on each one froze a burning enemy in place.
        // Fire flinches him at most once per FLAME_FLINCH_GAP; he keeps coming through it
        if (g_hurtCause==HC_FLAME){
            if (GetTime()-g_enemies[ei].flameFlinchAt < FLAME_FLINCH_GAP) return;
            g_enemies[ei].flameFlinchAt=GetTime(); }
        if (g_enemies[ei].hitT<0.45f) g_enemies[ei].hitT=0.45f;   // non-fatal hit -> flinch (never cuts a knockdown short)
        if (g_hurtCause!=HC_FLAME) EnemyVoice(&g_enemies[ei], EC_HURT, 0.8f, 1.2f);   // his own grunt, if his kind has one
        g_enemies[ei].hitRole=HitZoneRole(&g_enemies[ei], head, dir, hitPt);   // ...for where it landed
        { Enemy *e=&g_enemies[ei]; e->rehit=1;                 // a fresh hit re-jolts a flinch already playing
          // The hit shoves him: a jolt along the shot for every bullet (he keeps coming, slowed), and a
          // STAGGER -- knocked back a few steps, off balance -- when enough lands at once for his size.
          float mass=kEnemy[e->type].hp/100.0f; if (mass<0.8f) mass=0.8f; if (mass>4.0f) mass=4.0f;
          Vector3 pd={dir.x,0,dir.z}; float pl=Vector3Length(pd);
          if (g_hurtCause==HC_BLAST){ pd=(Vector3){e->pos.x-g_hurtFrom.x,0,e->pos.z-g_hurtFrom.z}; pl=Vector3Length(pd); }
          if (pl>1e-3f && g_hurtCause!=HC_FLAME){
              pd=Vector3Scale(pd,1.0f/pl);
              if (e->hitAccT<=0) e->hitAcc=0;
              e->hitAcc+=dmg; e->hitAccT=HIT_ACC_WINDOW;
              float need=fmaxf(STAGGER_DMG, e->maxHp*0.30f);
              if (e->hitAcc>=need && e->stagT<=0 && e->knockT<=0){
                  e->stagT=STAGGER_TIME; e->hitAcc=0; if (e->hitT<STAGGER_TIME) e->hitT=STAGGER_TIME;
                  e->vel=Vector3Add(Vector3Scale(e->vel,0.3f), Vector3Scale(pd,STAGGER_PUSH/mass));
                  DebugLog("stagger","\"i\":%d,\"push\":%.2f", ei, STAGGER_PUSH/mass);
              } else if (e->stagT<=0){
                  float j=fminf(2.4f, dmg*0.05f)/mass;
                  e->vel=Vector3Add(e->vel, Vector3Scale(pd,j));
              }
          } }
        DebugLog("flinch","\"i\":%d,\"role\":%d,\"at\":[%.2f,%.2f,%.2f]", ei, g_enemies[ei].hitRole, hitPt.x,hitPt.y,hitPt.z);
        if (g_enemies[ei].hitRole==CL_HIT_LEG && g_hurtCause==HC_BULLET){            // a leg hit: he limps for a while
            if (g_enemies[ei].limpT<=0) DebugLog("limp","\"i\":%d", ei);
            g_enemies[ei].limpT=LIMP_TIME; }
        if (g_hurtCause==HC_BLAST && dmg>=KNOCK_BLAST_DMG){                            // a blast that doesn't kill flattens him
            Enemy *e=&g_enemies[ei]; Vector3 a={e->pos.x-g_hurtFrom.x,0,e->pos.z-g_hurtFrom.z}; float al=Vector3Length(a);
            EnemyKnock(ei, al>1e-3f ? Vector3Scale(a, fminf(5.0f, 1.5f+dmg*0.03f)/al) : (Vector3){0,0,0}); }
    }
}

// ---- Grenade launcher (slot 0) -- arcing projectile + radial explosion ----------
typedef struct { Vector3 pos, vel; float life; int active; int bounces; } Grenade;
#define MAX_GREN     8
#define GREN_SPEED   30.0f     // launch speed (units/s)
#define GREN_GRAV    24.0f     // downward accel -> the lob arc
#define GREN_RADIUS  4.8f      // explosion blast radius
#define GREN_DMG     140.0f    // damage at the centre, linear falloff to 0 at the edge
#define GREN_GLANCE  0.42f     // a hit shallower than this (cosine to the surface normal, ~25 deg off the surface) bounces
#define GREN_LIFE    4.0f      // fuse: detonates after this long if it hasn't hit anything
#define GL_FIRE_CD   0.85f     // seconds between launches
// 'allanims' frame layout (605 frames total). Wrist-motion analysis of the clip shows
// the hold pose at frame 0 (and ~531-573), with action bursts at ~127-131, ~179-183,
// ~388-392 -- the first burst pair reads as the shell-load action:
#define GL_IDLE_FRAME   0      // rest/hold pose kept between shots
// The launcher's one baked clip ('allanims', 605 frames) holds the whole reload: the drum swings
// open (30-90), six passes of the left hand feeding a grenade (one every ~54 frames from 90),
// and the swing shut (395-440). The reload plays open -> one feed pass per grenade, repeated
// until the drum is full or the reserve is dry -> shut. Each pass loads ONE round as the hand
// reaches the drum, so pulling the trigger with a round in can cut it short (it shuts fast,
// then fires) -- reload one, shoot it, reload again, as the shotgun does.
#define GL_OPEN_FROM    30
#define GL_OPEN_TO      90
#define GL_FEED_FROM    90
#define GL_FEED_AT      113    // the grenade goes in
#define GL_FEED_TO      144
#define GL_SHUT_FROM    395
#define GL_SHUT_TO      440

static Grenade g_gren[MAX_GREN];
// Viewmodel placement: canonical gun frame (ComputeVmFrame) -> view space, where the
// vm camera sits at the origin looking -Z. Class length * trim scale -> trim rotation
// + recoil/sway/raise -> aim at the crosshair convergence point -> class position +
// trim offset + bob/sway/recoil/raise. The draw prepends w->vmAlign.
static float g_vmAimR=0.05f, g_vmAimD=0.04f;   // barrel tilt right/down (VMAIM="r d" dev override)
static Matrix VmPlace(const Weapon *w){
    float bobX=sinf(g_bob)*0.010f, bobY=-fabsf(cosf(g_bob))*0.008f;
    // recoil: muzzle kicks UP (pitch) and the gun shoves back, then settles.
    float r=g_recoil, ease=g_vmRaise*g_vmRaise;
    Vector3 cp=kVmClass[w->vmClass].pos;
    Vector3 P={ cp.x+g_vmOff.x+bobX - g_vmSway.x*0.0022f,
                cp.y+g_vmOff.y+bobY + g_vmSway.y*0.0022f + r*0.010f - ease*0.30f,
                cp.z+g_vmOff.z + r*0.045f };
    // aim the barrel at the crosshair convergence point
    // (aimed a touch right of and below it: a low-right gun aimed dead parallel still reads as
    // pointing up-left in perspective -- VM_AIM_R/VM_AIM_D are that tilt in view units per unit ahead)
    float conv=kVmClass[w->vmClass].conv;
    Vector3 fwd=Vector3Normalize((Vector3){-P.x+conv*g_vmAimR,-P.y-conv*g_vmAimD,-conv-P.z});
    Vector3 rt=Vector3Normalize(Vector3CrossProduct(fwd,(Vector3){0,1,0}));
    Vector3 upv=Vector3CrossProduct(rt,fwd);
    Matrix aim={ rt.x, upv.x, -fwd.x, 0,
                 rt.y, upv.y, -fwd.y, 0,
                 rt.z, upv.z, -fwd.z, 0,
                 0,    0,     0,      1 };
    Matrix trimR=MatrixMultiply(MatrixRotateZ(DEG2RAD*(g_vmRoll + g_vmSway.x*0.35f)),
                 MatrixMultiply(MatrixRotateX(DEG2RAD*(g_vmPitch + r*5.0f - ease*25.0f)), MatrixRotateY(DEG2RAD*g_vmYaw)));
    float L=kVmClass[w->vmClass].len*g_vmScale;
    return MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(L,L,L),trimR),aim),MatrixTranslate(P.x,P.y,P.z));
}
// The muzzle tip as a WORLD point `depth` units ahead of the eye, on the same screen
// ray as the drawn barrel. The vm camera's FOV differs from the world camera's, so
// the view-space tip is re-projected rather than copied. Effects spawned here
// (tracers, flame, smoke) leave the gun wherever the class table frames it.
static Vector3 VmMuzzleWorld(Vector3 eye, Vector3 fwd, float depth){
    const Weapon *w=&g_weapons[g_curWeapon];
    Vector3 m=Vector3Transform((Vector3){0,0,-0.5f}, VmPlace(w));
    if (m.z>-1e-3f) return Vector3Add(eye,Vector3Scale(fwd,depth));
    float T=tanf(DEG2RAD*g_cam.fovy*0.5f)/tanf(DEG2RAD*g_vmCam.fovy*0.5f);
    float sx=m.x/(-m.z)*T, sy=m.y/(-m.z)*T;
    Vector3 right=Vector3Normalize(Vector3CrossProduct(fwd,(Vector3){0,1,0}));
    Vector3 up=Vector3CrossProduct(right,fwd);
    return Vector3Add(eye, Vector3Scale(Vector3Add(fwd, Vector3Add(Vector3Scale(right,sx),Vector3Scale(up,sy))), depth));
}
static void LaunchGrenade(Camera3D cam){
    Vector3 dir=Vector3Normalize(Vector3Subtract(cam.target,cam.position));
    Vector3 muzzle=Vector3Add(cam.position, Vector3Scale(dir,0.6f));
    for (int i=0;i<MAX_GREN;i++) if (!g_gren[i].active){
        g_gren[i]=(Grenade){ muzzle, Vector3Scale(dir,GREN_SPEED), GREN_LIFE, 1 };
        g_gren[i].vel.y += 3.0f;                            // a touch of loft -> it arcs, not a flat line
        DebugLog("grenade","\"launched\":1");
        return;
    }
}
static void GrenadeExplode(Vector3 p){
    int killsBefore=g_kills;
    for (int i=0;i<MAX_ENEMIES;i++){                        // radial damage, linear falloff
        if (g_enemies[i].state!=1) continue;
        Vector3 c=(Vector3){g_enemies[i].pos.x, g_enemies[i].pos.y+ENEMY_HEIGHT*0.5f, g_enemies[i].pos.z};
        float d=Vector3Distance(p,c);
        if (d<=GREN_RADIUS){ g_hurtCause=HC_BLAST; g_hurtFrom=p;
            HurtEnemy(i, GREN_DMG*(1.0f-d/GREN_RADIUS), 0, (Vector3){0,1,0}, c, 1); g_hurtCause=HC_BULLET; }
    }
    DynLightAdd((Vector3){p.x,p.y+0.6f,p.z}, (Vector3){3.2f,1.9f,0.8f}, 20.0f, 0.50f, 0);   // the blast lights everything round it
    for (int q=0;q<26;q++) SpawnSmoke(p,1);                 // fireball: smoke...
    for (int q=0;q<34;q++) SpawnFlame(p,(Vector3){GetRandomValue(-100,100)/100.0f, GetRandomValue(-30,100)/100.0f, GetRandomValue(-100,100)/100.0f}); // ...radial flame
    SpawnImpact(p,(Vector3){0,1,0});                        // bright spark glints
    {   // soot on the GROUND under the blast (a grenade bursts on the enemy it hits, at chest height:
        // the mark used to hang there in the air), and none at all if it went off high up
        Vector3 sp=p; int ok=!(g_hasMap && g_mapCol.ready);
        if (!ok){ float fd=MapRayNearest((Vector3){p.x,p.y+0.1f,p.z},(Vector3){0,-1,0},3.0f); if (fd>0.0f){ sp.y=p.y+0.1f-fd; ok=1; } }
        if (ok) AddScorch(sp,(Vector3){0,1,0}, 1.3f);
    }
    g_shake=fminf(1.0f, g_shake+0.7f);                     // concussion
    if (g_audio && g_hasGrenSnd) PlaySound(g_grenSnd);
    if (g_kills-killsBefore>=MULTIKILL_N && g_audio && g_hasCrowdSnd){            // a bunch at once: the crowd scream
        SfxAt(g_crowdSnd, p, 1.0f); DebugLog("multikill","\"n\":%d", g_kills-killsBefore); }
    DebugLog("grenade","\"explode\":[%.1f,%.1f,%.1f]", p.x,p.y,p.z);
}
static void UpdateGrenades(float dt){
    for (int i=0;i<MAX_GREN;i++){ Grenade *g=&g_gren[i]; if (!g->active) continue;
        g->life-=dt;
        Vector3 prev=g->pos;
        g->vel.y -= GREN_GRAV*dt;
        g->pos=Vector3Add(g->pos, Vector3Scale(g->vel,dt));
        int hit=0; Vector3 hp=g->pos;
        Vector3 seg=Vector3Subtract(g->pos,prev); float L=Vector3Length(seg);
        if (g_hasMap && g_mapCol.ready){                    // hit map geometry along this step?
            if (L>1e-4f){ Vector3 d=Vector3Scale(seg,1.0f/L), wn={0,1,0}; float wd=MapRayNearestEx(prev,d,L+0.15f,&wn,NULL);
                if (wd>0.0f && wd<=L+0.1f){ hp=Vector3Add(prev,Vector3Scale(d,wd)); hit=1;
                    // a GLANCING strike skips off the surface (twice at most) with a clank; anything squarer bursts
                    float into=-Vector3DotProduct(d,wn); if (into<0){ wn=Vector3Negate(wn); into=-into; }
                    if (into<GREN_GLANCE && g->bounces<2){
                        g->bounces++; hit=0;
                        g->vel=Vector3Scale(Vector3Subtract(g->vel, Vector3Scale(wn, 2.0f*Vector3DotProduct(g->vel,wn))), 0.55f);
                        g->pos=Vector3Add(hp, Vector3Scale(wn,0.06f));
                        if (g_audio && g_hasGrenBounceSnd){ SetSoundPitch(g_grenBounceSnd,0.92f+GetRandomValue(0,16)/100.0f); SfxAt(g_grenBounceSnd, g->pos, 0.8f); }
                        DebugLog("grenade","\"bounce\":%d", g->bounces);
                        continue; } } }
        } else if (g->pos.y<=0.12f){ hp=(Vector3){g->pos.x,0.12f,g->pos.z}; hit=1; }   // arena floor
        if (L>1e-4f){                                       // a body in the way, before the wall/floor: burst on it
            Vector3 d=Vector3Scale(seg,1.0f/L); float ed; Vector3 ep; int head=0;
            float reach = hit ? Vector3Distance(prev,hp) : L;
            int ei=HitEnemy(prev,d,reach,&ed,&ep,&head);
            if (ei>=0){ hp=ep; hit=1; DebugLog("grenade","\"direct\":%d", ei); }
        }
        if (!hit && g->life<=0.0f){ hit=1; hp=g->pos; }     // fuse ran out -> air burst
        if (hit){ GrenadeExplode(hp); g->active=0; }
    }
}

static void TryReload(void);                       // fwd decl (defined with StartReload below)
static int g_shotsFired=0, g_shotsHit=0;           // per-pellet accuracy tally (results screen, M2)
// Random direction inside a cone of half-angle `deg` around d (uniform over the disc).
static Vector3 SpreadDir(Vector3 d, float deg){
    if (deg<=0.0f) return d;
    Vector3 up = (fabsf(d.y)<0.99f) ? (Vector3){0,1,0} : (Vector3){1,0,0};
    Vector3 r1=Vector3Normalize(Vector3CrossProduct(d,up));
    Vector3 r2=Vector3CrossProduct(d,r1);
    float ang=GetRandomValue(0,6283)/1000.0f;
    float rad=tanf(DEG2RAD*deg)*sqrtf(GetRandomValue(0,1000)/1000.0f);
    return Vector3Normalize(Vector3Add(d, Vector3Add(Vector3Scale(r1,cosf(ang)*rad), Vector3Scale(r2,sinf(ang)*rad))));
}
static void Fire(Camera3D cam) {
    if (g_fireCd>0) return;
    Weapon *cw=&g_weapons[g_curWeapon];
    if (cw->magSize>0 && cw->mag<=0){ g_fireCd=0.35f;                          // trigger on empty: the click, then a reload if there is anything to load
        if (g_audio && g_hasDrySnd && !g_reloading){ SetSoundVolume(g_drySnd,0.8f); PlaySound(g_drySnd); }
        TryReload(); return; }
    if (cw->magSize>0) cw->mag--;
    g_fireCd = (cw->fireCd>0.0f) ? cw->fireCd : 0.12f;   // per-weapon fire rate (slow shotguns etc.)
    // One-shot weapons (e.g. shotguns) sound their report here, per shot; auto
    // weapons loop in Update() instead.
    if (g_audio && cw->hasSnd && !cw->sndLoop) PlaySound(cw->fireSnd);
    Vector3 aim=Vector3Normalize(Vector3Subtract(cam.target,cam.position));
    Vector3 muzzle=VmMuzzleWorld(cam.position,aim,0.6f);   // tracers leave the drawn barrel
    DynLightAdd(Vector3Add(muzzle,Vector3Scale(aim,0.4f)), (Vector3){1.9f,1.35f,0.70f}, 12.0f, 0.075f, 0);   // the shot lights the room
    SpawnMuzzleFlash(Vector3Add(muzzle,Vector3Scale(aim,0.10f)), aim);                                        // flame at the barrel
    {   // spent case out of the ejection port: right and up, tumbling
        Vector3 rt=Vector3Normalize(Vector3CrossProduct(aim,(Vector3){0,1,0})), upv=Vector3CrossProduct(rt,aim);
        Casing c={0}; int shell=(cw->pellets>1);
        c.pos=Vector3Add(cam.position, Vector3Add(Vector3Scale(aim,0.80f), Vector3Add(Vector3Scale(rt,0.30f), Vector3Scale(upv,-0.22f))));
        c.vel=Vector3Add(Vector3Scale(rt,2.2f+GetRandomValue(0,120)/100.0f), Vector3Add(Vector3Scale(upv,1.8f+GetRandomValue(0,100)/100.0f), Vector3Scale(aim,GetRandomValue(-60,40)/100.0f)));
        c.ax=Vector3Normalize((Vector3){GetRandomValue(-100,100)/100.0f, 0.3f, GetRandomValue(-100,100)/100.0f}); c.spin=14.0f+GetRandomValue(0,140)/10.0f;
        c.ang=GetRandomValue(0,628)/100.0f; c.life=9.0f;
        c.len=shell?0.062f:0.040f; c.rad=shell?0.011f:0.0058f;
        c.col=shell?(Color){120,22,18,255}:(Color){138,106,44,255};
        c.floorY=cam.position.y-1.6f;
        if (g_hasMap && g_mapCol.ready){ float fd=MapRayNearest(c.pos,(Vector3){0,-1,0},6.0f); if (fd>0.0f) c.floorY=c.pos.y-fd; }
        g_casings[g_casingNext]=c; g_casingNext=(g_casingNext+1)%MAX_CASINGS;
    }
    float dmg=(cw->dmg>0.0f)?cw->dmg:ENEMY_DMG_PER_SHOT;
    int pellets=(cw->pellets>1)?cw->pellets:1, hits=0, headAny=0;
    Vector3 lastEnd=Vector3Add(cam.position,Vector3Scale(aim,80.0f));
    g_shotsFired+=pellets;
    static float shotDmg[MAX_ENEMIES]; memset(shotDmg,0,sizeof shotDmg);   // this trigger pull's damage per enemy
    for (int p=0;p<pellets;p++){
        Vector3 dir=SpreadDir(aim, cw->spread);
        // World stop: on a map, the brushes; otherwise the legacy arena planes/
        // crates. Never both -- the invisible arena floor/walls/crates exist at
        // boot regardless of map and would silently eat bullets mid-air.
        Vector3 hit,nrm; float worldDist=80.0f; int worldHit=0;
        if (g_hasMap && g_mapCol.ready){
            Vector3 mn; float wd=MapRayNearestEx(cam.position,dir,80.0f,&mn,NULL);
            if (wd>0.0f){ worldDist=wd; hit=Vector3Add(cam.position,Vector3Scale(dir,wd)); nrm=mn; worldHit=1; }
        } else if (RaycastWorld(cam.position,dir,&hit,&nrm)){
            worldDist=Vector3Distance(cam.position,hit); worldHit=1;
        }
        Vector3 end = worldHit ? hit : Vector3Add(cam.position,Vector3Scale(dir,80.0f));
        // enemy hit takes priority if it's closer than the world geometry
        float ed; Vector3 ep; int head=0;
        int ei=HitEnemy(cam.position,dir,worldDist,&ed,&ep,&head);
        if (ei>=0){
            end=ep; hits++; g_shotsHit++; if (head) headAny=1;
            shotDmg[ei]+=head ? dmg*HEADSHOT_MULT : dmg;
            if (g_audio && g_hasFleshSnd && g_fleshSndCd<=0.0f){ SetSoundPitch(g_fleshSnd,0.9f+GetRandomValue(0,22)/100.0f); SfxAt(g_fleshSnd, ep, 0.7f); g_fleshSndCd=0.09f; }
            HurtEnemy(ei, head ? dmg*HEADSHOT_MULT : dmg, head, dir, ep, 1);
        } else if (worldHit){
            SpawnImpact(hit,nrm);
            AddHole(hit,nrm);
            if (g_audio && g_hasWallSnd && g_wallSndCd<=0.0f){ SetSoundPitch(g_wallSnd,0.9f+GetRandomValue(0,25)/100.0f); SfxAt(g_wallSnd, hit, 0.55f); g_wallSndCd=0.09f; }
        }
        lastEnd=end;
        for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life<=0){ g_tracers[i]=(Tracer){muzzle,end,0.05f}; break; }
    }
    if (pellets>1) for (int i=0;i<MAX_ENEMIES;i++)                     // a point-blank shotgun blast flattens him
        if (shotDmg[i]>=KNOCK_SHOT_DMG && g_enemies[i].state==1 && Vector3Distance(g_enemies[i].pos,cam.position)<=KNOCK_SHOT_DIST)
            EnemyKnock(i, Vector3Scale((Vector3){aim.x,0,aim.z}, 2.5f));
    // Code-driven recoil instead of the Shoot clip (that clip repositions the
    // gun out of the viewmodel frame -> "gun goes away"). Kick rises to 1, decays.
    g_recoil=1.0f;
    if (hits>0){ g_hitMark=0.14f;
        if (g_audio && g_hasSy){ SetSoundPitch(g_sy[SY_HIT], headAny?1.35f:(0.94f+GetRandomValue(0,12)/100.0f)); SetSoundVolume(g_sy[SY_HIT],0.30f); PlaySound(g_sy[SY_HIT]); } }
    if (cw->recoilKick>0.0f){                      // camera kick: mostly up, a touch of wander
        g_recoilRet += DEG2RAD*cw->recoilKick*0.62f;   // ...and most of it comes back down as the gun settles
        g_pitch += DEG2RAD*cw->recoilKick*(0.7f+GetRandomValue(0,60)/100.0f);
        g_yaw   += DEG2RAD*cw->recoilKick*(GetRandomValue(-30,30)/100.0f);
        if (g_pitch>1.5f) g_pitch=1.5f;
    }
    if (cw->playShoot && g_aShoot!=g_aIdle && g_aShoot<g_gunAnimN && !g_reloading){
        g_curAnim=g_aShoot; g_animOnce=1; g_animT=0.0f;    // play the model's Shot clip (AK)
    }
    DebugLog("fire","\"slot\":%d,\"hits\":%d,\"head\":%d,\"mag\":%d,\"end\":[%.2f,%.2f,%.2f]",
             g_curWeapon, hits, headAny, cw->mag, lastEnd.x,lastEnd.y,lastEnd.z);
}

// Can the enemy stand at (x,z) coming from height curY? The spot must have a floor
// that's a sane STEP UP (<=ENEMY_STEP) or a DROP it can take (<=NAV_MAXFALL, ~one
// storey -- so it'll leap off a balcony to chase) -- but NOT off into the void (no
// floor) or off a cliff taller than NAV_MAXFALL -- and be clear of walls. This single
// test drives all enemy movement: they slide along walls, climb stairs, drop off
// ledges toward the player, and still refuse bottomless edges.
#define ENEMY_STEP    0.7f    // max riser an enemy can step up (matches the player's auto-step)
static int EnemyMoveOK(float x, float z, float curY){
    if (!g_mapCol.ready) return 1;
    float top=curY+ENEMY_STEP, fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},80.0f);
    if (fd<=0) return 0;                                // no floor under the spot -> the void
    float ny=top-fd;                                   // destination floor height
    if (ny>curY+ENEMY_STEP)  return 0;                 // riser too tall to step up
    if (ny<curY-NAV_MAXFALL) return 0;                 // drop taller than a storey -> a cliff, don't leap
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

// ---- Ranged + support enemies ------------------------------------------------
// Bolts: slow glowing radiation shots aimed where the player WAS (dodge by moving);
// hitscan: instant, but accuracy falls with distance and while the player moves; raise:
// the priest channels a beam into a corpse and it stands back up.
typedef struct { Vector3 pos, vel; float life, dmg; int active; } Bolt;
#define MAX_BOLTS 32
static Bolt  g_bolts[MAX_BOLTS];
typedef struct { Vector3 a, b; float t; } Beam;     // priest raise beam (t: seconds left)
#define MAX_BEAMS 8
static Beam  g_beams[MAX_BEAMS];
static Sound g_eShotSnd, g_eBoltSnd, g_eRaiseSnd; static int g_hasEShot=0, g_hasEBolt=0, g_hasERaise=0;
static float g_playerMoveAmt=0.0f;                   // 0..1 how fast the player is moving (spoils hitscan aim)
// A sound that comes from somewhere: quieter with distance, panned to the side it is on, and
// muffled to under half when a wall stands between it and the player.
static void SfxAt(Sound snd, Vector3 at, float gain){
    if (!g_audio) return;
    Vector3 to=Vector3Subtract(at,g_cam.position); float d=Vector3Length(to);
    float v=1.0f-d/45.0f; if (v<0.10f) v=0.10f;
    if (d>1.0f && g_hasMap && g_mapCol.ready){ float w=MapRayNearest(g_cam.position,Vector3Scale(to,1.0f/d),d-0.4f); if (w>0.0f && w<d-0.6f) v*=0.42f; }
    Vector3 fw=Vector3Subtract(g_cam.target,g_cam.position); float pan=0.0f;
    if (d>0.3f){ float rx=-fw.z, rz=fw.x, rl=sqrtf(rx*rx+rz*rz); if (rl>1e-4f) pan=((to.x*rx+to.z*rz)/(rl*d))*0.85f; }
    SetSoundVolume(snd, v*gain); SetSoundPan(snd, pan); PlaySound(snd);
}
static Vector3 g_sfxAt;   // where the next EnemySfx comes from (set by the caller: the enemy)
static void EnemySfx(Sound snd, int has, float d){
    (void)d; if (!g_audio || !has) return;
    SfxAt(snd, g_sfxAt, 1.0f);
}
static Vector3 EnemyMuzzle(const Enemy *e){            // gun/hand height, a little toward the player
    Vector3 to=Vector3Subtract(g_pos,e->pos); to.y=0; float l=Vector3Length(to); if (l>1e-3f) to=Vector3Scale(to,1.0f/l);
    float sc=(kEnemy[e->type].scale*e->sc);
    return (Vector3){ e->pos.x+to.x*0.55f, e->drawY+1.42f*sc, e->pos.z+to.z*0.55f };
}
// Line of sight is judged from the enemy's OWN EYES -- never from the offset muzzle. The
// muzzle sits 0.55 m toward the player, so an enemy hidden just behind a wall's edge could
// poke it round the corner and shoot (unseen, the tracer leaving the wall).
static Vector3 EnemyEye(const Enemy *e){ return (Vector3){ e->pos.x, e->drawY+1.45f*(kEnemy[e->type].scale*e->sc), e->pos.z }; }
static int EnemyClearShot(const Enemy *e){              // fresh, uncached: eye -> player's eye
    if (!(g_hasMap && g_mapCol.ready)) return 1;
    Vector3 a=EnemyEye(e), d=Vector3Subtract(g_cam.position,a); float L=Vector3Length(d);
    if (L<0.3f) return 1;
    float h=MapRayNearest(a,Vector3Scale(d,1.0f/L),L);
    return (h<=0.0f || h>=L-0.3f);
}
static int EnemySeesPlayer(Enemy *e, float dt){        // cached LOS for the ENGAGE decision, re-tested ~5x a second
    e->losT-=dt; if (e->losT>0) return e->los;
    e->losT=0.2f+GetRandomValue(0,10)/100.0f;
    e->los=EnemyClearShot(e);
    return e->los;
}
// Where tracers/bolts leave from: the gun -- unless the gun is inside a wall, then the body.
static Vector3 EnemyShotOrigin(const Enemy *e){
    Vector3 eye=EnemyEye(e), m=EnemyMuzzle(e);
    if (g_hasMap && g_mapCol.ready){ Vector3 d=Vector3Subtract(m,eye); float L=Vector3Length(d);
        if (L>1e-3f){ float h=MapRayNearest(eye,Vector3Scale(d,1.0f/L),L); if (h>0.0f && h<L) return eye; } }
    return m;
}
static void EnemyHitscan(Enemy *e, float d){
    if (!EnemyClearShot(e)){ e->los=0; e->losT=0.25f; e->burstLeft=0; return; }   // lost sight since the cached check
    Vector3 m=EnemyShotOrigin(e);
    float acc=0.86f-d*0.022f-0.30f*g_playerMoveAmt; if (acc<0.12f) acc=0.12f;
    if (e->type==EN_GUNNER) acc*=0.75f;                    // the hose sprays
    int hit = GetRandomValue(0,999) < (int)(acc*1000);
    Vector3 tgt = Vector3Add(g_cam.position,(Vector3){0,-0.35f,0});
    if (!hit){ tgt.x+=GetRandomValue(-150,150)/100.0f; tgt.y+=GetRandomValue(-60,90)/100.0f; tgt.z+=GetRandomValue(-150,150)/100.0f; }
    Vector3 dir=Vector3Normalize(Vector3Subtract(tgt,m)); float L=Vector3Distance(tgt,m)+(hit?0.0f:20.0f);
    if (g_hasMap && g_mapCol.ready){ float w=MapRayNearest(m,dir,L); if (w>0.0f && w<L){ L=w; if (hit && w<Vector3Distance(tgt,m)-0.3f) hit=0; } }
    Vector3 end=Vector3Add(m,Vector3Scale(dir,L));
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life<=0){ g_tracers[i]=(Tracer){m,end,0.06f}; break; }
    SpawnMuzzleFlash(m, dir);
    if (hit && !g_godMode) g_playerHp-=kEnemy[e->type].shotDmg;
    g_sfxAt=e->pos; EnemySfx(g_eShotSnd,g_hasEShot,d);
    DebugLog("eshot","\"type\":\"%s\",\"d\":%.1f,\"hit\":%d", kEnemy[e->type].name, d, hit);
}
// the mutant's throw (CMU 124_01, a pitch) is baked at 60 fps; the hand lets go 86% in
#define BOLT_THROW_FPS    60.0f
#define BOLT_RELEASE_FRAC 0.857f
static float BoltReleaseSec(Enemy *e){
    ModelAnimation *a=RigClip(e->type,CL_ATTACK); return ANIM_FRAMES(*a)*BOLT_RELEASE_FRAC/BOLT_THROW_FPS;
}
// Where a bone of a posed enemy is in the world: the clip's model-space bone transform at
// `frame`, through the same scale/yaw/position DrawModelEx uses for this enemy.
static int RigBoneIndex(const EnemyRig *rg, const char *name){
    for (int i=0;i<rg->model.skeleton.boneCount;i++) if (!strcmp(rg->model.skeleton.bones[i].name,name)) return i;
    return -1;
}
static Vector3 RigBoneWorld(int type, int clip, float frame, const char *bone, Vector3 at, float yawDeg){
    EnemyRig *rg=RigOf(type); int b=RigBoneIndex(rg,bone);
    ModelAnimation *an=RigClip(type,clip);
    if (b<0 || !an || an->keyframeCount<=0) return at;
    Transform t=RigSample(an,frame,b);
    float esc=ENEMY_SCALE*kEnemy[type].scale;
    Vector3 v=Vector3RotateByAxisAngle(Vector3Scale(t.translation,esc),(Vector3){0,1,0},yawDeg*DEG2RAD);
    return Vector3Add(at,v);
}
// The bolt leaves the THROWING HAND at the release frame (a fixed point in front of the face
// read as the fireball appearing behind his head on the side-on pitch). Falls back to the
// usual origin if a wall stands between his eyes and his hand.
static Vector3 EnemyBoltOrigin(const Enemy *e){
    float yaw=atan2f(g_pos.x-e->pos.x, g_pos.z-e->pos.z)*RAD2DEG + ENEMY_YAW_OFFSET;
    Vector3 h; const EnemyRig *brg=RigOf(e->type);
    if (e->poseOK && brg->bHandR>=0)                         // the hand as posed (he may be turned / twisted)
        h=Vector3Add((Vector3){e->pos.x,e->drawY,e->pos.z}, Vector3RotateByAxisAngle(Vector3Scale(e->pose[brg->bHandR].translation,ENEMY_SCALE*(kEnemy[e->type].scale*e->sc)),(Vector3){0,1,0},e->yaw*DEG2RAD));
    else h=RigBoneWorld(e->type, e->clip, e->animT, "RightFingerBase", (Vector3){e->pos.x,e->drawY,e->pos.z}, yaw);
    if (g_hasMap && g_mapCol.ready){ Vector3 eye=EnemyEye(e), d=Vector3Subtract(h,eye); float L=Vector3Length(d);
        if (L>1e-3f){ float w=MapRayNearest(eye,Vector3Scale(d,1.0f/L),L); if (w>0.0f && w<L) return EnemyShotOrigin(e); } }
    return h;
}
static void EnemyBolt(Enemy *e, float d){
    if (!EnemyClearShot(e)){ e->los=0; e->losT=0.25f; return; }
    Vector3 m=EnemyBoltOrigin(e), tgt=Vector3Add(g_cam.position,(Vector3){0,-0.3f,0});
    Vector3 v=Vector3Scale(Vector3Normalize(Vector3Subtract(tgt,m)),13.0f);
    for (int i=0;i<MAX_BOLTS;i++) if (!g_bolts[i].active){ g_bolts[i]=(Bolt){m,v,3.0f,(e->boss && e->type==EN_PRIEST)?ARCH_BOLT_DMG:kEnemy[e->type].shotDmg,1}; break; }
    DebugLog("ebolt","\"d\":%.1f,\"from\":[%.2f,%.2f,%.2f],\"enemy\":[%.2f,%.2f,%.2f]",d,m.x,m.y,m.z,e->pos.x,e->drawY,e->pos.z);
    g_sfxAt=e->pos; EnemySfx(g_eBoltSnd,g_hasEBolt,d);
}
static void BoltsReset(void){ memset(g_bolts,0,sizeof g_bolts); memset(g_beams,0,sizeof g_beams); }
// Silence everything that could still be sounding: death and "play again" call this, so a
// roar that was mid-way (the flamethrower loop, a mutant's 12 s bolt hiss) does not carry on
// over the results screen and into the next run.
static void StopAllSfx(void){
    if (!g_audio) return;
    for (int i=0;i<g_numWeapons;i++){ if (g_weapons[i].hasSnd) StopSound(g_weapons[i].fireSnd); if (g_weapons[i].hasAuxSnd) StopSound(g_weapons[i].auxSnd); }
    if (g_hasEShot) StopSound(g_eShotSnd); if (g_hasEBolt) StopSound(g_eBoltSnd); if (g_hasERaise) StopSound(g_eRaiseSnd);
    if (g_hasAttackSnd) StopSound(g_attackSnd); if (g_hasHeartSnd) StopSound(g_heartSnd);
    EnemyVoiceStopAll();
    if (g_hasRunSnd) StopSound(g_runSnd); if (g_hasKnockSnd) StopSound(g_knockSnd); g_runVol=0.0f;
    if (g_hasBossSnd) StopSound(g_bossSnd); if (g_hasBossDeadSnd) StopSound(g_bossDeadSnd);
    if (g_hasMobSnd) StopSound(g_mobSnd); if (g_hasCrowdSnd) StopSound(g_crowdSnd); if (g_hasAppearSnd) StopSound(g_appearSnd);
    for (int i=0;i<g_nDeathSnd;i++) StopSound(g_deathSnd[i]);
    if (g_hasSgDeathSnd) StopSound(g_sgDeathSnd); if (g_hasSpinUpSnd) StopSound(g_spinUpSnd);
}
static void BoltsUpdate(float dt){
    { int any=0; for (int i=0;i<MAX_BOLTS;i++) if (g_bolts[i].active){ any=1; break; }     // the bolt's hiss ends with the last bolt
      if (!any && g_audio && g_hasEBolt && IsSoundPlaying(g_eBoltSnd)) StopSound(g_eBoltSnd); }
    for (int i=0;i<MAX_BOLTS;i++){ Bolt *b=&g_bolts[i]; if (!b->active) continue;
        b->life-=dt; if (b->life<=0){ b->active=0; continue; }
        DynLightAdd(b->pos, (Vector3){0.35f,1.25f,0.30f}, 5.5f, 0.05f, 1);          // a radiation bolt glows as it flies
        Vector3 step=Vector3Scale(b->vel,dt); float sl=Vector3Length(step);
        if (g_hasMap && g_mapCol.ready && sl>1e-5f){
            float w=MapRayNearest(b->pos,Vector3Scale(step,1.0f/sl),sl);
            if (w>0.0f && w<=sl){ Vector3 hp=Vector3Add(b->pos,Vector3Scale(step,w/sl));
                for (int q=0;q<10;q++) SpawnLiquid(hp,Vector3Negate(Vector3Scale(step,1.0f/sl)));
                b->active=0; continue; }
        }
        b->pos=Vector3Add(b->pos,step);
        Vector3 chest=Vector3Add(g_cam.position,(Vector3){0,-0.45f,0});
        if (Vector3Distance(b->pos,chest)<0.6f){
            if (!g_godMode) g_playerHp-=b->dmg;
            DebugLog("bolthit","\"dmg\":%.0f",b->dmg);
            b->active=0;
        }
    }
    for (int i=0;i<MAX_BEAMS;i++) if (g_beams[i].t>0) g_beams[i].t-=dt;
}
static void DrawBolts(void){
    float tm=(float)GetTime();
    for (int i=0;i<MAX_BOLTS;i++){ Bolt *b=&g_bolts[i]; if (!b->active) continue;
        DrawSphere(b->pos,0.16f,(Color){170,255,90,255});
        DrawSphereWires(b->pos,0.28f+0.04f*sinf(tm*30.0f+i),6,8,(Color){90,255,60,140});
        Vector3 tail=Vector3Subtract(b->pos,Vector3Scale(Vector3Normalize(b->vel),0.7f));
        DrawLine3D(b->pos,tail,(Color){120,255,80,200});
    }
    for (int i=0;i<MAX_BEAMS;i++){ Beam *m=&g_beams[i]; if (m->t<=0) continue;
        for (int k=0;k<5;k++){                              // a crackling bundle of lines, re-jittered every frame
            Vector3 j1={GetRandomValue(-8,8)/100.0f,GetRandomValue(-8,8)/100.0f,GetRandomValue(-8,8)/100.0f};
            Vector3 mid=Vector3Add(Vector3Lerp(m->a,m->b,0.5f),(Vector3){GetRandomValue(-25,25)/100.0f,GetRandomValue(-10,30)/100.0f,GetRandomValue(-25,25)/100.0f});
            DrawLine3D(Vector3Add(m->a,j1),mid,(Color){140,255,110,230}); DrawLine3D(mid,Vector3Add(m->b,j1),(Color){140,255,110,230});
        }
        DrawSphere(m->b,0.25f+0.1f*sinf(tm*20),(Color){150,255,120,120});
    }
}
// Priest: find the nearest corpse within range, channel 1.6 s, then raise it at 60% HP.
// Returns 1 while busy raising (the caller then holds the priest still in its cast clip).
static int PriestRaise(int idx, float dt){
    Enemy *e=&g_enemies[idx];
    if (e->raiseC>=0){
        Corpse *c=&g_corpses[e->raiseC];
        if (!c->active){ e->raiseC=-1; e->chanT=0; return 0; }
        e->chanT-=dt;
        for (int i=0;i<MAX_BEAMS;i++) if (g_beams[i].t<=0 || i==MAX_BEAMS-1){ g_beams[i]=(Beam){EnemyMuzzle(e),(Vector3){c->pos.x,c->pos.y+0.25f,c->pos.z},0.05f}; break; }
        if (e->chanT<=0){
            int slot=-1; for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state==0){ slot=i; break; }
            if (slot>=0){
                EnemyArm(slot,c->pos,c->type);
                g_enemies[slot].hp=g_enemies[slot].maxHp*0.6f;
                c->active=0;
                g_sfxAt=e->pos; EnemySfx(g_eRaiseSnd,g_hasERaise,Vector3Distance(e->pos,g_pos));
                DebugLog("raise","\"type\":\"%s\"",kEnemy[g_enemies[slot].type].name);
            }
            e->raiseC=-1; e->fireT=e->boss ? (e->phase?1.6f:2.4f) : kEnemy[e->type].rate;
            return 0;
        }
        return 1;
    }
    if (e->fireT>0) return 0;
    int best=-1; float bd=e->boss ? 45.0f : kEnemy[e->type].range;
    for (int c=0;c<MAX_CORPSES;c++){ if (!g_corpses[c].active || g_corpses[c].type==EN_HEADCHEF || g_corpses[c].type==EN_PRIEST) continue;
        float d=Vector3Distance(g_corpses[c].pos,e->pos); if (d<bd){ bd=d; best=c; } }
    if (best<0) return 0;
    e->raiseC=best; e->chanT=e->boss?1.0f:1.6f;
    return 1;
}
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
            if (e->knockT>0) e->knockT-=dt;
            if (e->limpT>0) e->limpT-=dt;
            if (e->atkRest>0) e->atkRest-=dt;
            if (e->stagT>0) e->stagT-=dt;
            if (e->hitAccT>0) e->hitAccT-=dt;
            if (d<1e-3f) d=1e-3f;
            if (e->riseT>0){                                        // coming up out of the floor: the glow dies as he stands
                float k=fminf(1.0f, e->riseT/1.2f);
                DynLightAdd((Vector3){e->pos.x,e->pos.y+0.35f,e->pos.z}, (Vector3){0.25f*k,1.5f*k,0.35f*k}, 6.5f, 0.05f, 1);
                if (GetRandomValue(0,99)<(int)(1400*dt)) SpawnSmoke((Vector3){e->pos.x+GetRandomValue(-45,45)/100.0f, e->pos.y+0.1f, e->pos.z+GetRandomValue(-45,45)/100.0f}, 0);
                e->riseT-=dt; }
            if (!e->charged && e->riseT<=0 && d<CHARGE_VOICE_RANGE){ e->charged=1; EnemyVoice(e, EC_CHARGE, 0.8f, 7.0f); }   // his kind's voice, once, as he closes
            if (e->type==EN_PRIEST && !e->boss) DynLightAdd((Vector3){e->pos.x,e->pos.y+0.9f,e->pos.z}, (Vector3){0.30f,0.95f,0.28f}, 6.0f, 0.05f, 1);   // his lantern
            if (e->type==EN_PRIEST && e->boss && e->riseT<=0){                 // THE ARCHIMANDRITE
                if (!e->phase && e->hp<e->maxHp*0.5f){ e->phase=1; e->sumT=0.5f; e->boltT=1.0f;
                    if (g_audio && g_hasBossSnd && !IsSoundPlaying(g_bossSnd)){ SetSoundVolume(g_bossSnd,0.7f); PlaySound(g_bossSnd); }
                    for (int q=0;q<10;q++) SpawnSmoke((Vector3){e->pos.x,e->pos.y+1.2f,e->pos.z},1);
                    DebugLog("bossphase","\"phase\":2,\"hp\":%.0f", e->hp); }
                float fl=0.85f+GetRandomValue(0,30)/100.0f;                    // the lantern: green, then a guttering red
                DynLightAdd((Vector3){e->pos.x,e->pos.y+1.3f,e->pos.z}, e->phase?(Vector3){1.7f*fl,0.30f*fl,0.15f*fl}:(Vector3){0.40f,1.30f,0.38f}, e->phase?11.0f:9.0f, 0.05f, 1);
                e->sumT-=dt;
                if (e->sumT<=0.0f){                                            // no corpse to hand: call one up out of the floor beside him
                    int up=0, slot=-1, anyCorpse=0;
                    for (int k=0;k<MAX_ENEMIES;k++){ if (g_enemies[k].state==1 && k!=i) up++; if (g_enemies[k].state==0 && slot<0) slot=k; }
                    for (int c=0;c<MAX_CORPSES;c++) if (g_corpses[c].active && g_corpses[c].type!=EN_HEADCHEF && g_corpses[c].type!=EN_PRIEST) anyCorpse=1;
                    if (!anyCorpse && slot>=0 && up<(e->phase?ARCH_MINIONS+2:ARCH_MINIONS)){
                        static const int kDead[4]={EN_COOK,EN_LINECOOK,EN_LIQUIDATOR,EN_REVENANT};
                        for (int tr=0;tr<12;tr++){ float a=GetRandomValue(0,628)/100.0f, rr=2.5f+GetRandomValue(0,35)/10.0f;
                            float x=e->pos.x+cosf(a)*rr, z=e->pos.z+sinf(a)*rr, top=SpawnProbeTop(x,z), fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},80.0f);
                            if (fd>0 && EnemySpawnOK(x,top-fd,z)){
                                EnemyArm(slot,(Vector3){x,top-fd,z},kDead[GetRandomValue(0,e->phase?3:2)]); g_enemies[slot].hp=g_enemies[slot].maxHp*0.7f; EnemyRiseIn(slot);
                                g_sfxAt=e->pos; EnemySfx(g_eRaiseSnd,g_hasERaise,d);
                                DebugLog("summon","\"type\":\"%s\",\"up\":%d", kEnemy[g_enemies[slot].type].name, up+1); break; } } }
                    e->sumT=e->phase?2.4f:4.2f;
                }
                if (e->phase){ e->boltT-=dt;                                   // second phase: he throws radiation as well
                    if (e->boltT<=0.0f && d<30.0f && e->hitT<=0 && e->knockT<=0){ EnemyBolt(e,d); e->boltT=1.5f+GetRandomValue(0,60)/100.0f; } }
            }
            if (e->pushV.x!=0 || e->pushV.z!=0){                       // shoved (knockdown): slide, walls stop it
                EnemyNudge(i, e->pushV.x*dt, e->pushV.z*dt);
                float k=expf(-4.0f*dt); e->pushV.x*=k; e->pushV.z*=k;
                if (fabsf(e->pushV.x)+fabsf(e->pushV.z)<0.05f) e->pushV=(Vector3){0,0,0}; }
            // "Running in place" = it's trying to chase but the wall won't let it move at
            // all (an unreachable spot). stuckT accrues only while it tries-but-can't-move
            // (updated in the chase block below); a NAVIGATING enemy moves every frame even
            // when it curves around, so this never false-flags it. flowChanged resets it.
            if (flowChanged) e->stuckT=0.0f;
            int stuck = (g_hasMap && e->stuckT > 1.0f);
            if (stuck){ e->stuckT += dt; if (e->stuckT > 2.5f) e->stuckT = 0.0f; }   // idle ~1.5 s, then try again
            // self-heal: if the body is wedged INSIDE wall geometry (spawned/pushed into an
            // impossible spot, e.g. clipping through a wall) it can never free itself ->
            // respawn it somewhere valid. Tracked independently of chase state, so it also
            // fires when the enemy is jammed in a wall right next to the player (attack range).
            if (g_mapCol.ready && MapSphereHitsWall((Vector3){e->pos.x, e->pos.y+0.9f, e->pos.z}, 0.35f)){
                e->embedT += dt;
                if (e->embedT > 0.8f){                              // relocate to a navigable cell -- same enemy, same hp (no free heal)
                    float keepHp=e->hp; int ty=e->type;
                    DebugLog("relocate","\"i\":%d,\"type\":\"%s\",\"from\":[%.2f,%.2f,%.2f],\"d\":%.2f",i,kEnemy[ty].name,e->pos.x,e->pos.y,e->pos.z,d);
                    SpawnEnemyT(i,ty,0); g_enemies[i].hp=keepHp; continue;
                }
            } else e->embedT = 0.0f;
            // ---- What he wants: a direction and pace (wish, wspd), whether his front stays on the player
            // (focus) and whether the attack clip is up (wantAtk). Melee kinds close in, swing, then step
            // off for a moment; shooters stop to aim, fire once they are steady, then shift position.
            int kind=kEnemy[e->type].kind;
            if (e->fireT>0) e->fireT-=dt;
            int ranged=(kind==AK_HITSCAN || kind==AK_BOLT);
            float reach=ENEMY_REACH(e->type), rng=kEnemy[e->type].range;
            Vector3 toP={dx/d,0,dz/d};
            float pyaw=atan2f(dx,dz)*RAD2DEG + ENEMY_YAW_OFFSET;
            int sees=(ranged ? d<=rng*1.3f : d<=FLANK_DIST) ? EnemySeesPlayer(e,dt) : 0;
            int casting=(kind==AK_RAISE && e->hitT<=0 && PriestRaise(i,dt));
            int down=(e->knockT>0), hurt=(e->hitT>0), stag=(e->stagT>0);
            if (hurt) e->swing=0;                              // a flinch cuts a swing / a throw short
            float top=(e->limpT>0) ? fminf(e->spd,LIMP_SPEED) : e->spd;
            Vector3 wish={0,0,0}, navDir={0,0,0}; float wspd=0; int focus=0, wantAtk=0, chase=0, nav=0;
            if (down || stag || casting){ focus=casting; }
            else if (ranged){
                Vector3 tn={toP.z*e->side,0,-toP.x*e->side};   // across his line to the player
                if (e->mode==EM_ADVANCE && d<=rng && sees && !hurt){ e->mode=EM_AIM; e->aimT=0; e->lostT=0; e->shots=1; }
                if (e->mode==EM_AIM){
                    focus=1;
                    int busy=(e->swing || e->burstLeft>0);
                    if (sees) e->lostT=0; else e->lostT+=dt;
                    if (hurt) e->aimT=0;                           // a hit spoils his aim
                    if (!busy && (e->lostT>LOST_TIME || d>rng*1.1f)) e->mode=EM_ADVANCE;
                    else if (!busy && !hurt && e->shots>0 && d<rng*BACKOFF_FRAC){            // too close: give ground
                        e->mode=EM_MOVE; e->modeT=0.9f+GetRandomValue(0,50)/100.0f; e->shots=0;
                        DebugLog("emode","\"i\":%d,\"to\":\"backoff\",\"d\":%.1f",i,d); }
                    else if (kind==AK_HITSCAN){
                        wantAtk=1;
                        if (!hurt && e->clip==g_eAttack && e->blendT<=0 && sees && fabsf(AngDiff(pyaw,e->yaw))<14.0f && Vector3Length(e->mv)<0.7f) e->aimT+=dt;
                        if (!hurt && e->aimT>=AIM_SETTLE && e->fireT<=0){
                            int first=(e->burstLeft<=0);
                            EnemyHitscan(e,d);
                            if (first) e->burstLeft=kEnemy[e->type].burst-1; else e->burstLeft--;
                            e->fireT=(e->burstLeft>0)?0.11f:kEnemy[e->type].rate;
                            if (e->burstLeft<=0){ e->shots++;
                                if (GetRandomValue(0,99)<RELOCATE_PCT){                      // fired: shift before the next one
                                    e->mode=EM_MOVE; e->modeT=0.8f+GetRandomValue(0,80)/100.0f; e->side=-e->side; e->shots=0;
                                    DebugLog("emode","\"i\":%d,\"to\":\"shift\",\"d\":%.1f",i,d); } }
                        }
                    } else if (e->swing || (!hurt && sees && e->fireT<=BoltReleaseSec(e))) wantAtk=1;   // the throw: wound up only when one is ready
                }
                if (e->mode==EM_MOVE){
                    focus=1; e->modeT-=dt;
                    float rad=(d<rng*BACKOFF_FRAC*1.4f) ? -0.9f : (d>rng*0.6f ? 0.6f : -0.45f);
                    wish=Vector3Normalize((Vector3){tn.x*0.8f+toP.x*rad,0,tn.z*0.8f+toP.z*rad}); wspd=top*0.8f;
                    if (kind==AK_HITSCAN) wantAtk=1;               // the gun stays up while he shifts
                    if (e->stuckT>0.25f){ e->side=-e->side; e->stuckT=0; e->modeT-=0.35f; }   // walled in that way: the other
                    if (e->modeT<=0){ e->mode=sees?EM_AIM:EM_ADVANCE; e->aimT=0; e->lostT=0; }
                }
                if (e->mode==EM_ADVANCE) chase=1;
            } else {
                int nfA=ANIM_FRAMES(*RigClip(e->type,g_eAttack));
                Vector3 tn={toP.z*e->side,0,-toP.x*e->side};
                if (e->swing){                                     // mid-swing: still driving in until the blow lands, then planted
                    wantAtk=1; focus=1;
                    float strike=nfA*(EnemyDualWield(e->type)?DUAL_STRIKE_A:ATK_STRIKE_FRAC);
                    if (e->animT<strike){ wish=toP; wspd=top*Clamp((d-reach*0.68f)/0.7f,0.0f,1.0f); }
                } else if (e->atkRest>0 && d<reach+1.2f && !hurt){   // just swung: a step off to the side before the next
                    focus=1;
                    float rad=(d<reach*0.95f) ? -0.8f : (d>reach*1.3f ? 0.5f : -0.45f);
                    wish=Vector3Normalize((Vector3){tn.x*0.6f+toP.x*rad,0,tn.z*0.6f+toP.z*rad}); wspd=fminf(top,1.5f);
                } else {
                    float lunge=fminf(LUNGE_MAX, Vector3Length(e->mv)*0.28f);   // a runner starts his swing from further out
                    if (d<=reach+lunge && !hurt) wantAtk=1; else chase=1;
                }
            }
            if (chase && !stuck){                              // close in along the nav flow field (else a beeline)...
                Vector3 sd, fl=toP;
                if (g_hasMap && MapNavSteer(e->pos,&sd)){ fl=(Vector3){sd.x,0,sd.z}; nav=1; navDir=fl; }
                Vector3 w=fl;
                if (sees && d<FLANK_DIST && d>reach+0.6f){     // ...coming at him from this one's own side, not in a file
                    float k=e->flank*FLANK_GAIN*Clamp((d-(reach+0.6f))/4.0f,0.0f,1.0f);
                    w.x+=toP.z*k; w.z-=toP.x*k; }
                for (int j=0;j<MAX_ENEMIES;j++){               // ...and giving the others room
                    if (j==i || g_enemies[j].state!=1 || fabsf(g_enemies[j].pos.y-e->pos.y)>1.5f) continue;
                    float ox=e->pos.x-g_enemies[j].pos.x, oz=e->pos.z-g_enemies[j].pos.z, od=sqrtf(ox*ox+oz*oz);
                    if (od<SEP_DIST && od>1e-3f){ float k=(1.0f-od/SEP_DIST)*SEP_GAIN/od; w.x+=ox*k; w.z+=oz*k; }
                }
                float wl=sqrtf(w.x*w.x+w.z*w.z);
                wish=(wl>0.2f) ? (Vector3){w.x/wl,0,w.z/wl} : fl;
                wspd=top;
                if (!ranged) wspd*=Clamp((d-reach*0.55f)/0.9f,0.0f,1.0f);
            }
            if (hurt && !down && !stag) wspd*=HIT_SLOW;       // a light hit slows him; it doesn't root him
            // ---- Momentum: his velocity chases the wish at a limited acceleration (hit shoves live in it too)
            { Vector3 dd=Vector3Subtract(Vector3Scale(wish,wspd),e->vel); dd.y=0;
              float vl0=Vector3Length(e->vel), acc=stag ? 5.0f : (wspd>vl0 ? ENEMY_ACCEL : ENEMY_BRAKE);
              float dl=Vector3Length(dd), mxs=acc*dt; if (dl>mxs) dd=Vector3Scale(dd,mxs/dl);
              e->vel=Vector3Add(e->vel,dd); e->vel.y=0;
              if (down) e->vel=(Vector3){0,0,0}; }
            { float vl=Vector3Length(e->vel);
              if (vl>0.02f){
                Vector3 b4=e->pos; float step=vl*dt, mx=e->vel.x/vl, mz=e->vel.z/vl;
                EnemyNudge(i, mx*step, mz*step);
                #define STILL() (fabsf(e->pos.x-b4.x)<1e-5f && fabsf(e->pos.z-b4.z)<1e-5f)
                if (STILL() && nav) EnemyNudge(i, navDir.x*step, navDir.z*step);   // his own line is walled off: take the route's
                // wedged (the heading runs into a wall and per-axis collision blocks BOTH axes)? sweep the
                // heading outward to either side -- nearest angle first -- until one frees up, so he slides
                // out of corners instead of running in place. Goes out to +-150 deg to escape concave pockets.
                if (g_hasMap && wspd>0.1f && STILL()){
                    const float ANG[10]={0.524f,-0.524f, 1.047f,-1.047f, 1.571f,-1.571f, 2.094f,-2.094f, 2.618f,-2.618f};
                    for (int k=0;k<10;k++){
                        float c=cosf(ANG[k]), sn=sinf(ANG[k]);
                        EnemyNudge(i, (mx*c-mz*sn)*step, (mx*sn+mz*c)*step);
                        if (!STILL()) break;
                    }
                }
                if (STILL()) e->vel=Vector3Scale(e->vel,0.5f);             // flat against something: no speed stored up
                #undef STILL
                // tried to move this frame: if he barely did, he's grinding a wall; if he moved at all (even
                // sliding/curving), he's making his way -> not stuck. Threshold = a quarter of the intended
                // step: frame-rate and speed independent.
                float m2=(e->pos.x-b4.x)*(e->pos.x-b4.x)+(e->pos.z-b4.z)*(e->pos.z-b4.z);
                if (wspd>0.3f){ if (m2 < 0.0625f*step*step) e->stuckT += dt; else e->stuckT = 0.0f; }
              } }
            // what he actually covered (after walls, shoves and last frame's crowding) drives legs and facing
            if (dt>1e-5f){ Vector3 m={(e->pos.x-e->lastPos.x)/dt,0,(e->pos.z-e->lastPos.z)/dt};
                float ml=Vector3Length(m); if (ml>12.0f) m=Vector3Scale(m,12.0f/ml);
                e->mv=Vector3Add(e->mv,Vector3Scale(Vector3Subtract(m,e->mv),1.0f-expf(-12.0f*dt))); e->lastPos=e->pos; }
            float sp=Vector3Length(e->mv);
            // ---- Facing: he turns at a rate, into the way he's going (backing up when he keeps his front to
            // the player); standing, he turns to the player. Chest and head make up the difference.
            { float tgt=e->yaw, rate=(e->type==EN_HEADCHEF)?220.0f:(e->type==EN_BUTCHER)?240.0f:(e->type==EN_FIEND)?300.0f:460.0f;
              if (down || stag) rate=0;
              else if (sp>0.45f && wspd>0.1f){
                  float myaw=atan2f(e->mv.x,e->mv.z)*RAD2DEG + ENEMY_YAW_OFFSET;
                  if (focus){ float rel=fabsf(AngDiff(myaw,pyaw)); if (e->back ? rel<70.0f : rel>110.0f) e->back=!e->back; }
                  else e->back=0;
                  tgt=e->back ? myaw+180.0f : myaw;
              } else if (focus || sees || d<6.0f){ tgt=pyaw; rate*=1.25f; }
              float dy=AngDiff(tgt,e->yaw), st=Clamp(dy*10.0f*dt,-rate*dt,rate*dt);
              if (fabsf(st)>fabsf(dy)) st=dy;
              e->yaw+=st; if (e->yaw>180.0f) e->yaw-=360.0f; if (e->yaw<-180.0f) e->yaw+=360.0f;
              if (dt>1e-5f) e->yawRate+=(st/dt-e->yawRate)*(1.0f-expf(-10.0f*dt));
              float off=AngDiff(pyaw,e->yaw), tt=0, ht=0;
              if (!down && (focus || (sees && d<16.0f)) && fabsf(off)<150.0f){
                  tt=Clamp(off,-TWIST_MAX,TWIST_MAX)*(focus?1.0f:0.5f);
                  ht=Clamp(off-tt,-HEAD_MAX,HEAD_MAX); }
              float k=1.0f-expf(-9.0f*dt); e->twist+=(tt-e->twist)*k; e->headTw+=(ht-e->headTw)*k;
              Vector3 fw={sinf((e->yaw-ENEMY_YAW_OFFSET)*DEG2RAD),0,cosf((e->yaw-ENEMY_YAW_OFFSET)*DEG2RAD)};
              float fwdSp=Vector3DotProduct(e->mv,fw);
              float ln=Clamp(-0.0032f*e->yawRate*sp,-8.0f,8.0f);                      // into the turn
              float pt=Clamp(Vector3DotProduct(Vector3Subtract(Vector3Scale(wish,wspd),e->mv),fw)*0.9f,-5.0f,6.0f) + (stag?-5.0f:0.0f);   // into a start, back on a stop / a stagger
              float kl=1.0f-expf(-6.0f*dt); e->lean+=(ln-e->lean)*kl; e->pitch+=(pt-e->pitch)*kl;
              // ---- Legs: the gait for the pace he's really making, stepping in time with the ground
              EnemyRig *lr=RigOf(e->type); float sc=(kEnemy[e->type].scale*e->sc);
              int rev=(fwdSp<-0.15f), lw;
              if (sp<(e->loco==CL_IDLE?0.35f:0.2f)) lw=CL_IDLE;
              else if (e->limpT>0) lw=CL_LIMP;
              else {                                             // the gait that walks nearest this pace (sticky, so it doesn't flap)
                  int top=(rev && lr->gaitN>2) ? 2 : lr->gaitN; float best=1e9f; lw=CL_WALK;   // backing up: a walk run backward, never a sprint
                  for (int g=0; g<top; g++){ int r=lr->gait[g]; float m=lr->mps[r]*sc; if (m<0.1f) continue;
                      float d=fabsf(logf(sp/m)); if (r==e->loco) d-=0.14f;
                      if (d<best){ best=d; lw=r; } } }
              if (lw!=e->loco && lr->animN>0){
                  int nfO=ANIM_FRAMES(*RigClip(e->type,e->loco)), nfN=ANIM_FRAMES(*RigClip(e->type,lw));
                  e->locoPrev=e->loco; e->locoPrevT=e->locoT; e->locoBlend=LOCO_BLEND;
                  e->locoT=(e->loco!=CL_IDLE && lw!=CL_IDLE && nfO>0) ? e->locoT/nfO*nfN : 0.0f;   // keep the stride's phase
                  e->loco=lw; }
              if (e->locoBlend>0) e->locoBlend-=dt;
              float lrate=30.0f;
              if (e->loco!=CL_IDLE){ float m=lr->mps[e->loco]*sc; if (m<0.1f) m=(e->loco==CL_LIMP)?LIMP_CLIP_MPS:1.3f;
                  lrate=30.0f*Clamp(sp/m,0.5f,(e->loco==CL_LIMP)?2.2f:1.7f); if (rev) lrate=-lrate; }
              if (lr->animN>0){ int nfL=ANIM_FRAMES(*RigClip(e->type,e->loco)); e->locoT+=lrate*dt;
                  if (nfL>0){ while (e->locoT>=nfL) e->locoT-=nfL; while (e->locoT<0) e->locoT+=nfL; } }
              float lt=Clamp((sp-0.3f)/0.8f,0.0f,1.0f);                                // moving: legs walk; standing: the action clip's own stance
              if (down) e->legW=0; else e->legW+=(lt-e->legW)*(1.0f-expf(-9.0f*dt));
            }
            // ---- The action clip. Priority: knockdown > flinch > cast > attack > none (g_eRun = just locomotion)
            int want = down ? CL_KNOCK : hurt ? (e->hitRole>=CL_HIT_HEAD ? e->hitRole : g_eHit) : casting ? CL_CAST : wantAtk ? g_eAttack : g_eRun;
            if (want!=e->clip || (e->rehit && hurt && !down && e->clip!=CL_KNOCK && e->animT>5.0f)){
                if (want==g_eAttack && e->clip!=g_eAttack && g_audio){                       // lunging into an attack: his own swing, else the shared roar
                    if (EnemyVoice(e, EC_ATTACK, 0.9f, 0.35f)) e->roarT=1.0f;
                    else if (e->roarT<=0 && g_hasAttackSnd){ SfxAt(g_attackSnd, e->pos, 1.0f); e->roarT=2.5f; }   // cooldown so it doesn't spam at the range edge
                }
                EnemyPoseSnap(e, want==CL_KNOCK || e->clip==CL_KNOCK);            // ease in from the pose he's in, never snap
                e->prevClip=e->clip; e->prevT=e->animT;
                DebugLog("clipsw","\"i\":%d,\"from\":%d,\"to\":%d,\"atT\":%.1f,\"d\":%.2f",i,e->clip,want,e->animT,d);
                e->clip=want; e->animT=0;
                e->swing = (want==g_eAttack && kind!=AK_HITSCAN);
                if (want==g_eAttack) e->atkRate=0.88f+GetRandomValue(0,24)/100.0f;   // no two swings quite the same pace
            }
            e->rehit=0;
            if (e->blendT>0) e->blendT-=dt;
            if (g_hasMap && stuck && e->hitT<=0 && !wantAtk){
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
                    if (l>1e-4f) EnemyNudge(i, ox/l*e->spd*dt*0.6f, oz/l*e->spd*dt*0.6f);
                }
            }
            if (g_hasMap && g_mapCol.ready){                   // keep the enemy standing on the map floor
                // look down from only a step above the feet, not 4m up -- otherwise the
                // snap can grab an overhead landing and wedge the enemy under the stairs.
                float top=e->pos.y+ENEMY_STEP, fd=MapRayNearest((Vector3){e->pos.x,top,e->pos.z},(Vector3){0,-1,0},40.0f);
                if (fd>0) e->pos.y=top-fd;
            }
            // ease the RENDER height toward the real floor so stair steps ramp smoothly
            // instead of popping. A real ledge DROP (drawY well above the new floor, or
            // already mid-fall) is animated under gravity so the body falls and lands
            // instead of teleporting. Clamp the climb side so the feet ride ON each step.
            float dy=e->pos.y-e->drawY;
            if (e->fallV<0.0f || dy<-1.5f){                    // mid-fall, or just stepped off a ledge
                e->fallV -= 22.0f*dt; e->drawY += e->fallV*dt;
                if (e->drawY <= e->pos.y){ e->drawY=e->pos.y; e->fallV=0.0f; }   // landed
            } else if (fabsf(dy)>1.5f){
                e->drawY=e->pos.y;                            // big upward pop (spawn / stairs up) -> snap
            } else {
                e->drawY += dy*(1.0f-expf(-16.0f*dt));
                if (e->drawY < e->pos.y-0.03f) e->drawY = e->pos.y-0.03f;   // feet never sink into the stairs
            }
            if (g_meleeDrain){                                 // legacy A/B: untimed contact drain
                if (e->clip==g_eAttack && e->hitT<=0 && !g_godMode) g_playerHp-=ENEMY_TOUCH_DMG*dt;
            } else if (e->atkCd>0) e->atkCd-=dt;
            if (RigOf(e->type)->animN>0){                      // advance the action clip
                int nf=ANIM_FRAMES(*RigClip(e->type,e->clip));
                float spd=(e->clip==g_eAttack && !ranged) ? 90.0f*e->atkRate : ((e->clip==g_eAttack && kind==AK_BOLT) ? BOLT_THROW_FPS : 30.0f);    // melee swings and throws are baked for these rates; aims/casts/flinches at 30
                if (e->clip==CL_KNOCK) spd=60.0f;                                  // baked on the 60 fps timeline: real time
                float prevT=e->animT;
                e->animT+=dt*spd;
                // Timed melee (default): the swing damages ONCE, when the attack clip
                // crosses its strike frame -- so the windup is dodgeable (step out of
                // range before it lands) and a real cooldown separates hits, instead
                // of the old unreadable touch-drain.
                if (!g_meleeDrain && !ranged && e->clip==g_eAttack && nf>0 && e->atkCd<=0 && e->hitT<=0){
                    // dual wielders (cook, butcher, head chef) land each blade separately
                    int dual=EnemyDualWield(e->type), ns=dual?2:1;
                    const float fr[2]={ dual?DUAL_STRIKE_A:ATK_STRIKE_FRAC, DUAL_STRIKE_B };
                    for (int k=0;k<ns && e->atkCd<=0;k++){ float strike=nf*fr[k];
                        if (prevT<strike && e->animT>=strike && d<=reach*1.15f && !g_godMode){
                            g_playerHp-=kEnemy[e->type].meleeDmg*(dual?DUAL_DMG:1.0f);   // per-type swing damage
                            DebugLog("melee","\"type\":\"%s\",\"blade\":%d,\"dmg\":%.1f,\"hp\":%.0f",kEnemy[e->type].name,k,kEnemy[e->type].meleeDmg*(dual?DUAL_DMG:1.0f),g_playerHp);
                            e->atkCd = dual ? 0.25f : ATK_COOLDOWN;
                        }
                    }
                }
                if (kind==AK_BOLT && e->clip==g_eAttack && e->hitT<=0){   // the bolt leaves the hand at release
                    float rel=nf*BOLT_RELEASE_FRAC;
                    if (prevT<rel && e->animT>=rel && e->fireT<=0){ EnemyBolt(e,d); e->fireT=kEnemy[e->type].rate; }
                }
                if (nf>0 && e->animT>=nf){
                    if (e->clip==CL_KNOCK || e->clip==g_eHit || (e->clip>=CL_HIT_HEAD && e->clip<=CL_HIT_LEG)) e->animT=(float)nf-1.0f;   // held on the last frame until he recovers
                    else { e->animT-=nf;
                        if (e->swing){ e->swing=0;                             // the swing / throw is done
                            if (ranged){ e->mode=EM_MOVE; e->modeT=0.7f+GetRandomValue(0,70)/100.0f; e->shots=0; if (GetRandomValue(0,1)) e->side=-e->side; }   // thrown: shift
                            else { e->atkRest=0.25f+GetRandomValue(0,35)/100.0f; if (!GetRandomValue(0,2)) e->side=-e->side; } } }
                }
            }
            EnemyPoseBuild(e);
        } else if (e->state==2){                              // dying: play the death anim to its settle frame, then leave a corpse
            if (e->blendT>0) e->blendT-=dt;
            { float dy=AngDiff(e->deathYaw,e->yaw), st=Clamp(dy*8.0f*dt,-540.0f*dt,540.0f*dt);   // turned by the shot as he goes down
              if (fabsf(st)>fabsf(dy)) st=dy; e->yaw+=st; e->vel=(Vector3){0,0,0}; }
            int nf = (RigOf(e->type)->animN>0) ? RigOf(e->type)->settle[e->deathRole?e->deathRole:CL_DEATH] : 0;   // stop at the "on the floor" frame, not the spring-back last frame
            if (e->pushV.x!=0 || e->pushV.z!=0){                       // thrown by a blast: the body slides on
                EnemyNudge(i, e->pushV.x*dt, e->pushV.z*dt);
                float k=expf(-2.5f*dt); e->pushV.x*=k; e->pushV.z*=k; }
            if (nf>0){ e->animT+=dt*60.0f; if (e->animT>(float)nf) e->animT=(float)nf; }
            e->deathT+=dt;
            // wait until the death anim has actually settled (body on the floor),
            // not a fixed timer -- otherwise the corpse is recorded mid-fall.
            if ((nf<=0 || e->animT>=(float)nf) && e->deathT>0.2f){
                float yaw=e->poseOK ? e->yaw : e->deathYaw;                      // the way he fell
                Corpse *cp=&g_corpses[g_corpseNext]; *cp=(Corpse){0};            // lasting body, posed at the final death frame
                cp->role=e->deathRole?e->deathRole:CL_DEATH;
                cp->wN=e->wN; for (int k=0;k<e->wN;k++){ cp->wBone[k]=e->wBone[k]; cp->wOff[k]=e->wOff[k]; cp->wSz[k]=e->wSz[k]; }
                cp->pos=e->pos; cp->yaw=yaw; cp->active=1; cp->type=e->type; cp->burnN=e->burnN;   // keep the soot marks it earned while alive
                for (int k=0;k<e->burnN;k++){ cp->burnY[k]=e->burnY[k]; cp->burnR[k]=e->burnR[k]; }
                g_corpseNext=(g_corpseNext+1)%MAX_CORPSES;
                e->state=0;                                // slot freed -- the wave director owns ALL spawning (no 1:1 respawn)
            }
            if (e->state==2) EnemyPoseBuild(e);
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

#define PLAYER_BODY_R 0.42f
static int PlayerIntoEnemy(float nx, float nz){
    for (int i=0;i<MAX_ENEMIES;i++){ const Enemy *e=&g_enemies[i];
        if (e->state!=1 || e->riseT>0 || e->knockT>0) continue;                     // on the floor: step over him
        if (fabsf((g_pos.y-EYE_H)-e->pos.y) > ENEMY_HEIGHT) continue;               // another storey
        float mind=PLAYER_BODY_R+ENEMY_RADIUS*(kEnemy[e->type].scale*e->sc);
        float dx=nx-e->pos.x, dz=nz-e->pos.z, d2=dx*dx+dz*dz;
        if (d2>=mind*mind) continue;
        float ox=g_pos.x-e->pos.x, oz=g_pos.z-e->pos.z;
        if (d2<ox*ox+oz*oz) return 1;                                               // inside his space and still closing
    }
    return 0;
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


// Begin the active weapon's reload: multi-phase sequence if it has one, else the
// single reload clip, else nothing. Used by the R key and the shotgun's pump.
static float g_shellJump=-1.0f;        // >=0: at this clip frame, jump to the weapon's closing pump (shellPump)
static void StartReload(void){
    if (g_reloading) return;
    g_shellJump=-1.0f;
    g_burstLeft=0;                                    // a reload always cancels a pending burst
    if (g_reloadSeqN>0){ g_reloading=1; g_reloadStep=0; g_curAnim=g_reloadSeq[0]; g_animOnce=1; g_animT=0.0f; }
    else if (g_aReload!=g_aIdle && g_aReload<g_gunAnimN){ g_reloading=1; g_reloadStep=-1; g_curAnim=g_aReload; g_animOnce=1; g_animT=0.0f; }
}

// ---- Ammo (M1) --------------------------------------------------------------
// Refill lands when the reload ANIM completes (FinishReload, called from the
// anim-advance code), so switching weapons mid-reload cancels the reload.
static void FinishReload(void){
    Weapon *w=&g_weapons[g_curWeapon];
    if (w->magSize<=0) return;
    int need=w->magSize-w->mag, take=(need<w->reserve)?need:w->reserve;
    w->mag+=take; w->reserve-=take;
    DebugLog("reload","\"slot\":%d,\"mag\":%d,\"reserve\":%d", g_curWeapon, w->mag, w->reserve);
}
// Shell-by-shell feed (remington): its one reload clip pushes a shell into the tube at
// shellFirst + k*shellCycle. Each insert crossed this frame loads one round; once the tube is
// full (or the reserve dry) the clip skips to the last insert's follow-through (the pose
// after every insert matches), and a 5th+ shell replays one insert cycle. The trigger can
// cut the reload short at any time (WeaponTrigger) -- what's in the tube is what you have.
#define SHELL_WITHDRAW 16.0f           // frames after an insert before the hand is clear of the gun
static float g_rackIn=0.0f;            // >0: the pump sounds when this runs out (after a shot, or closing a shell reload)
static int   g_rackShot=0;             // that pending pump follows a shot (it sounds even though no reload is running)
static void ShellFeed(Weapon *w, float pvT){
    if (!g_reloading || w->shellCycle<=0 || w->magSize<=0 || g_curAnim!=g_aReload) return;
    if (g_shellJump>=0.0f && g_animT>=g_shellJump){                // tube full: the hand has come away -> straight to the closing pump
        g_animT=w->shellPump+(g_animT-g_shellJump); g_shellJump=-1.0f; }
    float last=w->shellFirst+(w->shellCount-1)*w->shellCycle;
    for (int k=0;k<w->shellCount;k++){
        float fk=w->shellFirst+k*w->shellCycle;
        if (!(pvT<fk && g_animT>=fk)) continue;
        if (w->mag<w->magSize && w->reserve>0){ w->mag++; w->reserve--;
            if (g_audio && g_hasShellSnd){ SetSoundPitch(g_shellSnd,0.96f+GetRandomValue(0,8)/100.0f); SetSoundVolume(g_shellSnd,0.8f); PlaySound(g_shellSnd); } }   // the shell going in
        DebugLog("reload","\"slot\":%d,\"mag\":%d,\"reserve\":%d,\"shell\":1", g_curWeapon, w->mag, w->reserve);
        if (w->mag>=w->magSize || w->reserve<=0){                  // full (or dry): let the hand withdraw, then skip the clip's spare inserts
            if (w->shellPump>0.0f){ g_shellJump=fk+SHELL_WITHDRAW; g_rackIn=(SHELL_WITHDRAW+w->shellRackAt-w->shellPump)/30.0f; }
            else { if (fk<last) g_animT=last+(g_animT-fk); g_rackIn=0.45f; } }
        else if (k==w->shellCount-1) g_animT=fk-w->shellCycle+(g_animT-fk);                   // more to load: one more cycle
        break;
    }
}
// R key / empty-mag entry point. The grenade launcher scrubs its reload slice of
// 'allanims'; weapons without any reload clip (minigun belt, flamethrower tank)
// refill instantly behind a fixed delay; everything else plays its Reload clip.
static void TryReload(void){
    Weapon *w=&g_weapons[g_curWeapon];
    if (g_reloading || w->magSize<=0 || w->mag>=w->magSize || w->reserve<=0) return;
    if (w->grenade){ g_reloading=1; g_animOnce=0; g_curAnim=g_aIdle; g_animT=(float)GL_OPEN_FROM; g_glPhase=1; g_glCut=0; g_glFire=0;
        if (g_audio && g_hasGlSnd[0]) PlaySound(g_glSnd[0]);                      // the drum swings open
        return; }
    if (w->reloadSeqN<=0 && (w->aReload==w->aIdle || w->aReload>=w->animN)){ g_burstLeft=0; FinishReload(); g_fireCd=1.2f; return; }
    StartReload();
}

// Options/pause menu: a vertical stack of buttons centred on screen. DrawMenu and
// the click hit-test (in Update) both call MenuRect, so the layout can't drift.
#define MENU_N 4
static Rectangle MenuRect(int i){
    int W=GetScreenWidth(), H=GetScreenHeight();
    float bw=360, bh=54, gap=16, total=MENU_N*bh+(MENU_N-1)*gap;
    return (Rectangle){ W/2.0f-bw/2.0f, H/2.0f-total/2.0f+24+i*(bh+gap), bw, bh };
}
// Persist the fullscreen choice across launches (per-machine, gitignored like the
// tune files). Default is windowed; the menu writes "fullscreen 1" once enabled.
static char g_lbSaved[4];              // leaderboard initials, remembered between runs (scores.h)
static void LoadOptions(void){
    FILE *f=fopen("options.txt","r"); if(!f) return;
    int fs=0; if (fscanf(f,"fullscreen %d",&fs)==1) g_fullscreen=(fs!=0);
    char in[8]={0}; if (fscanf(f," initials %3[A-Z0-9]",in)==1) strcpy(g_lbSaved,in);
    fclose(f);
}
static void SaveOptions(void){
    FILE *f=fopen("options.txt","w"); if(!f) return;
    fprintf(f,"fullscreen %d\n", g_fullscreen?1:0);
    if (g_lbSaved[0]) fprintf(f,"initials %s\n", g_lbSaved);
    fclose(f);
}
// Fullscreen as a plain UNDECORATED window sized to the monitor -- NOT raylib's
// ToggleBorderlessWindowed(), which on macOS drops the window into a native
// fullscreen Space that swallows Cmd-Tab and stops delivering keys (so ESC could
// never reach the menu). A borderless normal window stays a normal window: Cmd-Tab
// works and ESC opens the menu. on=1 cover the monitor, on=0 restore 1280x720.
static void ApplyFullscreen(int on){
    int mon=GetCurrentMonitor();
    int mw=GetMonitorWidth(mon), mh=GetMonitorHeight(mon);
    if (on){
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowSize(mw, mh);
        SetWindowPosition(0, 0);
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowSize(1280, 720);
        SetWindowPosition((mw-1280)/2, (mh-720)/2);   // re-centre on the monitor
    }
}

// Bring the player back after death: full health, back to the spawn point. The
// field is left as-is -- the enemies that killed you are still out there.
#include <ctype.h>
#include "scores.h"                    // leaderboard: initials on the death screen, post + top scores
static void RespawnPlayer(void){
    g_playerHp=100.0f; g_prevHp=100.0f; g_hurt=0.0f; PickupsReset();
    g_dead=0; g_deadT=0.0f; StopAllSfx();
    for (int i=0;i<g_numWeapons;i++){ g_weapons[i].mag=g_weapons[i].magSize; g_weapons[i].reserve=g_weapons[i].reserveStart; }
    g_flameFuel=0.0f; g_burstLeft=0;           // fresh loadout with the fresh life; no pending burst
    g_pos = g_hasMap ? g_spawnResetPos : (Vector3){0,EYE_H,6};
    g_vy=0.0f; g_grounded=0; g_eyeSmooth=0.0f;
    DebugLog("respawn","\"pos\":[%.1f,%.1f,%.1f]", g_pos.x,g_pos.y,g_pos.z);
}

// The live weapon's trigger, one frame: fire cadence, recoil/sway decay, bursts, the
// grenade lob, the flame cone, minigun spin-up, LMG sound-gated bursts, R reload and the
// looped fire sound. held/pressed = the trigger (--autofire adds to them after the cooldown
// ticks, as it always has); fwd = aim direction
// from g_pos (g_cam must be set). Shared by Update and the enemy gallery's range mode.
static void WeaponTrigger(float dt, Vector3 fwd, int held, int pressed){
    if (g_fireCd>0) g_fireCd-=dt;
    if (g_recoil>0) g_recoil=fmaxf(0.0f, g_recoil - dt*7.0f);   // recoil settles in ~0.14s
    if (g_recoilRet>0){ float r=fminf(g_recoilRet, g_recoilRet*dt*9.0f+dt*0.004f); g_pitch-=r; g_recoilRet-=r; }   // the muzzle drops back toward the aim point
    if (g_rackIn>0){ g_rackIn-=dt; if (g_rackIn<=0){ if ((g_reloading || g_rackShot) && g_audio && g_weapons[g_curWeapon].autoReload && g_weapons[g_curWeapon].hasAuxSnd) PlaySound(g_weapons[g_curWeapon].auxSnd); g_rackShot=0; } }
    if (g_wallSndCd>0) g_wallSndCd-=dt;
    if (g_fleshSndCd>0) g_fleshSndCd-=dt;
    if (g_hitMark>0) g_hitMark-=dt;
    if (g_killMark>0) g_killMark-=dt;
    int lmb=held || g_autoFire;
    int lmbP=pressed || (g_autoFire && g_fireCd<=0.0f);
    { float k=fminf(1.0f,dt*9.0f);                                 // sway: chase the look delta, clamp big flicks
      Vector2 t={ fmaxf(-40.0f,fminf(40.0f,g_vmLook.x)), fmaxf(-40.0f,fminf(40.0f,g_vmLook.y)) };
      g_vmSway.x+=(t.x-g_vmSway.x)*k; g_vmSway.y+=(t.y-g_vmSway.y)*k; g_vmLook=(Vector2){0,0}; }
    if (g_vmRaise>0) g_vmRaise=fmaxf(0.0f, g_vmRaise - dt*3.5f);   // weapon raise ~0.3s
    if (g_hsFlash>0) g_hsFlash-=dt;                            // "HEADSHOT!" flash fades out
    Weapon *cw=&g_weapons[g_curWeapon];
    int firingNow=0;                                       // looped fire sound active this frame?

    { static int uf=0; if (g_reloadAt>0 && ++uf==g_reloadAt){ if (cw->grenade){ cw->mag=0; TryReload(); } else if (getenv("CHERNOBYL_RELOAD_REAL")){ if (!strcmp(getenv("CHERNOBYL_RELOAD_REAL"),"empty")) cw->mag=0; TryReload(); } else StartReload(); } }   // (the launcher reloads by scrubbing: empty it and take the real path)
    // shell-fed reload + a round in the tube: the trigger stops loading and fires
    if (g_reloading && cw->grenade && cw->mag>0 && lmbP && !g_glCut){     // launcher: shut the drum on what's in it, then fire
        g_glCut=1; if (g_glPhase!=3){ g_glPhase=3; g_animT=(float)GL_SHUT_FROM; if (g_audio && g_hasGlSnd[2]) PlaySound(g_glSnd[2]); }
        DebugLog("reloadcut","\"slot\":%d,\"mag\":%d", g_curWeapon, cw->mag); }
    if (g_reloading && cw->shellCycle>0 && cw->mag>0 && lmbP){
        g_reloading=0; g_reloadStep=-1; g_curAnim=g_aIdle; g_animOnce=0; g_animT=0.0f;
        DebugLog("reloadcut","\"slot\":%d,\"mag\":%d", g_curWeapon, cw->mag); }
    if (IsKeyPressed(KEY_R)){
        TryReload();
    }
    // empty, with rounds in reserve: reload without being asked -- after the last shot, or the
    // moment an empty gun is drawn again (it used to wait for a dry trigger pull)
    if (!g_reloading && cw->has && cw->magSize>0 && cw->mag<=0 && cw->reserve>0 && g_fireCd<=0.0f && g_vmRaise<=0.0f) TryReload();
    // just drawn and not full: top it up -- guns that are loaded by hand (the shotgun's tube, the
    // launcher's drum; firing cuts either short). The belt and the fuel tank are left as they are.
    if (g_drawTopUp && g_vmRaise<=0.0f){ g_drawTopUp=0;
        int byHand = cw->grenade || cw->shellCycle>0 || cw->shellReload;
        if (byHand && !g_reloading && cw->has && cw->magSize>0 && cw->mag<cw->magSize && cw->reserve>0 && !lmb) TryReload(); }
    else if (!g_reloading){
        if (cw->burst>0){                                  // rifle: one 3-round burst per trigger pull
            if (lmbP && g_burstLeft==0) g_burstLeft=cw->burst;
            // (a dry Fire starts the reload, which zeroes the burst: counting that pull down
            // too left it at -1, and the gun never fired again until it was swapped out)
            if (g_burstLeft>0){ firingNow=1; if (g_fireCd<=0.0f){ Fire(g_cam); if (g_burstLeft>0) g_burstLeft--; } }
        } else if (cw->grenade){                            // grenade launcher: lob one grenade per click
            if ((lmbP || g_glFire) && g_fireCd<=0.0f){
                g_glFire=0;
                if (cw->magSize>0 && cw->mag<=0){ if (g_audio && g_hasDrySnd && !g_reloading){ SetSoundVolume(g_drySnd,0.8f); PlaySound(g_drySnd); } g_fireCd=0.35f; TryReload(); }
                else {
                    if (cw->magSize>0) cw->mag--;
                    g_fireCd=GL_FIRE_CD; g_recoil=1.0f;   // (not counted in g_shotsFired -- grenades can't score g_shotsHit)
                    if (cw->recoilKick>0.0f){ g_pitch += DEG2RAD*cw->recoilKick; if (g_pitch>1.5f) g_pitch=1.5f; }
                    if (g_audio && cw->hasSnd) PlaySound(cw->fireSnd);
                    LaunchGrenade(g_cam);
                }
            }
        } else if (!cw->spinUp && !cw->soundGated && !cw->flame && !cw->melee && !cw->grenade && lmb){ // full-auto / one-shot (shotgun, sawnoff)
            int canFire=(cw->magSize<=0 || cw->mag>0);
            int willFire=(g_fireCd<=0.0f && canFire);
            firingNow=canFire; Fire(g_cam);            // dry gun: no fire-loop audio, but Fire still auto-reloads
            if (willFire && cw->autoReload){               // shotgun: cock between shots (the Shot clip
                // already animates the action -- playing the shell-Reload clip here
                // would block firing for its whole length)
                g_rackIn=0.34f; g_rackShot=1;              // the pump comes AFTER the boom, not under it
            }
        }
    }

    // Flamethrower (slot 5): hold the trigger -> a continuous cone of fire. Spews
    // flame billboards along the aim and burns every alive enemy inside the cone +
    // range (LOS-checked on a map so it can't burn through walls). No per-shot cd.
    if (cw->flame){
        int haveFuel = (cw->magSize<=0 || cw->mag>0);
        int firing = lmb && !g_reloading && haveFuel && g_fireCd<=0.0f;   // fireCd also enforces the 1.2s tank swap
        if (lmb && !haveFuel && !g_reloading && g_fireCd<=0.0f) TryReload();   // dry trigger -> swap tanks
        if (firing){
            g_flameHeat += dt;                                           // warm up while held
            firingNow=1;                                                 // the roar plays through the whole warm-up
            // Emit from the GUN'S NOZZLE, not screen centre: offset right + a hair down
            // (where the viewmodel barrel tip sits) and forward past the barrel, then the
            // jet flies out along the aim. Built from the camera basis so it tracks the gun.
            Vector3 muzzle=VmMuzzleWorld(g_pos, fwd, 0.95f);             // at the drawn nozzle tip
            if (g_flameHeat < FLAME_WARMUP){                             // NOT lit yet: spit raw fuel, no damage
                for (int q=0;q<2;q++) SpawnLiquid(muzzle, fwd);
            } else {                                                     // ignited: flame + burn
                g_flameFuel += dt*FLAME_FUEL_PER_SEC;                    // fuel burn (tank = mag)
                while (g_flameFuel>=1.0f && cw->mag>0){ cw->mag--; g_flameFuel-=1.0f; }
                for (int q=0;q<5;q++) SpawnFlame(muzzle, fwd);          // a dense, wide gout streaming from the nozzle
                { float fl=0.85f+GetRandomValue(0,30)/100.0f;           // the stream is a fire: flickering light along it
                  DynLightAdd(Vector3Add(muzzle,Vector3Scale(fwd,1.6f)), (Vector3){1.7f*fl,0.95f*fl,0.35f*fl}, 10.0f, 0.06f, 1);
                  DynLightAdd(Vector3Add(muzzle,Vector3Scale(fwd,4.6f)), (Vector3){1.3f*fl,0.70f*fl,0.25f*fl}, 9.0f, 0.06f, 1); }
                for (int i=0;i<MAX_ENEMIES;i++){
                    if (g_enemies[i].state!=1) continue;
                    Vector3 cen=(Vector3){g_enemies[i].pos.x, g_enemies[i].pos.y+ENEMY_HEIGHT*0.5f, g_enemies[i].pos.z};
                    Vector3 to=Vector3Subtract(cen, muzzle); float dist=Vector3Length(to);
                    if (dist>FLAME_RANGE || dist<1e-3f) continue;
                    Vector3 tn=Vector3Scale(to, 1.0f/dist);
                    if (Vector3DotProduct(tn, fwd) < FLAME_COS) continue;          // outside the cone
                    if (g_hasMap && g_mapCol.ready){                                // wall between -> shielded
                        float wd=MapRayNearest(muzzle, tn, dist); if (wd>0 && wd<dist-0.3f) continue; }
                    // Char ONLY where the flame actually licks the body: the ray's height
                    // at the enemy's distance -> a local mark that grows the longer you hold
                    // it there. Sweeping the stream leaves several marks; a tap leaves one small one.
                    Enemy *be=&g_enemies[i];
                    float ch=muzzle.y + fwd.y*dist - be->pos.y;                     // contact height above the feet
                    if (ch<0.15f) ch=0.15f; if (ch>ENEMY_HEIGHT-0.15f) ch=ENEMY_HEIGHT-0.15f;
                    int m=-1; for (int k=0;k<be->burnN;k++) if (fabsf(be->burnY[k]-ch)<0.30f){ m=k; break; }
                    if (m<0 && be->burnN<MAX_BURN){ m=be->burnN++; be->burnY[m]=ch; be->burnR[m]=0.08f; }
                    if (m>=0) be->burnR[m]=fminf(0.34f, be->burnR[m]+dt*0.7f);      // darkens/spreads with sustained flame
                    g_hurtCause=HC_FLAME; HurtEnemy(i, FLAME_DPS*dt, 0, tn, cen, 0); g_hurtCause=HC_BULLET;   // burning: no blood spurt
                }
                // Scorch whatever the stream lands on (floor/wall/prop), throttled so marks
                // build up steadily rather than every frame. The map ray reports the real
                // surface normal now, so the decal lies flat on the face it hit.
                g_scorchT += dt;
                if (g_scorchT >= 0.04f){
                    Vector3 sp, sn; int landed=0;
                    if (g_hasMap && g_mapCol.ready){
                        Vector3 mnrm; float wd=MapRayNearestEx(muzzle, fwd, FLAME_RANGE, &mnrm, NULL);
                        if (wd>0){ sp=Vector3Add(muzzle, Vector3Scale(fwd,wd)); sn=mnrm; landed=1; }
                    } else { Vector3 hp,hn;
                        if (RaycastWorld(muzzle, fwd, &hp,&hn)){ if (Vector3Distance(muzzle,hp)<=FLAME_RANGE){ sp=hp; sn=hn; landed=1; } } }
                    if (landed){ g_scorchT=0.0f; AddScorch(sp, sn, 0.40f+GetRandomValue(0,35)/100.0f); }
                }
            }
        } else {
            g_flameHeat = fmaxf(0.0f, g_flameHeat - dt*2.0f);           // cools off when you release -> warms up again next time
        }
    }

    // Melee (no weapon uses it since the knife was cut; kept for a future melee rig): a wide melee slash on each click. Hits the nearest enemy in a
    // short frontal arc; reuses the model's Shot clip as the swing if it has one.
    if (cw->melee && !g_reloading && lmbP && g_fireCd<=0.0f){
        g_fireCd=KNIFE_CD; g_recoil=1.0f;                                  // a swing kick
        if (g_aShoot!=g_aIdle && g_aShoot<g_gunAnimN){ g_curAnim=g_aShoot; g_animOnce=1; g_animT=0.0f; }
        if (g_audio && cw->hasSnd) PlaySound(cw->fireSnd);
        int bestE=-1; float bestD=KNIFE_RANGE;
        for (int i=0;i<MAX_ENEMIES;i++){
            if (g_enemies[i].state!=1) continue;
            Vector3 cen=(Vector3){g_enemies[i].pos.x, g_enemies[i].pos.y+ENEMY_HEIGHT*0.5f, g_enemies[i].pos.z};
            Vector3 to=Vector3Subtract(cen, g_pos); float d=Vector3Length(to);
            if (d>KNIFE_RANGE || d<1e-3f) continue;
            Vector3 tn=Vector3Scale(to, 1.0f/d);
            if (Vector3DotProduct(tn, fwd) < KNIFE_COS) continue;          // outside the slash arc
            if (d<bestD){ bestD=d; bestE=i; }                              // nearest in-arc enemy
        }
        if (bestE>=0){
            Vector3 cen=(Vector3){g_enemies[bestE].pos.x, g_enemies[bestE].pos.y+ENEMY_HEIGHT*0.6f, g_enemies[bestE].pos.z};
            g_hurtCause=HC_MELEE; HurtEnemy(bestE, KNIFE_DMG, 0, fwd, cen, 1); g_hurtCause=HC_BULLET;
        }
    }

    // minigun: hold the trigger -> the barrel SPINS UP for MG_SPINUP (1/3 s) with no
    // bullets, then once it's at speed it fires full-auto (hard 5s cap -> overheat +
    // cooldown sound). Firing also drives g_shake (screen jitter + edge vignette).
    if (cw->spinUp){
        if (!lmb) g_mgLock=0;                              // releasing clears the overheat lock
        int trigger = lmb && !g_reloading && !g_mgLock;
        int canFire = (cw->magSize<=0 || cw->mag>0);       // empty belt: barrel still revs, nothing fires/heats
        if (trigger){
            if (g_mgSpinT<=0.0f && g_audio && g_hasSpinUpSnd){ SetSoundVolume(g_spinUpSnd,0.7f); PlaySound(g_spinUpSnd); }   // the barrels wind up
            g_mgSpinT += dt;
            g_spin = fminf(1.0f, g_mgSpinT/MG_SPINUP);     // barrel accelerates, reaching full at MG_SPINUP
            if (g_audio && cw->hasAuxSnd && IsSoundPlaying(cw->auxSnd)) StopSound(cw->auxSnd);  // revving -> cut cooldown
            if (g_mgSpinT >= MG_SPINUP){                   // AT SPEED -> actually shoot bullets
                Fire(g_cam);
                if (canFire){ firingNow=1; g_mgHeat+=dt; if (g_mgHeat>=5.0f) g_mgLock=1; }   // heat/roar only while really firing
            }
            g_shake = fminf(1.0f, g_shake + dt*(canFire?3.5f:1.0f));   // dry rev shakes far less
        } else {
            if (g_mgFiring && g_spin>0.3f && g_audio && cw->hasAuxSnd) PlaySound(cw->auxSnd);   // just stopped -> cooldown sound
            g_mgSpinT=0.0f;
            g_spin=fmaxf(0.0f, g_spin-dt*0.5f);            // barrel spins down (~2s to stop)
            if (g_spin<=0.0f && g_audio && cw->hasAuxSnd && IsSoundPlaying(cw->auxSnd)) StopSound(cw->auxSnd);  // stopped -> cut sound
            g_mgHeat=0.0f;
            g_shake=fmaxf(0.0f, g_shake-dt*4.0f);
        }
        g_mgFiring = (trigger && canFire && g_mgSpinT>=MG_SPINUP);    // "firing" = actually shooting (drives the cooldown-sound edge)
        // smoke/steam out of the spinning barrels: thick gunsmoke while firing, lighter
        // steam while the barrel spins down and cools. Emitted at the muzzle in world space.
        if (g_spin>0.02f){
            Vector3 muzzle=VmMuzzleWorld(g_pos, fwd, 1.1f);                      // at the drawn barrel tips
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
            if (cw->magSize>0 && cw->mag<=0){              // empty: reload instead of starting a burst sound
                if (g_fireCd<=0.0f){ g_fireCd=0.35f; TryReload(); }
            }
            else if (playing) Fire(g_cam);                 // shooting during the burst
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

}

// Advance the live weapon's viewmodel clips one frame (idle hold, walk loop, shot,
// reload sequence, grenade-launcher scrub, minigun barrel spin). Shared with the gallery.
static void WeaponAnimate(float dt, int moving){
    Weapon *cw=&g_weapons[g_curWeapon];
    // advance weapon animation. idle = HOLD the last frame of Take (static ready
    // pose, no wandering). One-shot clips (Shoot/Reload) play through, then snap
    // back to the held idle.
    if (g_gunAnimN>0 && g_curAnim<g_gunAnimN){
        int nf=ANIM_FRAMES(g_gunAnim[g_curAnim]);
        if (cw->grenade){
            // grenade launcher: one baked 'allanims' clip. HOLD the idle frame between
            // shots; R scrubs the reload sub-range (GL_RELOAD_FROM..TO) then snaps back.
            float hold = (cw->idleHold>=0)?(float)cw->idleHold:(float)GL_IDLE_FRAME;   // Z/X-tuned grip frame
            if (g_vmHoldDev>=0){ hold=(float)g_vmHoldDev; static int once=0; if (!once){ once=1; DebugLog("vmhold","\"frames\":%d",nf); } }   // --vm-hold N (dev)
            if (g_reloading){
                float pv=g_animT;
                g_animT += dt*(g_glPhase==2 ? 52.0f : (g_glCut ? 130.0f : 62.0f));
                if (g_glPhase==1 && g_animT>=(float)GL_OPEN_TO){ g_glPhase=2; g_animT=(float)GL_FEED_FROM+(g_animT-GL_OPEN_TO); }
                else if (g_glPhase==2){
                    if (pv<(float)GL_FEED_AT && g_animT>=(float)GL_FEED_AT && cw->mag<cw->magSize && cw->reserve>0){
                        cw->mag++; cw->reserve--;
                        if (g_audio && g_hasGlSnd[1]){ SetSoundPitch(g_glSnd[1],0.95f+GetRandomValue(0,10)/100.0f); PlaySound(g_glSnd[1]); }   // a grenade seats
                        DebugLog("reload","\"slot\":%d,\"mag\":%d,\"reserve\":%d,\"shell\":1", g_curWeapon, cw->mag, cw->reserve); }
                    if (g_animT>=(float)GL_FEED_TO){
                        if (cw->mag<cw->magSize && cw->reserve>0) g_animT-=(float)(GL_FEED_TO-GL_FEED_FROM);     // another one
                        else { g_glPhase=3; g_animT=(float)GL_SHUT_FROM; if (g_audio && g_hasGlSnd[2]) PlaySound(g_glSnd[2]); } }
                }
                else if (g_glPhase==3 && g_animT>=(float)GL_SHUT_TO){
                    g_reloading=0; g_glPhase=0; g_animT=hold; if (g_glCut){ g_glCut=0; g_glFire=1; } }
                else if (g_glPhase==0){ g_reloading=0; g_animT=hold; }                                          // (never left mid-air)
            } else g_animT = (nf>0) ? hold : 0.0f;
        } else if (g_weapons[g_curWeapon].spinUp && g_curAnim==g_aIdle && !g_animOnce){
            // minigun: the barrel mesh is rigid (not skinned) and 'allanims' only
            // animates the arms -- so scrubbing it just flails the hands. Instead
            // HOLD the arms at a static grip and spin the barrel geometrically
            // (DrawViewmodel rotates the barrel mesh by g_mgBarrelAngle).
            // Z/X-tuned idleHold overrides the default grip frame.
            g_animT = (cw->idleHold>=0) ? (float)cw->idleHold : ((nf>0) ? (float)MG_IDLE_FRAME : 0.0f);
            g_mgBarrelAngle += dt*g_spin*42.0f;            // ~6.7 rev/s at full spin-up
            if (g_mgBarrelAngle>2.0f*PI) g_mgBarrelAngle-=2.0f*PI;
        } else if (!g_animOnce && (g_curAnim==g_aIdle || g_curAnim==g_aWalk)){
            // locomotion: if the rig has a Walk clip, loop it while moving; otherwise
            // (or when standing still) HOLD the idle clip on its ready frame.
            if (g_aWalk>=0 && moving){
                if (g_curAnim!=g_aWalk){ g_curAnim=g_aWalk; g_animT=0.0f; }
                int wf=ANIM_FRAMES(g_gunAnim[g_curAnim]);
                g_animT += dt*30.0f; if (wf>0 && g_animT>=wf) g_animT-=wf;   // loop the walk cycle
            } else {
                if (g_curAnim!=g_aIdle){ g_curAnim=g_aIdle; g_animT=0.0f; }
                int idf=ANIM_FRAMES(g_gunAnim[g_curAnim]);
                if (cw->idleHold>=0) g_animT=(float)cw->idleHold;                 // Z/X-tuned grip frame wins
                else if (g_aWalk>=0){ g_animT+=dt*30.0f; if (idf>0 && g_animT>=idf) g_animT-=idf; }  // real FPS rig (has Walk): LOOP the idle -- it grips in every frame
                else g_animT = idf>0 ? (float)(idf-1) : 0.0f;                     // legacy rigs: freeze on the ready frame
            }
        } else {
            float pvT=g_animT;
            g_animT += dt*30.0f;
            ShellFeed(cw, pvT);
            // nf<=0 (empty/malformed clip) counts as instantly finished, so a
            // zero-frame reload phase can't wedge g_reloading=1 forever.
            if (nf<=0 || g_animT>=nf){
                if (g_animOnce){
                    if (g_reloadStep>=0 && g_reloadStep+1<g_reloadSeqN){
                        g_reloadStep++;                  // next reload phase
                        g_curAnim=g_reloadSeq[g_reloadStep]; g_animT=0.0f;
                    } else {                             // sequence (or one-shot) done
                        int feeding=0;
                        if (g_reloading){
                            Weapon *rw=&g_weapons[g_curWeapon];
                            if (rw->shellReload && rw->magSize>0){
                                // shell-by-shell (benelli): each pass of the insert clip feeds ONE
                                // shell, looping until the tube is full or the reserve runs dry.
                                if (rw->mag<rw->magSize && rw->reserve>0){ rw->mag++; rw->reserve--; }
                                DebugLog("reload","\"slot\":%d,\"mag\":%d,\"reserve\":%d", g_curWeapon, rw->mag, rw->reserve);
                                if (rw->mag<rw->magSize && rw->reserve>0){
                                    g_reloadStep=(g_reloadSeqN>0)?0:-1;
                                    g_curAnim=(g_reloadSeqN>0)?g_reloadSeq[0]:g_aReload;
                                    g_animT=0.0f; feeding=1;
                                }
                            } else if (rw->shellCycle<=0) FinishReload();   // a completed RELOAD refills (shell feeders already did)
                        }
                        if (!feeding){ g_reloadStep=-1; g_reloading=0; g_curAnim=g_aIdle; g_animOnce=0; g_animT=0.0f; }
                    }
                } else g_animT = nf>0 ? g_animT-nf : 0.0f;
            }
        }
    }

}

// Tracers fade; sparks and blood fly under gravity and settle on the floor. Shared with the gallery.
static void EffectsUpdate(float dt){
    DynLightsUpdate(dt); CasingsUpdate(dt); { void GibsUpdate(float); GibsUpdate(dt); }
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life>0) g_tracers[i].life-=dt;
    for (int i=0;i<MAX_SPARKS;i++) if (g_sparks[i].life>0){
        Spark *sp=&g_sparks[i];
        int resting=(sp->vel.x==0 && sp->vel.y==0 && sp->vel.z==0 && sp->pos.y<=sp->rest+0.03f);
        if (resting){
            sp->life-=dt;                          // glints + fine spray fade where they landed
        } else {
            sp->life-=dt;
            sp->vel.y-=12.0f*dt;
            sp->pos=Vector3Add(sp->pos,Vector3Scale(sp->vel,dt));
            if (sp->pos.y<=sp->rest){              // hit the floor -> settle
                float hl=sqrtf(sp->vel.x*sp->vel.x+sp->vel.z*sp->vel.z);  // keep the horizontal heading for a directional splat
                if (hl>0.001f){ sp->ix=sp->vel.x/hl; sp->iz=sp->vel.z/hl; }
                sp->pos.y=sp->rest+0.015f; sp->vel=(Vector3){0,0,0};
                if (sp->lasting){ StainAddGob(sp); sp->life=0.0f; } // lasting spatter: baked into the stain mesh, particle freed
                else if (sp->life<0.5f) sp->life=0.5f;             // brief impact glow
            }
        }
    }

}

static void Update(void) {
    float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;

    // While P-paused, ANY of P / ESC / click resumes (handled here, before ESC would
    // otherwise open the options menu). Stays captured so play resumes seamlessly.
    if (g_paused){
        if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){
            g_paused=0; DebugLog("pause","\"paused\":false");
        }
        return;
    }

    // ESC toggles the options/pause menu. While open the cursor is freed for
    // clicking and the whole gameplay update is skipped (early return), so the
    // world freezes behind the overlay.
    { static int mt=0; if (!mt && getenv("CHERNOBYL_MENU_TEST")){ mt=1; g_menu=1; } }   // dev: open the ESC menu for a screenshot
    if (IsKeyPressed(KEY_ESCAPE)){
        g_menu=!g_menu;
        if (g_menu) EnableCursor(); else DisableCursor();
        DebugLog("menu","\"open\":%s", g_menu?"true":"false");
    }
    if (g_menu){
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){
            Vector2 mp=GetMousePosition();
            for (int i=0;i<MENU_N;i++) if (CheckCollisionPointRec(mp,MenuRect(i))){
                if (g_audio && g_hasMenuSelSnd){ SetSoundVolume(g_menuSelSnd,0.6f); PlaySound(g_menuSelSnd); }
                if (i==0){                                  // Fullscreen toggle
                    g_fullscreen=!g_fullscreen; ApplyFullscreen(g_fullscreen); SaveOptions();
                    EnableCursor();                         // the toggle can re-grab; keep it free for the menu
                    DebugLog("options","\"fullscreen\":%s", g_fullscreen?"true":"false");
                } else if (i==1){ g_returnToMenu=1; g_menu=0;     // Restart Run -> reload the arena from wave 1
                    DebugLog("menu","\"selectMap\":true"); }
                else if (i==2){ g_menu=0; DisableCursor(); }      // Resume
                else if (i==3) g_quit=1;                          // Quit
            }
        }
        return;
    }

    // P pauses/unpauses the game: the gameplay update is skipped (early return) so
    // the world freezes, but the cursor stays captured so play resumes seamlessly.
    // The cursor stays hidden and captured in every in-game state -- playing, paused, the
    // death screen -- and only the ESC menu (which needs a pointer) frees it. macOS shows it
    // again whenever the window loses focus, so re-grab the moment we have focus back.
    if (!g_menu && IsWindowFocused() && !IsCursorHidden()) DisableCursor();
    if (IsKeyPressed(KEY_P)){
        g_paused=!g_paused;
        DebugLog("pause","\"paused\":%s", g_paused?"true":"false");
    }
    if (g_paused) return;

    // Dead: the world is frozen (no move/fire, enemies stop too) behind the "YOU
    // DIED" overlay. After a short beat, ENTER or a click respawns you at spawn.
    if (g_dead){
        g_deadT += dt;
        if (g_hurt>0) g_hurt=fmaxf(0.0f, g_hurt-dt*1.2f);
        if (g_deadT>1.0f && LbDeadUpdate()){            // initials / posting first; then ENTER or a click plays again
            if (g_hasMap) g_returnToMenu=1;            // a death ENDS the run -> reload the arena, wave 1
            else RespawnPlayer();                      // built-in arena keeps the quick respawn
        }
        return;
    }

    // --cycle (soak): step through every slot twice -- spending ammo on pass 1 --
    // logging the counts at each stop. The soak asserts pass 2 reads back pass 1's
    // spent values: any slot-state corruption around the ActivateWeapon /
    // StashActiveTuning swap shows up as a mismatch.
    if (g_cycleTest){
        static int cf=0, k=0;
        if (k<2*g_numWeapons && ++cf>=30){
            cf=0; int slot=(k+1)%g_numWeapons;      // 1..N-1,0 twice -- ends back on the boot slot
            SwitchWeapon(slot);
            Weapon *w=&g_weapons[slot];
            if (k<g_numWeapons && w->magSize>0){ w->mag-=(w->mag<3?w->mag:3); w->reserve-=(w->reserve<2?w->reserve:2); }
            DebugLog("ammo","\"phase\":%d,\"slot\":%d,\"mag\":%d,\"reserve\":%d,\"magSize\":%d",
                     (k<g_numWeapons)?1:2, slot, w->mag, w->reserve, w->magSize);
            k++;
        }
    }

    // Keep the mouse captured the whole time we're playing. macOS releases the
    // cursor whenever the window loses focus (Cmd-Tab, notifications, etc.), so
    // re-grab it the instant we have focus again and it's become visible.
    if (IsWindowFocused() && !IsCursorHidden()) DisableCursor();

    if (IsWindowReady() && IsCursorHidden()){
        Vector2 md=GetMouseDelta();
        g_vmLook=md;
        g_yaw-=md.x*0.0025f; g_pitch-=md.y*0.0025f;
        if (g_pitch>1.5f) g_pitch=1.5f;  if (g_pitch<-1.5f) g_pitch=-1.5f;
    }
    Vector3 fwd =Vector3Normalize((Vector3){ sinf(g_yaw)*cosf(g_pitch), sinf(g_pitch), cosf(g_yaw)*cosf(g_pitch) });
    Vector3 flat=Vector3Normalize((Vector3){ sinf(g_yaw), 0, cosf(g_yaw) });
    Vector3 right=Vector3Normalize(Vector3CrossProduct(flat,(Vector3){0,1,0}));

    float speed=(IsKeyDown(KEY_LEFT_SHIFT)?9.0f:5.0f)*(g_speedT>0.0f?1.5f:1.0f);   // SPEED power-up
    Vector3 wish={0,0,0}; int moving=0;
    if (IsKeyDown(KEY_W)){wish=Vector3Add(wish,flat);moving=1;}
    if (IsKeyDown(KEY_S)){wish=Vector3Subtract(wish,flat);moving=1;}
    if (IsKeyDown(KEY_D)){wish=Vector3Add(wish,right);moving=1;}
    if (IsKeyDown(KEY_A)){wish=Vector3Subtract(wish,right);moving=1;}
    if (g_kite){                                        // dev autopilot: face the nearest enemy, back away
        float bd=1e9f; int bi=-1;
        for (int k=0;k<MAX_ENEMIES;k++) if (g_enemies[k].state==1){ float dd=Vector3Distance(g_enemies[k].pos,g_pos); if (dd<bd){ bd=dd; bi=k; } }
        if (bi>=0){ g_yaw=atan2f(g_enemies[bi].pos.x-g_pos.x, g_enemies[bi].pos.z-g_pos.z); g_pitch=-0.05f;
            flat=(Vector3){sinf(g_yaw),0,cosf(g_yaw)}; right=Vector3Normalize(Vector3CrossProduct(flat,(Vector3){0,1,0}));
            wish=Vector3Subtract(wish,flat); if (((int)(GetTime()/3.0))&1) wish=Vector3Add(wish,Vector3Scale(right,0.6f)); moving=1; }
    }
    g_playerMoveAmt += ((moving?1.0f:0.0f)-g_playerMoveAmt)*fminf(1.0f,dt*6.0f);   // spoils enemy hitscan aim
    Vector3 mv={0,0,0};
    if (moving && Vector3Length(wish)>0.001f){
        wish=Vector3Scale(Vector3Normalize(wish),speed*dt);
        { float pb=g_bob; mv=wish; g_bob+=dt*(IsKeyDown(KEY_LEFT_SHIFT)?14.0f:10.0f);
          if (g_grounded && g_audio && g_hasSy && floorf(g_bob/3.14159f)!=floorf(pb/3.14159f)){    // a boot lands each half-cycle of the bob
              SetSoundPitch(g_sy[SY_STEP], 0.88f+GetRandomValue(0,24)/100.0f); SetSoundVolume(g_sy[SY_STEP], IsKeyDown(KEY_LEFT_SHIFT)?0.34f:0.24f);
              SetSoundPan(g_sy[SY_STEP], (((int)floorf(g_bob/3.14159f))&1)?0.18f:-0.18f); PlaySound(g_sy[SY_STEP]); } }
    }
    if (g_hasMap && !g_noclip){                         // block into walls per-axis (so you slide, never drift)
        float by=g_pos.y-0.55f;
        // ...and into enemies: a body on its feet is solid. Each axis step is refused if it would end
        // inside one AND closer to him than now (so you can always back out of a crowd, never through it)
        if (!MapSphereHitsWall((Vector3){g_pos.x+mv.x, by, g_pos.z}, 0.42f) && !PlayerIntoEnemy(g_pos.x+mv.x, g_pos.z)) g_pos.x+=mv.x;
        if (!MapSphereHitsWall((Vector3){g_pos.x, by, g_pos.z+mv.z}, 0.42f) && !PlayerIntoEnemy(g_pos.x, g_pos.z+mv.z)) g_pos.z+=mv.z;
    } else { g_pos.x+=mv.x; g_pos.z+=mv.z; }
    if (g_hasMap && g_noclip){                         // F: free-fly / noclip (inspect or escape)
        if (IsKeyDown(KEY_SPACE))        g_pos.y += speed*dt;
        if (IsKeyDown(KEY_LEFT_CONTROL)) g_pos.y -= speed*dt;
        g_vy=0; g_grounded=1;
    } else if (g_hasMap){                               // FPS collision against the loaded map mesh
        if (g_grounded && IsKeyPressed(KEY_SPACE)){ g_vy=6.0f; g_grounded=0;
            if (g_audio && g_hasSy){ SetSoundPitch(g_sy[SY_JUMP],0.95f+GetRandomValue(0,12)/100.0f); SetSoundVolume(g_sy[SY_JUMP],0.30f); PlaySound(g_sy[SY_JUMP]); } }
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
            if (dn>0 && g_pos.y<=floorEye){                                                  // land
                if (g_vy<-2.5f && g_audio && g_hasSy){ SetSoundPitch(g_sy[SY_LAND],0.92f+GetRandomValue(0,14)/100.0f);
                    SetSoundVolume(g_sy[SY_LAND], fminf(0.75f, 0.28f+(-g_vy)*0.035f)); PlaySound(g_sy[SY_LAND]); }   // harder falls land louder
                g_pos.y=floorEye; g_vy=0.0f; g_grounded=1; }
            if (g_vy>0.0f){ float up=MapRayNearest(g_pos,(Vector3){0,1,0},0.4f); if (up>0) g_vy=0.0f; }  // ceiling
        }
    } else {
        if (g_grounded && IsKeyPressed(KEY_SPACE)){ g_vy=5.0f; g_grounded=0; }
        g_vy-=16.0f*dt; g_pos.y+=g_vy*dt;
        if (g_pos.y<=EYE_H){ g_pos.y=EYE_H; g_vy=0; g_grounded=1; }
    }
    // Fell out of the world (off a ledge into the void). Lethal in normal play; in
    // god mode just teleport back to spawn so dev play isn't an endless fall.
    if (g_hasMap && !g_noclip && g_pos.y < g_voidY){
        if (!g_godMode) g_playerHp = 0.0f;
        g_pos=g_spawnResetPos; g_vy=0.0f; g_grounded=0; g_eyeSmooth=0.0f;
        DebugLog("void","\"fell\":true,\"died\":%s", g_godMode?"false":"true");
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
    WeaponTrigger(dt, fwd, IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsMouseButtonPressed(MOUSE_BUTTON_LEFT));

    UpdateEnemies(dt);
    UpdateWaves(dt); PickupsUpdate(dt); BoltsUpdate(dt); g_runT+=dt;                       // wave director + the run clock
    UpdateSmoke(dt);
    UpdateGrenades(dt);

    WeaponAnimate(dt, moving);                             // the gun's idle/shot/reload clips

    // overlay + inspect + reset
    if (IsKeyPressed(KEY_GRAVE) || IsKeyPressed(KEY_TAB)) g_devOverlay=!g_devOverlay;
    if (IsKeyPressed(KEY_V)) g_inspect=!g_inspect;
    if (IsKeyPressed(KEY_G)){ g_godMode=!g_godMode; if (g_godMode){ g_playerHp=100.0f; g_cheatUsed=1; }   // refill on re-enable; the run is tagged
        DebugLog("mode","\"godMode\":%s", g_godMode?"true":"false"); }
    if (IsKeyPressed(KEY_F) && g_hasMap){ g_noclip=!g_noclip; g_vy=0.0f;   // map: fly/noclip <-> walk
        DebugLog("mode","\"noclip\":%s", g_noclip?"true":"false"); }
    if (IsKeyPressed(KEY_N)){                          // N: toggle orient mode (no enemies, weapon-aim tuning)
        g_noEnemies=!g_noEnemies;
        for (int i=0;i<MAX_ENEMIES;i++) g_enemies[i].state=0;   // clear the field
        for (int c=0;c<MAX_CORPSES;c++) g_corpses[c].active=0;  // and clear corpses
        g_playerHp=100.0f; PickupsReset();             // full health (clears a leftover YOU DIED)
        if (!g_noEnemies){ g_wave=0; g_waveBreather=0.0f; }     // restart the wave director from wave 1
        DebugLog("mode","\"noEnemies\":%s", g_noEnemies?"true":"false");
    }
    // Number keys -> weapon slots. Custom order so the two showpieces lead: 1=minigun,
    // 2=flamethrower, 3=grenade launcher, then the rest. (g_weapons indices: 0 grenade
    // launcher,1 remington,2 minigun,3 LMG,4 AK,5 flamethrower.)
    static const int kSlotForKey[4]={2,5,0,1};        // 1 minigun, 2 flamethrower, 3 grenade launcher, 4 Remington
    for (int k=0;k<4;k++) if (IsKeyPressed(KEY_ONE+k)) SwitchWeapon(kSlotForKey[k]);
    if (IsKeyPressed(KEY_ZERO)){ Weapon *w=&g_weapons[g_curWeapon]; g_vmOff=w->off0; g_vmScale=w->scale0; g_vmYaw=w->yaw0; g_vmPitch=w->pitch0; g_vmRoll=w->roll0; }
    // Save: ENTER (or F5). On Mac F5 is a system key (dictation/keyboard light)
    // and gets eaten by the OS, so ENTER is the reliable bind. g_savedMsg flashes
    // an on-screen confirmation so you KNOW it wrote.
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_F5)){
        StashActiveTuning();
        SaveTune(g_weapons[g_curWeapon].trimPath,g_vmOff,g_vmScale,g_vmYaw,g_vmPitch,g_vmRoll,g_weapons[g_curWeapon].idleHold);
        { Weapon *w=&g_weapons[g_curWeapon]; SavePin(w->tunePath,w->pinOff,w->pinScale,w->pinYaw,w->pinPitch,w->pinRoll,w->armsROff,w->armsLOff); }   // grip/hand tunes save for every weapon
        g_savedMsg=2.0f;   // seconds to show "SAVED"
        DebugLog("vmsave","\"slot\":%d,\"label\":\"%s\",\"path\":\"%s\",\"saved\":true",
                 g_curWeapon, g_weapons[g_curWeapon].label, g_weapons[g_curWeapon].trimPath);
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
    // Fine steps by default; hold SHIFT for coarse (5x). The same base feeds the
    // hands-manager nudges, so their net speeds are unchanged by this slowdown.
    float fine=(IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT))?5.0f:1.0f;
    float ns=dt*(g_noEnemies?0.9f:0.15f)*fine;
    float rs=dt*(g_noEnemies?36.0f:18.0f)*fine;
    float ss=(g_noEnemies?0.9f:0.3f)*fine;
    Weapon *cwp=&g_weapons[g_curWeapon];
    int bStates = cwp->pinGun ? 2 : 3;   // pinned gun: VIEWMODEL/GUN GRIP; every other weapon: VIEWMODEL/LEFT HAND/RIGHT HAND ("hands manager")
    if (IsKeyPressed(KEY_B) && bStates>1) g_tuneTarget=(g_tuneTarget+1)%bStates;
    if (g_tuneTarget>=bStates) g_tuneTarget=0;
    if (g_tuneTarget>=1 && !cwp->pinGun){                                        // hands manager: nudge ONE hand (1=left, 2=right), baked into the verts
        Weapon *w=cwp;
        int side = (g_tuneTarget==2) ? 1 : -1;
        int bone = (side==1) ? w->handBone : w->handBoneL;
        float np=ns/fmaxf(g_vmScale*cwp->vmBase,1e-9f)*0.0667f;   // SHIFT-coarse comes via ns
        Vector3 dv={0,0,0};
        if (IsKeyDown(KEY_I)) dv.y+=np;  if (IsKeyDown(KEY_K)) dv.y-=np;
        if (IsKeyDown(KEY_L)) dv.x+=np;  if (IsKeyDown(KEY_J)) dv.x-=np;
        if (IsKeyDown(KEY_O)) dv.z+=np;  if (IsKeyDown(KEY_U)) dv.z-=np;
        if (fabsf(dv.x)+fabsf(dv.y)+fabsf(dv.z)>0.0f){
            dv=NudgeToBindSpace(w, bone, dv);          // keys track view axes even on held-frame rigs
            ApplyArmsSideDelta(w, dv, side);
            if (side==1) w->armsROff=Vector3Add(w->armsROff,dv);
            else         w->armsLOff=Vector3Add(w->armsLOff,dv);
        }
    } else if (g_tuneTarget==1){                                                 // pinned gun: tune the gun's grip on the hand bone
        Weapon *w=cwp;
        float np=ns/fmaxf(g_vmScale*cwp->vmBase,1e-9f)*0.0667f;   // model-unit step; SHIFT-coarse comes via ns, net speed unchanged
        if (IsKeyDown(KEY_I)) w->pinOff.y+=np;  if (IsKeyDown(KEY_K)) w->pinOff.y-=np;
        if (IsKeyDown(KEY_L)) w->pinOff.x+=np;  if (IsKeyDown(KEY_J)) w->pinOff.x-=np;
        if (IsKeyDown(KEY_O)) w->pinOff.z+=np;  if (IsKeyDown(KEY_U)) w->pinOff.z-=np;
        if (IsKeyDown(KEY_EQUAL)) w->pinScale*=(1.0f+dt*ss);  if (IsKeyDown(KEY_MINUS)) w->pinScale*=(1.0f-dt*ss);
        if (IsKeyDown(KEY_RIGHT_BRACKET)) w->pinYaw+=rs;  if (IsKeyDown(KEY_LEFT_BRACKET)) w->pinYaw-=rs;
        if (IsKeyDown(KEY_APOSTROPHE)) w->pinPitch+=rs;   if (IsKeyDown(KEY_SEMICOLON)) w->pinPitch-=rs;
        if (IsKeyDown(KEY_PERIOD)) w->pinRoll+=rs;        if (IsKeyDown(KEY_COMMA)) w->pinRoll-=rs;
    } else {
        if (IsKeyDown(KEY_I)) g_vmOff.y+=ns;  if (IsKeyDown(KEY_K)) g_vmOff.y-=ns;
        if (IsKeyDown(KEY_L)) g_vmOff.x+=ns;  if (IsKeyDown(KEY_J)) g_vmOff.x-=ns;
        if (IsKeyDown(KEY_O)) g_vmOff.z+=ns;  if (IsKeyDown(KEY_U)) g_vmOff.z-=ns;
        if (IsKeyDown(KEY_EQUAL)) g_vmScale*=(1.0f+dt*ss);  if (IsKeyDown(KEY_MINUS)) g_vmScale*=(1.0f-dt*ss);
        if (IsKeyDown(KEY_RIGHT_BRACKET)) g_vmYaw+=rs;  if (IsKeyDown(KEY_LEFT_BRACKET)) g_vmYaw-=rs;
        if (IsKeyDown(KEY_APOSTROPHE)) g_vmPitch+=rs;   if (IsKeyDown(KEY_SEMICOLON)) g_vmPitch-=rs;
        if (IsKeyDown(KEY_PERIOD)) g_vmRoll+=rs;        if (IsKeyDown(KEY_COMMA)) g_vmRoll-=rs;
    }
    // Z / X scrub the ready-pose HOLD frame for the current weapon (find the frame where
    // the hands grip the gun). SHIFT = coarse (x10). Held key auto-repeats. ENTER saves it.
    {
        int xup=(IsKeyPressed(KEY_X)||IsKeyPressedRepeat(KEY_X)), xdn=(IsKeyPressed(KEY_Z)||IsKeyPressedRepeat(KEY_Z));
        if (xup||xdn){
            Weapon *w=&g_weapons[g_curWeapon];
            int clip = w->grenade ? 0 : g_aIdle;
            int maxf = (g_gunAnimN>0 && clip<g_gunAnimN) ? ANIM_FRAMES(g_gunAnim[clip]) : 1;
            if (w->idleHold<0) w->idleHold = w->grenade ? GL_IDLE_FRAME : (maxf>0?maxf-1:0);
            int step=(IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT))?10:1;
            w->idleHold += xup?step:-step;
            if (w->idleHold<0) w->idleHold=0;  if (maxf>0 && w->idleHold>=maxf) w->idleHold=maxf-1;
            DebugLog("idlehold","\"slot\":%d,\"frame\":%d,\"maxf\":%d", g_curWeapon, w->idleHold, maxf);
        }
    }
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

    EffectsUpdate(dt);                                     // tracers fade, blood/sparks fly and settle

    // Damage feedback + death (after every hp change this frame: enemy melee, void).
    if (g_playerHp < g_prevHp-0.01f){                               // took a hit -> punch the red flash
        g_hurt = fminf(1.0f, g_hurt + (g_prevHp-g_playerHp)*0.04f + 0.12f);
        if (g_playerHp<g_prevHp-1.0f && g_audio && g_hasHurtSnd && !IsSoundPlaying(g_hurtSnd))
            PlaySound(g_hurtSnd);                                   // real hits thud (sub-point ticks stay quiet)
        // a lot of damage in a short while: sometimes he cries out (a yell or a scream, never back to back)
        g_painAcc += g_prevHp-g_playerHp;
        if (g_painAcc>=PAIN_DMG && g_painCd<=0.0f && g_audio && g_nPainSnd>0 && g_playerHp>0.0f){
            g_painAcc=0.0f;
            if (GetRandomValue(0,99)<65){ static int last=-1; int k=GetRandomValue(0,g_nPainSnd-1); if (k==last && g_nPainSnd>1) k=(k+1)%g_nPainSnd; last=k;
                SetSoundPitch(g_painSnd[k], 0.95f+GetRandomValue(0,10)/100.0f); SetSoundVolume(g_painSnd[k],0.85f); PlaySound(g_painSnd[k]);
                g_painCd=PAIN_GAP; DebugLog("pain","\"snd\":%d,\"hp\":%.0f", k, g_playerHp); }
        }
    }
    if (g_appearCd>0) g_appearCd-=dt;                          // nobody in sight, then somebody: the sting (as ../Chernobyl)
    if (g_seenNow>0 && g_seenPrev==0 && g_appearCd<=0.0f && g_audio && g_hasAppearSnd && !g_dead){
        SetSoundVolume(g_appearSnd,0.55f); PlaySound(g_appearSnd); g_appearCd=6.0f; DebugLog("appear","\"n\":%d", g_seenNow); }
    g_seenPrev=g_seenNow;
    for (int t=0;t<EN_NTYPES;t++) for (int c=0;c<EC_N;c++) if (g_eCueCd[t][c]>0) g_eCueCd[t][c]-=dt;
    if (g_audio && g_hasRunSnd){                               // running feet: the nearest hostile actually running at him
        float best=1e9f; Vector3 at={0}; 
        if (!g_dead) for (int i=0;i<MAX_ENEMIES;i++){ const Enemy *e=&g_enemies[i];
            if (e->state!=1 || e->riseT>0 || e->knockT>0) continue;
            float sp=sqrtf(e->vel.x*e->vel.x+e->vel.z*e->vel.z); if (sp<RUN_SND_SPEED) continue;
            float dd=Vector3Distance(e->pos,g_pos); if (dd<best){ best=dd; at=e->pos; } }
        float want=(best<RUN_SND_RANGE) ? 0.55f*(1.0f-best/RUN_SND_RANGE) : 0.0f;
        g_runVol+=(want-g_runVol)*fminf(1.0f,dt*6.0f);
        if (g_runVol>0.02f){
            Vector3 to=Vector3Subtract(at,g_cam.position), fw=Vector3Subtract(g_cam.target,g_cam.position); float tl=Vector3Length(to), rl=sqrtf(fw.x*fw.x+fw.z*fw.z);
            SetSoundVolume(g_runSnd,g_runVol); SetSoundPan(g_runSnd,(tl>0.3f && rl>1e-4f)?((to.x*-fw.z+to.z*fw.x)/(rl*tl))*0.85f:0.0f);
            if (!IsSoundPlaying(g_runSnd)) PlaySound(g_runSnd);
        } else if (IsSoundPlaying(g_runSnd)) StopSound(g_runSnd);
    }
    if (g_surrCd>0) g_surrCd-=dt;                              // surrounded: SURR_N or more inside SURR_RANGE, in front AND behind
    if (g_surrCd<=0.0f && g_audio && g_hasSurrSnd && !g_dead && !g_godMode){
        Vector3 fw=Vector3Subtract(g_cam.target,g_cam.position); int n=0, ahead=0, behind=0;
        for (int i=0;i<MAX_ENEMIES;i++){ const Enemy *e=&g_enemies[i];
            if (e->state!=1 || e->riseT>0 || e->knockT>0) continue;
            float dx=e->pos.x-g_pos.x, dz=e->pos.z-g_pos.z; if (dx*dx+dz*dz>SURR_RANGE*SURR_RANGE) continue;
            n++; if (dx*fw.x+dz*fw.z>=0) ahead++; else behind++; }
        if (n>=SURR_N && ahead>0 && behind>0){ SetSoundVolume(g_surrSnd,0.85f); PlaySound(g_surrSnd); g_surrCd=25.0f; DebugLog("surrounded","\"n\":%d", n); }
    }
    if (g_mobCd>0) g_mobCd-=dt;                                // a pack charging: MOB_N or more up and closing inside MOB_RANGE
    if (g_mobCd<=0.0f && g_audio && g_hasMobSnd && !g_dead && !IsSoundPlaying(g_mobSnd)){
        int n=0; for (int i=0;i<MAX_ENEMIES;i++){ const Enemy *e=&g_enemies[i];
            if (e->state==1 && e->riseT<=0 && e->knockT<=0 && Vector3Distance(e->pos,g_pos)<MOB_RANGE) n++; }
        if (n>=MOB_N){ SetSoundVolume(g_mobSnd,0.50f); PlaySound(g_mobSnd); g_mobCd=14.0f; DebugLog("mob","\"n\":%d", n); }
    }
    if (g_painAcc>0) g_painAcc=fmaxf(0.0f, g_painAcc-dt*PAIN_DMG/2.5f);     // "lately" = about the last 2.5 s
    if (g_painCd>0) g_painCd-=dt;
    g_prevHp = g_playerHp;
    if (g_hurt>0) g_hurt = fmaxf(0.0f, g_hurt - dt*1.5f);
    // low-health heartbeat: repeats while under 30 hp, stops on heal/death
    if (g_audio && g_hasHeartSnd){
        int low = (!g_dead && g_playerHp>0.0f && g_playerHp<30.0f && !g_godMode);
        if (low && !IsSoundPlaying(g_heartSnd)) PlaySound(g_heartSnd);
        if (!low && IsSoundPlaying(g_heartSnd)) StopSound(g_heartSnd);
    }
    if (!g_godMode && !g_noEnemies && g_playerHp<=0.0f && !g_dead){ // killed -> enter the death/respawn loop
        g_dead=1; g_deadT=0.0f;
        StopAllSfx();
        if (g_audio && g_hasDieSnd){ SetSoundVolume(g_dieSnd,0.9f); PlaySound(g_dieSnd); }
        LbOnDeath(g_wave*100 + g_kills*10 + g_headshots*15, g_weapons[g_curWeapon].label);
        DebugLog("death","\"kills\":%d,\"headshots\":%d,\"cheats\":%d", g_kills, g_headshots, g_cheatUsed);
    }
}

// Tracers, sparks, blood (flying, spatter, pools), scorch and in-flight grenades. Inside BeginMode3D.
// Particles are a few centimetres across, so each is a flat shape turned to the viewer: a
// tapered streak for blood in flight, a hexagon for a droplet or a glint. (They were spheres and
// cylinders of 70 to 290 vertices each, which swamped the draw batch in a firefight.)
static void DrawEffects(void){
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life>0)
        DrawLine3D(g_tracers[i].a,g_tracers[i].b,(Color){255,240,150,255});
    { void DrawGibs(void); DrawGibs(); }
    DiscTable();
    rlBegin(RL_TRIANGLES);                                   // bullet holes: pale chipped rim, black pit
    for (int i=0;i<MAX_HOLES;i++){ Hole *h=&g_holes[i]; if (!h->active) continue;
        Vector3 n=h->n, up=(fabsf(n.y)>0.9f)?(Vector3){1,0,0}:(Vector3){0,1,0};
        Vector3 T=Vector3Normalize(Vector3CrossProduct(up,n)), B=Vector3CrossProduct(n,T);
        for (int ring=0;ring<2;ring++){
            float dr=ring?h->r:h->r*1.55f, off=ring?0.016f:0.012f;
            Vector3 c=Vector3Add(h->pos,Vector3Scale(n,off));
            if (ring) rlColor4ub(6,5,5,235); else rlColor4ub(70,64,58,95);
            for (int q=0;q<10;q++){
                float c0=g_discCos[10][q]*dr, s0=g_discSin[10][q]*dr, c1=g_discCos[10][q+1]*dr, s1=g_discSin[10][q+1]*dr;
                rlVertex3f(c.x,c.y,c.z);
                rlVertex3f(c.x+T.x*c0+B.x*s0, c.y+T.y*c0+B.y*s0, c.z+T.z*c0+B.z*s0);
                rlVertex3f(c.x+T.x*c1+B.x*s1, c.y+T.y*c1+B.y*s1, c.z+T.z*c1+B.z*s1);
                rlVertex3f(c.x,c.y,c.z);                         // (both windings: culling is on here)
                rlVertex3f(c.x+T.x*c1+B.x*s1, c.y+T.y*c1+B.y*s1, c.z+T.z*c1+B.z*s1);
                rlVertex3f(c.x+T.x*c0+B.x*s0, c.y+T.y*c0+B.y*s0, c.z+T.z*c0+B.z*s0);
            } } }
    rlEnd();
    for (int i=0;i<MAX_CASINGS;i++){ Casing *c=&g_casings[i]; if (c->life<=0) continue;
        Vector3 d=(c->bounces>=3) ? (Vector3){cosf(c->ang),0,sinf(c->ang)}
                                  : Vector3RotateByAxisAngle((Vector3){1,0,0}, c->ax, c->ang);
        Color col=c->col; if (c->life<1.0f) col.a=(unsigned char)(255*c->life);
        DrawCylinderEx(Vector3Subtract(c->pos,Vector3Scale(d,c->len*0.5f)), Vector3Add(c->pos,Vector3Scale(d,c->len*0.5f)), c->rad, c->rad, 6, col);
    }
    StainDraw();                                           // settled blood: one mesh, one call
    Matrix view=rlGetMatrixModelview(), inv=MatrixInvert(view);     // the active 3D camera (game or gallery)
    Vector3 eye={inv.m12,inv.m13,inv.m14}, right={view.m0,view.m4,view.m8}, up={view.m1,view.m5,view.m9};
    DiscTable();
    rlBegin(RL_TRIANGLES);
    for (int i=0;i<MAX_SPARKS;i++) if (g_sparks[i].life>0){
        Spark *sp=&g_sparks[i];
        float k=(sp->life0>0)?sp->life/sp->life0:1.0f; if (k>1.0f) k=1.0f;
        Color c=sp->col; c.a=(unsigned char)(k*255.0f);
        float r, spd=0.0f;
        if (sp->blood){ spd=Vector3Length(sp->vel);
            r=(sp->size>0.03f?0.03f:sp->size)*0.55f; }     // capped so even gobs stay droplet-sized, never grapefruits
        else r=sp->size*(0.45f+0.55f*k)*0.6f;              // impact glint: tiny bright spark, shrinking as it fades
        rlColor4ub(c.r,c.g,c.b,c.a);
        if (sp->blood && spd>0.5f){                        // moving blood: a tapered teardrop pointing the way it flies
            Vector3 d=Vector3Scale(sp->vel,1.0f/spd);
            float len=sp->size*1.5f+spd*0.025f, rt=r*0.25f;
            Vector3 tail=Vector3Subtract(sp->pos,Vector3Scale(d,len));
            Vector3 sd=Vector3CrossProduct(d,Vector3Subtract(eye,sp->pos));
            float sl=Vector3Length(sd); if (sl<1e-6f) continue;       // flying straight at the eye: nothing to see
            sd=Vector3Scale(sd,1.0f/sl);
            Vector3 A=Vector3Add(sp->pos,Vector3Scale(sd,r)), B=Vector3Subtract(sp->pos,Vector3Scale(sd,r));
            Vector3 C=Vector3Subtract(tail,Vector3Scale(sd,rt)), D=Vector3Add(tail,Vector3Scale(sd,rt));
            rlVertex3f(A.x,A.y,A.z); rlVertex3f(B.x,B.y,B.z); rlVertex3f(C.x,C.y,C.z);
            rlVertex3f(A.x,A.y,A.z); rlVertex3f(C.x,C.y,C.z); rlVertex3f(D.x,D.y,D.z);
        } else {                                           // droplet or glint: a hexagon facing the viewer
            Vector3 p=sp->pos;
            for (int q=0;q<6;q++){
                float c0=g_discCos[6][q]*r, s0=g_discSin[6][q]*r, c1=g_discCos[6][q+1]*r, s1=g_discSin[6][q+1]*r;
                rlVertex3f(p.x,p.y,p.z);
                rlVertex3f(p.x+right.x*c0+up.x*s0, p.y+right.y*c0+up.y*s0, p.z+right.z*c0+up.z*s0);
                rlVertex3f(p.x+right.x*c1+up.x*s1, p.y+right.y*c1+up.y*s1, p.z+right.z*c1+up.z*s1);
            }
        }
    }
    rlEnd();
    DrawScorch();                                          // flamethrower soot, laid on the surfaces it touched
    for (int i=0;i<MAX_GREN;i++) if (g_gren[i].active)     // in-flight grenades: a small dark sphere
        DrawSphereEx(g_gren[i].pos, 0.13f, 8, 8, (Color){45,50,42,255});
}

static void DrawWorld(void) {
    if (g_hasMap){                          // SPIKE: draw the loaded .map (no collision yet)
        rlDisableBackfaceCulling();         // brush winding flips under the Z->Y axis swap
        if (g_hasScrollShader){             // this frame's dynamic lights + fog
            float pos[24], col[24], rad[8], vp[3]={g_cam.position.x,g_cam.position.y,g_cam.position.z}, zero[2]={0,0};
            int n=DynLightsNear(g_cam.position, 8, 45.0f, pos, col, rad);
            if (n>0){ SetShaderValueV(g_scrollShader,g_wLoc[0],pos,SHADER_UNIFORM_VEC3,n); SetShaderValueV(g_scrollShader,g_wLoc[1],col,SHADER_UNIFORM_VEC3,n);
                      SetShaderValueV(g_scrollShader,g_wLoc[2],rad,SHADER_UNIFORM_FLOAT,n); }
            SetShaderValue(g_scrollShader,g_wLoc[3],&n,SHADER_UNIFORM_INT);
            SetShaderValue(g_scrollShader,g_wLoc[4],vp,SHADER_UNIFORM_VEC3);
            SetShaderValue(g_scrollShader,g_wLoc[5],kFog,SHADER_UNIFORM_VEC4);
            SetShaderValue(g_scrollShader,g_scrollLoc,zero,SHADER_UNIFORM_VEC2);
        }
        if (g_hasScrollShader && g_mapMeshScrollAny){
            // per-mesh draw so flowing liquids/warpzones can scroll their UVs (tcMod scroll)
            float t=(float)GetTime();
            for (int i=0;i<g_map.meshCount;i++){
                float sx=g_mapMeshScroll[i*2], sy=g_mapMeshScroll[i*2+1];
                Vector2 off={ sx*t, sy*t };
                SetShaderValue(g_scrollShader,g_scrollLoc,&off,SHADER_UNIFORM_VEC2);
                DrawMesh(g_map.meshes[i], g_map.materials[g_map.meshMaterial[i]], g_map.transform);
            }
        } else {
            DrawModel(g_map,(Vector3){0,0,0},1.0f,WHITE);
        }
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
    DrawEffects();                                         // tracers, sparks, blood, scorch, grenades
}

// Soot smudges on a body: one soft dark billboard per flame-char mark, at its height
// up the body, nudged just outside the near surface (toward the camera) so the model
// doesn't occlude it. vscale compresses the heights for a prone corpse. base = feet.
static void DrawBurnMarks(Vector3 base, float vscale, const float *by, const float *br, int n){
    if (!g_hasSmokeTex) return;
    for (int k=0;k<n;k++){
        Vector3 bp={base.x, base.y + by[k]*vscale, base.z};
        float hx=g_cam.position.x-bp.x, hz=g_cam.position.z-bp.z, L=sqrtf(hx*hx+hz*hz);
        if (L>1e-3f){ bp.x+=hx*0.52f/L; bp.z+=hz*0.52f/L; }      // sit on the near surface, not the body axis
        DrawBillboard(g_cam, g_smokeTex, bp, br[k]*2.2f, (Color){14,11,9,230});
    }
}

// Distance LOD: past ENEMY_LOD_DIST from the camera an enemy draws its decimated mesh
// (same materials, same pose). --no-lod / CHERNOBYL_NO_LOD turn it off (A/B, screenshots).
#define ENEMY_LOD_DIST 12.0f
static int g_noLod = 0;
static Model *EnemyLodModel(EnemyRig *rg, Vector3 at){
    if (!rg->hasLod || g_noLod) return &rg->model;
    float dx=at.x-g_cam.position.x, dz=at.z-g_cam.position.z;
    static float d2=-1.0f;
    if (d2<0.0f){ const char *e=getenv("CHERNOBYL_LOD_DIST"); float d=e?(float)atof(e):ENEMY_LOD_DIST; d2=d*d; }   // dev: tune/test
    return (dx*dx+dz*dz > d2) ? &rg->lod : &rg->model;
}
// Draw all alive/dying enemies. The shared model is posed to each enemy's own
// animation cursor right before drawing, so they animate independently.
static void DrawEnemies(void){
    g_seenNow=0;
    if (!g_hasEnemy) return;
    for (int i=0;i<MAX_ENEMIES;i++){
        Enemy *e=&g_enemies[i];
        if (e->state==0) continue;
        // Occlusion cull: if the map blocks the camera's view of the enemy's feet, chest
        // AND head, it's fully behind a wall/stairs -> don't draw it, so no part of the
        // model pokes through the geometry to the player's side. Any point visible -> draw
        // (so an enemy peeking around cover still shows).
        if (g_hasMap && g_mapCol.ready){
            // Sample the body's SILHOUETTE, not just its centre line: feet/chest/head at the
            // centre and at shoulder width either side (across the view). Centre-only tests
            // hid an enemy half-way round a corner -- he popped out and back in while chasing.
            const float hy[3]={0.15f,0.9f,1.7f}, hx[3]={0.0f,-0.55f,0.55f}; int vis=0;
            float sx=g_cam.position.z-e->pos.z, sz=e->pos.x-g_cam.position.x, sl=sqrtf(sx*sx+sz*sz);
            if (sl>1e-4f){ sx/=sl; sz/=sl; } else { sx=1; sz=0; }
            float esc=(kEnemy[e->type].scale*e->sc);
            for (int q=0;q<9 && !vis;q++){
                int p=q%3, h=q/3;
                Vector3 pt={e->pos.x+sx*hx[h]*esc, e->drawY+hy[p]*esc, e->pos.z+sz*hx[h]*esc};
                Vector3 to=Vector3Subtract(pt,g_cam.position); float dist=Vector3Length(to);
                if (dist<0.5f){ vis=1; break; }                      // basically on top of us -> visible
                float hit=MapRayNearest(g_cam.position, Vector3Scale(to,1.0f/dist), dist);
                if (hit<=0.0f || hit>=dist-0.15f) vis=1;              // nothing blocks this point
            }
            static int lastVis[MAX_ENEMIES];
            if (vis!=lastVis[i]) DebugLog("cullflip","\"i\":%d,\"vis\":%d,\"d\":%.2f,\"pos\":[%.2f,%.2f,%.2f],\"cam\":[%.2f,%.2f,%.2f]",
                                          i,vis,Vector3Distance(e->pos,g_pos),e->pos.x,e->drawY,e->pos.z,g_cam.position.x,g_cam.position.y,g_cam.position.z);
            lastVis[i]=vis;
            if (!vis) continue;
        }
        if (e->state==1){                                     // alive, unhidden and inside the view cone: he is ON SCREEN
            Vector3 to={e->pos.x-g_cam.position.x, e->pos.y+1.0f-g_cam.position.y, e->pos.z-g_cam.position.z};
            Vector3 fw=Vector3Normalize(Vector3Subtract(g_cam.target,g_cam.position)); float tl=Vector3Length(to);
            if (tl>0.1f && Vector3DotProduct(to,fw)/tl>0.72f) g_seenNow++;
        }
        float dx=g_pos.x-e->pos.x, dz=g_pos.z-e->pos.z;       // face the player (yaw about +Y)
        float yaw=atan2f(dx,dz)*RAD2DEG + ENEMY_YAW_OFFSET;
        if (e->state==2) yaw=e->deathYaw;                     // the dying body keeps the way it was falling
        EnemyRig *rg=RigOf(e->type);
        int stored=(e->poseOK && rg->gpu && rg->layer);       // posed by EnemyPoseBuild: his own facing, layered clips
        if (stored){ yaw=e->yaw; RigPoseStored(rg,e->pose); }
        else if (rg->animN>0){
            ModelAnimation *cur=RigClip(e->type, e->state==2?(e->deathRole?e->deathRole:CL_DEATH):e->clip);
            if (e->blendT>0) RigPoseBlend(rg, RigClip(e->type,e->prevClip), e->prevT, cur, e->animT, 1.0f-e->blendT/ENEMY_BLEND);
            else RigPose(rg, cur, e->animT);
        }
        if (rg->gpu){ EnemyLightUpload(e->pos); EnemyGlowDim(e->state==2?0.55f:0.0f); }   // the glow gutters as he falls
        Vector3 dpos={e->pos.x, e->drawY, e->pos.z};          // smoothed height -> no stair-step jitter
        float esc=ENEMY_SCALE*(kEnemy[e->type].scale*e->sc);          // per-type size: the head chef looms
        DrawModelEx(*EnemyLodModel(rg,dpos), dpos, (Vector3){0,1,0}, yaw,   // no sink: dying body falls onto the floor, not through
                    (Vector3){esc,esc,esc}, kEnemy[e->type].tint);
        {   // a tight dark core at the feet inside a wide soft penumbra; it spreads as the body goes down
            float sk=(kEnemy[e->type].scale*e->sc)*(e->state==2?1.45f:1.0f);
            GroundBlob(dpos, 0.95f*sk, 0.42f); GroundBlob(dpos, 0.50f*sk, e->state==2?0.30f:0.50f);
        }
        if (e->burnN>0) DrawBurnMarks(dpos, 1.0f, e->burnY, e->burnR, e->burnN);   // localised char where the flame touched
        if (rg->animN>0){                                     // remember the pose shown (new wounds go on it), draw the old ones
            e->drawAn=RigClip(e->type, e->state==2?(e->deathRole?e->deathRole:CL_DEATH):e->clip); e->drawFrame=e->animT; e->drawYaw=yaw;
            g_woundPose=stored?e->pose:NULL;
            if (e->wN>0) DrawWounds(e->drawAn, e->animT, dpos, yaw, esc, e->wN, e->wBone, e->wOff, e->wSz);
            g_woundPose=NULL;
        }
        if (e->state==1 && e->hp<e->maxHp && !e->boss){       // HP bar above damaged enemies (bosses get the HUD bar)
            Vector3 hp={e->pos.x, e->pos.y+ENEMY_HEIGHT*(kEnemy[e->type].scale*e->sc)+0.3f, e->pos.z};
            DrawCube(hp, 0.6f*(e->hp/e->maxHp), 0.08f, 0.02f, (Color){230,60,60,255});
        }
    }
    // lasting corpses: each rig's model posed at its death clip's settle frame, lying where each enemy fell
    for (int ri=0; ri<EN_NTYPES; ri++){
        EnemyRig *rg=&g_rig[ri]; if (!rg->ok || rg->animN<=0) continue;
        static const int corpseRoles[5]={CL_DEATH,CL_DEATH_HEAD,CL_DEATH_LEG,CL_DEATH_BLAST,CL_DEATH_RIDDLED};
        for (int cr=0; cr<5; cr++){ int role=corpseRoles[cr];
        int any=0; for (int c=0;c<MAX_CORPSES && !any;c++) any=(g_corpses[c].active && RigOf(g_corpses[c].type)==rg && (g_corpses[c].role?g_corpses[c].role:CL_DEATH)==role);
        if (!any) continue;
        RigPose(rg, &rg->anim[rg->clip[role]], (float)rg->settle[role]);
        for (int c=0;c<MAX_CORPSES;c++) if (g_corpses[c].active && RigOf(g_corpses[c].type)==rg && (g_corpses[c].role?g_corpses[c].role:CL_DEATH)==role){
            float csc=ENEMY_SCALE*kEnemy[g_corpses[c].type].scale;
            if (rg->gpu){ EnemyLightUpload(g_corpses[c].pos); EnemyGlowDim(0.85f); }
            DrawModelEx(*EnemyLodModel(rg,g_corpses[c].pos), g_corpses[c].pos, (Vector3){0,1,0}, g_corpses[c].yaw,
                        (Vector3){csc,csc,csc}, kEnemy[g_corpses[c].type].tint);
            if (g_corpses[c].wN>0) DrawWounds(&rg->anim[rg->clip[role]], (float)rg->settle[role], g_corpses[c].pos, g_corpses[c].yaw, csc,
                                              g_corpses[c].wN, g_corpses[c].wBone, g_corpses[c].wOff, g_corpses[c].wSz);
            if (g_corpses[c].burnN>0){   // prone body: compress the mark heights so soot sits on it, not floating above
                Vector3 b={g_corpses[c].pos.x, g_corpses[c].pos.y+0.05f, g_corpses[c].pos.z};
                DrawBurnMarks(b, 0.30f, g_corpses[c].burnY, g_corpses[c].burnR, g_corpses[c].burnN);
            }
        }
        }
    }
    DrawGroundBlobs();
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
static void DrawGunSpinMesh(Model model, Vector3 pos, float scale, int spinMesh, float angle,
                            unsigned long long gunMask, Vector3 armsOff){
    Matrix mt = MatrixMultiply(model.transform, MatrixMultiply(MatrixScale(scale,scale,scale), MatrixTranslate(pos.x,pos.y,pos.z)));
    Matrix spun = MatrixMultiply(MatrixRotateZ(angle), mt);   // spin in local space (barrels run along local Z, centred on X=Y=0)
    // The skinned ARMS (meshes not in gunMask) can be slid as a rigid unit onto the
    // grips (this rig only renders sanely at its baked pose -- see gun-pin memory --
    // so the hands can't be re-posed per frame; a model-space nudge seats them).
    Matrix armsM = MatrixMultiply(MatrixTranslate(armsOff.x,armsOff.y,armsOff.z), mt);
    for (int i=0;i<model.meshCount;i++){
        int isGun = (i<64) && ((gunMask>>i)&1ULL);
        Matrix m = (i==spinMesh)?spun:(isGun?mt:armsM);
        DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], m);
    }
}

// Compose a raylib Transform (S, R quat, T) into a matrix exactly the way raylib's own
// skinning does (scale, then rotate, then translate).
static Matrix XformToMatrix(Transform t){
    return MatrixMultiply(MatrixMultiply(MatrixScale(t.scale.x,t.scale.y,t.scale.z),
                                         QuaternionToMatrix(t.rotation)),
                          MatrixTranslate(t.translation.x,t.translation.y,t.translation.z));
}
// Draw a weapon whose gun meshes are STATIC and detached from the skinned arms (the
// grenade launcher: 'allanims' moves only the arm bones; the launcher was authored
// loose at the model root). The arms pose normally. For the gun meshes we build the
// hand bone's bind->pose matrix FROM THE ANIMATION KEYFRAME DATA (the model's runtime
// boneMatrices are NOT reliably populated by this raylib build -- which is why the
// first pin attempt didn't move the gun at all) and ride the gun on it:
//   recentre gun on its own bind centre -> grip trim (rot/scale about the gun, offset
//   in hand-bind space so it turns WITH the hand) -> seat at the palm -> skin -> view.
// Default trim (zero) = the gun's centre sits exactly in the palm.
// Model-space matrix that carries the pinned gun's bind-space verts onto the posed
// hand (identity when there's no hand/keyframe data). Shared by the draw and by
// ComputeVmFrame, which needs to measure the gun exactly where it gets drawn.
static Matrix PinnedGunLocal(Model model, ModelAnimation *anim, float frameF,
                             int handBone, Vector3 gunC, Matrix align, Vector3 seat, int authored,
                             Vector3 pinOff, float pinScale, float pinYaw, float pinPitch, float pinRoll){
    Matrix local = MatrixIdentity();
    if (handBone>=0 && anim && anim->keyframePoses && anim->keyframeCount>0){
        int kf=(int)frameF; if (kf<0) kf=0; if (kf>=anim->keyframeCount) kf=anim->keyframeCount-1;
        Matrix bindM = XformToMatrix(model.skeleton.bindPose[handBone]);
        Matrix poseM = XformToMatrix(anim->keyframePoses[kf][handBone]);
        Matrix skin  = MatrixMultiply(MatrixInvert(bindM), poseM);      // bind space -> posed space (raylib's skinning product)
        Matrix trim  = MatrixMultiply(MatrixScale(pinScale,pinScale,pinScale),
                       MatrixMultiply(MatrixRotateZ(DEG2RAD*pinRoll),
                       MatrixMultiply(MatrixRotateX(DEG2RAD*pinPitch), MatrixRotateY(DEG2RAD*pinYaw))));
        Matrix pre;
        if (authored){
            // The bake already places the gun exactly where the hand animation was
            // authored to grip it -> KEEP that placement (identity at zero trim); the
            // user trim only pivots about the gun's own centre for fine nudges.
            pre = MatrixMultiply(MatrixMultiply(MatrixTranslate(-gunC.x,-gunC.y,-gunC.z), trim),
                                 MatrixTranslate(gunC.x+pinOff.x, gunC.y+pinOff.y, gunC.z+pinOff.z));
        } else {
            // recentre on the gun -> auto-align its long axis to the palm axis -> user
            // trim -> seat at the palm midpoint (+offset) -> ride the hand -> viewmodel
            pre = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-gunC.x,-gunC.y,-gunC.z), align), trim),
                                 MatrixTranslate(seat.x+pinOff.x, seat.y+pinOff.y, seat.z+pinOff.z));
        }
        local = MatrixMultiply(pre, skin);
    }
    return local;
}
static void DrawPinnedGun(Model model, ModelAnimation *anim, float frameF, Vector3 pos, float scale,
                          int handBone, Vector3 gunC, Matrix align, Vector3 seat, unsigned long long gunMask, int authored,
                          Vector3 pinOff, float pinScale, float pinYaw, float pinPitch, float pinRoll){
    Matrix mt = MatrixMultiply(model.transform, MatrixMultiply(MatrixScale(scale,scale,scale), MatrixTranslate(pos.x,pos.y,pos.z)));
    Matrix gunMat = MatrixMultiply(PinnedGunLocal(model, anim, frameF, handBone, gunC, align, seat, authored,
                                                  pinOff, pinScale, pinYaw, pinPitch, pinRoll), mt);
    for (int i=0;i<model.meshCount;i++){
        int isGun = (i<64) && ((gunMask>>i)&1ULL);                    // arms (blended skin) pose at mt; gun parts ride the hand
        DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], isGun?gunMat:mt);
    }
}

// Reload stabiliser. Some rigs' reload clips swing the whole gun hard -- the
// Remington's flips ~70deg+ onto its back to feed the loading port, and the arm
// that does it sweeps across the lens -- which reads as "the gun goes upside down".
// While reloading, measure how far the gun-body bone has moved from its rest pose
// and counter-transform the WHOLE rig (arms included, so the hands stay on the gun)
// by `reloadSteady` of that motion, pivoting on the gun's current centre. The pump,
// shells and hands still animate; the gun just stays mostly upright and in frame.
static float VmRestFrame(const Weapon *w);
static Matrix VmSteady(const Weapon *w){
    if (!g_reloading || w->reloadSteady<=0.0f || w->pinGun || w->gunBone<0) return MatrixIdentity();
    if (w->animN<=0 || g_curAnim<0 || g_curAnim>=w->animN || w->aIdle<0 || w->aIdle>=w->animN) return MatrixIdentity();
    ModelAnimation *ra=&w->anim[w->aIdle], *ca=&w->anim[g_curAnim];
    if (!ra->keyframePoses || !ca->keyframePoses || ra->keyframeCount<=0 || ca->keyframeCount<=0) return MatrixIdentity();
    int rf=(int)VmRestFrame(w); if (rf<0) rf=0; if (rf>=ra->keyframeCount) rf=ra->keyframeCount-1;
    int cf=(int)g_animT;        if (cf<0) cf=0; if (cf>=ca->keyframeCount) cf=ca->keyframeCount-1;
    Transform t0=ra->keyframePoses[rf][w->gunBone], t1=ca->keyframePoses[cf][w->gunBone];
    Quaternion back=QuaternionMultiply(t0.rotation, QuaternionInvert(t1.rotation));   // current -> rest orientation
    float k=w->reloadSteady; { const char *e=getenv("VMSTEADY"); if (e) k=(float)atof(e); }   // dev A/B
    Quaternion qk=QuaternionSlerp(QuaternionIdentity(), back, k);
    // where the rest-pose gun centre has been carried to by the clip right now
    Vector3 cNow=Vector3Add(Vector3RotateByQuaternion(Vector3Subtract(w->gunC,t0.translation),
                            QuaternionMultiply(t1.rotation,QuaternionInvert(t0.rotation))), t1.translation);
    Vector3 cTo=Vector3Lerp(cNow, w->gunC, k);
    return MatrixMultiply(MatrixMultiply(MatrixTranslate(-cNow.x,-cNow.y,-cNow.z), QuaternionToMatrix(qk)),
                          MatrixTranslate(cTo.x,cTo.y,cTo.z));
}

// The frame the weapon rests on between actions -- mirrors the hold logic in the
// update loop, so the auto frame is measured from the pose the player actually sees.
static float VmRestFrame(const Weapon *w){
    if (w->animN<=0 || w->aIdle<0 || w->aIdle>=w->animN) return 0.0f;
    if (w->idleHold>=0) return (float)w->idleHold;
    if (w->grenade)     return (float)GL_IDLE_FRAME;
    if (w->spinUp)      return (float)MG_IDLE_FRAME;
    if (w->aWalk>=0)    return 0.0f;
    int nf=ANIM_FRAMES(w->anim[w->aIdle]); return nf>0 ? (float)(nf-1) : 0.0f;
}
// Dominant eigenvector of a symmetric 3x3 (power iteration), seeded off-axis.
static Vector3 PowerIter(double C[3][3], double *lambda){
    double a=0.57,b=0.61,c=0.55;
    for (int it=0;it<64;it++){
        double x=C[0][0]*a+C[0][1]*b+C[0][2]*c, y=C[1][0]*a+C[1][1]*b+C[1][2]*c, z=C[2][0]*a+C[2][1]*b+C[2][2]*c;
        double L=sqrt(x*x+y*y+z*z); if (L<1e-18) break; a=x/L; b=y/L; c=z/L;
    }
    double x=C[0][0]*a+C[0][1]*b+C[0][2]*c, y=C[1][0]*a+C[1][1]*b+C[1][2]*c, z=C[2][0]*a+C[2][1]*b+C[2][2]*c;
    if (lambda) *lambda=a*x+b*y+c*z;
    return (Vector3){(float)a,(float)b,(float)c};
}
// Is bone b part of the ARMS (vs the gun)? By name, not ancestry: rigs hang gun
// bones off the wrist (flamethrower) and call the gun "Body" (remington), so the
// test is: an arm/hand/finger word in the name, or a non-root bone whose subtree
// holds one (benelli's torso 'body_01', 'pose_controller').
static int BoneNameIsArm(const char *bn){
    char low[64]; int j=0; for(;bn[j]&&j<63;j++){char c=bn[j]; low[j]=(c>='A'&&c<='Z')?c+32:c;} low[j]=0;
    static const char *kw[]={"arm","elbow","wrist","hand","thumb","point","index","middle","ring","pink","finger",
                             "knuckle","metacarpal","shoulder","clavicle","twist","chest","palm","bone_l","bone_r",
                             "ik_","pole","cam","head",NULL};
    for (int i=0;kw[i];i++) if (strstr(low,kw[i])) return 1;
    return 0;
}
static void ClassifyArmBones(Model *m, unsigned char *isArm){
    int nb=m->skeleton.boneCount;
    for (int b=0;b<nb;b++) isArm[b]=(unsigned char)BoneNameIsArm(m->skeleton.bones[b].name);
    for (int b=0;b<nb;b++){                       // propagate "has an arm below me" up to non-root ancestors
        if (!isArm[b]) continue;
        for (int p=m->skeleton.bones[b].parent; p>=0; p=m->skeleton.bones[p].parent){
            char low[64]; const char *bn=m->skeleton.bones[p].name; int j=0;
            for(;bn[j]&&j<63;j++){char c=bn[j]; low[j]=(c>='A'&&c<='Z')?c+32:c;} low[j]=0;
            if (strstr(low,"root") || strstr(low,"object")) break;
            isArm[p]=1;
        }
    }
}
// Vertex v of mesh ms: arm (1) or gun (0)? Dominant skin bone decides; unskinned = gun.
static int VertIsArm(const Mesh *ms, int v, const unsigned char *isArm, int nb){
    if (!ms->boneIndices || !ms->boneWeights) return 0;
    int best=-1; float bw=0.0f;
    for (int c=0;c<4;c++){ float wt=ms->boneWeights[v*4+c]; if (wt>bw){ bw=wt; best=(int)ms->boneIndices[v*4+c]; } }
    return (best>=0 && best<nb && bw>0.0f) ? isArm[best] : 0;
}
static int VertDomBone(const Mesh *ms, int v){
    if (!ms->boneIndices || !ms->boneWeights) return -1;
    int best=-1; float bw=0.0f;
    for (int c=0;c<4;c++){ float wt=ms->boneWeights[v*4+c]; if (wt>bw){ bw=wt; best=(int)ms->boneIndices[v*4+c]; } }
    return bw>0.0f ? best : -1;
}
// Measure the weapon at its rest pose and build vmAlign: the model->canonical
// matrix that puts the GUN (not the arms) centred at the origin, barrel along -Z,
// top of the gun +Y, length 1. Nothing here is hand-tuned per model:
//   barrel axis  = principal axis of the gun-part vertices
//   gun verts    = verts whose dominant bone isn't arm-named, main gun bone subtree only
//   muzzle end   = the THIN end (barrel vs stock/receiver); arms decide only if the ends match
//   up           = the gun's 2nd axis (mag/grip/stock make guns taller than wide),
//                  signed so the side that sticks out further (mag, grip) is DOWN;
//                  round sections (minigun) use the authored +Y.
static void ComputeVmFrame(Weapon *w, int slot){
    if (!w->has) return;
    float fr=VmRestFrame(w);
    ModelAnimation *an=(w->animN>0 && w->aIdle>=0 && w->aIdle<w->animN)?&w->anim[w->aIdle]:NULL;
    if (an) ANIM_APPLY(w->model, *an, fr);
    Matrix pinL = MatrixIdentity();
    if (w->pinGun) pinL=PinnedGunLocal(w->model, an, fr, w->handBone, w->gunBindC, w->pinAlign, w->pinSeat, w->pinAuthored,
                                       w->pinOff, w->pinScale, w->pinYaw, w->pinPitch, w->pinRoll);
    // pass 0: means of gun + arm verts; pass 1: gun covariance
    double gs[3]={0,0,0}, as[3]={0,0,0}; long gn=0, anum=0;
    double C[3][3]={{0}};
    // Gun-vs-arm split: the pinned launcher's gun is whole static meshes (gunMask);
    // every other rig is split PER VERTEX by its dominant bone's name.
    int nb=w->model.skeleton.boneCount; if (nb>256) nb=256;
    unsigned char isArm[256]={0}; ClassifyArmBones(&w->model,isArm);
    // Keep only the MAIN gun body: the gun bone carrying the most verts, plus its
    // subtree (bolt, pump, trigger). Reload props on their own root-level bones --
    // the AK's spare mag, the Remington's loose shell -- sit far from the gun at
    // rest and would skew the axes. Unskinned verts form their own bucket (-1).
    unsigned char keep[257]; memset(keep,1,sizeof keep);   // index b+1; [0] = unskinned
    if (!w->pinGun && nb>0){
        long cnt[257]={0};
        for (int k=0;k<w->model.meshCount;k++){ Mesh *ms=&w->model.meshes[k]; if (ms->vertexCount<=0) continue;
            for (int v=0;v<ms->vertexCount;v++){ if (VertIsArm(ms,v,isArm,nb)) continue;
                int b=VertDomBone(ms,v); if (b>=nb) b=-1; cnt[b+1]++; } }
        int mb=-1; long mc=-1; for (int b=-1;b<nb;b++) if (cnt[b+1]>mc){ mc=cnt[b+1]; mb=b; }
        w->gunBone=mb;
        if (mb>=0){
            memset(keep,0,sizeof keep);
            for (int b=0;b<nb;b++){ int in=0; for (int q=b;q>=0;q=w->model.skeleton.bones[q].parent) if (q==mb){ in=1; break; } keep[b+1]=(unsigned char)in; }
        } else { memset(keep,0,sizeof keep); keep[0]=1; }
    }
    for (int pass=0;pass<2;pass++){
        double gm[3]={ gn?gs[0]/gn:0, gn?gs[1]/gn:0, gn?gs[2]/gn:0 };
        for (int k=0;k<w->model.meshCount;k++){
            Mesh *ms=&w->model.meshes[k]; if (ms->vertexCount<=0) continue;
            int meshGun = (k<64 && ((w->gunMask>>k)&1ULL));
            int pinned = w->pinGun && meshGun;
            float *vv = (!pinned && ms->animVertices) ? ms->animVertices : ms->vertices; if (!vv) continue;
            for (int v=0;v<ms->vertexCount;v++){
                int isGun, armV=0;
                if (w->pinGun) isGun=meshGun;
                else { armV=VertIsArm(ms,v,isArm,nb); int b=VertDomBone(ms,v); if (b>=nb) b=-1; isGun = !armV && keep[b+1]; }
                if (!w->pinGun && !isGun && !armV) continue;        // gun prop outside the main body: ignore entirely
                Vector3 p={vv[v*3],vv[v*3+1],vv[v*3+2]};
                if (pinned) p=Vector3Transform(p,pinL);
                if (pass==0){
                    if (isGun){ gs[0]+=p.x; gs[1]+=p.y; gs[2]+=p.z; gn++; }
                    else      { as[0]+=p.x; as[1]+=p.y; as[2]+=p.z; anum++; }
                } else if (isGun){
                    double d[3]={p.x-gm[0],p.y-gm[1],p.z-gm[2]};
                    for (int i=0;i<3;i++) for (int j=0;j<3;j++) C[i][j]+=d[i]*d[j];
                }
            }
        }
    }
    if (gn<8){ DebugLog("vmframe","\"slot\":%d,\"error\":\"no gun verts\"",slot); return; }
    Vector3 gc={(float)(gs[0]/gn),(float)(gs[1]/gn),(float)(gs[2]/gn)};
    w->gunC=gc;
    double l1,l2,l3;
    Vector3 u=PowerIter(C,&l1);
    double D[3][3]; double uu[3]={u.x,u.y,u.z};
    for (int i=0;i<3;i++) for (int j=0;j<3;j++) D[i][j]=C[i][j]-l1*uu[i]*uu[j];
    Vector3 v2=PowerIter(D,&l2);
    v2=Vector3Normalize(Vector3Subtract(v2,Vector3Scale(u,Vector3DotProduct(v2,u))));
    l3=(C[0][0]+C[1][1]+C[2][2])-l1-l2;
    // muzzle end. Primary cue = SHAPE: the muzzle end is thin (barrel), the rear is
    // thick (stock, receiver, grip). Measure the cross-section spread of the end
    // 18%% slices along u. Fallback cue (ends similar): the end away from the arms.
    Vector3 ac = anum>0 ? (Vector3){(float)(as[0]/anum),(float)(as[1]/anum),(float)(as[2]/anum)} : gc;
    Vector3 toArms=Vector3Subtract(ac,gc);
    float ulo=1e30f, uhi=-1e30f;
    double endS[2]={0,0}; long endN[2]={0,0};
    for (int pass=0;pass<2;pass++)
    for (int k=0;k<w->model.meshCount;k++){
        Mesh *ms=&w->model.meshes[k]; if (ms->vertexCount<=0) continue;
        int meshGun = (k<64 && ((w->gunMask>>k)&1ULL));
        if (w->pinGun && !meshGun) continue;
        float *vv = (!w->pinGun && ms->animVertices) ? ms->animVertices : ms->vertices; if (!vv) continue;
        for (int v=0;v<ms->vertexCount;v++){
            if (!w->pinGun){ if (VertIsArm(ms,v,isArm,nb)) continue; int b=VertDomBone(ms,v); if (b>=nb) b=-1; if (!keep[b+1]) continue; }
            Vector3 p={vv[v*3],vv[v*3+1],vv[v*3+2]}; if (w->pinGun) p=Vector3Transform(p,pinL);
            Vector3 d=Vector3Subtract(p,gc); float q=Vector3DotProduct(d,u);
            if (pass==0){ if (q<ulo) ulo=q; if (q>uhi) uhi=q; continue; }
            float span=uhi-ulo; int e = (q>uhi-0.18f*span) ? 1 : ((q<ulo+0.18f*span) ? 0 : -1);
            if (e<0) continue;
            Vector3 perp=Vector3Subtract(d,Vector3Scale(u,q));
            endS[e]+=Vector3LengthSqr(perp); endN[e]++;
        }
    }
    double rmsLo = endN[0] ? sqrt(endS[0]/endN[0]) : 0, rmsHi = endN[1] ? sqrt(endS[1]/endN[1]) : 0;
    double thin = (fmin(rmsLo,rmsHi)>1e-12) ? fmax(rmsLo,rmsHi)/fmin(rmsLo,rmsHi) : 1.0;
    int byThin = thin>1.25;
    Vector3 f;
    if (byThin) f = (rmsHi<rmsLo) ? u : Vector3Negate(u);              // thin end = muzzle
    else        f = (anum>0 && Vector3DotProduct(toArms,u)>0.0f) ? Vector3Negate(u) : u;
    int armsSay = (anum>0 && Vector3DotProduct(toArms,u)>0.0f) ? -1 : 1, thinSay = (rmsHi<rmsLo) ? 1 : -1;
    // up = the gun's 2nd axis, signed by SHAPE: mags, grips and triggers hang below
    // the bore, so the side of v2 that sticks out FURTHER from the mean is down.
    // (Arms are a poor cue -- shoulders sit at eye height, ABOVE the gun -- so they
    // only decide when the section is round or the shape is near-symmetric.)
    float vlo=0.0f, vhi=0.0f;
    for (int k=0;k<w->model.meshCount;k++){
        Mesh *ms=&w->model.meshes[k]; if (ms->vertexCount<=0) continue;
        int meshGun = (k<64 && ((w->gunMask>>k)&1ULL));
        if (w->pinGun && !meshGun) continue;
        float *vv = (!w->pinGun && ms->animVertices) ? ms->animVertices : ms->vertices; if (!vv) continue;
        for (int v=0;v<ms->vertexCount;v++){
            if (!w->pinGun){ if (VertIsArm(ms,v,isArm,nb)) continue; int b=VertDomBone(ms,v); if (b>=nb) b=-1; if (!keep[b+1]) continue; }
            Vector3 p={vv[v*3],vv[v*3+1],vv[v*3+2]}; if (w->pinGun) p=Vector3Transform(p,pinL);
            float q=Vector3DotProduct(Vector3Subtract(p,gc),v2); if (q<vlo) vlo=q; if (q>vhi) vhi=q;
        }
    }
    Vector3 up;
    int roundSection = (l3>1e-12 && l2/l3<1.6);
    float asym = (fminf(-vlo,vhi)>1e-9f) ? fmaxf(-vlo,vhi)/fminf(-vlo,vhi) : 99.0f;
    Vector3 below=Vector3Subtract(toArms,Vector3Scale(f,Vector3DotProduct(toArms,f)));
    int byShape = !roundSection && asym>1.15f;
    if (byShape) up = (-vlo > vhi) ? v2 : Vector3Negate(v2);   // lo side sticks out further -> lo is down
    else if (roundSection && fabsf(f.y)<0.9f){                  // round barrel cluster (minigun): shape has no "down", and
        up=Vector3Normalize(Vector3Subtract((Vector3){0,1,0},Vector3Scale(f,f.y)));   // arms are no cue -> trust the authored +Y
    }
    else if (anum>0 && Vector3Length(below)>1e-6f){
        Vector3 bdir=Vector3Normalize(below);
        up = roundSection ? Vector3Negate(bdir) : ((Vector3DotProduct(v2,bdir)>0.0f) ? Vector3Negate(v2) : v2);
    } else up=v2;
    Vector3 r=Vector3Normalize(Vector3CrossProduct(f,up));     // right = forward x up
    up=Vector3CrossProduct(r,f);
    // extents in the gun frame -> true bbox centre + length
    float lo[3]={1e30f,1e30f,1e30f}, hi[3]={-1e30f,-1e30f,-1e30f};
    for (int k=0;k<w->model.meshCount;k++){
        Mesh *ms=&w->model.meshes[k]; if (ms->vertexCount<=0) continue;
        int meshGun = (k<64 && ((w->gunMask>>k)&1ULL));
        if (w->pinGun && !meshGun) continue;
        int pinned = w->pinGun;
        float *vv = (!pinned && ms->animVertices) ? ms->animVertices : ms->vertices; if (!vv) continue;
        for (int v=0;v<ms->vertexCount;v++){
            if (!w->pinGun){ if (VertIsArm(ms,v,isArm,nb)) continue; int b=VertDomBone(ms,v); if (b>=nb) b=-1; if (!keep[b+1]) continue; }
            Vector3 p={vv[v*3],vv[v*3+1],vv[v*3+2]}; if (pinned) p=Vector3Transform(p,pinL);
            Vector3 d=Vector3Subtract(p,gc);
            float q[3]={Vector3DotProduct(d,r),Vector3DotProduct(d,up),-Vector3DotProduct(d,f)};
            for (int i=0;i<3;i++){ if(q[i]<lo[i])lo[i]=q[i]; if(q[i]>hi[i])hi[i]=q[i]; }
        }
    }
    float len=fmaxf(hi[2]-lo[2],1e-6f);
    Vector3 cq={(lo[0]+hi[0])*0.5f,(lo[1]+hi[1])*0.5f,(lo[2]+hi[2])*0.5f};
    // rotation rows = r, up, -f  (v -> (r.v, up.v, -f.v))
    Matrix R={ r.x, r.y, r.z, 0,
               up.x,up.y,up.z,0,
              -f.x,-f.y,-f.z, 0,
               0,  0,  0,   1 };
    w->vmAlign = MatrixMultiply(MatrixMultiply(MatrixTranslate(-gc.x,-gc.y,-gc.z), R),
                 MatrixMultiply(MatrixTranslate(-cq.x,-cq.y,-cq.z), MatrixScale(1.0f/len,1.0f/len,1.0f/len)));
    w->gunLen=len; w->vmBase=kVmClass[w->vmClass].len/len;
    DebugLog("vmframe","\"slot\":%d,\"label\":\"%s\",\"frame\":%.0f,\"gunVerts\":%ld,\"armVerts\":%ld,\"len\":%.4f,\"eig\":[%.3g,%.3g,%.3g],\"round\":%d,\"asym\":%.2f,\"byShape\":%d,\"thin\":%.2f,\"thinVsArms\":\"%s\",\"fwd\":[%.2f,%.2f,%.2f],\"up\":[%.2f,%.2f,%.2f]",
             slot,w->label,fr,gn,anum,len,l1,l2,l3,roundSection,asym,byShape,thin,(armsSay==thinSay)?"agree":"DISAGREE",f.x,f.y,f.z,up.x,up.y,up.z);
}


static void DrawViewmodel(void){
    // In orient mode the live first-person viewmodel must always show so tuning
    // is visible; inspect (which ignores the transform) is suppressed there.
    if (!g_hasGun || (g_inspect && !g_noEnemies)) return;
    PoseGun();
    Weapon *w=&g_weapons[g_curWeapon];
    Matrix M=MatrixMultiply(VmSteady(w), MatrixMultiply(w->vmAlign, VmPlace(w)));
    rlDrawRenderBatchActive();
#if !defined(_WIN32)
    glClear(GL_DEPTH_BUFFER_BIT);   // weapon always on top
#endif
    BeginMode3D(g_vmCam);
        g_gun.transform=M;            // the whole rig transform; draws below pass pos 0 / scale 1
        Vector3 o={0,0,0};
        if (w->spinUp)                // minigun: spin the barrel mesh, hands held static (+ B-tuned arms nudge)
            DrawGunSpinMesh(g_gun, o, 1.0f, MG_BARREL_MESH, g_mgBarrelAngle, w->gunMask, w->pinOff);
        else if (w->pinGun){          // grenade launcher: ride the static gun on the animated hand
            ModelAnimation *an=(g_gunAnimN>0 && g_curAnim<g_gunAnimN)?&g_gunAnim[g_curAnim]:NULL;
            DrawPinnedGun(g_gun, an, g_animT, o, 1.0f, w->handBone, w->gunBindC, w->pinAlign, w->pinSeat, w->gunMask, w->pinAuthored,
                          w->pinOff, w->pinScale, w->pinYaw, w->pinPitch, w->pinRoll);
        } else                        // everything else: per-mesh draw with the arms nudge
            DrawGunSpinMesh(g_gun, o, 1.0f, -1, 0.0f, w->gunMask, w->pinOff);
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
    // boss bar (../chernobyl interlude): wide, top-centre, names the boss
    for (int i=0;i<MAX_ENEMIES;i++){ Enemy *e=&g_enemies[i];
        if (!e->boss || e->state!=1) continue;
        int bw=W/2, bx=W/4, by=46; float f=fmaxf(0.0f,e->hp/e->maxHp);
        DrawRectangle(bx-3,by-3,bw+6,24,(Color){0,0,0,180});
        DrawRectangle(bx,by,(int)(bw*f),18,(Color){200,30,30,255});
        DrawRectangleLines(bx-3,by-3,bw+6,24,(Color){255,200,80,220});
        const char *t=BossName(e->type); int fs=22, tw=MeasureText(t,fs);
        DrawText(t,W/2-tw/2+2,by+26,fs,(Color){0,0,0,200}); DrawText(t,W/2-tw/2,by+24,fs,(Color){255,200,80,255});
        break;
    }
    // power-up timers + pickup message
    { int y=H-110;
      if (g_quadT>0){ DrawText(TextFormat("QUAD  %d",(int)g_quadT+1),W-170,y,24,(Color){255,90,255,255}); y-=28; }
      if (g_speedT>0){ DrawText(TextFormat("SPEED %d",(int)g_speedT+1),W-170,y,24,(Color){90,240,255,255}); }
      if (g_pkMsgT>0){ int fs=28, tw=MeasureText(g_pkMsg,fs); unsigned char a=(unsigned char)(255*fminf(1.0f,g_pkMsgT/0.5f));
          DrawText(g_pkMsg,W/2-tw/2+2,H/2+92,fs,(Color){0,0,0,a}); DrawText(g_pkMsg,W/2-tw/2,H/2+90,fs,(Color){140,255,140,a}); } }
    DrawFPS(W-90,10);
    // kills + headshots + player health
    DrawText(TextFormat("KILLS %d", g_kills), W-160, 36, 22, (Color){255,230,120,255});
    DrawText(TextFormat("HEADSHOTS %d", g_headshots), W-160, 60, 18, (Color){255,140,50,255});
    if (g_spawnMsg>0){                                 // F2 confirmation
        const char *t="SPAWN SET"; int fs=26, tw=MeasureText(t,fs);
        DrawText(t, W/2-tw/2, H/2+70, fs, (Color){120,220,255,255});
    }
    if (g_hitMark>0 || g_killMark>0){                       // hit marker: four ticks round the crosshair, red and wider on a kill
        int kill=g_killMark>0; float k=kill?fminf(1.0f,g_killMark/0.16f):fminf(1.0f,g_hitMark/0.07f);
        float r0=kill?9.0f+(1.0f-k)*6.0f:7.0f, r1=r0+(kill?11.0f:8.0f);
        Color c=kill?(Color){255,60,40,(unsigned char)(255*k)}:(Color){255,255,255,(unsigned char)(230*k)};
        for (int q=0;q<4;q++){ float sx=(q&1)?1.0f:-1.0f, sy=(q&2)?1.0f:-1.0f;
            DrawLineEx((Vector2){W/2+sx*r0,H/2+sy*r0},(Vector2){W/2+sx*r1,H/2+sy*r1},kill?3.0f:2.0f,c); }
    }
    // "HEADSHOT!" punch near the crosshair: full for ~0.65s, then fades over 0.45s
    if (g_hsFlash>0){
        const char *t="HEADSHOT!"; int fs=36, tw=MeasureText(t,fs), x=W/2-tw/2, y=H/2-74;
        float k=fminf(1.0f, g_hsFlash/0.45f);
        DrawText(t, x+2, y+2, fs, (Color){0,0,0,(unsigned char)(180*k)});
        DrawText(t, x,   y,   fs, (Color){255,70,40,(unsigned char)(255*k)});
    }
    DrawRectangle(20, H-44, 224, 24, (Color){0,0,0,150});
    DrawRectangle(22, H-42, (int)(220*fminf(g_playerHp,100.0f)/100.0f), 20, (Color){200,40,40,255});
    if (g_playerHp>100.0f) DrawRectangle(22, H-42, (int)(220*(g_playerHp-100.0f)/100.0f), 20, (Color){60,120,255,230});   // mega-health overcharge
    DrawText(TextFormat("HP %d", (int)g_playerHp), 28, H-40, 16, RAYWHITE);
    if (g_godMode) DrawText("GOD", 112, H-40, 16, (Color){120,220,255,255});   // invulnerable (cheat)
    // ammo (bottom-right): mag / reserve for the active weapon
    { Weapon *hw=&g_weapons[g_curWeapon];
      if (hw->magSize>0){
        const char *at=TextFormat("%d / %d", hw->mag, hw->reserve);
        int fs=30, aw=MeasureText(at,fs);
        DrawText(at, W-28-aw, H-50, fs, (hw->mag==0)?(Color){255,90,60,255}:RAYWHITE);
        DrawText(hw->label, W-28-MeasureText(hw->label,16), H-72, 16, (Color){205,205,215,210});
        if (hw->shellCycle>0){                               // shotgun: one shell per round in the tube, empties dim
            int sw=10, sh=26, gap=6, x0=W-28-hw->magSize*(sw+gap)+gap, y0=H-104;
            for (int k=0;k<hw->magSize;k++){
                int x=x0+k*(sw+gap), full=(k<hw->mag);
                Color hull=full?(Color){200,40,35,255}:(Color){60,30,30,140}, brass=full?(Color){214,170,70,255}:(Color){70,60,40,140};
                DrawRectangle(x+1, y0+2, sw, sh, (Color){0,0,0,120});                  // drop shadow
                DrawRectangle(x, y0, sw, sh-8, hull);
                DrawRectangle(x, y0+sh-8, sw, 8, brass);
                DrawRectangleLines(x, y0, sw, sh, full?(Color){20,10,10,200}:(Color){120,120,120,120});
            }
        }
        if (g_reloading){ const char *rt="RELOADING"; DrawText(rt, W-28-MeasureText(rt,16), H-18, 16, (Color){255,210,90,255}); }
        else if (hw->mag<=0 && hw->reserve<=0){ const char *rt="OUT OF AMMO"; DrawText(rt, W-28-MeasureText(rt,16), H-18, 16, (Color){255,90,60,255}); }
      }
    }
    // wave banner (top centre): current wave + field, breather countdown, or the
    // opening GET READY beat.
    if (g_hasEnemy && !g_noEnemies && !g_stress && !g_dead && (g_wave>0 || g_waveBreather>0.0f)){
        char wb[64];
        if (g_wave==0)                snprintf(wb,sizeof wb,"GET READY");
        else if (g_waveBreather>0.0f) snprintf(wb,sizeof wb,"WAVE %d CLEARED  -  next in %d", g_wave, (int)g_waveBreather+1);
        else {
            int alive=0; for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state==1) alive++;
            snprintf(wb,sizeof wb, g_waveBudget>0 ? "WAVE %d  -  %d hostiles +" : "WAVE %d  -  %d hostiles", g_wave, alive);
        }
        int wfs=24, ww=MeasureText(wb,wfs);
        DrawText(wb, W/2-ww/2+2, 14+2, wfs, (Color){0,0,0,180});
        DrawText(wb, W/2-ww/2,   14,   wfs, (Color){255,210,60,255});
    }
    if (g_dead){                                                   // run results: death ends the run
        const char *t="YOU DIED"; int fs=72, tw=MeasureText(t,fs);
        DrawText(t, W/2-tw/2+3, H/2-98+3, fs, (Color){0,0,0,200});
        DrawText(t, W/2-tw/2,   H/2-98,   fs, (Color){220,40,40,255});
        int score = g_wave*100 + g_kills*10 + g_headshots*15;
        int acc   = g_shotsFired>0 ? (int)(100.0f*g_shotsHit/g_shotsFired+0.5f) : 0;
        char l1[96], l2[96];
        snprintf(l1,sizeof l1,"WAVE %d      SCORE %d%s", g_wave, score, g_cheatUsed?"  (cheats used)":"");
        snprintf(l2,sizeof l2,"%d kills   %d headshots   %d%% accuracy   %d:%02d",
                 g_kills, g_headshots, acc, (int)(g_runT/60.0f), (int)g_runT%60);
        int w1=MeasureText(l1,30); DrawText(l1, W/2-w1/2, H/2-10, 30, (Color){255,210,60,255});
        int w2=MeasureText(l2,22); DrawText(l2, W/2-w2/2, H/2+30, 22, (Color){235,210,150,255});
        if (g_deadT>1.0f){                                         // prompt fades in after the beat
            int again=(g_lb.state==LB_OFF || g_lb.state==LB_DONE || g_lb.state==LB_FAILED);
            const char *p=g_hasMap?"ENTER or click  -  play again":"ENTER or click to respawn";
            int pw=MeasureText(p,22);
            if (again) DrawText(p, W/2-pw/2, H/2+72, 22, (Color){200,200,210,255});
            LbDraw(W,H);
        }
    }
    // Big unmistakable mode banner so "floating" can be diagnosed: INSPECT mode
    // intentionally floats the gun in front of you; press V to get back to FP.
    if (g_inspect)
        DrawText("INSPECT MODE - press V for first-person", W/2-220, 30, 24, (Color){255,180,40,255});
    if (g_devOverlay){
        DrawRectangle(0,0,380,90,(Color){0,0,0,150});
        DrawText("CHERNOBYL 2  (LMB fire, R reload)",6,6,12,GRAY);
        DrawText(TextFormat("%s  [%s]  1-4=weapon N=mode V=inspect G=god 0=reset", g_inspect?"INSPECT":"FP", g_weapons[g_curWeapon].label),8,22,16,LIME);
        if (g_noEnemies){   // orient mode panel - bigger + drop-shadowed for legibility
            DrawRectangle(6,96,600,430,(Color){0,0,0,215});
            DrawRectangleLines(6,96,600,430,(Color){255,210,60,255});
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
            TextSh(TextFormat("Z / X      pose-hold frame: %d  (SHIFT=x10)", g_weapons[g_curWeapon].idleHold), 18,410,19,(Color){255,200,120,255});
            {   // hands manager: B cycles what the tuning keys drive, on every weapon
                Weapon *w=&g_weapons[g_curWeapon];
                const char *what = (g_tuneTarget==2) ? "RIGHT HAND" : (g_tuneTarget==1 ? (w->pinGun?"GUN GRIP":"LEFT HAND") : "VIEWMODEL");
                TextSh(TextFormat("B          tuning: %s   %s", what, g_tuneTarget?"(IJKL/UO move)":""), 18,432,19,(Color){120,230,255,255});
                if (g_tuneTarget==1 && w->pinGun) TextSh(TextFormat("   off %.2f %.2f %.2f  scale %.2f  yaw %.0f pit %.0f rol %.0f", w->pinOff.x,w->pinOff.y,w->pinOff.z,w->pinScale,w->pinYaw,w->pinPitch,w->pinRoll), 18,452,18,(Color){200,220,255,255});
                if (g_tuneTarget==1 && !w->pinGun) TextSh(TextFormat("   left-hand off %.3f %.3f %.3f", w->armsLOff.x,w->armsLOff.y,w->armsLOff.z), 18,452,18,(Color){200,220,255,255});
                if (g_tuneTarget==2) TextSh(TextFormat("   right-hand off %.3f %.3f %.3f", w->armsROff.x,w->armsROff.y,w->armsROff.z), 18,452,18,(Color){200,220,255,255});
            }
            TextSh("0 reset      ENTER save  (or F5)",18,474,19,(Color){170,255,170,255});
            if (g_savedMsg>0) TextSh(TextFormat("SAVED  ->  %s",g_weapons[g_curWeapon].trimPath),18,498,20,(Color){90,255,90,255});
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
// The ESC menu, in the game's own colours: the title screen's cream-on-black with a red rule
// and red accents (as the intro's CONTROLS / WEAPONS panels), a hazard stripe across the top of
// the panel, flat dark rows with a red bar that slides in under the pointer.
static void DrawMenu(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    const Color cream={238,234,222,255}, red={214,40,28,255}, dim={150,146,138,255};
    DrawRectangle(0,0,W,H,(Color){4,3,3,222});                                   // the world (or the death screen) drops right back
    DrawRectangleGradientV(0,0,W,H/3,(Color){60,6,4,70},(Color){0,0,0,0});
    Rectangle first=MenuRect(0), last=MenuRect(MENU_N-1);
    float px=first.x-34, pw=first.width+68, py=first.y-132, ph=(last.y+last.height)-py+62;
    DrawRectangle((int)px,(int)py,(int)pw,(int)ph,(Color){12,11,11,238});
    DrawRectangleLinesEx((Rectangle){px,py,pw,ph},1,(Color){70,26,22,255});
    BeginScissorMode((int)px,(int)py,(int)pw,8);                                 // hazard stripe: black and yellow, slanted
    DrawRectangle((int)px,(int)py,(int)pw,8,(Color){226,178,36,255});
    for (float x=px-16;x<px+pw+16;x+=22) DrawTriangle((Vector2){x+11,py},(Vector2){x,py+8},(Vector2){x+11,py+8},(Color){14,12,10,255}),
                                         DrawTriangle((Vector2){x+11,py},(Vector2){x+11,py+8},(Vector2){x+22,py},(Color){14,12,10,255});
    EndScissorMode();
    DrawText("CHERNOBYL 2  //  LIQUIDATOR", (int)first.x, (int)py+30, 16, red);
    DrawText("OPTIONS", (int)first.x, (int)py+52, 44, cream);
    DrawRectangle((int)first.x,(int)py+104,(int)first.width,2,red);
    Vector2 mp=GetMousePosition();
    { int hv=-1; for (int i=0;i<MENU_N;i++) if (CheckCollisionPointRec(mp,MenuRect(i))) hv=i;      // rolled onto a new row: the tick
      if (hv>=0 && hv!=g_menuHover && g_audio && g_hasMenuSnd){ SetSoundVolume(g_menuSnd,0.5f); PlaySound(g_menuSnd); }
      g_menuHover=hv; }
    const char *labels[MENU_N]={"FULLSCREEN","RESTART RUN","RESUME","QUIT"};
    for (int i=0;i<MENU_N;i++){
        Rectangle r=MenuRect(i); int hov=CheckCollisionPointRec(mp,r);
        DrawRectangleRec(r, hov?(Color){52,14,11,245}:(Color){22,20,20,235});
        DrawRectangle((int)r.x,(int)r.y,hov?8:3,(int)r.height, hov?red:(Color){110,34,28,255});
        DrawRectangle((int)r.x,(int)(r.y+r.height-1),(int)r.width,1,(Color){52,46,44,255});
        int fs=26;
        DrawText(labels[i], (int)r.x+30, (int)(r.y+r.height/2-fs/2), fs, hov?(Color){255,250,240,255}:cream);
        if (i==0){ const char *v=g_fullscreen?"ON":"OFF"; int vw=MeasureText(v,fs);              // the value sits at the right, as a setting
            DrawText(v, (int)(r.x+r.width-24-vw), (int)(r.y+r.height/2-fs/2), fs, g_fullscreen?(Color){255,210,60,255}:dim); }
        else if (hov){ const char *v=">"; DrawText(v, (int)(r.x+r.width-24-MeasureText(v,fs)), (int)(r.y+r.height/2-fs/2), fs, red); }
    }
    DrawText("ESC", (int)first.x, (int)(last.y+last.height+22), 16, red);
    DrawText("close", (int)first.x+44, (int)(last.y+last.height+22), 16, dim);
    { const char *v=TextFormat("WAVE %d   KILLS %d", g_wave, g_kills); DrawText(v, (int)(first.x+first.width-MeasureText(v,16)), (int)(last.y+last.height+22), 16, dim); }
}

// Minimal "PAUSED" overlay (P-pause). Unlike the options menu this keeps the
// cursor captured, so it's just a dimmer + centred label over the frozen world.
static void DrawPaused(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    DrawRectangle(0,0,W,H,(Color){0,0,0,120});
    const char *t="PAUSED"; int ts=64, tw=MeasureText(t,ts);
    TextSh(t, W/2-tw/2, H/2-ts/2, ts, (Color){255,210,60,255});
    const char *h="press P, ESC, or click to resume"; int hs=22, hw=MeasureText(h,hs);
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

// Red screen feedback: a punch on taking damage (g_hurt) plus a steady glow when
// health is low, drawn as red edge bands. On death the whole screen washes dark red.
static void DrawDamageOverlay(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    float lowhp = (!g_godMode && !g_dead && g_playerHp>0 && g_playerHp<35.0f) ? (1.0f-g_playerHp/35.0f) : 0.0f;
    float k = fmaxf(g_hurt, lowhp*0.45f);
    if (k>0.001f){
        unsigned char a=(unsigned char)(fminf(1.0f,k)*150.0f);
        Color edge={150,0,0,a}, clear={150,0,0,0};
        int bw=W/4, bh=H/4;
        DrawRectangleGradientH(0,0,bw,H, edge, clear);          // left
        DrawRectangleGradientH(W-bw,0,bw,H, clear, edge);       // right
        DrawRectangleGradientV(0,0,W,bh, edge, clear);          // top
        DrawRectangleGradientV(0,H-bh,W,bh, clear, edge);       // bottom
    }
    if (g_dead) DrawRectangle(0,0,W,H,(Color){45,0,0,(unsigned char)(fminf(1.0f,g_deadT*1.2f)*170.0f)});
}

// Frame-time samples for the perf gate: collected under --debug (skipping the
// 30-frame warmup) and logged as one "perf" JSONL event at shutdown. 4096
// samples ~= 34 s at 120 fps -- plenty for a soak window.
static float g_perfDt[4096]; static int g_perfN=0;
static int CmpFloat(const void *a, const void *b){ float x=*(const float*)a, y=*(const float*)b; return (x>y)-(x<y); }
static void LogPerfStats(void){
    if (g_perfN<8) return;
    qsort(g_perfDt, g_perfN, sizeof(float), CmpFloat);
    double sum=0; for (int i=0;i<g_perfN;i++) sum+=g_perfDt[i];
    DebugLog("perf","\"frames\":%d,\"avg_ms\":%.2f,\"p95_ms\":%.2f,\"max_ms\":%.2f",
             g_perfN, 1000.0*sum/g_perfN, 1000.0f*g_perfDt[(int)(g_perfN*0.95f)], 1000.0f*g_perfDt[g_perfN-1]);
}

// ---- Post-processing ---------------------------------------------------------
// The 3-D scene + viewmodel render into a half-float target (emission and fire run past 1.0),
// then: bright pass -> quarter-res separable blur = bloom; the composite adds it and applies a
// highlight roll-off, a touch of contrast, chromatic fringing that swells when hurt,
// desaturation at low health, a vignette and film grain. The HUD draws after, untouched.
// --no-post (or a failed target) draws straight to the screen as before.
static int g_postOff=0;
static struct { int w,h,ok,hdr; RenderTexture2D scene,a,b; Shader bright,blur,comp; int lDir,lBloom,lP,lTime; } g_pp;
static RenderTexture2D PostTargetHDR(int w, int h, int *hdr){
    RenderTexture2D t={0};
    t.id=rlLoadFramebuffer();
    if (t.id>0){
        rlEnableFramebuffer(t.id);
        t.texture.id=rlLoadTexture(NULL,w,h,RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16,1);
        t.texture.width=w; t.texture.height=h; t.texture.format=PIXELFORMAT_UNCOMPRESSED_R16G16B16A16; t.texture.mipmaps=1;
        t.depth.id=rlLoadTextureDepth(w,h,true);
        t.depth.width=w; t.depth.height=h; t.depth.format=19; t.depth.mipmaps=1;
        rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
        rlFramebufferAttach(t.id,t.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_RENDERBUFFER,0);
        int ok=(t.texture.id>0) && rlFramebufferComplete(t.id);
        rlDisableFramebuffer();
        if (ok){ *hdr=1; return t; }
        rlUnloadFramebuffer(t.id); if (t.texture.id>0) rlUnloadTexture(t.texture.id);
    }
    *hdr=0; return LoadRenderTexture(w,h);
}
static void PostInit(int w, int h){
    static const char *fsBright="#version 330\n"
        "in vec2 fragTexCoord; uniform sampler2D texture0; out vec4 finalColor;\n"
        "void main(){ vec3 c=min(texture(texture0,fragTexCoord).rgb, vec3(6.0)); float l=max(c.r,max(c.g,c.b));\n"
        "  finalColor=vec4(c*smoothstep(0.80,1.45,l),1.0); }\n";
    static const char *fsBlur="#version 330\n"
        "in vec2 fragTexCoord; uniform sampler2D texture0; uniform vec2 dir; out vec4 finalColor;\n"
        "void main(){ vec3 c=texture(texture0,fragTexCoord).rgb*0.227;\n"
        "  c+=(texture(texture0,fragTexCoord+dir*1.385).rgb+texture(texture0,fragTexCoord-dir*1.385).rgb)*0.316;\n"
        "  c+=(texture(texture0,fragTexCoord+dir*3.231).rgb+texture(texture0,fragTexCoord-dir*3.231).rgb)*0.070;\n"
        "  finalColor=vec4(c,1.0); }\n";
    static const char *fsComp="#version 330\n"
        "in vec2 fragTexCoord; uniform sampler2D texture0; uniform sampler2D bloomTex; uniform vec4 P; uniform float time; out vec4 finalColor;\n"   // P = hurt, lowhp, shake, -
        "void main(){ vec2 uv=fragTexCoord, d=uv-0.5; float ca=0.0010+P.x*0.0060+P.z*0.0030;\n"
        "  vec3 c=vec3(texture(texture0,uv+d*ca).r, texture(texture0,uv).g, texture(texture0,uv-d*ca).b);\n"
        "  c+=texture(bloomTex,uv).rgb*0.70;\n"
        "  vec3 hi=max(c-0.80,0.0); c=min(c,0.80)+hi/(1.0+hi*1.6);\n"                 // highlights roll off instead of clipping
        "  c=c*1.30+0.012; c=mix(c, c*c*(3.0-2.0*c), 0.12);\n"                                         // a little contrast
        "  float l=dot(c,vec3(0.299,0.587,0.114)); c=mix(c, vec3(l)*vec3(1.05,0.96,0.94), P.y*0.75);\n"   // near death the colour drains
        "  c*=mix(0.74,1.0, smoothstep(0.88,0.32,length(d*vec2(1.0,0.88))));\n"       // vignette
        "  float n=fract(sin(dot(gl_FragCoord.xy+vec2(time*61.0,time*37.0), vec2(12.9898,78.233)))*43758.5453);\n"
        "  c+=(n-0.5)*0.030*(1.2-l);\n"                                                 // grain, stronger in the dark
        "  finalColor=vec4(c,1.0); }\n";
    if (g_pp.ok){ UnloadRenderTexture(g_pp.scene); UnloadRenderTexture(g_pp.a); UnloadRenderTexture(g_pp.b); }
    else if (!g_pp.bright.id){
        g_pp.bright=LoadShaderFromMemory(0,fsBright); g_pp.blur=LoadShaderFromMemory(0,fsBlur); g_pp.comp=LoadShaderFromMemory(0,fsComp);
        g_pp.lDir=GetShaderLocation(g_pp.blur,"dir"); g_pp.lBloom=GetShaderLocation(g_pp.comp,"bloomTex");
        g_pp.lP=GetShaderLocation(g_pp.comp,"P"); g_pp.lTime=GetShaderLocation(g_pp.comp,"time");
    }
    g_pp.scene=PostTargetHDR(w,h,&g_pp.hdr);
    g_pp.a=LoadRenderTexture(w/4,h/4); g_pp.b=LoadRenderTexture(w/4,h/4);
    SetTextureFilter(g_pp.scene.texture,TEXTURE_FILTER_BILINEAR); SetTextureFilter(g_pp.a.texture,TEXTURE_FILTER_BILINEAR); SetTextureFilter(g_pp.b.texture,TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(g_pp.a.texture,TEXTURE_WRAP_CLAMP); SetTextureWrap(g_pp.b.texture,TEXTURE_WRAP_CLAMP); SetTextureWrap(g_pp.scene.texture,TEXTURE_WRAP_CLAMP);
    g_pp.w=w; g_pp.h=h;
    g_pp.ok=(g_pp.scene.id>0 && g_pp.a.id>0 && g_pp.b.id>0 && g_pp.bright.id>0 && g_pp.blur.id>0 && g_pp.comp.id>0);
    DebugLog("post","\"ok\":%d,\"hdr\":%d,\"w\":%d,\"h\":%d", g_pp.ok, g_pp.hdr, w, h);
}
static void PostBlit(Texture2D src, int dw, int dh){        // a render texture, the right way up, filling dw x dh
    DrawTexturePro(src,(Rectangle){0,0,(float)src.width,-(float)src.height},(Rectangle){0,0,(float)dw,(float)dh},(Vector2){0,0},0,WHITE);
}
// The sound bed, fed EVERY frame (playing, paused, dead, in the menu -- a music stream that is
// not updated stutters): the synthesized drone + tension layer, and the wave's own background
// track. Waves 1, 2 and 3 each have one (assets/wave1_bg.mp3 .. wave3_bg.mp3); later waves
// cycle through them until they get their own. Tracks cross-fade over ~2 s on a wave change,
// duck under the boss's entrance, and drop right down on the death screen and in the menu.
static void AudioBedUpdate(float dt){
    if (!g_audio) return;
    if (g_hasAmb){
        int up=0; for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state==1) up++;
        float want=fminf(1.0f, up/6.0f); if (g_dead) want=0.0f;
        g_ambTense+=(want-g_ambTense)*fminf(1.0f, dt*(want>g_ambTense?0.9f:0.35f));
        float bed=(g_nWaveBg>0)?0.45f:1.0f;                       // the drone sits back when a track is playing
        SetMusicVolume(g_amb[1], g_ambTense*0.75f*bed); SetMusicVolume(g_amb[0], (0.55f-0.20f*g_ambTense)*bed);
        UpdateMusicStream(g_amb[0]); UpdateMusicStream(g_amb[1]);
    }
    if (g_nWaveBg>0){
        int want=(g_wave>=1 && g_hasMap) ? (g_wave-1)%g_nWaveBg : -1;
        float level=WAVE_BG_VOL; if (g_dead || g_menu) level*=0.35f; if (g_hasBossSnd && IsSoundPlaying(g_bossSnd)) level*=0.40f;
        for (int k=0;k<g_nWaveBg;k++){
            float tgt=(k==want)?level:0.0f;
            g_waveBgVol[k]+=(tgt-g_waveBgVol[k])*fminf(1.0f, dt*1.6f);
            if (g_waveBgVol[k]>0.004f && !IsMusicStreamPlaying(g_waveBg[k])) PlayMusicStream(g_waveBg[k]);
            if (g_waveBgVol[k]<=0.004f && tgt==0.0f && IsMusicStreamPlaying(g_waveBg[k])) StopMusicStream(g_waveBg[k]);   // faded out: rewound for next time
            SetMusicVolume(g_waveBg[k], g_waveBgVol[k]);
            if (IsMusicStreamPlaying(g_waveBg[k])) UpdateMusicStream(g_waveBg[k]);
        }
    }
}
static void DrawScene(void){
    ClearBackground((Color){70,90,110,255});
    if (g_hasMap) DrawSky();                  // gradient sky shows through the map's sky openings
    BeginMode3D(g_cam);
        DrawWorld();
        DrawEnemies();
        DrawPickups();
        DrawBolts();
        DrawSmoke(g_cam);                     // minigun muzzle smoke/steam (billboards, drawn last)
    EndMode3D();
    if (g_inspect && !g_noEnemies) DrawInspect();
    DrawViewmodel();
}
static void Frame(void) {
    static int frameNo=0;
    if (g_debug && frameNo>30 && g_perfN<4096) g_perfDt[g_perfN++]=GetFrameTime();
    AudioBedUpdate(GetFrameTime());
    Update();
    g_fogOn=g_hasMap;
    int post=!g_postOff;
    if (post && (g_pp.w!=GetScreenWidth() || g_pp.h!=GetScreenHeight())) PostInit(GetScreenWidth(),GetScreenHeight());
    post=post && g_pp.ok;
    if (post){
        BeginTextureMode(g_pp.scene); DrawScene(); EndTextureMode();
        BeginTextureMode(g_pp.a); BeginShaderMode(g_pp.bright); PostBlit(g_pp.scene.texture,g_pp.a.texture.width,g_pp.a.texture.height); EndShaderMode(); EndTextureMode();
        for (int it=0;it<2;it++){                 // two rounds of the 9-tap blur at quarter res: a wide, soft glow
            float dx[2]={(1.0f+it)/g_pp.a.texture.width,0}, dy[2]={0,(1.0f+it)/g_pp.a.texture.height};
            BeginTextureMode(g_pp.b); BeginShaderMode(g_pp.blur); SetShaderValue(g_pp.blur,g_pp.lDir,dx,SHADER_UNIFORM_VEC2);
                PostBlit(g_pp.a.texture,g_pp.b.texture.width,g_pp.b.texture.height); EndShaderMode(); EndTextureMode();
            BeginTextureMode(g_pp.a); BeginShaderMode(g_pp.blur); SetShaderValue(g_pp.blur,g_pp.lDir,dy,SHADER_UNIFORM_VEC2);
                PostBlit(g_pp.b.texture,g_pp.a.texture.width,g_pp.a.texture.height); EndShaderMode(); EndTextureMode();
        }
    }
    BeginDrawing();
        if (post){
            float lowhp=(!g_godMode && !g_dead && g_playerHp>0 && g_playerHp<35.0f) ? (1.0f-g_playerHp/35.0f) : (g_dead?1.0f:0.0f);
            float P[4]={g_hurt,lowhp,g_shake,0}, tm=(float)GetTime();
            ClearBackground(BLACK);
            BeginShaderMode(g_pp.comp);
                SetShaderValue(g_pp.comp,g_pp.lP,P,SHADER_UNIFORM_VEC4); SetShaderValue(g_pp.comp,g_pp.lTime,&tm,SHADER_UNIFORM_FLOAT);
                SetShaderValueTexture(g_pp.comp,g_pp.lBloom,g_pp.a.texture);
                PostBlit(g_pp.scene.texture,GetScreenWidth(),GetScreenHeight());
            EndShaderMode();
        } else DrawScene();
        DrawVignette(g_shake);                    // minigun: blurred/darkened edges while it rips
        DrawDamageOverlay();                       // red edge flash on damage / low hp / death wash
        DrawHUD();
        if (g_menu) DrawMenu();
        else if (g_paused) DrawPaused();
    EndDrawing();
    if (g_debug && (frameNo%30==0))
        DebugLog("frame","\"n\":%d,\"fps\":%d,\"anim\":%d,\"pos\":[%.1f,%.1f,%.1f]",
                 frameNo,GetFPS(),g_curAnim,g_pos.x,g_pos.y,g_pos.z);
    if (g_debug && frameNo%60==0) for (int i=0;i<MAX_ENEMIES;i++) if (g_enemies[i].state){   // where every live enemy is + what it is doing
        const Enemy *e=&g_enemies[i]; Vector3 sd={0}; int nav=g_hasMap?MapNavSteer(e->pos,&sd):0;
        DebugLog("epos","\"nav\":%d,\"steer\":[%.2f,%.2f],\"reach\":%d,\"i\":%d,\"type\":\"%s\",\"st\":%d,\"clip\":%d,\"pos\":[%.1f,%.1f,%.1f],\"drawY\":%.2f,\"d\":%.1f,\"stuck\":%.1f,\"embed\":%.1f",
                 nav,sd.x,sd.z,g_hasMap?MapNavReachable(e->pos):0,i,kEnemy[e->type].name,e->state,e->clip,e->pos.x,e->pos.y,e->pos.z,e->drawY,Vector3Distance(e->pos,g_cam.position),e->stuckT,e->embedT);
    }
    if (g_shotEvery>0 && frameNo>0 && frameNo%g_shotEvery==0){
        const char *fn=TextFormat("build/shots/f%04d.png", frameNo);
        TakeScreenshot(fn);
        DebugLog("shotseq","\"file\":\"%s\",\"anim\":%d,\"animName\":\"%s\",\"animT\":%.1f,\"reloading\":%d", fn, g_curAnim,
                 (g_gunAnimN>0&&g_curAnim<g_gunAnimN)?g_gunAnim[g_curAnim].name:"", g_animT, g_reloading);
    }
    if (g_shotFrame>0 && frameNo==g_shotFrame){
        TakeScreenshot("chernobyl2-shot.png");
        DebugLog("shot","\"file\":\"chernobyl2-shot.png\",\"frame\":%d", frameNo);
    }
    frameNo++;
}

// ---- Map selection menu (startup) ------------------------------------------
// Scans maps/*.map and lets the player pick one before the game loads. Each
// entry shows the map's worldspawn "message" title and, if present, a thumbnail
// from maps/shots/<name>.png (extracted from the original pk3 by tools/stage_map.sh).
#define MAPSEL_MAX 128
typedef struct { char path[256]; char name[96]; char title[96]; Texture2D shot; int hasShot; } MapEntry;
static MapEntry g_mapList[MAPSEL_MAX]; static int g_mapListN=0;

// Pull the worldspawn "message" "..." title out of the first chunk of a .map.
static void MapReadTitle(const char *path, char *out, int outsz){
    out[0]=0; FILE *f=fopen(path,"r"); if(!f) return;
    char buf[8192]; size_t n=fread(buf,1,sizeof buf-1,f); buf[n]=0; fclose(f);
    const char *m=strstr(buf,"\"message\"");
    if (m){ m=strchr(m+9,'"'); if(m){ const char *e=strchr(m+1,'"');
        if(e && e-m-1<outsz){ int L=(int)(e-m-1); memcpy(out,m+1,L); out[L]=0; } } }
}
static int MapEntryCmp(const void *a, const void *b){ return strcmp(((const MapEntry*)a)->name,((const MapEntry*)b)->name); }

static void ScanMaps(void){
    g_mapListN=0;
    DIR *d=opendir("maps"); if(!d) d=opendir("../maps");
    const char *dir = d ? (opendir("maps")?"maps":"../maps") : "maps";
    if(!d) return; closedir(d);
    d=opendir(dir); if(!d) return;
    struct dirent *e;
    while ((e=readdir(d)) && g_mapListN<MAPSEL_MAX){
        const char *nm=e->d_name; size_t L=strlen(nm);
        if (L<5 || strcmp(nm+L-4,".map")) continue;
        MapEntry *me=&g_mapList[g_mapListN];
        snprintf(me->path,sizeof me->path,"%s/%s",dir,nm);
        snprintf(me->name,sizeof me->name,"%.*s",(int)(L-4),nm);
        MapReadTitle(me->path,me->title,sizeof me->title);
        if (!me->title[0]) snprintf(me->title,sizeof me->title,"%s",me->name);
        me->hasShot=0;
        char sp[300]; snprintf(sp,sizeof sp,"maps/shots/%s.png",me->name);
        if (!FileExists(sp)) snprintf(sp,sizeof sp,"../maps/shots/%s.png",me->name);
        if (FileExists(sp)){ me->shot=LoadTexture(sp); if(me->shot.id>0){ me->hasShot=1;
            SetTextureFilter(me->shot,TEXTURE_FILTER_BILINEAR); } }
        g_mapListN++;
    }
    closedir(d);
    qsort(g_mapList,g_mapListN,sizeof(MapEntry),MapEntryCmp);
}

// Modal map picker. Returns a malloc'd path to the chosen .map, or NULL to quit.
// Runs its own draw loop (the window/GL context already exists).
static char *RunMapSelect(void){
    ScanMaps();
    if (g_mapListN==0) return NULL;                       // nothing to pick -> built-in arena
    int sel=0, top=0, frame=0;
    EnableCursor();
    while (!WindowShouldClose()){
        frame++;
        if (g_maxFrames>0 && frame>g_maxFrames){ EnableCursor(); return NULL; }   // --frames covers the picker too (headless runs must never hang here)
        int W=GetScreenWidth(), H=GetScreenHeight();
        // layout: a scrolling list on the left, a big preview on the right. Shrink the
        // row height so the whole list fits on screen (down to a floor); only longer
        // lists than that scroll.
        int listX=60, listW=W*42/100, listY=150, avail=H-listY-70;
        int rowH=44; if (g_mapListN*rowH>avail){ rowH=avail/g_mapListN; if(rowH<26)rowH=26; }
        int visible=avail/rowH; if(visible<1)visible=1;
        Vector2 mp=GetMousePosition();
        // input ---------------------------------------------------------------
        if (IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_S)) sel=(sel+1)%g_mapListN;
        if (IsKeyPressed(KEY_UP)  ||IsKeyPressed(KEY_W)) sel=(sel-1+g_mapListN)%g_mapListN;
        sel -= (int)GetMouseWheelMove(); if(sel<0)sel=0; if(sel>=g_mapListN)sel=g_mapListN-1;
        for (int i=top;i<top+visible && i<g_mapListN;i++){
            Rectangle r={(float)listX,(float)(listY+(i-top)*rowH),(float)listW,(float)(rowH-6)};
            if (CheckCollisionPointRec(mp,r)){
                sel=i;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) goto chosen;
            }
        }
        if (sel<top) top=sel; if (sel>=top+visible) top=sel-visible+1;
        if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)) goto chosen;
        if (IsKeyPressed(KEY_ESCAPE)){ EnableCursor(); return NULL; }
        // draw ----------------------------------------------------------------
        BeginDrawing();
        ClearBackground((Color){14,16,22,255});
        DrawRectangleGradientV(0,0,W,H,(Color){20,24,34,255},(Color){10,11,16,255});
        TextSh("CHERNOBYL 2", 60, 48, 30, (Color){120,140,170,255});
        TextSh("SELECT MAP", 60, 84, 52, (Color){255,210,60,255});
        // preview panel (right)
        int pvX=listX+listW+40, pvW=W-pvX-60, pvY=listY, pvH=H-listY-70;
        if (pvW>80){
            DrawRectangle(pvX,pvY,pvW,pvH,(Color){0,0,0,150});
            DrawRectangleLines(pvX,pvY,pvW,pvH,(Color){90,105,130,255});
            MapEntry *me=&g_mapList[sel];
            if (me->hasShot){
                float ar=(float)me->shot.width/me->shot.height;
                int iw=pvW-20, ih=(int)(iw/ar); if(ih>pvH-70){ ih=pvH-70; iw=(int)(ih*ar);}
                DrawTexturePro(me->shot,(Rectangle){0,0,(float)me->shot.width,(float)me->shot.height},
                    (Rectangle){(float)(pvX+(pvW-iw)/2),(float)(pvY+10),(float)iw,(float)ih},(Vector2){0,0},0,WHITE);
            } else {
                const char *ns="(no preview)"; int nw=MeasureText(ns,22);
                DrawText(ns,pvX+(pvW-nw)/2,pvY+pvH/2-11,22,(Color){90,100,120,255});
            }
            int tw=MeasureText(me->title,30);
            TextSh(me->title, pvX+(pvW-tw)/2, pvY+pvH-44, 30, RAYWHITE);
        }
        // list (left)
        for (int i=top;i<top+visible && i<g_mapListN;i++){
            Rectangle r={(float)listX,(float)(listY+(i-top)*rowH),(float)listW,(float)(rowH-6)};
            int on=(i==sel), hov=CheckCollisionPointRec(mp,r);
            DrawRectangleRec(r, on?(Color){58,82,120,240}:(hov?(Color){34,44,62,230}:(Color){22,28,40,210}));
            DrawRectangleLinesEx(r,2, on?(Color){255,210,60,255}:(Color){70,84,108,255});
            TextSh(g_mapList[i].title, (int)r.x+16, (int)r.y+(rowH-6)/2-11, 22, on?(Color){255,235,150,255}:RAYWHITE);
        }
        if (g_mapListN>visible){
            char sc[32]; snprintf(sc,sizeof sc,"%d / %d", sel+1, g_mapListN);
            DrawText(sc, listX, listY+visible*rowH+8, 18, (Color){120,135,160,255});
        }
        TextSh("UP/DOWN or mouse to choose     ENTER to play     ESC to quit",
               60, H-44, 20, (Color){150,165,190,255});
        EndDrawing();
        if (g_shotFrame>0 && frame>=g_shotFrame){          // headless: capture the menu and exit
            TakeScreenshot("chernobyl2-shot.png");
            DebugLog("mapselect","\"menushot\":%d,\"maps\":%d", frame, g_mapListN);
            EnableCursor(); return NULL;
        }
    }
    EnableCursor(); return NULL;
  chosen:;
    char *out=(char*)malloc(256); snprintf(out,256,"%s",g_mapList[sel].path);
    for (int i=0;i<g_mapListN;i++) if (g_mapList[i].hasShot) UnloadTexture(g_mapList[i].shot);
    DisableCursor();
    return out;
}

// Choose a spawn facing that looks into open space, not a wall/corner. Casts a ring
// of horizontal rays from the eye and picks the heading whose narrow cone is clearest
// (min distance over a few sub-rays, so a lucky gap through a doorway doesn't win).
// Returns a yaw in the game's convention (forward = (sin yaw, 0, cos yaw)).
static float PickOpenYaw(Vector3 eye){
    if (!g_mapCol.ready) return 0.0f;
    const int N=24; const float R=120.0f;
    float best=-1.0f, bestYaw=0.0f;
    for (int i=0;i<N;i++){
        float yaw=(float)i/(float)N*6.2831853f, score=1e30f;
        for (int j=-1;j<=1;j++){                       // sample the heading +- ~10 deg
            float a=yaw+(float)j*0.18f;
            float d=MapRayNearest(eye,(Vector3){sinf(a),0.0f,cosf(a)},R);
            if (d<0.0f) d=R;                           // nothing in range -> wide open
            if (d<score) score=d;
        }
        if (score>best){ best=score; bestYaw=yaw; }
    }
    return bestYaw;
}

// Load g_mapPath and (re)initialise everything tied to the current map: geometry,
// the UV-scroll shader, player spawn + nav, and a fresh wave of enemies. Safe to
// call repeatedly (e.g. after the ESC "Select Map" button). g_mapPath==NULL -> the
// built-in arena. The one-time setup (weapons, enemy model, sounds) is NOT redone.
static void RunResetExtras(void){                     // in-flight things and pickups from the last run
    memset(g_gren,0,sizeof g_gren); memset(g_sparks,0,sizeof g_sparks); PickupsReset();
}
static void StartSelectedMap(void){
    StopAllSfx();                                      // a fresh run starts quiet
    if (g_mapPath){
        g_map=LoadQ3MapModel(g_mapPath,&g_hasMap);
        int tt=0; for(int mi=0;mi<g_map.meshCount;mi++) tt+=g_map.meshes[mi].triangleCount;
        DebugLog("map","\"path\":\"%s\",\"ok\":%s,\"textures\":%d,\"tris\":%d,\"lights\":%d,\"bake_sec\":%.2f",
                 JStr(g_mapPath), g_hasMap?"true":"false", g_map.meshCount, tt, g_nMapLights, g_mapAOsec);
        if (!g_hasMap) TraceLog(LOG_WARNING,"map: failed to load %s", g_mapPath);
    } else g_hasMap=0;

    // The world shader, on every map material: raylib's textured shader (baked light in vertex
    // colour) plus a UV offset for flowing liquids / warpzones (tcMod scroll), the dynamic
    // lights (DynLight) and distance fog. Built once, reused.
    if (g_hasMap){
        if (!g_hasScrollShader){
            const char *vs="#version 330\n"
                "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor; in vec3 vertexNormal;\n"
                "uniform mat4 mvp; uniform mat4 matModel; out vec2 fragTexCoord; out vec4 fragColor; out vec3 fragPos; out vec3 fragNormal;\n"
                "void main(){ fragTexCoord=vertexTexCoord; fragColor=vertexColor; fragPos=(matModel*vec4(vertexPosition,1.0)).xyz;\n"
                "  fragNormal=mat3(matModel)*vertexNormal; gl_Position=mvp*vec4(vertexPosition,1.0); }\n";
            const char *fs="#version 330\n"
                "in vec2 fragTexCoord; in vec4 fragColor; in vec3 fragPos; in vec3 fragNormal;\n"
                "uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec2 uvOffset;\n"
                "uniform vec3 dPos[8]; uniform vec3 dCol[8]; uniform float dR[8]; uniform int dN; uniform vec3 viewPos; uniform vec4 fog;\n"
                "out vec4 finalColor;\n"
                "void main(){ vec4 t=texture(texture0, fragTexCoord+uvOffset); vec4 base=t*colDiffuse; vec3 c=base.rgb*fragColor.rgb;\n"
                "  vec3 N=normalize(fragNormal);\n"
                "  for (int i=0;i<8;i++){ if (i>=dN) break; vec3 dl=dPos[i]-fragPos; float d=length(dl);\n"
                "    if (d<dR[i]){ float att=1.0-d/dR[i]; att*=att; float nl=abs(dot(N,dl/max(d,1e-4)))*0.75+0.25;\n"   // (brush winding flips: either face lights)
                "      c+=base.rgb*dCol[i]*att*nl; } }\n"
                "  c=mix(c, fog.rgb, 1.0-exp(-length(viewPos-fragPos)*fog.a));\n"
                "  finalColor=vec4(c, base.a*fragColor.a); }\n";
            g_scrollShader=LoadShaderFromMemory(vs,fs);
            if (g_scrollShader.id>0){ g_scrollLoc=GetShaderLocation(g_scrollShader,"uvOffset"); g_hasScrollShader=1;
                const char *nm[6]={"dPos","dCol","dR","dN","viewPos","fog"};
                for (int i=0;i<6;i++) g_wLoc[i]=GetShaderLocation(g_scrollShader,nm[i]); }
        }
        if (g_hasScrollShader){
            for (int i=0;i<g_map.materialCount;i++) g_map.materials[i].shader=g_scrollShader;
            DebugLog("mapfx","\"worldShader\":true,\"scroll\":%d", g_mapMeshScrollAny);
        }
    }

    if (g_hasMap){                                    // start at a map spawn point, drop to the floor
        g_pos = g_hasSpawn ? (Vector3){g_mapSpawn.x, g_mapSpawn.y+2.0f, g_mapSpawn.z} : (Vector3){0,12,0};
        g_yaw=0.0f; g_pitch=0.0f; g_vy=0.0f; g_grounded=0;
        Vector3 sp; float sy,sp2; int haveCustom=0;   // a saved spawn (F2) overrides the map's start
        if (LoadSpawn(&sp,&sy,&sp2)){ g_pos=sp; g_yaw=sy; g_pitch=sp2; haveCustom=1;
            DebugLog("spawn","\"custom\":true,\"pos\":[%.1f,%.1f,%.1f]", sp.x,sp.y,sp.z); }
        if (g_lookSet){ g_yaw=g_lookYaw*0.01745329252f; g_pitch=g_lookPitch*0.01745329252f; }  // dev: aim camera
        else if (!haveCustom){ g_yaw=PickOpenYaw(g_pos);          // face the most open direction, not a wall
            DebugLog("spawnface","\"yaw\":%.1f", g_yaw*57.29578f); }
        MapNavBuild(g_pos);                           // flood the walkable nav grid from the start point
        DebugLog("nav","\"grid\":[%d,%d],\"walkable\":%d,\"ok\":%d", g_navNX, g_navNZ, g_navCount, g_navOK);
        g_spawnResetPos=g_pos; g_voidY=-12.0f;        // below the lowest floor (normalised to y=0) -> fell off
    } else { g_pos=(Vector3){0,EYE_H,6}; g_voidY=-1e9f; }

    // Fresh RUN for the (new) map: health, loadout, tallies, and any in-flight
    // death/reload state all reset -- same contract as RespawnPlayer, so dying
    // and picking a new map can't disagree about what "fresh" means. (A dead
    // player can reach here via ESC -> Select Map, so g_dead MUST clear.)
    g_playerHp=100.0f; g_prevHp=100.0f; g_hurt=0.0f; g_dead=0; g_deadT=0.0f; PickupsReset();
    g_vy=0.0f; g_eyeSmooth=0.0f;
    for (int i=0;i<g_numWeapons;i++){ g_weapons[i].mag=g_weapons[i].magSize; g_weapons[i].reserve=g_weapons[i].reserveStart; }
    g_flameFuel=0.0f; g_burstLeft=0; g_fireCd=0.0f;
    g_reloading=0; g_reloadStep=-1; g_animOnce=0; g_curAnim=g_aIdle; g_animT=0.0f;   // cancel a reload begun on the old map
    g_kills=0; g_headshots=0; g_shotsFired=0; g_shotsHit=0; g_cheatUsed=0;
    g_wave=0; g_waveBudget=0; g_waveBreather=0.0f; g_spawnTick=0.0f; g_runT=0.0f;    // wave director restarts with the run
    g_cam=(Camera3D){ g_pos, Vector3Add(g_pos,(Vector3){0,0,-1}), (Vector3){0,1,0}, 70.0f, CAMERA_PERSPECTIVE };
    for (int i=0;i<MAX_ENEMIES;i++) g_enemies[i].state=0;          // clear any survivors from the previous map
    StainReset();                                                  // and the blood on its floors
    // ...and everything else the last run left lying about: a restarted run is a NEW game
    for (int c=0;c<MAX_CORPSES;c++) g_corpses[c].active=0;         // the dead
    memset(g_gibs,0,sizeof g_gibs); memset(g_casings,0,sizeof g_casings); memset(g_holes,0,sizeof g_holes);
    memset(g_scorch,0,sizeof g_scorch); memset(g_smoke,0,sizeof g_smoke); memset(g_tracers,0,sizeof g_tracers);
    memset(g_dyn,0,sizeof g_dyn); RunResetExtras();
    g_hurt=0.0f; g_hitMark=g_killMark=0.0f; g_recoilRet=0.0f; g_painAcc=g_painCd=0.0f; g_seenPrev=g_seenNow=0; g_appearCd=g_mobCd=0.0f; g_ambTense=0.0f;
    // (no boot spawns -- the wave director trickles wave 1 in after a short grace)
}

// Tear down the current map before loading another. The scroll shader is shared by
// all map materials, so restore the default shader first or UnloadModel would free
// our shared one (it's kept for the next map and released at shutdown).
static void EndCurrentMap(void){
    if (!g_hasMap) return;
    if (g_hasScrollShader)
        for (int i=0;i<g_map.materialCount;i++){
            g_map.materials[i].shader.id   = rlGetShaderIdDefault();
            g_map.materials[i].shader.locs = rlGetShaderLocsDefault();
        }
    UnloadModel(g_map);
    g_hasMap=0;
}

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include "intro.h"                      // title intro (archival montage + Hell March)
#include "viewer.h"                     // --viewer: side-by-side enemy model comparison
#include "soundboard.h"                 // --sounds: every sound, grouped, playable, with the gaps marked

int main(int argc, char **argv) {
    int vmWeapon=-1; float vmOv[7]={0}; int vmHas=0;          // dev: --weapon / --vm framing overrides
    unsigned int seedArg=0; int seedSet=0;                    // --seed N : deterministic runs
    int fpsOv=-1;                                             // --fps N : target-fps override (perf gate)
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--debug")) g_debug=1;
        else if (!strcmp(argv[i],"--frames") && i+1<argc) g_maxFrames=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--seed") && i+1<argc){ seedArg=(unsigned int)strtoul(argv[++i],NULL,10); seedSet=1; }
        else if (!strcmp(argv[i],"--autofire")) g_autoFire=1;
        else if (!strcmp(argv[i],"--sounds")) g_soundboard=1;        // dev: the sound board (soundboard.h)
        else if (!strcmp(argv[i],"--mute")) g_mute=1;
        else if (!strcmp(argv[i],"--no-post")) g_postOff=1;          // draw straight to the screen: no bloom / grade / grain
        else if (!strcmp(argv[i],"--boss-wave") && i+1<argc){ g_devBoss=atoi(argv[++i]); if (g_devBoss<1) g_devBoss=1; }   // dev: straight to wave N's boss
        else if (!strcmp(argv[i],"--boss")) g_devBoss=1;                // dev: straight to the boss interlude
        else if (!strcmp(argv[i],"--corpses") && i+1<argc) g_devCorpses=atoi(argv[++i]);   // dev: pre-placed corpses
        else if (!strcmp(argv[i],"--enemy") && i+1<argc){                   // only spawn this enemy type (cook/linecook/butcher/liquidator/headchef)
            const char *nm=argv[++i];
            for (int t=0;t<EN_NTYPES;t++) if (!strcmp(nm,kEnemy[t].name)) g_forceEnemy=t;
            if (!strcmp(nm,"all")) g_forceEnemy=-2;
            if (g_forceEnemy==-1){ fprintf(stderr,"--enemy: unknown type '%s' (all|cook|linecook|butcher|liquidator|headchef)\n",nm); return 1; }
        }
        else if (!strcmp(argv[i],"--vm-hold") && i+1<argc) g_vmHoldDev=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--reload-at") && i+1<argc) g_reloadAt=atoi(argv[++i]);    // dev: force a reload at frame N
        else if (!strcmp(argv[i],"--kite")) g_kite=1;
        else if (!strcmp(argv[i],"--no-intro")) g_noIntro=1;                // skip the title intro
        else if (!strcmp(argv[i],"--no-lod")) g_noLod=1;                    // always draw full-detail enemies
        else if (!strcmp(argv[i],"--viewer") && i+1<argc) g_viewA=argv[++i];        // model viewer (see viewer.h)
        else if (!strcmp(argv[i],"--viewer-b") && i+1<argc) g_viewB=argv[++i];
        else if (!strcmp(argv[i],"--viewer-shots") && i+1<argc) g_viewShots=argv[++i];
        else if (!strcmp(argv[i],"--shot-every") && i+1<argc) g_shotEvery=atoi(argv[++i]);  // dev: numbered shots into build/shots/        // dev: hold the trigger (muzzle-FX screenshots)
        else if (!strcmp(argv[i],"--cycle")) g_cycleTest=1;          // soak: weapon-cycle ammo regression
        else if (!strcmp(argv[i],"--melee-drain")) g_meleeDrain=1;   // A/B: legacy enemy contact drain
        else if (!strcmp(argv[i],"--stress")) g_stress=1;            // perf gate: field saturated with cooks, no waves
        else if (!strcmp(argv[i],"--god")) g_godMode=1;              // headless/perf runs: no death
        else if (!strcmp(argv[i],"--fps") && i+1<argc) fpsOv=atoi(argv[++i]);   // SetTargetFPS override (0 = uncapped)
        else if (!strcmp(argv[i],"--shot") && i+1<argc) g_shotFrame=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--no-enemies")) g_noEnemies=1;
        else if (!strcmp(argv[i],"--map") && i+1<argc) g_mapPath=argv[++i];   // SPIKE: load a .map
        else if (!strcmp(argv[i],"--look") && i+2<argc){ g_lookYaw=(float)atof(argv[++i]); g_lookPitch=(float)atof(argv[++i]); g_lookSet=1; }   // dev: aim spawn camera (deg)
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
    // CHERNOBYL_BACKGROUND=1: automated test runs open a HIDDEN, unfocused window, so they
    // don't take the keyboard or cover what the person at the machine is doing. (Minimized
    // doesn't work: macOS stops a minimized window's buffer swaps and the loop stalls.)
    int bgRun = getenv("CHERNOBYL_BACKGROUND") && *getenv("CHERNOBYL_BACKGROUND") && strcmp(getenv("CHERNOBYL_BACKGROUND"),"0");
    if (bgRun) SetConfigFlags(FLAG_WINDOW_UNFOCUSED|FLAG_WINDOW_HIDDEN);
    InitWindow(1280,720,"Chernobyl 2");
    SetExitKey(KEY_NULL);              // ESC opens the options menu instead of quitting
    int ready=IsWindowReady();
    if (ready){                        // soft radial puff for the minigun smoke billboards
        Image si=GenImageGradientRadial(64,64,0.25f,(Color){255,255,255,255},(Color){255,255,255,0});
        g_smokeTex=LoadTextureFromImage(si); UnloadImage(si);
        SetTextureFilter(g_smokeTex,TEXTURE_FILTER_BILINEAR); g_hasSmokeTex=(g_smokeTex.id!=0);
    }
    DebugLog("boot","\"windowReady\":%s,\"raylib\":\"%s\"", ready?"true":"false", RAYLIB_VERSION);

    // InitWindow just seeded raylib's RNG from the clock, so --seed must be
    // applied after it (and before any GetRandomValue call) to stick.
    if (seedSet){ SetRandomSeed(seedArg); DebugLog("seed","\"value\":%u", seedArg); }

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
        ApplyFullscreen(1);
        DebugLog("window","\"borderless\":true,\"w\":%d,\"h\":%d,\"monitor\":%d",
                 GetScreenWidth(), GetScreenHeight(), GetCurrentMonitor());
    }

    SetTargetFPS(120);   // 120 Hz target (ProMotion etc.); all motion is dt-scaled so physics is unchanged
    if (fpsOv>=0) SetTargetFPS(fpsOv);   // --fps: perf-gate override (0 = uncapped, 30 = throttled dt check)
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

    if (g_viewA && strcmp(g_viewA,"all")){ int r=RunViewer(); CloseWindow(); return r; }   // --viewer <glb>: no game, no audio
    // (--viewer all, the enemy gallery, starts further down: its weapon mode needs the guns + audio)
    InitAudioDevice();                 // weapon fire sounds; harmless if it fails
    if (g_mute || getenv("CHERNOBYL_MUTE")) SetMasterVolume(0.0f);   // silent test runs
    if (getenv("CHERNOBYL_NO_LOD")) g_noLod=1;
    g_audio = IsAudioDeviceReady();
    DebugLog("audio","\"ready\":%s", g_audio?"true":"false");

    if (g_audio){                      // a random one of these plays on every enemy kill
        const char *deaths[]={ "enemy_death1.mp3","enemy_death2.mp3","enemy_death3.mp3","enemy_death4.mp3","enemy_death5.mp3" };
        for (int d=0; d<(int)(sizeof deaths/sizeof deaths[0]); d++){
            char p1[64],p2[64]; snprintf(p1,sizeof p1,"assets/%s",deaths[d]); snprintf(p2,sizeof p2,"../assets/%s",deaths[d]);
            const char *sp=FileExists(p1)?p1:(FileExists(p2)?p2:NULL);
            if (sp){ Sound s=LoadSound(sp); if (s.frameCount>0) g_deathSnd[g_nDeathSnd++]=s; }
        }
        const char *ap="assets/enemy_attack.mp3";
        if (!FileExists(ap)) ap="../assets/enemy_attack.mp3";
        if (FileExists(ap)){ g_attackSnd=LoadSound(ap); g_hasAttackSnd=(g_attackSnd.frameCount>0); }
        DebugLog("enemysnd","\"deaths\":%d,\"attack\":%s", g_nDeathSnd, g_hasAttackSnd?"true":"false");
        SynthInit();
        EnemyVoiceLoad();
        { const char *bg[3]={"assets/wave1_bg.mp3","assets/wave2_bg.mp3","assets/wave3_bg.mp3"};
          for (int k=0;k<3;k++) if (FileExists(bg[k])){ Music m=LoadMusicStream(bg[k]); if (IsMusicValid(m)){ m.looping=true; SetMusicVolume(m,0.0f); g_waveBg[g_nWaveBg++]=m; } }
          DebugLog("wavebg","\"tracks\":%d", g_nWaveBg); }
        { const char *gl[3]={"assets/gl_open.wav","assets/gl_insert.wav","assets/gl_shut.wav"};
          for (int k=0;k<3;k++) if (FileExists(gl[k])){ g_glSnd[k]=LoadSound(gl[k]); g_hasGlSnd[k]=(g_glSnd[k].frameCount>0); } }
        if (FileExists("assets/pickup_ammo.mp3")){ g_pkAmmoSnd=LoadSound("assets/pickup_ammo.mp3"); g_hasPkAmmoSnd=(g_pkAmmoSnd.frameCount>0); }
        if (FileExists("assets/menu_select.mp3")){ g_menuSelSnd=LoadSound("assets/menu_select.mp3"); g_hasMenuSelSnd=(g_menuSelSnd.frameCount>0); }
        if (FileExists("assets/surrounded.mp3")){ g_surrSnd=LoadSound("assets/surrounded.mp3"); g_hasSurrSnd=(g_surrSnd.frameCount>0); }
        if (FileExists("assets/menu_hover.mp3")){ g_menuSnd=LoadSound("assets/menu_hover.mp3"); g_hasMenuSnd=(g_menuSnd.frameCount>0); }
        if (FileExists("assets/dry_fire.mp3")){ g_drySnd=LoadSound("assets/dry_fire.mp3"); g_hasDrySnd=(g_drySnd.frameCount>0); }
        if (FileExists("assets/bullet_flesh.mp3")){ g_fleshSnd=LoadSound("assets/bullet_flesh.mp3"); g_hasFleshSnd=(g_fleshSnd.frameCount>0); }
        if (FileExists("assets/bullet_wall.wav")){ g_wallSnd=LoadSound("assets/bullet_wall.wav"); g_hasWallSnd=(g_wallSnd.frameCount>0); }
        if (FileExists("assets/enemy_run.mp3")){ g_runSnd=LoadSound("assets/enemy_run.mp3"); g_hasRunSnd=(g_runSnd.frameCount>0); }
        if (FileExists("assets/gib_burst.mp3")){ g_gibSnd=LoadSound("assets/gib_burst.mp3"); g_hasGibSnd=(g_gibSnd.frameCount>0); }
        if (FileExists("assets/enemy_knockdown.mp3")){ g_knockSnd=LoadSound("assets/enemy_knockdown.mp3"); g_hasKnockSnd=(g_knockSnd.frameCount>0); }
        if (FileExists("assets/grenade_bounce.mp3")){ g_grenBounceSnd=LoadSound("assets/grenade_bounce.mp3"); g_hasGrenBounceSnd=(g_grenBounceSnd.frameCount>0); }
        if (FileExists("assets/minigun_spinup.wav")){ g_spinUpSnd=LoadSound("assets/minigun_spinup.wav"); g_hasSpinUpSnd=(g_spinUpSnd.frameCount>0); }
        if (FileExists("assets/death_shotgun.mp3")){ g_sgDeathSnd=LoadSound("assets/death_shotgun.mp3"); g_hasSgDeathSnd=(g_sgDeathSnd.frameCount>0); }
        if (FileExists("assets/boss_dead.mp3")){ g_bossDeadSnd=LoadSound("assets/boss_dead.mp3"); g_hasBossDeadSnd=(g_bossDeadSnd.frameCount>0); }
        if (FileExists("assets/boss_entrance.mp3")){ g_bossSnd=LoadSound("assets/boss_entrance.mp3"); g_hasBossSnd=(g_bossSnd.frameCount>0); }
        if (FileExists("assets/angry_crowd.mp3")){ g_mobSnd=LoadSound("assets/angry_crowd.mp3"); g_hasMobSnd=(g_mobSnd.frameCount>0); }
        if (FileExists("assets/crowd_scream.mp3")){ g_crowdSnd=LoadSound("assets/crowd_scream.mp3"); g_hasCrowdSnd=(g_crowdSnd.frameCount>0); }
        if (FileExists("assets/enemy_appear.mp3")){ g_appearSnd=LoadSound("assets/enemy_appear.mp3"); g_hasAppearSnd=(g_appearSnd.frameCount>0); }
        if (FileExists("assets/player_death.mp3")){ g_dieSnd=LoadSound("assets/player_death.mp3");    g_hasDieSnd=(g_dieSnd.frameCount>0); }
        if (FileExists("assets/shotgun_shell.wav")){ g_shellSnd=LoadSound("assets/shotgun_shell.wav"); g_hasShellSnd=(g_shellSnd.frameCount>0); }
        { const char *pn[2]={"assets/pain_yell.mp3","assets/pain_scream.mp3"};
          for (int k=0;k<2;k++) if (FileExists(pn[k])){ Sound sd=LoadSound(pn[k]); if (sd.frameCount>0) g_painSnd[g_nPainSnd++]=sd; } }
        DebugLog("synth","\"cues\":%d,\"ambience\":%d", g_hasSy, g_hasAmb);
        // combat one-shots (M1, synthesized in-repo): grenade blast, hurt thud, low-hp heartbeat
        { const char *p="assets/explosion.wav";   if (!FileExists(p)) p="../assets/explosion.wav";
          if (FileExists(p)){ g_grenSnd=LoadSound(p);  g_hasGrenSnd =(g_grenSnd.frameCount>0); } }
        { const char *p="assets/player_hurt.wav"; if (!FileExists(p)) p="../assets/player_hurt.wav";
          if (FileExists(p)){ g_hurtSnd=LoadSound(p);  g_hasHurtSnd =(g_hurtSnd.frameCount>0); } }
        { const char *p="assets/heartbeat.wav";   if (!FileExists(p)) p="../assets/heartbeat.wav";
          if (FileExists(p)){ g_heartSnd=LoadSound(p); g_hasHeartSnd=(g_heartSnd.frameCount>0); SetSoundVolume(g_heartSnd,0.65f); } }
        { const char *p="assets/wave_start.wav";  if (!FileExists(p)) p="../assets/wave_start.wav";
          if (FileExists(p)){ g_waveSnd=LoadSound(p);  g_hasWaveSnd =(g_waveSnd.frameCount>0); } }
        { const char *p="assets/wave_clear.wav";  if (!FileExists(p)) p="../assets/wave_clear.wav";
          if (FileExists(p)){ g_clearSnd=LoadSound(p); g_hasClearSnd=(g_clearSnd.frameCount>0); SetSoundVolume(g_clearSnd,0.7f); } }
        { const char *p="assets/ak74_fire_single.mp3"; if (FileExists(p)){ g_eShotSnd=LoadSound(p); g_hasEShot=(g_eShotSnd.frameCount>0); } }
        { const char *p="assets/flame_fire.mp3";  if (FileExists(p)){ g_eBoltSnd=LoadSound(p); g_hasEBolt=(g_eBoltSnd.frameCount>0); } }
        { const char *p="assets/wave_start.wav";  if (FileExists(p)){ g_eRaiseSnd=LoadSound(p); g_hasERaise=(g_eRaiseSnd.frameCount>0); } }
        { const char *p="assets/pickup.wav";      if (!FileExists(p)) p="../assets/pickup.wav";
          if (FileExists(p)){ g_pkSnd=LoadSound(p);    g_hasPkSnd   =(g_pkSnd.frameCount>0);  SetSoundVolume(g_pkSnd,0.6f); } }
    }

    // (Map selection + load happens in the per-map outer loop below, via
    // RunMapSelect() + StartSelectedMap(), so the ESC "Select Map" button can
    // return here without redoing the one-time setup.)

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

    g_vmCam=(Camera3D){ (Vector3){0,0,0}, (Vector3){0,0,-1}, (Vector3){0,1,0}, 55.0f, CAMERA_PERSPECTIVE };  // g_cam is set per-map in StartSelectedMap

    g_posedBasis[0]=1;   // grenade launcher: animated rig -> frame from the posed idle, not the bind pose
    LoadWeapon(0, "assets/grenadelauncher.glb", "../assets/grenadelauncher.glb", "Grenade Launcher", "vm_tune_grenade.txt", VM_RIFLE);
    g_posedBasis[1]=1;   // remington: animated FPS rig
    LoadWeapon(1, "assets/remington.glb", "../assets/remington.glb", "Remington", "vm_tune_remington.txt", VM_RIFLE);
    LoadWeapon(2, "assets/minigun.glb", "../assets/minigun.glb", "Minigun", "vm_tune_minigun.txt", VM_HEAVY);
    // (slots 3 and 4 were the LMG and the AK-74M: cut from the roster. The slots stay empty --
    // has=0, so nothing can select them -- and the flamethrower keeps slot 5.)
    // new weapons (auto-framed; tune each live with N, ENTER saves). MP5 + Benelli
    // are clean rigs that animate fire+reload; the flamethrower/knife are model-only
    // for now (they hitscan -- no flame/melee mechanic yet).
    // (Benelli M4 cut 2026-11-02: its rig ships no visible hands. assets/benelli.glb kept as a spare.)
    // (MP5 + knife cut 2026-11-02: their arm rigs are broken at source -- the MP5's arms
    // never reach the gun in any clip, the knife is IK-driven, which raylib can't
    // evaluate. assets/mp5.glb + knife.glb kept as spares.)
    LoadWeapon(5, "assets/flamethrower.glb", "../assets/flamethrower.glb", "Flamethrower", "vm_tune_flame.txt", VM_HEAVY);
    g_numWeapons=6;
    ActivateWeapon( (vmWeapon>=0)?vmWeapon:0 );   // boot on slot 0 / grenade launcher (or --weapon N for tuning)
    if (vmHas){ g_vmOff=(Vector3){vmOv[0],vmOv[1],vmOv[2]}; g_vmScale=vmOv[3]; g_vmYaw=vmOv[4]; g_vmPitch=vmOv[5]; g_vmRoll=vmOv[6]; }

    // Per-weapon fire sounds. The two automatics loop while the trigger is held;
    // the shotgun fires one blast per shot.
    LoadWeaponSound(0, "assets/grenade_launch.mp3", "../assets/grenade_launch.mp3", 0);  // grenade launcher: its own launch thump
    if (!g_weapons[0].hasSnd) LoadWeaponSound(0, "assets/shotgun_fire.mp3", "../assets/shotgun_fire.mp3", 0);
    LoadWeaponSound(1, "assets/shotgun_fire.mp3", "../assets/shotgun_fire.mp3", 0);  // remington shotgun: one-shot
    LoadWeaponSound(2, "assets/minigun_fire.mp3", "../assets/minigun_fire.mp3", 1);  // minigun
    LoadWeaponAux(2, "assets/minigun_cooldown.mp3", "../assets/minigun_cooldown.mp3"); // minigun spin-down
    if (g_weapons[2].hasAuxSnd) SetSoundVolume(g_weapons[2].auxSnd, 0.4f);  // spin-down was too loud/abrupt -> soften it
    g_weapons[0].grenade=1;      // grenade launcher: lobs an arcing grenade, explodes on impact
    g_weapons[0].pinGun=1;       // its launcher mesh is detached from the arms -> ride it on the hand bone
    DebugPinProbe(&g_weapons[0]);   // boot ActivateWeapon ran BEFORE these flags -> probe explicitly now
    g_weapons[1].playShoot=1; g_weapons[1].fireCd=0.7f;   // remington: plays Shot clip per blast, semi-auto cadence; R plays Reload
    g_weapons[2].spinUp=1;       // minigun: 5s fire cap -> cooldown sound + lockout
    g_weapons[3].soundGated=1;   // LMG:     fire while the sound plays, then a 0.75s pause
    g_weapons[4].playShoot=1;    // AK-74M:  plays its Shot clip on fire; R plays AK_Reload
    g_weapons[5].flame=1;        // Flamethrower: continuous fire cone while held
    LoadWeaponSound(5, "assets/flame_fire.mp3", "../assets/flame_fire.mp3", 1);  // flamethrower: looped roar (silent if asset absent)

    // M1 combat identity: damage / pellets / spread(deg) / kick(deg) / mag / reserve
    // per slot. dmg 0 keeps the legacy ENEMY_DMG_PER_SHOT; magSize 0 = no ammo.
    {
        static const struct { float dmg; int pel; float spr, kick; int mag, res; } S[6]={
            {  0.0f, 0, 0.0f, 2.5f,    6,  18 },   // 0 grenade launcher (GREN_DMG on the blast)
            { 13.0f, 8, 3.5f, 3.2f,    5,  25 },   // 1 remington: a pellet wall, brutal up close
            {  7.0f, 0, 2.2f, 0.25f, 300, 300 },   // 2 minigun: belt-fed hose, wide
            { 14.0f, 0, 1.6f, 0.8f,   80, 160 },   // 3 LMG: chunky gated bursts
            { 34.0f, 0, 0.7f, 1.1f,   30, 120 },   // 4 AK-74M: 3-tap body, 1-tap head
            {  0.0f, 0, 0.0f, 0.0f,  100, 100 },   // 5 flamethrower: fuel units (FLAME_DPS while lit)
        };
        for (int i=0;i<6;i++){ Weapon *w=&g_weapons[i];
            w->dmg=S[i].dmg; w->pellets=S[i].pel; w->spread=S[i].spr; w->recoilKick=S[i].kick;
            w->magSize=S[i].mag; w->reserveStart=S[i].res; w->mag=S[i].mag; w->reserve=S[i].res; }
    }
    g_weapons[2].fireCd=0.06f;   // minigun: ~17 rounds/s once spun up
    g_weapons[4].burst=3;        // AK-74M: one 3-round burst per trigger pull
    g_weapons[1].autoReload=1;   // remington: cock between shots
    g_weapons[1].shellPump=300.0f; g_weapons[1].shellRackAt=352.0f;   // the 410-frame clip holds ~7 inserts, then the pump from ~300: a full tube skips there
    g_weapons[1].shellFirst=40.0f; g_weapons[1].shellCycle=40.0f; g_weapons[1].shellCount=4;   // its Reload clip: shells go in at 40/80/120/160, pump at 180
    LoadWeaponAux(1, "assets/shotgun_rack.wav", "../assets/shotgun_rack.wav");      // the pump: one rack cut from a pump-action recording
    if (!g_weapons[1].hasAuxSnd) LoadWeaponAux(1, "assets/shotgun_cock.mp3", "../assets/shotgun_cock.mp3");
    if (g_weapons[1].hasSnd) SetSoundVolume(g_weapons[1].fireSnd, 0.72f);    // the blast, pulled back so the pump after it can be heard
    // Auto viewmodel frames: measured after the per-slot flags (pinGun/grenade/spinUp
    // pick the rest pose + the pinned-gun path), so every gun is framed as drawn.
    // Per-weapon default trims (code-owned; a saved vm_trim_*.txt still wins).
    { struct { int slot; Vector3 off; float scale; } D[]={
        { 3, {0.0f, 0.03f, 0.25f}, 1.0f },     // LMG: pulled in from the heavy class slot so it fills the frame
      };
      for (unsigned i=0;i<sizeof D/sizeof D[0];i++){ Weapon *w=&g_weapons[D[i].slot];
          int saved=FileExists(w->trimPath);
          w->off0=D[i].off; w->scale0=D[i].scale;
          if (!saved){ w->off=w->off0; w->scale=w->scale0; } } }
    if (getenv("VMAIM")) sscanf(getenv("VMAIM"),"%f %f",&g_vmAimR,&g_vmAimD);
    for (int c=0;c<VM_NCLASS;c++){ char ev[16]; snprintf(ev,sizeof ev,"VMCLASS%d",c); const char *e=getenv(ev);
        if (e) sscanf(e,"%f %f %f %f %f",&kVmClass[c].len,&kVmClass[c].pos.x,&kVmClass[c].pos.y,&kVmClass[c].pos.z,&kVmClass[c].conv); }
    for (int i=0;i<g_numWeapons;i++) ComputeVmFrame(&g_weapons[i], i);
    if (vmWeapon<0) SwitchWeapon(2);   // the run starts on weapon key 1 (the minigun: kSlotForKey[0]); --weapon N still overrides
    ActivateWeapon(g_curWeapon);   // re-pose the live gun (measuring posed every rig)
    if (vmHas){ g_vmOff=(Vector3){vmOv[0],vmOv[1],vmOv[2]}; g_vmScale=vmOv[3]; g_vmYaw=vmOv[4]; g_vmPitch=vmOv[5]; g_vmRoll=vmOv[6]; }

    // Load one rig per enemy type: assets/<name>.glb. The cook is required (it stands in
    // for any type whose model is missing); the rest are optional.
    for (int t=0;t<EN_NTYPES;t++){
        char path[96]; snprintf(path,sizeof path,"assets/%s.glb",kEnemy[t].name);
        int ok=LoadEnemyRig(&g_rig[t], path);
        if (t==EN_COOK) g_hasEnemy=ok;
    }
    // (the starting wave of enemies is spawned per-map in StartSelectedMap)

    if (g_soundboard){                  // --sounds: the sound board, no map, no game
        int r=RunSoundboard();
        if (g_audio) CloseAudioDevice();
        CloseWindow(); return r;
    }
    if (g_viewA){                       // --viewer all: the enemy gallery (+ weapon range), no map, no game
        int r=RunGallery();
        if (g_audio) CloseAudioDevice();
        CloseWindow(); return r;
    }

#if defined(__EMSCRIPTEN__)
    if (!g_mapPath && FileExists(DEFAULT_MAP)) g_mapPath=DEFAULT_MAP;
    if (!g_mapPath){ char *p=RunMapSelect(); if (p) g_mapPath=p; }
    StartSelectedMap();
    emscripten_set_main_loop(Frame,0,1);
#else
    if (!RunIntro()){ DebugLog("intro","\"quit\":true"); CloseAudioDevice(); CloseWindow(); return 0; }
    // Per-run outer loop: the game IS the Chernobyl arena (maps/chernobyl.map, rebuilt
    // from ../chernobyl level 0 by tools/grid2map.py), so with no --map it loads that
    // straight away; the picker is only a fallback if the arena file is missing. "Restart
    // Run" (ESC menu) and the death screen loop back here and reload it.
    for (;;){
        if (!g_mapPath && FileExists(DEFAULT_MAP)) g_mapPath=DEFAULT_MAP;
        if (!g_mapPath){
            char *p=RunMapSelect();
            if (!p){ if (g_mapListN>0){ DebugLog("mapselect","\"quit\":true"); break; } } // user quit (no maps -> arena)
            else { g_mapPath=p; g_mapPathOwned=1; DebugLog("mapselect","\"path\":\"%s\"", JStr(g_mapPath)); }
        }
        StartSelectedMap();
        g_returnToMenu=0;
        int fc=0;
        while (!WindowShouldClose() && !g_quit && !g_returnToMenu){ Frame(); if (g_maxFrames>0 && ++fc>=g_maxFrames) break; }
        if (WindowShouldClose() || g_quit || g_maxFrames>0) break;   // real exit (headless --frames always exits)
        EndCurrentMap();                                             // "Restart Run": tear down and reload
        if (g_mapPathOwned){ free((void*)g_mapPath); g_mapPathOwned=0; }
        g_mapPath=NULL;                                              // -> DEFAULT_MAP again (picker only if it's gone)
    }
#endif

    StashActiveTuning();
    // Persist trims -- but never a --vm dev override (it would silently become the
    // weapon's saved framing), and only once a trim is non-neutral or already saved,
    // so untouched guns keep following the auto frame + class table.
    if (!vmHas) for (int i=0;i<g_numWeapons;i++){ Weapon *w=&g_weapons[i];
        int neutral = w->off.x==w->off0.x && w->off.y==w->off0.y && w->off.z==w->off0.z && w->scale==w->scale0 && w->yaw==w->yaw0 && w->pitch==w->pitch0 && w->roll==w->roll0;
        if (!neutral || FileExists(w->trimPath)) SaveTune(w->trimPath,w->off,w->scale,w->yaw,w->pitch,w->roll,w->idleHold); }
    LogPerfStats();                        // frame-time stats for the perf gate
    DebugLog("shutdown","\"ok\":true");
    for (int i=0;i<g_numWeapons;i++) if (g_weapons[i].has){
        if (g_weapons[i].anim) UnloadModelAnimations(g_weapons[i].anim,g_weapons[i].animN);
        UnloadModel(g_weapons[i].model);
    }
    for (int ri=0;ri<EN_NTYPES;ri++) if (g_rig[ri].ok){ if (g_rig[ri].anim) UnloadModelAnimations(g_rig[ri].anim,g_rig[ri].animN); UnloadModel(g_rig[ri].model); }
    UnloadModel(g_floor); UnloadModel(g_wall); UnloadModel(g_crate);
    if (g_hasScrollShader) UnloadShader(g_scrollShader);
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
