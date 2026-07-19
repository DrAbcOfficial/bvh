# BVH Projectile Optimizer

`bvh` is a standalone 32-bit Sven Co-op MetaMod plugin. It replaces the
per-frame engine collision work for `bdsc_bullet_proj` with a Bullet convex
sweep over a read-only collision world.

## Runtime model

- Every active player, monster, and `SOLID_BSP` world brush is represented by
  a synchronized `btBoxShape` using its GoldSrc absolute bounds.
- The world gets a `btBoxShape` bounds collider as requested, plus a
  `btBvhTriangleMeshShape` generated from the loaded BSP surfaces. The mesh
  is required for correct wall hits: a single world AABB would make the whole
  playable map solid and cause false hits.
- At `pfnStartFrame`, each projectile is swept across its next `frametime`
  displacement using its own bounding box. A predicted hit leaves it as
  `SOLID_TRIGGER`; otherwise this plugin changes it to `SOLID_NOT`.
- Optimization is deliberately limited to `MOVETYPE_FLY` and
  `MOVETYPE_FLYMISSILE`. A projectile with a due `Think` callback or a
  nonlinear movement type remains an engine trigger for that frame because
  script code can change its trajectory after `pfnStartFrame`.
- If world construction fails or a query cannot be trusted, the plugin fails
  open and leaves the projectile as an engine trigger. It never disables
  collision merely because Bullet data is unavailable.

The plugin only manages projectiles whose current solidity is
`SOLID_TRIGGER`, or whose `SOLID_NOT` value was previously set by this
plugin. Script code that intentionally uses `SOLID_NOT` or any blocking solid
mode remains untouched.

## Build

Initialize the `metamod` submodule, then configure with a 32-bit toolchain.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --target bvh
```

For Linux, keep `BVH_TARGET_32BIT=ON` and ensure the 32-bit C++ runtime and
toolchain packages are installed. World BSP extraction first resolves the
exported `sv` symbol and falls back to the Sven Co-op i686 GOT/PLT pattern used
by FallGuys when that symbol is hidden.

Install `bvh.dll` or `bvh.so` in `addons/metamod/dlls` and add it to
`plugins.ini`. Load it before normal gameplay starts. It does not link to BDSC
directly; BDSC must still be loaded for `bdsc_bullet_proj` to exist.

## Scope and safety

This plugin intentionally does not simulate projectile physics in Bullet and
does not dispatch `Touch` itself. Bullet only decides whether the original
GoldSrc movement should run its trigger collision for the next frame, so the
AngelScript `CProjBullet::Touch` implementation remains authoritative for
damage, decals, callbacks, ownership, and removal.
