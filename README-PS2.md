# Quakespasm on the PS2 (maxsdk)

Quakespasm (`Quake/`, the PS2's few changes under `__PS2__`: below) built with maxsdk and drawn through libgl (`libs/libgl`,
OpenGL on the GS). `ps2/` replaces the SDL platform layer.

```
make [FRAMES=n] [FIXEDFPS=n] [ARGS="+map e1m1"]   $(OUT)/quakespasm.elf (build/apps/quakespasm)
make run                                          the same in PCSX2 (game data in build/pcsx2-test/id1)
make -C pc [FRAMES=n] [ARGS=...] run              the same engine on the PC's OpenGL (below)
```

`FRAMES=n` ends the program after n drawn frames with a picture (`host:quakespasm.tga`);
`FIXEDFPS=n` makes the game's time 1/n s a frame (the same picture at the same frame, whatever
the speed); `ARGS` adds to the command line. `quakespasm.pak` (Quakespasm's own `default.cfg`,
with the controller bindings) goes next to the ELF.

`pc/` builds the same sources for the PC, drawn off screen through EGL (Mesa's llvmpipe), with
the PS2 build's GL paths (two texture units, texture_env_combine, no texture_env_add, no GLSL,
textures up to 256), screen (640 x 512) and command line, time 1/50 s a frame. Its
`quakespasm-pc.tga` and the PS2 build's `quakespasm.tga` (with `FIXEDFPS=50`) at the same
`FRAMES` and `ARGS` show the same frame: what differs is libgl's.

| | |
|---|---|
| Renderer | Quakespasm's fixed-function paths on libgl's extensions: two texture units, texture_env_combine (the overbright lightmaps in one pass), no texture_env_add (fullbrights as their own pass), no GLSL. Its RGBA lightmaps are grey: libgl keeps them as 8-bit brightness. `r_oldwater 1`: the liquids are warped per vertex (0 draws them into textures every frame with `glCopyTexSubImage2D`, a read back of the frame from GS memory on libgl, black with beam racing). No gamma on the GS: the picture is at gamma 1 |
| Video | `ps2/vid_ps2.c`: the TV's mode (NTSC 640 x 448, PAL 640 x 512) or 1080i (`vid_mode 1`), beam racing (`vid_beam`), 24-bit colour (`vid_rgb24`), set in Options, Video Options; `gl_ps2texquality`, `gl_ps2texlimits` (libgl's texture quality under memory pressure) |
| Input | `ps2/in_ps2.c`: pad 0 as Quakespasm's game controller (cross A, circle B, square X, triangle Y, L1/R1 the shoulders, L2/R2 the triggers, L3/R3 the sticks, START escape, SELECT tab); the left stick moves, the right one turns (`joy_*`) |
| Sound | `ps2/snd_ps2.c`: the mixer at 11025 Hz (`-mixspeed`; the sounds are 11025 Hz) into maxsdk's PCM stream (audsrv); music: WAV only |
| Files | stdio on the IOP file server (`host:`), `ps2/ps2_file.c`; directories (map and mod lists) through `ps2/include/dirent.h` |
| Beam racing | its record of a frame 512 KB (`MXGL_REC_BYTES`): Quakespasm's frames take about 200 KB, more than libgl's default 192 (the frame's end, the gun and the status bar, was left out) |
| Memory | `-particles 4096 -consize 64 +max_edicts 2048` (Quakespasm's defaults take 3 MiB more of zeroed hunk, which pages); the hunk 24 MiB (`-heapsize <KiB>`; with GLQuake's 16 the models' cache is evicted every frame of a demo, which loads them again); built into VM (`MX_MEMORY=vm`, the default: started by mxlink), swap file `quakespasm.swp`; `MX_MEMORY=phys` without VM |
| Network | the loopback only (single player) |

## Speed

`make ARGS="+timedemo demo1" run` plays the demo as fast as it can and ends the program with
Quake's result line (`PROF=1`: and a profile of the demo, `libs/maxsdk/tools/prof.py <elf>
<log>`). In PCSX2 measure with the EE speedhacks off (wait loop detection, INTC_STAT spin
detection): with them on, a tight loop that reads memory is taken for a wait and the emulated
clock jumps ahead, so that code seems slow. The PS2's changes to the engine, each under `__PS2__`:

- `Quake/r_alias.c`: a model's strips and fans lerped and shaded into arrays, one draw
  (`glMultiModeDrawArraysIBM`), not four GL calls a vertex.

`ps2/include/SDL.h` stands in for SDL's headers: the GL headers are libgl's, and `SDL_RWops`
(the localization file) is stdio.
