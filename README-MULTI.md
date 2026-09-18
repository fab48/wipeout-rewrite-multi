# wipEout rewrite – multi / VFX branch

Branch `vfx-pbr` on top of upstream `phoboslab/wipeout-rewrite` (commit d48f01c).
Game data (`wipeout/`) is not included, as in the original project.

## What's added

- **Exhaust VFX**: additive engine plume (blue gradient), engine flare and
  speed trail per engine.
- **Post processing** (Options > Video, all combinable): CRT, Bloom (only on
  additive effects and overbright pixels), radial Motion Blur (speed
  dependent, single player only), PBR Lighting.
- **PBR Lighting**: approximated, in the fragment shader. Flat face normals,
  GGX specular, fake sky reflection, emissive from overbright texels. Ship and
  track materials are derived from the albedo (dark = matte, saturated =
  glossy paint, grey-blue = metal).
- **Shield**: additive energy bubble with fresnel rim and running waves.
- **Two player split screen**: main menu > TWO PLAYERS. Top/bottom split, one
  camera, HUD, droid and view mode per player. Race ends when both players
  are done; no highscores are recorded.
- **Player 2 controls**: Options > Controls > PLAYER 2 CONTROLS. Defaults:
  keyboard I/K/J/L, N thrust, B fire, `,`/`.` brakes, O view; second
  gamepad (SDL build only). Stored in `controls2.dat`.
- **Performance**: all geometry is batched (model matrix applied on the
  CPU), view cone culling for track and scene, 720p resolution option,
  shader skips lighting math when PBR is off.

Tags: `v1-vfx-pbr` (visuals only), `v2-splitscreen` (everything above).

## Build (Linux)

    make sdl USER_CFLAGS="-march=native"

The save file format is unchanged; new options are packed into existing
fields, so existing highscores are kept.
