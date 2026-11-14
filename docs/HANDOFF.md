# Where things stand (written 2026-11-14, before a reboot)

A record of the working session of 13 and 14 November, so it can be picked up after a restart. Nothing here is a
plan of record; `PLAN.md` is the roadmap. Delete this file once it has been acted on.

## Not committed yet (in the working tree, built and tested locally)

`src/main.c` and `src/traps.h` hold four changes the owner asked for. None is committed, and none is in the browser
game.

1. **Gun tuning removed.** The keys that moved, turned, scaled and saved the gun in the hand (I J K L U O, - =,
   [ ], ; ', , ., Z X, B, 0, ENTER / F5, P's log) were live in normal play, in the browser too. All gone, with the
   N "orient mode" and its panel. The guns load their saved positions as before. Checked: a still of normal play
   shows the minigun unchanged.
2. **Traps always kill.** `TrapHurt` kills any ordinary enemy outright; the fire hose kills too. **Bosses are
   exempt** (they take each trap's own damage): the builder's call, to keep boss waves from being trivial. One line
   to change. Checked: a hidden run of the plant with every trap armed, 4 traps sprung, 8 enemies dead, no shots.
3. **H / F2 removed** (it wrote the player's position as the next launch's spawn). A spawn file already saved is
   still read.
4. **Dev keys removed on desktop:** the dev readout and its `` ` `` / TAB toggle, V (inspect), F (fly). **G (god
   mode) stays, desktop only**, and tags the run as cheated. The inspect and fly code is still in the source with
   nothing to switch it on.

To ship them: commit, `make web`, copy `build/web/dist/*` to `../ximg-web/chernobyl2-html/game/`, commit and push
that repo, fast-forward the server (see "Deploying" below).

`playcocola-prompt.txt` in the repo root is the owner's instruction file for the playtest round. It is untracked on
purpose.

## Live now

| What | Where | State |
|---|---|---|
| Browser game | <https://chernobyl2.ximg.app/game/> | game repo `9f8aa09`, site repo `0f627071`, server fast-forwarded |
| Intro as one video, with sound | <https://chernobyl2.ximg.app/intro.mp4> | the current cut; does not follow later recuts by itself |
| YouTube | <https://youtu.be/OdMbkCDGKUA> | public, the current cut. The first upload was deleted at the owner's request |
| Playcocola round | <https://app.playcocola.com/front/playtest_rounds/01m7c400hj3mvxr6rk0xrxteey> | **Approved and live** since 2026-11-14 (Public, survey entered); see `docs/PLAYTESTING.md` |

## Waiting on the owner

- **Playcocola:** decided on 2026-11-14: Public, with the eight survey questions entered. Approved the same day.
- **Bosses and traps:** whether a trap should kill a boss outright as well.
- **Site pages:** the changelog and play pages are behind the game (no holes, traps, new intro, or the fixes of
  13 and 14 November; the play page still says the browser build skips the intro). Playtesters are sent to the
  changelog after a session.
- **`tools/intro/fetch_sources.sh`** does not fetch the three videos added to the intro (`soldiers_dance.mp4`,
  `women.mp4`, `disaster2.mp4` in `build/intro_src/`, which is not in git). A fresh clone cannot rebuild this intro.

## What changed in the game on 13 and 14 November (all committed and live unless listed above)

- Enemies route round the breaches (the nav grid leaves their mouths out; the floor probe survives a slab seam at
  x=38 that had cut the reactor side off; an enemy off the grid heads for the nearest cell on it).
- Browser build: no fly key, no god key, `?args=` flags only from localhost; a large "click or press any key for
  sound" prompt on the intro; the intro's files are fetched under the build number.
- Minimap: hostiles only, in red.
- Intro: 73.6 s. Good times gained the soldiers' dance (four shots) and the disco (two shots); the last shot before
  the disaster is a girl's grin slowing and darkening for two seconds while the anthem runs down. Bad times gained
  the vent stack, the torn hall and the helicopter's fall (six seconds, to the ground). The shot of the wall going
  up after the helicopter was cut at the owner's request; every other shot of the first cut is still there.

## Known and not traced

- The headless web smoke test logs two 404s and a run of WebGL "bufferSubData: no buffer" warnings. Not checked
  whether they predate these changes.
- Unverified by hand: the sound prompt on a phone (touch), alt-tab pointer re-lock, the coolant shaders in a
  browser, several trap kinds springing in real play.
- YouTube's check on the current upload reported claimed content; only the first upload's details were read (the
  Soviet anthem recording, no impact on the video).

## Deploying

```
python3 tools/web/pack_web.py        # only if assets changed (the intro counts)
make web
cp -R build/web/dist/* ../ximg-web/chernobyl2-html/game/
# commit and push ../ximg-web, then on the server: fetch, check the incoming files do not
# overlap its local changes, and merge --ff-only origin/main. Verify with curl that the live
# page carries the new build number.
```

Local test: `node tools/web/serve.mjs`, then `node tools/web/smoke.mjs "http://127.0.0.1:8642/index.html?autostart=1&args=--god" 25 out.png`.

Rebuilding the intro: `build/introvenv/bin/python tools/intro/build_intro.py` (about 3 minutes), then the deploy
steps above with `pack_web.py`.

## Driving a signed-in Safari tab

Used for Playcocola and YouTube Studio. `tools/safari/run_js_in_tab.applescript` runs a JavaScript file in the first
tab whose address contains a given text, without bringing Safari forward:

```
osascript tools/safari/run_js_in_tab.applescript "studio.youtube.com" script.js
```

Learned the hard way:

- Open a second tab in the background for the work (`make new tab`, then set the window's current tab back), so a
  tab holding unsaved work is not navigated.
- Wrap calls in a time limit (`perl -e 'alarm 25; exec @ARGV' osascript ...`). A YouTube Studio tab stopped
  answering scripts altogether after a publish and had to be left; a fresh tab worked.
- YouTube Studio text boxes: in a background tab `execCommand('insertText')` fails (no focus). Set `textContent`
  and dispatch `beforeinput` and `input` with `composed: true`; the character counter confirms it took.
- YouTube upload: pass the file into the page in 2 MB base64 pieces, build a `File`, put it on
  `input[name=Filedata]` with a `DataTransfer`, dispatch `change`.
- A YouTube video's file cannot be replaced; a recut is a new upload with a new link.
- Check a publish from outside: `https://www.youtube.com/oembed?url=<link>&format=json` answers 200 when public,
  403 while private or draft, 404 once deleted.
- A current `yt-dlp` needs Python 3.10 or later and `--js-runtimes node`; it was installed in a temporary folder
  that a reboot clears.

The temporary working folder (test scripts, contact sheets, the downloader) lives under `/private/tmp` and does
not survive a reboot. Nothing in it is needed to continue.
