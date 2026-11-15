// C2Intro.m -- the title intro on the phone.
//
// The desktop game plays its montage itself, in a loop of its own (src/intro.h); the browser's page
// plays it as a <video> with the music beside it (web/shell.html). The phone does as the page does,
// with the page's files (build/web/intro, copied into the bundle as intro/): the montage over its
// music, then the CHERNOBYL 2 title held, the music playing on, until a tap. A tap during the
// montage jumps to the title (and the music to the stamp). The game loads underneath meanwhile;
// until it has, the title says LOADING where TAP TO BEGIN will be.
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import "C2Intro.h"

@implementation C2IntroView
{
    AVPlayer      *_video;
    AVPlayerLayer *_videoLayer;
    AVAudioPlayer *_music;
    UIImageView   *_title;
    UILabel       *_prompt;
    NSTimer       *_tick;
    int            _stage;          // 1 montage, 2 title, 3 leaving
    CFTimeInterval _stageAt;
    double         _tStamp, _tEnd;
    BOOL           _loaded;
    double         _auto[2];        // CHERNOBYL_INTRO_AUTO="skipAt,startAt": dev/test, taps by itself (as src/intro.h)
    CFTimeInterval _began;
    void         (^_done)(void);
}

+ (instancetype)introOver:(UIView *)host
{
    NSBundle *b = [NSBundle mainBundle];
    NSString *v = [b pathForResource:@"intro" ofType:@"mp4" inDirectory:@"intro"];
    NSString *t = [b pathForResource:@"intro_title" ofType:@"jpg" inDirectory:@"intro"];
    if (!v || !t) return nil;                                  // built without the intro: straight to the game
    C2IntroView *me = [[C2IntroView alloc] initWithFrame:host.bounds];
    me.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [host addSubview:me];
    [me startWithVideo:v title:t
                 music:[b pathForResource:@"intro_music" ofType:@"mp3" inDirectory:@"intro"]
                 times:[b pathForResource:@"intro_times" ofType:@"txt" inDirectory:@"intro"]];
    return me;
}

- (void)startWithVideo:(NSString *)v title:(NSString *)t music:(NSString *)m times:(NSString *)x
{
    self.backgroundColor = [UIColor blackColor];
    self.multipleTouchEnabled = NO;
    _tStamp = 55.0; _tEnd = 59.0;                              // from the build (intro_times.txt), as the desktop reads them
    NSString *txt = x ? [NSString stringWithContentsOfFile:x encoding:NSUTF8StringEncoding error:nil] : nil;
    for (NSString *line in [txt componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]]) {
        if ([line hasPrefix:@"stamp "]) _tStamp = [[line substringFromIndex:6] doubleValue];
        if ([line hasPrefix:@"end "])   _tEnd   = [[line substringFromIndex:4] doubleValue];
    }

    _title = [[UIImageView alloc] initWithImage:[UIImage imageWithContentsOfFile:t]];
    _title.contentMode = UIViewContentModeScaleAspectFit;
    _title.hidden = YES;
    [self addSubview:_title];

    _video = [AVPlayer playerWithURL:[NSURL fileURLWithPath:v]];
    _video.muted = YES;                                        // the picture has no sound of its own
    _video.actionAtItemEnd = AVPlayerActionAtItemEndPause;
    _videoLayer = [AVPlayerLayer playerLayerWithPlayer:_video];
    _videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;
    [self.layer addSublayer:_videoLayer];

    _prompt = [[UILabel alloc] init];
    _prompt.font = [UIFont fontWithName:@"Courier-Bold" size:20] ?: [UIFont boldSystemFontOfSize:20];
    _prompt.textColor = [UIColor colorWithWhite:0.93 alpha:1];
    _prompt.textAlignment = NSTextAlignmentCenter;
    _prompt.backgroundColor = [UIColor colorWithWhite:0 alpha:0.8];      // a dark plate under it: the emblem's handle runs through here
    _prompt.layer.cornerRadius = 8; _prompt.layer.masksToBounds = YES;
    _prompt.layer.borderWidth = 1; _prompt.layer.borderColor = [UIColor colorWithRed:0.59 green:0.09 blue:0.07 alpha:0.8].CGColor;
    _prompt.hidden = YES;
    [self addSubview:_prompt];

    if (m) {
        _music = [[AVAudioPlayer alloc] initWithContentsOfURL:[NSURL fileURLWithPath:m] error:nil];
        _music.numberOfLoops = -1;
        [_music prepareToPlay];
    }

    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:self selector:@selector(appAway)  name:UIApplicationWillResignActiveNotification object:nil];
    [nc addObserver:self selector:@selector(appBack)  name:UIApplicationDidBecomeActiveNotification  object:nil];

    _auto[0] = _auto[1] = -1;
    { const char *au = getenv("CHERNOBYL_INTRO_AUTO"); if (au) sscanf(au, "%lf,%lf", &_auto[0], &_auto[1]); }
    if (getenv("CHERNOBYL_MUTE")) _music.volume = 0;           // test runs are silent
    _stage = 1; _stageAt = _began = CACurrentMediaTime();
    [_music play];
    [_video play];
    __weak C2IntroView *weak = self;
    _tick = [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *tm) { [weak tick]; }];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    CGRect r = self.bounds;
    _title.frame = r;
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _videoLayer.frame = r;
    [CATransaction commit];
    _prompt.frame = CGRectMake(r.size.width * 0.5 - 120, r.size.height * 0.905 - 18, 240, 36);
}

- (void)tick
{
    for (int i = 0; i < 2; i++) if (_auto[i] >= 0 && CACurrentMediaTime() - _began >= _auto[i]) {
        if (i == 1 && !(_stage == 2 && _loaded && CACurrentMediaTime() - _stageAt > 1.0)) continue;   // (the start waits for the loading: a slow simulator)
        _auto[i] = -1; [self tap]; }
    if (_stage == 1) {
        double c = _music.isPlaying ? _music.currentTime : CMTimeGetSeconds(_video.currentTime);
        double vt = CMTimeGetSeconds(_video.currentTime), vd = CMTimeGetSeconds(_video.currentItem.duration);
        // the picture follows the music (the game loading underneath can hold the main thread, not either of these)
        if (_music.isPlaying && fabs(vt - c) > 0.25 && !isnan(vd) && c < vd - 0.3)
            [_video seekToTime:CMTimeMakeWithSeconds(c, 600) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        if (c >= _tEnd - 0.05 || (!isnan(vd) && vt >= vd - 0.05)) [self toTitle:NO];
    } else if (_stage == 2) {
        double t = CACurrentMediaTime() - _stageAt;
        _prompt.hidden = (t < 0.9);
        _prompt.text = _loaded ? @"TAP TO BEGIN" : @"LOADING...";
        _prompt.alpha = _loaded ? 0.55 + 0.45 * sin(CACurrentMediaTime() * 3.2) : 0.8;
    }
}

- (void)toTitle:(BOOL)skipped
{
    if (_stage != 1) return;
    if (skipped && _music) _music.currentTime = _tStamp - 0.05;      // straight to the stamp
    [_video pause];
    _videoLayer.hidden = YES; _title.hidden = NO;
    _stage = 2; _stageAt = CACurrentMediaTime();
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self tap]; }

- (void)tap
{
    double t = CACurrentMediaTime() - _stageAt;
    if (_stage == 1) { if (t > 1.5) [self toTitle:YES]; }
    else if (_stage == 2 && _loaded && t > 1.0) [self leave];
}

- (void)gameLoaded:(void (^)(void))done
{
    _done = [done copy];
    _loaded = YES;
    _stageAt = MAX(_stageAt, CACurrentMediaTime() - 0.65);     // a tap queued up during the loading is not the start
}

- (void)leave
{
    _stage = 3;
    [_music setVolume:0 fadeDuration:1.2];
    [UIView animateWithDuration:1.2 animations:^{ self->_title.alpha = 0; self->_prompt.alpha = 0; }
                     completion:^(BOOL f) {
        [self->_tick invalidate]; self->_tick = nil;
        [self->_music stop]; self->_music = nil; self->_video = nil;
        [[NSNotificationCenter defaultCenter] removeObserver:self];
        void (^done)(void) = self->_done; self->_done = nil;
        [self removeFromSuperview];
        if (done) done();
    }];
}

- (void)appAway { [_music pause]; [_video pause]; }
- (void)appBack { if (_stage == 1) { [_music play]; [_video play]; } else if (_stage == 2) [_music play]; }

@end
