# BVH 实机部署验证报告（2026-09-20）

- 环境：`F:\SteamLibrary\steamapps\common\Sven Co-op Dedicated Server`（Metamod-P 1.21p38，
  SC 5.26 / 服务端 build 10257，端口 27015 被占用故测试用 27016）
- 被测：bvh（`dfa1db1`，批次 1–3 + 协议 v2）、bdsc（`8b73736`）、bdsccpp（`42b43c5`）
- 地图：`crossfire`（世界 BVH 10026 三角形 / 43 collider）；`bvh_debug 2`

## 1. 部署步骤（可复现）

1. 构建 32 位 DLL：
   - bvh：`cmake -S . -B build -G "Visual Studio 18 2026" -A Win32` + `--build build --config Release --target bvh`
   - bdsccpp：`MSBuild bdsc.vcxproj -p:Configuration=Release -p:Platform=Win32`（post-build 复制到 `../build/addons/metamod/dlls/`）
2. 复制到 `svencoop/addons/metamod/dlls/`（旧文件备份为 `*.bak-20260920`）。两者已在
   `addons/metamod/plugins.ini` 注册，无需改动。
3. AngelScript 侧：bdsc 脚本需**同时**部署到两个脚本根（SC 合并搜索，addon 根优先）：
   - `svencoop_addon/scripts/plugins/baidusc/`（队伍入口 `Test1.as` 所在根）
   - `svencoop/scripts/plugins/baidusc/`
   注意原 `svencoop/scripts/plugins/baidusc` 是指向 G 盘客户端安装的**失效符号链接**，已替换为实目录。
4. 插件清单在 `svencoop/default_plugins.txt`（**不是** `scripts/plugins/` 下）：
   新增条目须写在根 `"plugins" { ... }` 块内部，写在块外会被静默忽略。

## 2. 部署期发现（与 bvh 无关，但影响可测性）

1. **bdsc DEV 分支（HEAD `d37129c`，含 `fe6952a`/`110e7ab`）无法独立编译**：
   `lib/Utility/PerformanceProfiler.as`、`lib/Hook.as`、`lib/Weapon/CBaseCustomWeapon.as`
   引用 `PerformanceProfiler::CChronoPoint` 与 `g_ChronoManager`，但该类型的定义文件
   在仓库**任何提交里都不存在**（`git grep "class CChronoPoint"` 全历史为空）——属于
   未提交的部署文件 + 部署漂移。测试部署以 `b3b9359`（26/8/8，profiler 未入链）为基线，
   复制 HEAD 版 `lib/Entity/CProjBullet.as`（与基线 diff 仅“移除一次 Precache 调用 + 轨迹标记块”），
   并按用法补 `lib/Utility/CChronoPointStub.as`（`class CChronoPoint` + `CChronoManager.Now()`，
   供探针禁用态编译通过）。
2. 旧部署快照（编译无误的那份）在修复过程中被覆盖，无法找回；G 盘客户端拷贝是 7 月 19 日的
   旧版且同样引用缺失类型。**建议团队尽快把 `CChronoPoint`/`g_ChronoManager` 的定义文件提交入库**，
   否则任何新部署都会编译失败。
3. `pev.maxspeed` 在弹体协议里是**飞行距离**（寿命 = `maxspeed / max(speed,1)`），
   测试脚本初值给 3.0 导致弹体出生即自毁，bvh 计数为 0——排查耗时项，记录备查。

## 3. 验证方法与结果

临时测试插件 `scripts/plugins/bvhtest.as`（已注册进 `default_plugins.txt`，测试后应移除）：
MapInit 后定时生成 4 波 × 32 发 `bdsc_bullet_proj`（1/3 静止悬停 2 秒寿命，其余 1600ups
水平飞行 3 秒寿命），并在 +8s/+14s 通过 `ServerCommand("bvh_status")` 回读聚合计数。

末次运行（frames=1294，约 20 秒）：

```
[BVH] world: ready=1 colliders=43 triangles=10026.
[BVH] frame avg: projectiles=12.27 culled=11.84 collision=0.44 fallback=0.00
      (think=0.00 move=0.00 trust=0.00/0.00 ownerFilter=0.00)
      lookahead=0.00 sweeps=12.27 (ray=12.27 box=0.00) update=0.519ms
[BVH] totals: projectiles=26678 culled=26393 collision=285 fallback=0
      (think=0 move=0 trust=24/0 ownerFilter=0) lookahead=0
      sweeps=22262 (ray=22262 box=0)
```

| 验证项 | 结果 | 结论 |
|--------|------|------|
| 插件加载 | 8/8 RUN（BDSC + BVH 新版） | ✅ |
| 世界构建 | ready=1 / 43 collider / 10026 三角形 | ✅ |
| 弹体管理 | 累计 26678 帧次（约 12 并发弹） | ✅ |
| 剔除率 | 26393/26678 = **98.9%** | ✅ |
| 预测命中恢复 | 285 帧次（撞墙前正确恢复 solid → 引擎 Touch 正常销毁） | ✅ |
| 零 hull → rayTest | sweeps=22262，**ray=22262 / box=0** | ✅ 批次 2 生效 |
| thinkDue 回退 | fallback=0（测试弹无中途 Think，符合预期） | ✅ |
| **实体轨迹标记协议** | **trust=24/0**（实体位路径 24 帧，配置路径 0） | ✅ 协议 v2 端到端 |
| 每帧开销 | update avg 0.52–0.81ms，峰值 234ms 仅出现在首帧世界构建 | ✅ |

`trust=24` 是静止弹存活到 `nextthink` 帧时，bvh 经
`GetModuleHandle/GetProcAddress` → `bdsc_bvh_get_trajectory_marker` →
`CGameObject::m_iTrajectoryStatic` 读到 `1` 后跳过 fallback 的次数，
即 **bdsccpp 写入 → bdsc 导出 → bvh 动态解析** 全链路实机成立。

## 4. 测试服务器上的遗留物（下次测试前清理）

- `svencoop/scripts/plugins/bvhtest.as` + `default_plugins.txt` 的 `BvhTest` 条目
- `svencoop/server.cfg` 末尾的 `// === BVH test hook ===` 块（`meta list` + `bvh_status`）
- `baidusc/lib/Utility/CChronoPointStub.as` 与 `include.as` 首行的 stub include
- 两个脚本根的 `baidusc/` 内容为 `b3b9359` + HEAD `CProjBullet.as` + stub 的混合部署

## 5. 下一步建议

1. 用真实玩家（或既有 GoldsrcNetClient 机器人）在弹幕场景复核：命中/伤害/贴花与未装插件一致，
   并采集 `bvh_status` 的剔除率与 Update 耗时前后对比（本轮为无玩家的合成弹幕基线）。
2. 让 bdsc 团队补齐 `CChronoPoint` 定义文件后，用 DEV 分支 HEAD 重新部署回归一次。
3. 批次 4 项（`pfnNumberOfEntities` 扫描上界、mesh collider 运动保守化、SOLID_BBOX 候选）
   仍待实施与实机验证。
