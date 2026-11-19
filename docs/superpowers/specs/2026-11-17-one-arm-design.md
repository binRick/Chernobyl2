# One arm for the guns: minigun and Remington — design (approved 2026-11-17)

## Intent
The owner noticed that every weapon has its own arm model ("check if we can make the arm model the same for each
weapon"): the minigun shows bare forearms and fingerless gloves, the Remington grey sleeves, the grenade launcher and
the claymore blue-grey sleeves and olive gloves. Switching guns changes the player's clothes. After a trial he said:
"go ahead with minigun and remington, use the launcher arm."

## Goals / non-goals / anti-goals
- Goals: the minigun and the Remington show the grenade launcher's arms (same sleeve, glove, texture), through idle,
  fire and reload, on desktop, browser and phone. The arm's picture is held once for every gun that wears it.
- Non-goals: the flamethrower (its rig's bind data is irregular; a first attempt came out stretched: a separate
  attempt, later). A shared skeleton or shared clips. The guns' unused normal / roughness maps.
- Anti-goals: hands that no longer meet the gun; any change to a gun's own motion, framing or timing; an open sleeve
  end showing on screen; hand-edited model files that the repo cannot rebuild.

## Constraints
- Each gun keeps its own skeleton, inverse bind matrices, clips, mesh order (main.c names the minigun's barrel by
  mesh index; `<gun>.glb.parts` and `.glb.ibm` are keyed to the file as it is).
- raylib draws viewmodels with its default shader: texture x vertex colour, no lighting. Only the colour map matters.
- Licences: the arm is DJMaesen's ("Grenadelauncher animated", CC-BY-4.0); the Remington rig is Cransh's (CC-BY-4.0).
  Changes must be stated in docs/LICENSES.md.

## Approaches
1. **Refit the launcher's arm mesh to each gun's own skeleton (picked).** Bone roles are matched by anatomy
   (shoulder, elbow, wrist, five fingers x three); each role gets a frame built from joint positions alone, so the
   rigs' own bone axes do not matter; a vertex is carried by target-frame x stretch x inverse(donor-frame), blended
   by its weights, which then name the target's joints. Trial: correct grips through idle and reload on both guns.
2. One shared arm skeleton with every clip retargeted: the hands stop meeting the guns unless each clip is re-animated.
3. Recolouring the existing arms: the minigun's are bare skin; the meshes and UV layouts differ.

## Decided
- A tool in the repo, `tools/arms/refit_arms.py` (numpy only), rebuilds `assets/minigun.glb` and
  `assets/remington.glb` from the files as they stood at commit b75ba60, read back out of git (the repo rebuilds them
  without carrying a second copy of two 9 MB models); the result is written without the old arm mesh, its three
  textures, or any arm texture at all.
- The game gives a refitted arm the launcher's own colour map at load (as src/mines.h does for the claymore). With no
  launcher to take it from (its file missing) the arms are kept and drawn in one plain colour, not hidden as an
  untextured mesh; the `weapon` debug line says which: `"sleeve":1` given, `-1` plain, `0` the gun wears its own.
- The launcher's sleeve is an open tube cut off a third of the way down the upper arm; each gun's own arm reached
  further up and was closed. So the upper sleeve is drawn out to where that gun's own skin began and closed with a
  shallow dome that wears a patch of the sleeve's own cloth (the Remington's camera looks straight at that end).
- Where a rig carries its forearm on twist bones (the Remington's: three, and no skin on the forearm bone itself), the
  forearm's weight is shared along them; left on the one bone the reload wrings the left wrist. (Found by the final
  review, 2026-11-17.)
- Each rebuilt file names the arm's author and licence beside its own (`asset.extras.arms`).

## The flamethrower (added 2026-11-18)
The owner, having seen the two guns: "flamethrower doesnt show the same arm model. is it possible to make flame
thrower arm consistent?" A trial in the game was shown the same day. Same approach, a third rig (Amine.Elouneg's
"CSO Salamader", a Counter-Strike arm skeleton, CC-BY-4.0); what differs:
- 40 of its 86 joints carry no bind pose (nothing hangs on them), the forearm joints among them: a forearm stands
  where its upper arm's bind and its own rest offset put it (which is where its first twist joint stands), and its
  skin hangs on the four twist joints down it, as the Remington's does on three.
- Its finger chains end in an exporter's stub (0.09 long, pointing across the finger), not at a fingertip: for this
  rig a fingertip is where the skin on the last joint ends, and the launcher's is taken the same way, so the glove's
  tip lands where the bare fingertip was.
- Its arms are two meshes (hands, sleeves) and wear a wristwatch (five more: case, glass, three lights) on the bare
  left forearm. All seven go: the launcher's sleeve is thicker than a bare wrist and swallowed the watch, and a watch
  on one gun only is the change of clothes this work removes. The file is left with the arm and the gun (2 meshes,
  3 images; was 8 and 10). The gun's place on screen did not move (the game's `vmframe` line is the same): its
  length and axis come from the gun's own vertices; which way is "up" is, for this gun, settled by where the arms
  are (its two sides are nearly alike), and a very different arm could turn it over.
- Its own sleeves end open at the shoulder, just under the lens, where the other two guns' are closed. So its new
  sleeves end open too. The first build capped them as the others are: when the look lags (the game lifts the gun
  up the picture as the head turns) the caps rose into view as domes two to six times what its own arms showed and
  hid the gun. Found by the review of 2026-11-18; the rule now is that a sleeve ends as the gun's own did, and
  `tests/arms.sh` draws each gun with its own arms and its new ones under the furthest lag and compares.

## Left to the builder
How far the sleeve is extended, the stretch across the bone, file layout of the kept originals, test thresholds.

## Cost, measured
The two guns' hands are now skinned as the launcher's are: 7588 vertices against 3370 (Remington) and 4540 (minigun),
about 0.1 ms a frame more on the development Mac (0.18 to 0.30 ms and 0.17 to 0.26 ms for the gun's whole draw).
