// C2Intro.h -- the title intro on the phone (C2Intro.m).
#import <UIKit/UIKit.h>

@interface C2IntroView : UIView
// Starts the intro over host and returns it; nil if the bundle has no intro (then there is none).
+ (instancetype)introOver:(UIView *)host;
// The game has loaded: the title may now be left with a tap, and done runs once it has faded out.
- (void)gameLoaded:(void (^)(void))done;
@end
