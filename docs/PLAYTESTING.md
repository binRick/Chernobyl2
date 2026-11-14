# Playtesting

Outside playtests of Chernobyl 2: LIQUIDATOR: where they run, what testers are told, and what came back.

## Playcocola round 1 (2026-11-14)

[Playcocola](https://playcocola.com) is a playtesting site for indie games. Testers record their screen, and their
voice when they think aloud, while they play; the developer watches the sessions and can attach a short survey.

| | |
|---|---|
| Round | [Chernobyl 2: LIQUIDATOR](https://app.playcocola.com/front/playtest_rounds/01m7c400hj3mvxr6rk0xrxteey) (developer sign-in needed) |
| Created | 2026-11-14 |
| Status | **Draft.** Only the developer can see it and Playcocola has not been told. Changing it to Private or Public sends it to their review (about 2 working days); Public is then announced once to every Playcocola playtester. That choice has not been made. |
| Open until | 2026-11-29 (15 days, the most the site allows) |
| Cost | Free. Money is only spent if a Bounty (paid sessions) is created. None has been. |
| Build tested | The browser build at <https://chernobyl2.ximg.app/game/>, commit `a1e19b6`. No download was uploaded. |
| Contact | The developer's contact, set on the Playcocola profile. |
| Cover | `docs/playcocola-cover.png`, 640x360, the game's title screen. |
| Reward | None offered. The reward field is empty. |

Free accounts keep up to 1 hour of recorded video in total, 1 hour per video at most, for 1 week. Watch sessions
soon after they arrive.

### What testers are told

**Description**

> Chernobyl 2: LIQUIDATOR is a first-person wave-survival horror shooter set inside the Chernobyl plant in 1986.
> Enemies climb out of glowing breaches in the floor and come for you in waves; you hold out as long as you can
> with four weapons, and a boss arrives between waves.
>
> What's different: eleven enemy types with different jobs (melee chasers, riflemen, a mutant that throws bolts you
> can dodge, a priest who raises the bodies you leave behind), traps around the plant that you arm and the enemies
> spring, and headshots that count for triple.
>
> How to play: it runs free in the browser, no install or account, at https://chernobyl2.ximg.app/game/ (a desktop
> browser with a keyboard and mouse; it downloads about 86 MB the first time and plays a title intro while it
> loads). Click once for sound, then any key to start.
>
> Controls: WASD move, mouse look, click to fire, R reload, Shift sprint, Space jump, 1 to 4 switch weapon
> (minigun, flamethrower, grenade launcher, Remington), E arm a trap, M mute, Esc menu.

**Playtester expectations**

> Please play as a new player and think out loud.
>
> 1. Watch or skip the intro and start a run.
> 2. Play until you die, then play at least one more run. Try each of the four weapons and arm at least one trap
>    (E, next to the marked machines).
>
> What I most want to know:
> - Was it clear what to do and what was killing you?
> - How did the shooting and movement feel? Which weapon did you like or avoid?
> - Did you notice the traps, and did you understand how to use them?
> - Was it too easy, too hard, or about right, and when did it get that way?
> - Any bugs, slowdowns, sound problems or enemies getting stuck, and which browser you used.
>
> Honest first impressions are worth more than polite ones.

**Other fields**

- Genres: FPS, First-Person, Shooter, Arena Shooter, Horror, Survival, Gore, Singleplayer.
- Minutes per Play Session: 20. Full gameplay minutes: 60 (an estimate; the waves do not end).
- Required checkboxes: "I have read the description and will play in a desktop browser with a keyboard and mouse."
  and "I understand my Play Session is recorded and shared with the developer."
- Shown after the checkboxes: "Play here: https://chernobyl2.ximg.app/game/ . The first load is about 86 MB. Click
  once for sound, press any key at the title to start, click the game to capture the mouse. Esc opens the menu and
  M mutes. The game has blood and gore."
- Shown after the session: "Thank you for playing. Changes are listed at https://chernobyl2.ximg.app/changelog.html
  and scores at https://chernobyl2.ximg.app/scores.html"

Known gap: the site's changelog and play pages are behind the game (no holes, traps, new intro or the fixes of
13 and 14 November), and testers are pointed at the changelog after their session.

### Survey (drafted, not entered)

Asked in the recording client right after a tester clicks Finish; eight questions is the limit. As of 2026-11-14
this exists only here: nothing has been typed into the site's survey editor and the survey is Off. Saving one sends
it to review with the round. Playcocola forbids asking testers for personal data (names, age, location, Discord);
none of these do.

| # | Question | Type | Required |
|---|---|---|---|
| 1 | How much did you enjoy it? | 1 to 5, "Not at all" to "A lot" | yes |
| 2 | How clear was it what to do? | 1 to 5, "Completely lost" to "Always clear" | yes |
| 3 | How was the difficulty? | one of: Too easy, About right, Too hard | yes |
| 4 | Which weapons did you use? | several of: Minigun, Flamethrower, Grenade launcher, Remington | no |
| 5 | Did you use the traps? | one of: Yes, I saw them but didn't understand them, I didn't notice them | yes |
| 6 | Any bugs, slowdowns or sound problems? Which browser were you on? | text | no |
| 7 | Would you play it again? | one of: Yes, Maybe, No | yes |
| 8 | If you could change one thing, what would it be? | text | no |

### Results

None yet. When sessions arrive, record here: the date, how many, what testers did, where they got stuck, the survey
averages, and what was changed in the game because of it (with the commit).

## How the round was set up

The form was filled in through the developer's own signed-in Safari, in a second tab opened in the background, by
JavaScript sent with `osascript` (Safari's "Allow JavaScript from Apple Events"), without bringing Safari to the
front. After the save, the round's edit page and its own page were read back and every field compared with the
text above. Things about that site's form, for next time:

- The cover's file input has the id `playtest_round_cover` and no `name` until the upload is done; the upload
  starts on the input's `input` event, not `change`.
- Required checkboxes are only written to the form when each text box gets real key and blur events. They are
  stored as one hidden value joined with `|||`; the edit page's HTML shows them only after its script runs, so read
  the hidden field or the round's own page to check them.
- Genres are a Choices.js list: an option is picked with a `mousedown` on it. In a background tab each pick takes
  about a second (timers are slowed), so wait for all of them before saving.
- The new-round form can be read without navigating a tab, with a same-origin request from any signed-in
  Playcocola tab.
