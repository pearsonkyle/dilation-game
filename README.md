# Δ

**Time moves when you move.**

A single-file SUPERHOT × *Matrix* × **TRON** homage in the demoscene tradition: one C file,
no assets on disk, no engine. Every texture, level, mesh, sound, and font is
synthesized at startup or runtime. Stripped down to immediate-mode OpenGL 2 and
SDL2, the whole game is one `.c` file and compiles to under ~150Kb.

![Title screen](screenshots/title.webp)

| Features | Image |
| --- | --- |
| **Locomotion** - Run, dodge, double-jump or wall-kick your way around enemies and incoming fire. | ![Run](screenshots/shot_run_pose.png) |
| **Pistol** - The laser pointer indicates where you're aiming but also charges as you move and dictates the distance you can shoot. Enemies will be highlighted when within range. | ![Lock-on](screenshots/shot_lockon.png) |
| **Katana** - Deflect incoming bullets with your katana or take out enemies at close range. | ![Katana](screenshots/shot_katana_pose.png) |
| **Dodge roll** - `SHIFT` / `CTRL` / `C` rolls you sideways evading enemy fire | ![Dodge roll](screenshots/shot_dodge.png) |
| **Shatter** - Defeating enemies sometimes leaves behind health packs or ammo. | ![Shatter](screenshots/shot_shatter.png) |
| **SDF blend-shell figures** - Every character body is capsule primitives merged into one draw call; a vertex shader snaps the merged mesh onto their combined smooth-min SDF, so joints have no seams, lighting flows across them (normals from the field gradient), and part colors blend by proximity. The OVERLORD below is 18 blended shapes in a single call. | ![Boss](screenshots/shot_boss.png) |


## Build & run

Requires SDL2 and OpenGL.

```sh
./build.sh        # clang on macOS, gcc on Linux
./dilation         # play
./dilation --level 2   # jump straight to a sector (0-based)
./dilation --seed 1234 # reseed the procedural levels
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
| `SPACE` | Jump — press again in the air for a double jump; into a wall to kick off |
| `SHIFT` / `CTRL` / `C` | Dodge roll |
| Left mouse | Fire (range grows the more you move) |
| Right mouse | Katana — kills up close, deflects bullets |
| `1`–`4` / `←` `→` | Select sector (title screen) |
| `ESC` | Sector select / quit |

Clear every agent in a sector to win.

## Regression mode

`./dilation --smoke` runs a fixed, deterministic choreography and writes ten PPM
screenshots, then prints `SMOKE OK`. It forces `tscale=1` and fixed RNG seeds,
so output is byte-stable run-to-run — a cheap visual/behavioral regression gate
for refactors.

## License

CC0 / public domain.
