// macicon.m - set the macOS Dock / Cmd-Tab icon at runtime.
//
// raylib's SetWindowIcon is a no-op on macOS (GLFW ignores it), and we ship no
// .app bundle, so the switcher/Dock icon defaults to the generic executable.
// Setting NSApplication's applicationIconImage overrides it for this process.
// Call after InitWindow (so the shared NSApplication exists). MRC build (no
// -fobjc-arc), hence the explicit autorelease.
#import <Cocoa/Cocoa.h>

void MacSetDockIcon(const char *path){
    @autoreleasepool {
        NSString *p = [NSString stringWithUTF8String:path];
        NSImage *img = [[[NSImage alloc] initWithContentsOfFile:p] autorelease];
        if (img) [NSApplication sharedApplication].applicationIconImage = img;
    }
}
