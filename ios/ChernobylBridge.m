// ChernobylBridge.m - UIKit + CAEAGLLayer + CADisplayLink shell that hosts the
// game's per-frame StepFrame() under iOS.
//
// Lifecycle:
//   1. main() -> UIApplicationMain -> ChernobylAppDelegate
//   2. SceneDelegate creates UIWindow with ChernobylGameViewController as root
//   3. GameVC.viewDidLoad creates EAGLContext (GLES3) + CAEAGLLayer-backed
//      view, wires color+depth renderbuffers + a default FBO, then calls
//      ChernobylGameInit() (renamed from main()).
//   4. CADisplayLink fires render: -> bind FBO -> ChernobylGameStep() -> present.
//   5. AppDelegate.applicationWillTerminate -> ChernobylRcoreRequestClose()
//      then ChernobylGameShutdown().
//
// Touch dispatch routes through ChernobylRcoreTouch{Begin,Move,End} so the game
// sees first-touch as MOUSE_BUTTON_LEFT (current shotgun fire path) and
// multi-touch on the touch APIs (for the future virtual-stick overlay).

#import <UIKit/UIKit.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/EAGLDrawable.h>
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES3/glext.h>
#import <QuartzCore/CAEAGLLayer.h>
#import <objc/runtime.h>

#import "ChernobylBridge.h"

// Device pixels per UIKit point. Lives here, not in rcore_ios.c, because
// rcore is baked into the prebuilt libraylib.a. The touch overlay divides
// look deltas by it so sensitivity is per-inch, not per-pixel.
static float s_contentScale = 1.f;
float ChernobylRcoreContentScale(void) { return s_contentScale; }

// ── EAGL view: a UIView whose backing layer is a CAEAGLLayer ────────────────

@interface ChernobylEAGLView : UIView
@end

@implementation ChernobylEAGLView
+ (Class)layerClass { return [CAEAGLLayer class]; }
@end

// ── Game view controller ────────────────────────────────────────────────────

@interface ChernobylGameViewController : UIViewController
{
    EAGLContext   *_glContext;
    CADisplayLink *_displayLink;
    GLuint         _colorRbo;
    GLuint         _depthRbo;
    GLuint         _defaultFbo;
    int            _backingW;
    int            _backingH;
    BOOL           _gameInitialized;
}
@end

@implementation ChernobylGameViewController

- (void)loadView
{
    // Manual UIView so we can use ChernobylEAGLView (CAEAGLLayer-backed). The
    // default loadView gives a plain UIView, which can't host GLES rendering.
    self.view = [[ChernobylEAGLView alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.view.backgroundColor = [UIColor blackColor];
    self.view.contentScaleFactor = [UIScreen mainScreen].nativeScale;
    self.view.multipleTouchEnabled = YES;
    s_contentScale = (float)self.view.contentScaleFactor;

    CAEAGLLayer *layer = (CAEAGLLayer *)self.view.layer;
    layer.opaque = YES;
    layer.contentsScale = self.view.contentScaleFactor;
    layer.drawableProperties = @{
        kEAGLDrawablePropertyRetainedBacking: @NO,
        kEAGLDrawablePropertyColorFormat:     kEAGLColorFormatRGBA8
    };
}

- (void)viewDidLoad
{
    [super viewDidLoad];

    _glContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!_glContext)
    {
        NSLog(@"Chernobyl: failed to create GLES3 context");
        return;
    }
    [EAGLContext setCurrentContext:_glContext];
    ChernobylRcoreSetEaglContext((__bridge void *)_glContext);

    [self createFramebuffer];

    // Storage base path for SaveMusicVol / config writes - use Documents dir
    // so it survives app reinstalls (well, doesn't, but at least it's writable
    // unlike the .app bundle).
    NSString *docs = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    ChernobylRcoreSetStorageBasePath(docs.UTF8String);

    // Hand pre-init dimensions to rcore so the game's InitWindow() arguments
    // (which we'll rewrite to use these on iOS) match what we created here.
    ChernobylRcoreSetScreenSize(_backingW, _backingH);

    ChernobylGameInit();
    _gameInitialized = YES;

    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(renderFrame:)];
    _displayLink.preferredFramesPerSecond = 60;
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    // Re-create the renderbuffers if the drawable size changed (rotation,
    // safe-area changes). Bail if nothing to update.
    CAEAGLLayer *layer = (CAEAGLLayer *)self.view.layer;
    CGSize sz = layer.bounds.size;
    int newW = (int)(sz.width  * layer.contentsScale);
    int newH = (int)(sz.height * layer.contentsScale);
    if ((newW == _backingW && newH == _backingH) || newW == 0 || newH == 0) return;
    [EAGLContext setCurrentContext:_glContext];
    [self destroyFramebuffer];
    [self createFramebuffer];
    if (_gameInitialized) ChernobylRcoreSetScreenSize(_backingW, _backingH);
}

- (void)createFramebuffer
{
    CAEAGLLayer *layer = (CAEAGLLayer *)self.view.layer;

    glGenRenderbuffers(1, &_colorRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, _colorRbo);
    [_glContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH,  &_backingW);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_backingH);

    glGenRenderbuffers(1, &_depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, _depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, _backingW, _backingH);

    glGenFramebuffers(1, &_defaultFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _defaultFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,        GL_RENDERBUFFER, _colorRbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _depthRbo);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        NSLog(@"Chernobyl: framebuffer incomplete: 0x%04x", status);

    ChernobylRcoreSetFbo(_defaultFbo, _colorRbo);
}

- (void)destroyFramebuffer
{
    if (_defaultFbo) { glDeleteFramebuffers(1, &_defaultFbo); _defaultFbo = 0; }
    if (_colorRbo)   { glDeleteRenderbuffers(1, &_colorRbo);  _colorRbo   = 0; }
    if (_depthRbo)   { glDeleteRenderbuffers(1, &_depthRbo);  _depthRbo   = 0; }
}

- (void)renderFrame:(CADisplayLink *)link
{
    if (!_gameInitialized) return;
    [EAGLContext setCurrentContext:_glContext];
    glBindFramebuffer(GL_FRAMEBUFFER, _defaultFbo);

    ChernobylGameStep();

    // Present the color renderbuffer that's bound to the CAEAGLLayer. The
    // game's EndDrawing already called rlDrawRenderBatchActive + glFlush
    // implicitly via raylib, so the GPU side is settled.
    glBindRenderbuffer(GL_RENDERBUFFER, _colorRbo);
    [_glContext presentRenderbuffer:GL_RENDERBUFFER];
}

// ── Touch dispatch ───────────────────────────────────────────────────────────
// Each UITouch gets a fresh monotonically-increasing id stamped on it at
// Begin. We used to hash the UITouch* pointer, but UIKit recycles touch
// objects at the same address - lift the look finger and tap FIRE within a
// frame and the overlay saw the "same" touch, kept its LOOK classification,
// and the shot never fired.
static char kTouchIdKey;
static int  s_nextTouchId = 1;

static int TouchID(UITouch *t, bool isBegin)
{
    NSNumber *n = objc_getAssociatedObject(t, &kTouchIdKey);
    if (isBegin || !n) {
        if (s_nextTouchId >= 0x7FFFFFFF) s_nextTouchId = 1;
        n = @(s_nextTouchId++);
        objc_setAssociatedObject(t, &kTouchIdKey, n, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return n.intValue;
}

- (void)dispatchTouches:(NSSet<UITouch *> *)touches mode:(int)mode
{
    CGFloat scale = self.view.contentScaleFactor;
    for (UITouch *t in touches)
    {
        CGPoint p = [t locationInView:self.view];
        float x = (float)(p.x * scale);
        float y = (float)(p.y * scale);
        int id = TouchID(t, mode == 0);
        if      (mode == 0) ChernobylRcoreTouchBegin(id, x, y);
        else if (mode == 1) ChernobylRcoreTouchMove (id, x, y);
        else                ChernobylRcoreTouchEnd  (id);
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event     { [self dispatchTouches:touches mode:0]; }
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event     { [self dispatchTouches:touches mode:1]; }
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event     { [self dispatchTouches:touches mode:2]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self dispatchTouches:touches mode:2]; }

// Landscape only - the HUD assumes a wider-than-tall viewport.
- (BOOL)shouldAutorotate { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    return UIInterfaceOrientationMaskLandscape;
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
// Swipes that start near a screen edge (stick thumb sliding down, look thumb
// near the top) otherwise get eaten by Control Centre / Notification Centre /
// home-indicator gestures, which cancels the touch and drops the stick
// mid-strafe. Deferring makes iOS require a second swipe for system UI.
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }

@end

// ── Scene delegate ───────────────────────────────────────────────────────────
// iOS 27 traps (EXC_BREAKPOINT in _UIApplicationEvaluateRuntimeIssueFor
// NoSceneLifecycleAdoption) at launch for apps linked against the iOS 27 SDK
// that still create their window in the app delegate. The window lives here
// now; Info.plist's UIApplicationSceneManifest names this class.

@interface ChernobylSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation ChernobylSceneDelegate

- (void)scene:(UIScene *)scene
    willConnectToSession:(UISceneSession *)session
                 options:(UISceneConnectionOptions *)connectionOptions
{
    if (![scene isKindOfClass:[UIWindowScene class]]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [[ChernobylGameViewController alloc] init];
    [self.window makeKeyAndVisible];
}

@end

// ── App delegate ─────────────────────────────────────────────────────────────

@interface ChernobylAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation ChernobylAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    return YES;
}

- (void)applicationWillTerminate:(UIApplication *)application
{
    ChernobylRcoreRequestClose();
    ChernobylGameShutdown();
}

@end

// ── Entry point ──────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([ChernobylAppDelegate class]));
    }
}
