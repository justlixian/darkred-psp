# DARKRED: PS Vita and PSP (PPSSPP) homebrew ports

A port of the GameMaker game **DARKRED** (`darkred_v1_0`, 124x175 portrait, 60 fps) to the **PS Vita** and the **PSP**.

Instead of re-creating the game by hand, this port **runs the game's original compiled GML bytecode** in a
small C virtual machine, with a software renderer, audio mixer and per-console input underneath. Story logic,
dialogue, word-wrapping, transitions and timing are the original's, not an approximation.

> The game is **18+** (its title screen says so). Use this only with your own copy of the game and the
> author's permission. `assets/darkred.pak` is converted from the game's `data.win`; don't redistribute it.

## Status: what is and isn't verified

| Verified here (headless build, automated: `make test`) | NOT verified (needs the real toolchain / device / emulator) |
|---|---|
| Boot, title, START, fade, map, dialogue, story scenes, the hidden location | Compiling/linking with VitaSDK or PSPSDK (the console layers were only syntax-checked against stub headers) |
| Original font metrics, word-wrap, typewriter text | Touch / pad feel, audio device behaviour, frame rate on real hardware or in an emulator |
| Room transitions, fade sequences, animation, depth sorting | Whether PPSSPP/Vita3K pick up the files exactly as described below |
| Save / load / reset across separate runs | |
| Audio mixer output (sample-accurate effect start) | |
| PSP scaler: exact pixel mapping at 1x and 1.5x, no overrun | |
| 30,000-frame random-tap soak test under ASan/UBSan: no crashes, no VM warnings | |

Expect the first build for each console may need a small fix (see Troubleshooting).

## Controls

| | PS Vita | PSP / PPSSPP |
|---|---|---|
| Click | Tap the touchscreen, or **Cross** | **Cross** or **Circle** |
| Jump between markers / buttons | **D-pad** | **D-pad** |
| Free cursor | Left stick | Analog nub |
| Screen size | 3x (372x525), fixed | 1.5x by default, **SELECT** toggles crisp 1x |

The cursor appears only when there is something to point at (title menu, map).
Desktop test build: mouse click, or arrow keys + `Z`/Enter/Space, `Esc` quits.

## Build

### Easiest: let GitHub build it for you (no installs)

Create a **private** GitHub repository, upload this whole folder (including the hidden `.github` folder), then open the
**Actions** tab > **build** > the latest run, and download the `DARKRED-PSP` (EBOOT.PBP + darkred.pak) and `DARKRED-Vita`
(VPK) artifacts. Keep the repo private: it contains the converted game data.

### 0. Convert the game data (any PC with Python 3 + Pillow)

```sh
pip install pillow
python3 tools/pack.py /path/to/data.win assets/darkred.pak     # data.win is inside darkred_v1_0.zip
```
A ready-made `assets/darkred.pak` (2.7 MB) is already included.

### PS Vita

Install [VitaSDK](https://vitasdk.org/) and SDL2 (`vdpm sdl2`), then:
```sh
export VITASDK=/usr/local/vitasdk; export PATH=$VITASDK/bin:$PATH
mkdir build && cd build && cmake .. && make        # -> build/darkred.vpk
```
Install the VPK with VitaShell, or in **Vita3K**: first install the PS Vita firmware (File > Install Firmware,
see https://vita3k.org/quickstart.html), then File > Install .vpk.

### PSP / PPSSPP

Install [pspdev](https://github.com/pspdev/pspdev) (provides `psp-config`, `psp-gcc`), then:
```sh
make -f Makefile.psp bundle        # -> out/DARKRED/EBOOT.PBP  and  out/DARKRED/darkred.pak
```
Copy the whole `DARKRED` folder to the memory stick's `PSP/GAME/` folder:

* **PPSSPP:** into its memstick folder, `<memstick>/PSP/GAME/DARKRED/` (Windows: `memstick\PSP\GAME\`; Android:
  `PSP/GAME/` on internal storage). It then shows up in the Games tab. Keep `darkred.pak` next to `EBOOT.PBP`.
* **Real PSP (custom firmware):** `ms0:/PSP/GAME/DARKRED/`.

PPSSPP default keyboard: arrow keys = D-pad, Z = Cross, X = Circle, Space = Select (change under Settings > Controls).
If the pak is missing the game shows a message on screen saying where to put it.

### Try it on a PC first (optional)
```sh
make            # needs SDL2 dev files (sdl2-config)
./darkred assets/darkred.pak
```

### Run the tests (no SDL needed)
```sh
make test       # builds the headless runtime with ASan/UBSan and runs tests/run_tests.sh
```

## Saves

* Vita: `ux0:data/DARKRED/save.sav`   PSP: `ms0:/PSP/SAVEDATA/DARKRED/save.sav`   Desktop: your SDL pref path.
* One deliberate fix: the original game *writes* `save.save` but *reads* `save.sav`, so "continue" could never find a
  save. Both names now map to one file. RESET still deletes the save, as in the original.

## About the hidden location

When the map room loads, the game creates one extra marker at the **top-left (about x=32, y=39)**. At `game_progress` 19 it
is an invisible 1-pixel sprite; at **20 or more** it becomes a visible marker ("MIRAGE ROUTE"). So it only shows up the next
time the map is *loaded* after progress reaches 20. Because the original could not carry progress between sessions (the
save bug above), finishing the game over several sessions would never get there; with this port's fix it does.

## How it works

```
data.win --tools/pack.py--> darkred.pak --> pak.c   streams the pak in
 (GameMaker)  resolves all variable/function      |
              references, pre-decodes bytecode    v
              to 16-byte records, 8-bit      vm.c   stack VM: every opcode the game uses, 2-D arrays,
              palettised atlas, font, audio,        compound assignment, with(), strings, mark-sweep GC
              rooms, sequences                rt.c   instances, rooms, layers, sequences, software renderer,
                                                     ~40 built-ins, mixer
                                              plat_sdl.c   Vita / desktop (SDL2)
                                              plat_psp.c   PSP (PSPSDK, framebuffer + audio thread)
                                              plat_null.c  headless backend used by the tests
```
`python3 tools/dump_code.py data.win <entry>` prints readable pseudo-GML for any script/event
(`gml_Object_obj_textbox_Step_0`, `gml_Script_scr_transition`, ...).

## Troubleshooting

* **Vita linker: undefined reference to `sceXxx...`**: add the matching `SceXxx_stub` to `target_link_libraries`.
* **PSP linker errors / black screen:** check the `LIBS` line in `Makefile.psp`; on a hang right after start, try a
  smaller `PSP_HEAP_SIZE_KB(...)` in `src/plat_psp.c`.
* **PSP is double-precision-heavy:** the PSP FPU is single-precision, so the VM's doubles are emulated in software.
  The game's logic is light, but the first frame of a long text box (it lays out all pages at once) may stutter slightly.
* **Wrong colours on Vita:** change `SDL_PIXELFORMAT_ARGB8888` in `src/plat_sdl.c`.

## Other consoles later

Everything except `plat_*.c` is platform-independent C. A new target only needs a platform file implementing the small
interface in `gm.h` (`plat_init/poll/present/wait`, audio, data dir).
