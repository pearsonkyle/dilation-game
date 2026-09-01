# Δ

**Time moves when you move.**

A single-file SUPERHOT × *Matrix* × **TRON** homage in the demoscene tradition: one C file,
no assets on disk, no engine. Every texture, level, mesh, sound, and font is
synthesized at startup or runtime. Stripped down to immediate-mode OpenGL 2 and
SDL2, the whole game is one `.c` file and compiles to ~190Kb.

Rendering is deferred through an HDR post stack: the scene draws into a
4x multisampled `RGBA16F` target and comes back out through a five-octave
bloom pyramid, an ACES tonemap, chromatic aberration, vignette, scanlines and
grain. Lighting is GGX with Schlick Fresnel, sphere-light lobes, a hemisphere
ambient and baked per-vertex occlusion; the obsidian floor is a Fresnel-weighted
planar mirror of everything standing on it, lamps included. Three quality tiers
scale the render target, the multisampling, the bloom octaves and the light
count: the game picks one from its own frame rate, and `Q` pins your choice.
The whole chain degrades in steps — no float target falls back to `RGBA8`, no
framebuffer objects at all falls back to tonemapping inline — so it still runs
on a GL 2.1 driver from 2006.

The avatar is one rig shared by the renderer and the simulation: the pistol
arm the laser and the rounds read is the arm you see, the feet are planted
with two-bone IK and take real steps when you turn, the run is a
travel-locked stride with a flight phase, and the katana lives in the left
hand so both weapons stay on the body at all times.

Every sector is generated from the seed on the title screen and checked for
reachability before you jack in; `R` rerolls it into a different building.

![Title screen](screenshots/title.webp)

| Features | Image |
| --- | --- |
| **Locomotion** - Run, dodge, double-jump or wall-kick your way around enemies and incoming fire. | ![Run](screenshots/shot_run_pose.png) |
| **Pistol** - The aim is physical: the gun is a thing your body carries, and the laser pointer is its barrel. Run and the beam swings with your stride; the moment you stop it snaps back onto your look ray and the off hand comes up to support the grip. Rounds go where the barrel points — fire mid-sprint with the gun swung low and the shot goes into the floor; every shot kicks the muzzle. Hold the right mouse button to aim down the sights: the camera pulls in to the eye and the gun comes up to centre. The beam also charges as you move, dictating shot range; enemies highlight when within reach, and the pointer never shrinks below 1m so you always see where the gun is pointed. | ![Lock-on](screenshots/shot_lockon.png) |
| **Katana** - Drawn from the saya on your back with the left hand (`F`): a wind-up over the shoulder and a cut across to the right hip that is live for as long as the blade visibly sweeps. Deflect incoming bullets or take out agents at close range while the pistol stays up. | ![Katana](screenshots/shot_katana_pose.png) |
| **Dodge roll** - `SHIFT` / `CTRL` / `C` rolls you sideways evading enemy fire | ![Dodge roll](screenshots/shot_dodge.png) |
| **Shatter** - Defeating enemies sometimes leaves behind health packs or ammo. | ![Shatter](screenshots/shot_shatter.png) |


## Build & run

Requires SDL2 and OpenGL.

```sh
./build.sh        # clang on macOS, gcc on Linux
./dilation         # play
./dilation --level 2   # jump straight to a sector (0-based)
./dilation --seed 1234 # reseed the procedural levels
./dilation --quality low   # pin a tier: low, medium, high (default: auto)
./dilation --msaa 0    # scene-target multisampling off (default 4x, capped by the tier)
./dilation --seed-sweep 200   # generate 200 seeds x 4 sectors headlessly and check each is winnable
```

Or build by hand:

```sh
gcc -Os dilation.c -o dilation -lSDL2 -lGL -lm        # Linux
clang -Os dilation.c -o dilation -I/opt/homebrew/include -L/opt/homebrew/lib \
  -lSDL2 -framework OpenGL -lm                       # macOS (Homebrew SDL2)
```

## Controls

| Input | Action |
| --- | --- |
| `WASD` | Move (also charges the pistol's range) |
| Mouse | Look |
| `SPACE` | Jump — press again in the air for a double jump; push into a wall and press to kick off it |
| `SHIFT` / `CTRL` / `C` | Dodge roll |
| Left mouse | Fire — shots follow the barrel; stopping settles the aim instantly (range grows the more you move) |
| Right mouse (hold) | Aim down the sights |
| `F` | Katana — kills up close, deflects bullets; presses are buffered, so a press during a cooldown lands the moment it can |
| `1`–`4` / `←` `→` | Select sector (title screen) |
| `R` | Reroll the sector's layout (title screen) |
| `Q` | Cycle the quality tier (low / medium / high) |
| `F11` / `Alt`+`Enter` | Toggle fullscreen |
| `M` | Mute / unmute |
| `ESC` | Sector select / quit |

Clear every agent in a sector to win, then click to jack straight into the next one.

## Regression mode

`./dilation --smoke` runs a fixed, deterministic choreography and writes fifteen PPM
screenshots, then prints `SMOKE OK`. It forces `tscale=1`, fixed RNG seeds and
the high quality tier, so output is byte-stable run-to-run — a cheap
visual/behavioral regression gate for refactors. Add `--strict` to compare every
shot against a `baseline/` directory and fail the run on any difference.

Purely visual randomness (camera shake, the HUD damage glitch) draws from a
second RNG stream, so what is on screen can never perturb the simulation the
gate is measuring.

## License

CC0 / public domain.
