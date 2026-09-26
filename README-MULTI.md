# wipEout rewrite – multi / VFX branch

Branch `vfx-pbr` on top of upstream `phoboslab/wipeout-rewrite`.
Game data (`wipeout/`) is not included, as in the original project: copy your
own next to the binary.

Binaries for Linux, Windows and macOS are built by GitHub Actions for every
`v*` tag: see the Releases page.

## Visuals

- **Exhaust VFX**: additive engine plume (blue gradient), engine flare and
  speed trail per engine; longer plume, wider trail and a flash on turbo.
- **Approximated PBR lighting** (optional): smooth per vertex normals (track
  and models, edges above 60° stay sharp), GGX specular with a soft sun,
  sky cubemap reflections rendered at track load, emissive from overbright
  texels / vertex colors. Materials are derived from the albedo: dark =
  matte, saturated = glossy paint, grey-blue = metal.
- **Point lights** (optional, 0..6): per pixel with specular on the exhausts,
  per vertex flashes on explosions, hard impacts and turbos.
- **Post processing**, all combinable: bloom (threshold + intensity), radial
  motion blur (single player), soft-knee tonemapping (normal blend only),
  CRT filter.
- **Glow passes**: boost pads (arrow only), active pickups with a ground
  halo, start lights and beacons, projectiles (rockets, missiles, e-bolt).
- **Impact effects**: sparks scaled with the impact, fire and a light flash on
  hard hits; explosions get a swelling flash and a shockwave ring.
- **Shield**: additive energy bubble with fresnel rim and running waves.
- Softer, height dependent ship shadows.
- **F3** toggles every render effect on/off for before/after comparisons.

## Two players

- Main menu > **TWO PLAYERS**: class, then **FULL GRID** (with the AI ships)
  or **DUEL** (just the two of you), then teams and pilots.
- Split screen top/bottom or side by side (Options > Video > Split screen),
  one camera, HUD, droid, view mode and P1/P2 marker per player. The race
  ends when the first player finishes; no highscores are recorded.
- Player 2 controls: Options > Controls > **PLAYER 2 CONTROLS**. Defaults:
  keyboard I/K/J/L, N thrust, B fire, `,`/`.` brakes, O view; second gamepad
  (SDL build). **SWAP GAMEPADS** exchanges the two pads.

## Options

- Video: draw distance (FULL/FAR/MEDIUM/NEAR), default view (internal /
  external), split screen orientation, 720p resolution, and the **RENDER
  EFFECTS** page (CRT, bloom, bloom threshold and intensity, motion blur and
  strength, tonemapping, PBR lighting, lighting brightness, point lights).
- Settings are stored in `settings.txt` (key = value) next to `save.dat`;
  the binary save format is unchanged, highscores are kept.

## Performance

All geometry is batched (model matrix applied on the CPU), view cone culling
for track and scene, point lights per vertex where possible, 4x anisotropy.
For old machines: PBR lighting off, point lights low, draw distance MEDIUM,
720p or 480p.

## Build (Linux)

    make sdl USER_CFLAGS="-march=native"

Player 2 gamepad and swap only exist in the SDL build; the web build has no
gamepad support at all.
