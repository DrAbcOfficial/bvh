# BVH Projectile Optimizer

`bvh` is a 32-bit Sven Co-op MetaMod plugin that uses Bullet BVH sweeps to
skip unnecessary engine collision for configured projectile entities.

## Install

Initialize the `metamod` submodule, then build with a 32-bit toolchain.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --target bvh
cmake --install build --config Release --prefix "C:\path\to\svencoop"
```

Add the installed plugin to `addons/metamod/plugins.ini`. BDSC must be loaded
before the configured projectile entities are spawned.

## Configure

Managed classnames are read from:

```text
addons/metamod/configs/bvh/projectile_classnames.cfg
```

The plugin creates a missing file with the default content:

```cfg
# One entity classname per line. Lines can include # or // comments.
# An empty file intentionally disables projectile BVH management.

bdsc_bullet_proj
```

Use one classname per line. Blank lines and text after `#` or `//` are ignored.
An empty file disables management. An existing unreadable file disables
management without overwriting it. Run `bvh_reload` to apply edits without
restarting; if the file cannot be read at that moment, the previous list
stays in effect.

## Behavior

- Each configured `MOVETYPE_FLY` or `MOVETYPE_FLYMISSILE` entity is swept over
  its next frame displacement.
- A clear sweep temporarily changes `solid` to `SOLID_NOT`.
- A predicted hit, unsafe query, due `Think`, or plugin reset restores the
  entity's initial `solid` value.
- Bullet only decides whether GoldSrc collision runs; GoldSrc still owns
  movement, `Touch`, damage, decals, callbacks, and removal.
- The Bullet world mirrors the map BSP for prediction: sky and submerged
  solid faces block, while water boundary faces stay passable like the
  engine treats them.
- Missing world data or an invalid query fails open and preserves engine
  collision.

## Debugging

Set `bvh_enabled` to `0` to bypass the plugin without unloading it.
Set `bvh_debug` to `0`, `1`, or `2` for disabled, lifecycle/event, or
per-frame output. Run `bvh_debug_status` to print the effective values.
Run `bvh_status` for aggregated per-frame statistics: culled ratio,
predicted hits, fallbacks, sweeps, and update time.
