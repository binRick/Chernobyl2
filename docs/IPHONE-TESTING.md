# Physical iPhone testing

## Session of 23 November 2026

Tested Chernobyl 2 on the owner's physical iPhone 14, running iOS 27.2.1. Xcode 27.2, CoreDevice and AXe supplied launch, screenshots and touch input. Screenshots were inspected after consequential actions. The phone initially needed its owner to unlock it.

The final gameplay fixes are in commit `6f5efa4`. Release commit `97222e0` sets version 1.0.1, build 4. That release was uploaded and reached Waiting for Review, with automatic release after approval. This records the observed submission state on 23 November.

### Verified through player controls

| Check | Observation |
|---|---|
| Title and launch | Tap to Begin opened the arena. Starting again after Quit to Title also worked. |
| Movement | A left-side drag advanced along the corridor. |
| Looking | A right-side horizontal drag turned the view. |
| Jump | The viewpoint rose and settled after landing. |
| Weapons | All five slots selected the expected weapon. Minigun, flamethrower, grenade launcher and Remington consumed ammo when fired. |
| Reload | Minigun firing reduced the magazine from 300 to 287. Reload restored 13 rounds from reserve. |
| Claymore | In the `--no-enemies --mines --seed 7` fixture, Fire increased the laid count and it subsequently became armed. |
| Damage and death | Ordinary enemies reduced player health. Death displayed run statistics and the initials keyboard. Skip started a fresh run at full health. |
| Manual pause and resume | Options opened and Resume returned to gameplay. |
| Home interruption | On the final build, Home followed by foregrounding returned to Options. A further five-second observation showed the run held frozen. |
| Restart after sustained play | From Wave 3, Restart Run reset the wave, kills, health and ammo without exiting the app. |
| Quit after sustained play | Quit to Title returned to the title card. An ordinary launch was restored after testing. |

### Progression and sustained combat

The longer run used `CHERNOBYL_ARGS="--bot --god --seed 7"` and `CHERNOBYL_INTRO_AUTO="1,3"`. These options automate play and prevent death. They establish progression and runtime coverage under that fixture.

- The Head Chef appeared, was defeated, and Wave 2 opened.
- The Archimandrite appeared in Wave 2, was defeated, and Wave 3 opened.
- The run reached 65 kills and five headshots. The Wave 3 Head Chef appeared before the run was stopped for restart and quit checks.
- Restart Run and Quit to Title succeeded after this run.
- The phone was then relaunched without test environment variables and left at the title.

### Failures found and corrected

| Failure | Cause and correction | Verification |
|---|---|---|
| Wave 1 stalled at 0 hostiles after 25 kills | The iOS package copied the initial web asset set and omitted the deferred enemies. The build now merges both asset sets and checks that boss models exist. | All 40 deferred files matched the packed source files. Both boss transitions worked on the phone. The distribution archive contained all eleven enemy models. |
| Quit stayed in Options | The iOS frame loop ignored the quit flag. It now returns to the native title card. | Touch Quit to Title, then begin again. Repeated after sustained combat. |
| Restart returned to Home on the first full-asset rebuild | Restart reloaded the entire arena. The final fix resets the run on the loaded default map. The precise OS termination reason was unconfirmed. | Restart restored a fresh run through touch, including after reaching Wave 3. |
| Returning from Home resumed combat immediately | Added scene interruption handling that opens the pause menu. | Final build returned paused and stayed frozen during the five-second check. |
| Boss bar overlapped weapon buttons | Moved the iPhone boss bar below the weapon row. | Inspected the Head Chef and Archimandrite HUD on the phone. |
| Desktop-only Options entries on iPhone | Removed Fullscreen and the ESC hint from the iPhone menu. Quit is labelled Quit to Title. | Inspected the deployed Options menu. |

The desktop build compiled and `tests/run-again.sh` passed after the shared run-reset changes. That test verified fresh trap state across repeated deaths and new runs.

### Performance and remaining gaps

Combat HUD samples were generally around 19–21 FPS, with some earlier combat captures around 15–26 FPS. Enemy-free control checks were around 30 FPS. These are sampled in-game readings. Frame-time percentiles were unverified. An Instruments capture produced no per-frame interval rows for this OpenGL ES app.

The following still need coverage when relevant to the next change.

- Simultaneous independent movement, aim and fire contacts. This session primarily used single-finger actions.
- Backward movement, strafing, vertical look limits and rotation changes.
- Human assessment of control feel, sound, haptics, thermal comfort and text readability.
- Leaderboard submission. Skip was used to avoid posting test scores.
- A manual contextual trap activation with an independently verified effect.
- Longer normal-damage survival and progression beyond Wave 3.

### Repeating the checks

Use the local `physical-iphone-testing` skill at `~/.codex/skills/physical-iphone-testing/SKILL.md`. It was updated from this session with controls, UI, interruption, progression and timeout guidance.

1. Identify the physical device with `xcrun devicectl list devices` and `axe doctor`. Read the current screen before sending input.
2. Build and install through the normal iOS build path. Verify deferred enemy assets in the resulting bundle.
3. Exercise the affected flow through touch and capture the observable result.
4. For progression, record the starting wave, boss, clear and loaded next wave. Record every launch override.
5. Repeat Restart Run and Quit to Title after sustained gameplay, then begin another game.
6. Stop at an unfamiliar screen or failed input/capture. Poll unfinished command sessions and obtain a fresh frame before retrying. CoreDevice and AXe timed out intermittently during this session.
7. Clear test environment variables, stop capture jobs and identify the final screen left on the phone.

Session screenshots and OCR files were kept in the local temporary directory `/var/folders/ys/mm4wydx14xj30myr_jjnk1240000gn/T/tmp.QjxbnbO1Xp`. That directory may be cleaned by macOS. A comparison image was saved to `~/Downloads/Chernobyl2-iPhone14-progression-2026-11-23.jpg`. This document retains the results for future sessions.
