// Touch overlay -- the on-screen controls of the iPhone build. Brought over from ../Chernobyl
// (src/touch_overlay.c, its iOS port), where they were tuned on real phones: a floating stick
// under the left thumb, the right half of the screen as a look pad, FIRE (drag on it to keep
// aiming), JUMP, PAUSE and the four weapons. This game adds RELOAD and an edge-triggered fire.
// Included once from main.c, on iOS only.
//
//   TouchOverlayPoll(playing)  at the top of the frame, before any input is read
//   TouchOverlayDraw(playing)  in the HUD pass
#include <stdbool.h>
#define GS_PLAY 1

// ── Per-touch state machine ────────────────────────────────────────────────
typedef enum {
    CTRL_NONE = 0,
    CTRL_STICK,
    CTRL_LOOK,
    CTRL_FIRE,
    CTRL_JUMP,
    CTRL_PAUSE,
    CTRL_RELOAD,
    CTRL_WPN,        // wpnIdx field selects which (0..3)
    CTRL_MENU_TAP,   // any-tap on non-gameplay screens
} TouchCtrl;

#define MAX_SLOTS 10

static struct {
    bool      active;
    int       pointId;
    TouchCtrl ctrl;
    int       wpnIdx;
    Vector2   origin;     // where the touch first landed (stick anchor)
    Vector2   pos;
    Vector2   prevPos;
} g_slots[MAX_SLOTS];

// Set by game.c each frame so the overlay can highlight the active weapon
// button. 0..3 = highlight that slot; -1 = no highlight.
static int g_currentWeapon = 0;
static void TouchOverlaySetSelectedWeapon(int w) { g_currentWeapon = w; }

// ── Per-frame outputs (recomputed in TouchOverlayPoll) ─────────────────────
static struct {
    bool  fwd, back, left, right;
    float stickX, stickY; // analog stick, -1..1 each axis (+x right, +y down/back)
    bool  fireDown;
    bool  lookActive;     // a look/fire finger moved this frame (auto-aim backs off)
    bool  jumpPressed;
    bool  reloadPressed;
    bool  firePressed;    // the frame a finger lands on FIRE
    bool  pausePressed;
    int   wpnPressed;     // -1 if no weapon button tapped this frame
    bool  menuConfirm;
    float menuTapX, menuTapY;   // position of menuConfirm tap (-1 if none)
    float lookDX, lookDY;
} g_out;

// ── Layout (recomputed each frame so it tracks resize / rotation) ──────────
static struct {
    Vector2 stickC; float stickR, knobR;
    Vector2 fireC;  float fireR;
    Vector2 jumpC;  float jumpR;
    Vector2 relC;   float relR;
    Vector2 pauseC; float pauseR;
    Vector2 wpnC[4]; float wpnR;
} g_lay;

static void RecalcLayout(void) {
    int W = GetScreenWidth();
    int H = GetScreenHeight();
    float m = (W < H) ? (float)W : (float)H;

    // stickC is the *fallback* base position used to draw a placeholder
    // hint when no thumb is on the left side. While the player is
    // actually touching, the stick anchors to wherever their thumb
    // landed (see TouchOverlayDraw + the CTRL_STICK origin in Poll).
    g_lay.stickC = (Vector2){ W * 0.15f, H * 0.70f };
    g_lay.stickR = m * 0.12f;
    g_lay.knobR  = m * 0.04f;

    g_lay.fireC  = (Vector2){ W * 0.90f, H * 0.70f };
    g_lay.fireR  = m * 0.09f;

    g_lay.jumpC  = (Vector2){ W * 0.76f, H * 0.70f };
    g_lay.jumpR  = m * 0.07f;

    g_lay.relC   = (Vector2){ W * 0.905f, H * 0.475f };    // above FIRE, a thumb's reach up (and clear of the minimap)
    g_lay.relR   = m * 0.06f;

    g_lay.pauseC = (Vector2){ W * 0.045f, H * 0.09f };     // top left: this game's top right is the kill count and the minimap
    g_lay.pauseR = m * 0.04f;

    for (int i = 0; i < 4; i++) {
        g_lay.wpnC[i] = (Vector2){ W * (0.125f + 0.078f*i), H * 0.09f };   // a row beside it, left of the wave banner
    }
    g_lay.wpnR = m * 0.04f;
}

static bool InCircle(Vector2 p, Vector2 c, float r) {
    float dx = p.x - c.x, dy = p.y - c.y;
    return (dx*dx + dy*dy) <= r*r;
}

static int FindSlotByPointId(int id) {
    for (int i = 0; i < MAX_SLOTS; i++)
        if (g_slots[i].active && g_slots[i].pointId == id) return i;
    return -1;
}

static int AllocSlot(void) {
    for (int i = 0; i < MAX_SLOTS; i++)
        if (!g_slots[i].active) return i;
    return -1;
}

// Highest-priority widget under the touch point. Returns CTRL_NONE if the
// touch is in dead space (left half of screen with no widget hit).
static TouchCtrl Classify(Vector2 p, int gs, int *wpnIdx) {
    *wpnIdx = -1;
    if (gs != GS_PLAY) return CTRL_MENU_TAP;     // any tap = ENTER on menu/dead/picker

    if (InCircle(p, g_lay.pauseC, g_lay.pauseR)) return CTRL_PAUSE;
    for (int i = 0; i < 4; i++) {
        if (InCircle(p, g_lay.wpnC[i], g_lay.wpnR)) { *wpnIdx = i; return CTRL_WPN; }
    }
    if (InCircle(p, g_lay.fireC, g_lay.fireR))   return CTRL_FIRE;
    if (InCircle(p, g_lay.jumpC, g_lay.jumpR))   return CTRL_JUMP;
    if (InCircle(p, g_lay.relC, g_lay.relR))     return CTRL_RELOAD;
    // Dynamic stick — anywhere on the left half that didn't hit a
    // higher-priority widget becomes the stick anchor for this touch.
    // The slot's origin (set in Poll() when the slot is allocated)
    // captures the landing point, so the visual base + the dx/dy math
    // both pivot around the player's actual thumb position.
    if (p.x < GetScreenWidth() * 0.5f) return CTRL_STICK;
    // Right half (and not in any other widget) = look pad.
    return CTRL_LOOK;
}

// Bridge-supplied queue of touch BEGIN events that may have completed
// between renders. Drained at the top of Poll so quick taps still fire
// edge-triggered events even when raylib's currentTouchState is already 0.
//
// On real iOS this is defined by ios/rcore_ios.c (the UIKit bridge). For
// the Mac iOS-preview build (CHERNOBYL_IOS_PREVIEW) the bridge doesn't
// exist - quick taps come through the mouse instead - so provide a
// no-op stub here.
#ifdef CHERNOBYL_IOS_PREVIEW
int ChernobylRcoreDrainTouchBegan(int *ids, float *xs, float *ys, int max) {
    (void)ids; (void)xs; (void)ys; (void)max;
    return 0;
}
float ChernobylRcoreContentScale(void) { return 2.f; }
#else
extern int   ChernobylRcoreDrainTouchBegan(int *ids, float *xs, float *ys, int max);
extern float ChernobylRcoreContentScale(void);
#endif

// Mouse-as-touch shim for the Mac preview: raylib's desktop platform
// doesn't synthesize touch events from mouse clicks (it sets
// currentTouchState but never grows pointCount), so the overlay's
// GetTouchPointCount/Position/Id reads return zero on Mac out of the
// box. Route them through GetMousePosition + IsMouseButtonDown instead
// so we can drive the overlay with a single mouse click.
#ifdef CHERNOBYL_IOS_PREVIEW
static int _PreviewTouchCount(void) {
    return IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 1 : 0;
}
static Vector2 _PreviewTouchPos(int idx) { (void)idx; return GetMousePosition(); }
static int _PreviewTouchId(int idx)      { (void)idx; return 0; }
#define _TouchCount _PreviewTouchCount
#define _TouchPos   _PreviewTouchPos
#define _TouchId    _PreviewTouchId
#else
#define _TouchCount GetTouchPointCount
#define _TouchPos   GetTouchPosition
#define _TouchId    GetTouchPointId
#endif

static void TouchOverlayPoll(int gs) {
    RecalcLayout();

    // Reset per-frame outputs - held-state ones are recomputed below;
    // edge-triggered ones default off and fire on touch begin.
    g_out.fwd = g_out.back = g_out.left = g_out.right = false;
    g_out.jumpPressed = g_out.pausePressed = g_out.menuConfirm = false;
    g_out.reloadPressed = g_out.firePressed = false;
    g_out.menuTapX = g_out.menuTapY = -1.f;
    g_out.wpnPressed = -1;
    g_out.lookDX = g_out.lookDY = 0.f;
    g_out.stickX = g_out.stickY = 0.f;
    g_out.fireDown = false;
    g_out.lookActive = false;

    // Drain begin events from the bridge queue. These cover instantaneous
    // taps that don't survive long enough to appear in CORE.Input.Touch.
    {
        int   ids[16];
        float xs[16], ys[16];
        int   nq = ChernobylRcoreDrainTouchBegan(ids, xs, ys, 16);
        for (int k = 0; k < nq; k++) {
            // Already accounted for if a slot got allocated for this pointId
            // earlier this frame (live + queue-replay would double-count).
            if (FindSlotByPointId(ids[k]) >= 0) continue;
            int wi;
            xs[k] /= g_uis; ys[k] /= g_uis;                     // the bridge reports device pixels; the game works in virtual ones
            TouchCtrl c = Classify((Vector2){ xs[k], ys[k] }, gs, &wi);
            switch (c) {
                case CTRL_JUMP:     g_out.jumpPressed = true; break;
                case CTRL_RELOAD:   g_out.reloadPressed = true; break;
                case CTRL_FIRE:     g_out.firePressed = true; break;
                case CTRL_PAUSE:    g_out.pausePressed = true; break;
                case CTRL_WPN:      g_out.wpnPressed = wi; break;
                case CTRL_MENU_TAP:
                    g_out.menuConfirm = true;
                    g_out.menuTapX = xs[k];
                    g_out.menuTapY = ys[k];
                    break;
                default: break;     // ignore - long-held controls covered
                                    // by the live CORE.Input.Touch path below
            }
        }
    }

    int n = _TouchCount();

    // Retire slots whose pointId no longer appears in raylib's touch list.
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!g_slots[i].active) continue;
        bool stillLive = false;
        for (int j = 0; j < n; j++) {
            if (_TouchId(j) == g_slots[i].pointId) { stillLive = true; break; }
        }
        if (!stillLive) {
            g_slots[i].active = false;
            g_slots[i].ctrl   = CTRL_NONE;
        }
    }

    // For each live touch, classify-on-begin or just update position.
    for (int j = 0; j < n; j++) {
        int     pid = _TouchId(j);
        Vector2 pos = _TouchPos(j);
        int slot = FindSlotByPointId(pid);
        if (slot < 0) {
            slot = AllocSlot();
            if (slot < 0) continue;
            int wi;
            TouchCtrl c = Classify(pos, gs, &wi);
            g_slots[slot].active  = true;
            g_slots[slot].pointId = pid;
            g_slots[slot].ctrl    = c;
            g_slots[slot].wpnIdx  = wi;
            g_slots[slot].origin  = pos;
            g_slots[slot].pos     = pos;
            g_slots[slot].prevPos = pos;
            // Edge-triggered events fire the moment a touch begins.
            switch (c) {
                case CTRL_JUMP:     g_out.jumpPressed = true; break;
                case CTRL_RELOAD:   g_out.reloadPressed = true; break;
                case CTRL_FIRE:     g_out.firePressed = true; break;
                case CTRL_PAUSE:    g_out.pausePressed = true; break;
                case CTRL_WPN:      g_out.wpnPressed = wi; break;
                case CTRL_MENU_TAP:
                    g_out.menuConfirm = true;
                    g_out.menuTapX = pos.x;
                    g_out.menuTapY = pos.y;
                    break;
                default: break;
            }
        } else {
            g_slots[slot].prevPos = g_slots[slot].pos;
            g_slots[slot].pos     = pos;
        }
    }

    // Bridge reports positions in device pixels; 3x Pro phones would turn
    // twice as fast as 2x ones on the same physical drag. Normalise look
    // deltas to UIKit points (~1/163 inch) so sensitivity is per-inch.
    float pxPerPt = ChernobylRcoreContentScale();
    if (pxPerPt < 1.f) pxPerPt = 1.f;
    pxPerPt /= g_uis;                                           // (positions here are virtual pixels: real / g_uis)

    // Aggregate held-state outputs from all live slots.
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!g_slots[i].active) continue;
        switch (g_slots[i].ctrl) {
            case CTRL_STICK: {
                float r  = g_lay.stickR;
                float dx = g_slots[i].pos.x - g_slots[i].origin.x;
                float dy = g_slots[i].pos.y - g_slots[i].origin.y;
                float mag = sqrtf(dx*dx + dy*dy);
                // Floating stick: dragging past the rim drags the base along
                // behind the thumb, so reversing direction responds at once
                // instead of having to travel back across the whole circle.
                if (mag > r) {
                    float over = mag - r;
                    g_slots[i].origin.x += dx / mag * over;
                    g_slots[i].origin.y += dy / mag * over;
                    dx = dx * r / mag; dy = dy * r / mag; mag = r;
                }
                // Radial deadzone, then rescale so output ramps 0..1 from
                // the deadzone edge (no jump in speed when leaving it).
                const float DZ = 0.12f;
                float n = mag / r;
                if (n > DZ) {
                    float k = (n - DZ) / (1.f - DZ) / n;
                    g_out.stickX = dx / r * k;
                    g_out.stickY = dy / r * k;
                    // Digital flags kept for callers that only want direction.
                    if (g_out.stickY < -0.40f) g_out.fwd   = true;
                    if (g_out.stickY >  0.40f) g_out.back  = true;
                    if (g_out.stickX < -0.40f) g_out.left  = true;
                    if (g_out.stickX >  0.40f) g_out.right = true;
                }
                break;
            }
            case CTRL_FIRE:
                g_out.fireDown = true;
                // fallthrough - dragging on FIRE also aims, so the player can
                // track a target while shooting with one thumb (standard
                // mobile-FPS behaviour; previously the view froze while firing).
            case CTRL_LOOK: {
                // ~1 point of drag = LOOK_TOUCH_GAIN mouse-pixels downstream.
                // Old path was 3.0 per device pixel = 9 per point on 3x
                // phones, 6 on 2x - twitchy on Pros. 7 * accel lands ~5-6
                // at normal drag speed on every device; g_setSens on top.
                const float LOOK_TOUCH_GAIN = 7.0f;
                float ddx = (g_slots[i].pos.x - g_slots[i].prevPos.x) / pxPerPt;
                float ddy = (g_slots[i].pos.y - g_slots[i].prevPos.y) / pxPerPt;
                // Mild acceleration: slow fine-aim drags get damped, fast
                // flicks get a little extra so 180s don't need two swipes.
                float sp = sqrtf(ddx*ddx + ddy*ddy);
                float accel = 0.65f + fminf(sp, 30.f) * 0.025f;   // 0.65 .. 1.4
                g_out.lookDX += ddx * LOOK_TOUCH_GAIN * accel;
                g_out.lookDY += ddy * LOOK_TOUCH_GAIN * accel;
                if (sp > 0.5f) g_out.lookActive = true;
                break;
            }
            default: break;
        }
    }
}

// Returns true if any live slot's ctrl matches `c` (and, for weapons, wpnIdx).
static bool IsCtrlHeld(TouchCtrl c, int wpnIdx) {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!g_slots[i].active) continue;
        if (g_slots[i].ctrl != c) continue;
        if (c == CTRL_WPN && g_slots[i].wpnIdx != wpnIdx) continue;
        return true;
    }
    return false;
}

static void TouchOverlayDraw(int gs) {
    RecalcLayout();
    if (gs != GS_PLAY) return;     // overlay only in gameplay

    Color baseFill = Fade((Color){200,200,200,255}, 0.20f);
    Color baseLine = Fade((Color){255,255,255,255}, 0.50f);
    Color knobFill = Fade((Color){255,255,255,255}, 0.55f);
    // Pressed-state alphas are roughly double the resting-state alpha so a
    // held button reads at a glance.
    Color fireFill = Fade((Color){200,40,40,255},   IsCtrlHeld(CTRL_FIRE, 0) ? 0.85f : 0.45f);
    Color fireLine = Fade((Color){255,80,80,255},   0.85f);
    Color jumpFill = Fade((Color){80,160,255,255},  IsCtrlHeld(CTRL_JUMP, 0) ? 0.80f : 0.40f);
    Color pauseFill= Fade((Color){80,160,255,255},  IsCtrlHeld(CTRL_PAUSE, 0) ? 0.80f : 0.40f);
    Color wpnLine  = Fade((Color){200,200,200,255}, 0.60f);
    Color textCol  = (Color){235,235,235,220};

    // Stick is dynamic and invisible until touched. Touch anywhere on
    // the left half - the base + knob get drawn at the landing point
    // for that touch's lifetime, then disappear when the finger lifts.
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!g_slots[i].active || g_slots[i].ctrl != CTRL_STICK) continue;
        Vector2 stickBase = g_slots[i].origin;
        DrawCircleV(stickBase, g_lay.stickR, baseFill);
        DrawCircleLinesV(stickBase, g_lay.stickR, baseLine);
        float dx = g_slots[i].pos.x - g_slots[i].origin.x;
        float dy = g_slots[i].pos.y - g_slots[i].origin.y;
        float r  = g_lay.stickR;
        float mag = sqrtf(dx*dx + dy*dy);
        if (mag > r) { dx = dx * r / mag; dy = dy * r / mag; }
        Vector2 knob = (Vector2){ stickBase.x + dx, stickBase.y + dy };
        DrawCircleV(knob, g_lay.knobR, knobFill);
        break;
    }

    // FIRE
    DrawCircleV(g_lay.fireC, g_lay.fireR, fireFill);
    DrawCircleLinesV(g_lay.fireC, g_lay.fireR, fireLine);
    int fz = (int)(g_lay.fireR * 0.7f);
    int fw = MeasureText("FIRE", fz);
    DrawText("FIRE", (int)g_lay.fireC.x - fw/2, (int)g_lay.fireC.y - fz/2, fz, textCol);

    // RELOAD
    DrawCircleV(g_lay.relC, g_lay.relR, Fade((Color){230,190,70,255}, IsCtrlHeld(CTRL_RELOAD, 0) ? 0.80f : 0.38f));
    DrawCircleLinesV(g_lay.relC, g_lay.relR, baseLine);
    { int rz = (int)(g_lay.relR * 0.5f); int rw = MeasureText("RELOAD", rz);
      DrawText("RELOAD", (int)g_lay.relC.x - rw/2, (int)g_lay.relC.y - rz/2, rz, textCol); }

    // JUMP
    DrawCircleV(g_lay.jumpC, g_lay.jumpR, jumpFill);
    DrawCircleLinesV(g_lay.jumpC, g_lay.jumpR, baseLine);
    int jz = (int)(g_lay.jumpR * 0.7f);
    int jw = MeasureText("JUMP", jz);
    DrawText("JUMP", (int)g_lay.jumpC.x - jw/2, (int)g_lay.jumpC.y - jz/2, jz, textCol);

    // PAUSE
    DrawCircleV(g_lay.pauseC, g_lay.pauseR, pauseFill);
    DrawCircleLinesV(g_lay.pauseC, g_lay.pauseR, baseLine);
    int pz = (int)(g_lay.pauseR * 0.9f);
    int pw = MeasureText("II", pz);
    DrawText("II", (int)g_lay.pauseC.x - pw/2, (int)g_lay.pauseC.y - pz/2, pz, textCol);

    // Weapon buttons 1..4. Highlight the currently-selected weapon (set by
    // game.c via TouchOverlaySetSelectedWeapon) and the one being tapped.
    static const char *labels[4] = {"1","2","3","4"};
    for (int i = 0; i < 4; i++) {
        bool isSelected = (i == g_currentWeapon);
        bool isTapped   = IsCtrlHeld(CTRL_WPN, i);
        Color fill = Fade((Color){80,80,80,255},
                          isTapped ? 0.85f : (isSelected ? 0.65f : 0.50f));
        Color ln   = isSelected ? Fade((Color){255,200,80,255}, 0.95f) : wpnLine;
        DrawCircleV(g_lay.wpnC[i], g_lay.wpnR, fill);
        DrawCircleLinesV(g_lay.wpnC[i], g_lay.wpnR, ln);
        int wz = (int)(g_lay.wpnR * 0.9f);
        int ww = MeasureText(labels[i], wz);
        DrawText(labels[i],
                 (int)g_lay.wpnC[i].x - ww/2,
                 (int)g_lay.wpnC[i].y - wz/2, wz, textCol);
    }
}

static bool  TouchOverlayIsForward(void)    { return g_out.fwd; }
static bool  TouchOverlayIsBack(void)       { return g_out.back; }
static bool  TouchOverlayIsLeft(void)       { return g_out.left; }
static bool  TouchOverlayIsRight(void)      { return g_out.right; }
static float TouchOverlayStickX(void)       { return g_out.stickX; }
static float TouchOverlayStickY(void)       { return g_out.stickY; }
static bool  TouchOverlayLookActive(void)   { return g_out.lookActive; }
static float TouchOverlayLookDX(void)       { return g_out.lookDX; }
static float TouchOverlayLookDY(void)       { return g_out.lookDY; }
static bool  TouchOverlayFireDown(void)     { return g_out.fireDown; }
static bool  TouchOverlayJumpPressed(void)  { return g_out.jumpPressed; }
static bool  TouchOverlayReloadPressed(void){ return g_out.reloadPressed; }
static bool  TouchOverlayFirePressed(void)  { return g_out.firePressed; }
static bool  TouchOverlayPausePressed(void) { return g_out.pausePressed; }
static int   TouchOverlayWeaponPressed(void){ return g_out.wpnPressed; }
static bool  TouchOverlayMenuConfirm(void)  { return g_out.menuConfirm; }
static void  TouchOverlayLastTapPos(float *outX, float *outY) {
    if (outX) *outX = g_out.menuTapX;
    if (outY) *outY = g_out.menuTapY;
}
