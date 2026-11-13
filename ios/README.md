# Chernobyl 2 - iPhone build

Laid out as `../Chernobyl`'s iOS port, and reusing it: the UIKit bridge, the prebuilt raylib for
iOS (its `rcore_ios.c` platform layer is compiled into that library), and the touch overlay.

    cp -R ../Chernobyl/vendor-ios/{include,lib-iphoneos,lib-iphonesimulator} vendor/raylib-ios/   # once (gitignored)
    python3 tools/web/pack_web.py          # the phone ships the browser build's asset set (build/web/fs)
    cd ios && xcodegen generate && cd ..   # after editing ios/project.yml
    xcodebuild build -project ios/Chernobyl2.xcodeproj -scheme Chernobyl2 -configuration Release \
        -destination 'platform=iOS Simulator,name=<a simulator>' -derivedDataPath build/ios CODE_SIGNING_ALLOWED=NO
    xcrun simctl boot <a simulator>; xcrun simctl install <it> build/ios/Build/Products/Release-iphonesimulator/Chernobyl2.app
    xcrun simctl launch <it> app.ximg.chernobyl2

What is where:

- `ChernobylBridge.{h,m}` - app delegate, scene, GL view, display link, touch dispatch (from ../Chernobyl).
- `C2Platform.m` - bundle and Documents paths, and the HTTP request the leaderboard uses.
- `../src/touch.h` - the on-screen controls (from ../Chernobyl's `touch_overlay.c`, plus RELOAD).
- `../src/main.c`, under `C2_IOS` - the virtual 720-line screen, the key / mouse answers taken from
  the overlay, aim assist, and the entry points the bridge calls (`ChernobylGameInit / Step / Shutdown`).
- `../src/scores.h`, under `C2_IOS` - the on-screen keys for leaderboard initials.

Not on the phone yet: the title intro (the desktop's is a blocking loop; the browser's is played by the page).
