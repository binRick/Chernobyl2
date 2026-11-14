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

- `C2Intro.{h,m}` - the title intro: the browser's video and music (`build/web/intro`), played over the loading, then the title held until a tap.

The phone is the easy version: `PLAYER_DMG_K` halves what hurts the player, and the auto-aim (`Frame`, under `C2_IOS`) pulls the view onto the nearest enemy in sight.

## App Store

App "Chernobyl 2", app ID `6834835921`, bundle `app.ximg.chernobyl2`, SKU `chernobyl2-ios-001`, team `4L7XW5NPCX`.

    ASC_KEY_ID=A3TP273HH7 ASC_ISSUER_ID=<issuer, in ../Chernobyl/CLAUDE.md> BUILD=<n> scripts/ios-appstore.sh

packs the assets, archives, exports, validates and uploads (`DRY_RUN=1` stops before the upload). `BUILD` goes up
with every upload; the store version is `MARKETING_VERSION` in `project.yml`. The listing (text, screenshots,
category, price, age rating 18+, review contact) was set through the App Store Connect API; App Privacy
("Gameplay Content", not linked, no tracking) and the age rating's last step only exist on the website.

Stills for the store page come from a simulator on autopilot (it draws about 2 frames a second, so let it run):

    SIMCTL_CHILD_CHERNOBYL_CLEAN_HUD=1 SIMCTL_CHILD_CHERNOBYL_MUTE=1 SIMCTL_CHILD_CHERNOBYL_INTRO_AUTO="12,20" \
    SIMCTL_CHILD_CHERNOBYL_ARGS="--kite --autofire --god --mute --seed 7" xcrun simctl launch <sim> app.ximg.chernobyl2

`CHERNOBYL_ARGS` gives the phone build the desktop's flags; `CHERNOBYL_INTRO_AUTO="skipAt,startAt"` taps the intro.
