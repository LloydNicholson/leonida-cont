# Runbook: the retail boot on `test/cont`

Hand-over for whoever continues the Linux/Wine boot of the retail executable (1.0.2.1). State of 24 November 2026.
Read [AGENTS.md](../AGENTS.md) first. This is a **test branch**: it stacks other people's pull requests (authors kept)
and unreviewed local fixes. Nothing on it is meant to be merged as it is.

## Where it stands

Measured on an RX 9060 XT (RADV, Mesa 27.0-devel), Wine 11.22 staging, clang-cl 22 + xwin SDK 10.0.26100:

| | |
|---|---|
| Boot | reaches the frame loop at about 200 s (pipeline cache off); no exceptions in the last runs |
| Presents | paced at the title's 33,333 us interval, into the title's own window, none failed |
| Pipelines | slot 86, slot 57 (16 logged failures) and over 1,000 derived through slot 55, none of those failed |
| Draws seen | `DrawInstanced` only (console list slot 12); no indexed, mesh or indirect draw |
| Picture | **black**, except about 2 s of pure white as the frame loop starts (source unknown) |
| Ends with | a 14 GB memory cap at about 230-250 s |

## The open question: black frames

Ruled out by the log at this point of the boot: refused DirectStorage requests (none) and skipped mesh draws (none made).
Fixed, and still black: slot 55 refused (old 7759, then slot 57 bases), slot 57 element stride, the executable's embedded
shaders (PR 387), and audio commands dropped unanswered (PR 404; whether the title reads the replies is not known).

Draft PR 321 reaches the same point and reports black as well: a forced clear colour and a forced pixel stage reach the
screen there, and its author reads the black as a transparent layer the title draws while it waits.

Next steps, in order:

1. **Capture what is presented.** `LEONIDA_CAPTURE=<dir> LEONIDA_CAPTURE_EVERY=5 LEONIDA_CAPTURE_LIMIT=120` writes the
   presented images and logs their mean colour. White in them: the title drew the flash. Only black: the flash was the
   window being cleared.
2. If black: log, for a few frames, each draw's pipeline, target, blend state and vertex count, and whether the
   presented resource is ever written.
3. Find what the title waits for: sample its threads once the frame loop runs. Audio initialises at the same moment
   (202.5 s); that is a correlation, nothing more.

Suspects, all hypotheses: slot 57 guesses its target formats (`R8G8B8A8_UNORM`, `D32_FLOAT`) and, where no slot 55 record
overrides it, its blend, depth and raster state; console list slots 70, 71, 72, 74, 75, 126, 131, 132 and 142 are recorded
and not forwarded; queue slot 31 is not modelled; about a quarter of the pipeline library's programs did not translate;
185 "no translated compute shader" lines in a run (slot 67: PR 19 is merged, but no `library/` shader folder is built).

## The other blocker: memory

Growth is anonymous memory in native code (64 MB glibc arenas), in bursts. With RADV's shader cache on the cap is hit
at about 60 s, with `RADV_DEBUG=nocache` at about 240 s. Hypothesis: driver memory of about 50,000 pipelines, compiled
eagerly. Compiling on first draw is not written, and no pull request was found for it. Always run under a cap: without
one the machine runs out of memory and the kernel kills other programs.

## Branches

| Branch | What |
|---|---|
| `test/cont` | this one: the old test branch merged with `main` `d2cd6c0d9`, plus the fixes below |
| `test/pr6919` (local only) | the branch before that merge; an ancestor of this one |

**Merging `main`.** `main` holds many boot commits as ancestors *without their content* (merged, then reverted or dropped).
A plain `git merge` applies those reverts silently; the first try lost the TLS fix in `pe_loader.cpp`. This branch was
merged with `git merge-recursive 0d31cdd64 -- HEAD upstream/main` (the real fork point as the only base). Afterwards
compare both sides' added lines with the result, **`CMakeLists.txt` included**: `host_stall.cpp` fell out of `gta6rt`
that way. The next merge starts from `d2cd6c0d9`; check what it does before trusting it.

The same fact makes old fixes findable: `git log --remotes -i --grep=<topic>` found slot 55's commit.

## Rules that were agreed

- For **every** error, search first, before writing code: open and merged pull requests, `old-pr/*` branches, and history on
  all remotes. Pull an existing fix in (`git cherry-pick -x`); say so when there is none.
- Claim in the coordination issue (14) before opening a pull request. None of the local fixes below is claimed.
- PR numbers restarted with the new repository: numbers above 500 here are the old one's.
- No game data in commits or posts, captures and screenshots included.

## Local fixes (no pull request)

Device slots 78/79/80 reach the PresentX pacer (overlaps PRs 402 and 414: prefer theirs if they work); the title's window
is presented into (lookup from old 8977); queue slot 35 unwraps list proxies; graphics compiles, shader index loading and
all of pipeline creation count as host stalls (widens old 7858); slot 57 pipelines can be derived from; identical shader
bytecode is kept once; slot 57 input elements are name + 28 bytes (from `docs/fspx.md`'s formula, not proven for every
blob); diagnostics marked "test branch" or "test-branch diagnostic".

Main's own choices that are *not* used here: the slot 14 console descriptor heap handler (stored, never called) and its
list proxy. The frame pacer tests of old 7135 (event 2) were dropped in the merge; its code is kept.

## Build and test

Cross-build with clang-cl as in [linux.md](linux.md) (`runtime/cmake/clang-cl-xwin.cmake`, `-DXWIN_DIR=`,
`-DLEONIDA_BUILD_WINDOWS_RUNTIME=ON -DLEONIDA_SDL3=ON -DLEONIDA_SDL3_FETCH=ON`), then `cmake --build` and `ctest`.

Full suite at the merge (`291ef76ed`): 31 of 312 fail; not repeated since, only related tests. Plain `main` fails 8 of the
12 that were new against the old branch here. Known and open:

- `d3d12_console_descriptor_heap`: a real regression (slot 14, above).
- `d3d12_rect_list`: fails here and passes at old 7448's own head, with the same code. Something else on the branch breaks it;
  root parameters are refused because no graphics root layout is bound. It may matter for full-screen draws.
- `d3d12_existing_heap`, `d3d12_xmem_placed`: assert main's fallback, which the slot 133 shadow buffer changes.
- `device_proxy`: needs WARP, so slots 55 and 57 have no test coverage under Wine beyond `pso_stream`.

## Shaders

Besides `make_game.py`'s output, the runtime loads `fspx_gfx/` (14,668 graphics programs here: `tools/shader_library.py`,
then `tools/rdna2spirv.py`, then `tools/pack_fspx_gfx.py`) and `exe/` (PR 387's `exe_shaders` step, 10 programs) from the
`LEONIDA_SHADERS` folder. Running `make_game.py` on this branch retranslates everything, as `tools/` changed.

## A run

```sh
export LEONIDA_D3D12=vulkan LEONIDA_SHADERS=<work>/shaders/spirv RADV_DEBUG=nocache
rm -f <work>/boot.jsonl      # --trace refuses to overwrite, so before every run
systemd-run --user --scope -p MemoryMax=14G -p MemorySwapMax=0 wine runtime/build/windows/gta6rt.exe \
  --run <game>/game_scarlett_master.exe --game-dir 'Z:\<game>' --trace 'Z:\<work>\boot.jsonl' 2> <work>/d3d12.log
```

Read it with `python3 -I tools/boot_trace.py <work>/boot.jsonl` and by tallying `[rt]` lines. The trace records the first
call of each audio function only. A null write at `game+0x2215c35` around 33-45 s is the title's startup hang check, not
the fault itself. `--trace-d3d12` writes about 1 GB in minutes; leave it off.
