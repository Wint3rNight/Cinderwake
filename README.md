# Cinderwake

A one-on-one souls-like boss fight in Unreal Engine 5.8, written in C++. You play a fast sword fighter against a 3.7 m hovering executioner with a hammer. It's a portfolio project and a work in progress: the player's whole kit, the combat core and the boss's foundation with his first attack are built. His other attacks, his decision system and the second phase come next.

## How it's put together

The combat rules are plain C++ with no engine types: the player's action rules, the boss's state machine, health, stamina, poise, attack timing and hit resolution. That keeps them unit testable without loading a level. Thin Unreal components run them on the actors.

- Attacks are data assets timed in real seconds and mapped onto measured points in each animation clip. The montage is paused and posed from the gameplay clock every tick, so hit windows, hit-stop and animation can't drift apart. Weapons hit through spheres swept between two sockets.
- Every hit goes through one function, `ResolveCombatHit`, in a fixed order: dead, immune, already hit, dodged, perfect guard, blocked, then health and poise.
- The boss's state belongs to `FBossStateMachine`. Requests carry a priority and are checked against a transition table, a stagger earned under armour waits for a safe point, and states time out so he can't get stuck.
- The HUD is drawn on the canvas from C++, and input is read from the player controller in one function.

Good places to start: `Source/BossTest/Combat/HitResolver.cpp`, `Combat/AttackComponent.cpp`, `Player/PlayerActions.cpp` and `Boss/BossStateMachine.cpp`.

## Running it

The characters are Epic Games' free Paragon assets, which can't be redistributed, so they aren't in this repo. Add Paragon: Sevarog and Paragon: Feng Mao from Fab to the project (they land in `Content/ParagonSevarog` and `Content/ParagonFengMao`), then open `BossTest.uproject` in Unreal 5.8 and let it build the module. The arena is `/Game/BossTest/BossArena`.

On Linux, build with the editor closed:

```
<Engine>/Engine/Build/BatchFiles/Linux/Build.sh BossTestEditor Linux Development -Project="$PWD/BossTest.uproject"
```

Three of Feng Mao's moves are borrowed from Paragon Kwang and Aurora, retargeted and mirrored onto him. Those clips are derived from Fab content too, so they aren't in the repo either: add Paragon: Kwang and Paragon: Aurora as well, build, then run `Scripts/build_borrowed_clips.py` once (the header says how). Without them the charged heavy, the second heavy and the third light won't play.

Controls: WASD and the mouse, left click for a light attack, right click (or F) for a heavy and hold it to charge, Space to dodge, Shift to sprint, Q to guard (tap it just before a hit lands for a perfect guard), E to heal, Tab to lock on. H shows the debug keys.

## Tests

Unit tests cover the rules. Smoke tests load the arena, start a play session and drive it with simulated key presses: the boss lands a swing, a guard blocks it, a timed guard is a perfect guard, holding heavy charges it. Run them from the editor's Session Frontend under `BossTest`, or headless:

```
UnrealEditor-Cmd BossTest.uproject -ExecCmds="Automation RunTests BossTest.;Quit" -unattended -nullrhi
```

## Licence

The code is MIT (see `LICENSE`). The Paragon characters belong to Epic Games under the Fab licence.
