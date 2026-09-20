# BVH Projectile Optimizer 优化点挖掘（第二轮评审稿）

- 代码基线：`4e513c8`（工作树干净），Sven Co-op MetaMod 插件
- 后续评审：第三轮（基于实体生命周期三方对照与引擎语义校准）见 `docs/OPTIMIZATION_ROUND3.md`
- 方法：全量走读 `src/` 八个编译单元，按 **每帧热路径 / 剔除率与覆盖面 / 构建期与内存 / 健壮性 / 可观测性与运维** 五个维度挖掘
- 每项标注：优先级（P0 最高）、预期收益、风险、工作量（S/M/L）
- 结论摘要见 §1，逐项展开见 §3–§7，实施批次建议见 §8

---

## 1. 总览

| # | 项 | 维度 | 优先级 | 收益 | 风险 | 工作量 |
|---|----|------|--------|------|------|--------|
| 1 | Linux 语法检查编译错误（model_provider.cpp:195） | 健壮性 | **P0** | 恢复 Linux 构建 | 低 | S |
| 2 | 语法检查脚本在 Git Bash 下路径失效 | 工程面 | **P0** | 守住回归底线 | 低 | S |
| 3 | `IsManaged` 每实体每帧临时 `std::string` 堆分配 | 热路径 | **P1** | 高（扫描热循环） | 低 | S |
| 4 | 每帧两遍全实体扫描 + 每帧 `unordered_set` 分配 | 热路径 | **P1** | 高 | 低 | M |
| 5 | 静态 collider 每帧重复 `updateSingleAabb` | 热路径 | P1 | 中（刷型多的地图） | 低 | S |
| 6 | 每次查询构造 `btBoxShape` | 热路径 | P1 | 中（弹雨场景） | 低 | S |
| 7 | sweep 命中后不提前终止（closest 语义浪费） | 热路径 | P1 | 中 | 低 | S |
| 8 | `GetDebugLevel()` 每次走引擎回调 | 热路径 | P1 | 低-中 | 低 | S |
| 9 | `staleProjectiles` 每帧 vector 构造 | 热路径 | P2 | 低 | 低 | S |
| 10 | suppressed / tracked 双 map 合一 | 热路径+健壮性 | P1 | 中 | 低 | M |
| 11 | 碰撞网剔除 SKY/TURB/UNDERWATER 面形成"洞" | 覆盖面+正确性 | **P1** | 修正确性，间接提剔除率 | 中 | M |
| 12 | SOLID_BSP 实体 AABB 近似过粗，刷型密集区剔除率归零 | 覆盖面 | P2 | 高（特定地图） | 中 | L |
| 13 | SOLID_TRIGGER / SOLID_BBOX 不在预测范围 | 覆盖面 | P2 | 行为面确认 | 中 | M |
| 14 | 每帧 Think 弹体永远走 fallback，剔除率≈0 | 覆盖面 | P2 | 高（若命中此场景） | 中 | M |
| 15 | 多帧前瞻扫描（实验性） | 覆盖面 | P3 | 中 | 中-高 | M |
| 16 | 世界网格构建 `removeDuplicateVertices=true` | 构建期 | P2 | 中（大地图加载） | 低 | S |
| 17 | 16 位索引降内存 | 构建期 | P3 | 低 | 低 | S |
| 18 | `bvh_status` 聚合统计 + Update 计时 | 可观测性 | **P1** | 支撑一切后续度量 | 低 | M |
| 19 | 状态切换日志节流 | 可观测性 | P3 | 低 | 低 | S |
| 20 | `bvh_reload` / `bvh_enabled` | 运维 | P3 | 运维便利 | 低 | S |
| 21 | HLSDK 头 deprecated-copy 告警压制 | 工程面 | P3 | 输出干净 | 低 | S |

**明确不建议做**（§7）：多线程 sweep、替换引擎移动/权威判定。

---

## 2. 现状基线（数据流）

```
StartFrame(每帧)
 └─ CProjectileGate::Update()                      runtime/projectile_gate.cpp:22
     ├─ CCollisionWorld::Synchronize()             physics/collision_world.cpp:163
     │   └─ 第 1 遍全实体扫描（1..maxEntities）：client/monster/SOLID_BSP → 盒 collider 增删改
     ├─ 第 2 遍全实体扫描：classname 命中配置 → 登记 / 判定
     │   ├─ Think 到期或 movetype 非 FLY* → 恢复 solid（fallback）
     │   └─ CCollisionWorld::WouldProjectileHit()   physics/collision_world.cpp:206
     │       ├─ 每次 new 一个 btBoxShape(:229)
     │       └─ convexSweepTest（closest-hit 回调 :84）
     └─ 命中预测 → 恢复 solid；无命中 → solid=SOLID_NOT（本帧免引擎碰撞）
```

关键成本点：两遍 O(maxEntities) 扫描、每实体一次字符串集合查找、每弹一次 `btBoxShape` 构造 + convex sweep、每 collider 每帧一次 broadphase 更新。

---

## 3. P0：前置阻塞项

### 3.1 Linux 语法检查编译错误

- **证据**：`cc4.log` —— `src/engine/model_provider.cpp:195`：`cannot convert 'model_t* (**)[8192]' to 'model_t* (*)[8192]'`
- **根因**：成员 `m_precachedModels` 类型是 `model_t* (*)[8192]`（`model_provider.h:17`）。Windows 分支从指令立即数处**读取指针变量的值**，所以解引用一次（`:119` 有一层 `*`）；Linux 分支 `sv + 0x276148` 本身就是 `sv.models` 数组地址，当前却把指针强转成 `(**)[8192]` 且不解引用，多了一层间接。
- **修复**：改为
  ```cpp
  m_precachedModels = reinterpret_cast<model_t* (*)[8192]>(
      static_cast<char*>(serverState) + kSvModelsOffset);
  ```
  （与 `GetModel` 中 `(*m_precachedModels)[modelIndex]` 的两层解引用配套正确。）
- **风险**：低；仅改类型层级，不改变运行时地址计算。
- **工作量**：S。

### 3.2 语法检查脚本从 Git Bash 无法调用

- **证据**：`build_check.log` —— `bash: C:/Program Files/Git/mnt/d/Desktop/bvh/tools/check_syntax.sh: No such file or directory`（MSYS 把 `/mnt/...` 改写成了宿主路径）。
- **修复**：调用侧加 `MSYS_NO_PATHCONV=1`，统一走 `wsl.exe -e bash -c "cd /mnt/d/Desktop/bvh && bash tools/check_syntax.sh"`；或提供 `tools/check_syntax.cmd` 包装。顺带在脚本里固化 `-DLINUX -D_LINUX`（`cc3.log` 中 "OS unrecognized" 即漏宏所致，现已隐式修复但值得固化）。
- **工作量**：S。建议与 3.1 一起先做，让后续每项优化都有"可编译"底线。

---

## 4. P1：每帧热路径

### 4.1 `IsManaged` 在扫描热循环里的堆分配

- **现状**：`CProjectileClassConfig::m_classnames` 是 `std::unordered_set<std::string>`（`projectile_class_config.h:22`）。`IsManaged(const char*)`（`:137`）调 `find(classname)` 会构造临时 `std::string`；默认类名 `bdsc_bullet_proj` 恰好 16 字节，超过 MSVC/libstdc++ SSO 上限（15），**每实体每帧一次堆分配 + 哈希**。`Update()` 对 1..maxEntities 每个非 free 实体各调一次（`projectile_gate.cpp:57`），Sven Co-op 实体规模（2048–4096）下是每帧数百到上千次无效分配。
- **方案**（C++17 透明查找，零临时对象）：
  ```cpp
  struct StringHash {
      using is_transparent = void;
      std::size_t operator()(std::string_view v) const noexcept { return std::hash<std::string_view>{}(v); }
  };
  std::unordered_set<std::string, StringHash, std::equal_to<>> m_classnames;
  // 查找：m_classnames.find(std::string_view(classname))
  ```
- **可选加强**：扫描前先用整数预过滤（`movetype == MOVETYPE_FLY || MOVETYPE_FLYMISSILE`，必要时加 `solid != SOLID_NOT`），绝大多数实体在这里就被整型比较拦下，不再进入字符串查找。注意点：非 FLY 的受管实体将不再登记 `initialSolid`——分析后认为安全（从未被剔除过的实体无所谓"初始 solid"；它在变成 FLY 首帧登记的 solid 就是引擎原生值），但建议与 §6.4 的统计一起灰度验证。
- **风险**：低（透明查找部分无行为变化）。**收益**：高。**工作量**：S（预过滤 +S）。

### 4.2 每帧两遍全实体扫描 + 每帧 `unordered_set` 分配

- **现状**：`Update()` 开头调 `Synchronize()`（`projectile_gate.cpp:24`）做第一遍 1..maxEntities 扫描（`collision_world.cpp:170`），随后 gate 自己再做第二遍（`projectile_gate.cpp:55`）。`Synchronize()` 内部还每帧构造 `std::unordered_set<int> activeColliders`（`:169`），节点逐个堆分配。
- **方案**：
  1. 合并为单遍扫描：一次循环里同时完成 collider 目标收集与受管弹体判定，扫描结果写入可复用的成员缓冲。
  2. 用**代际戳**替代 `activeColliders` 集合：`CBoxCollider` 增加 `int lastSeenFrame`，每帧扫描时盖戳，扫描结束后移除 `lastSeenFrame != 本帧` 的 collider。零分配、零额外集合。
- **收益**：扫描与 `INDEXENT` 调用减半；消除每帧一整套 set 分配。实体规模越大收益越大。
- **风险**：低，逻辑等价重排。**工作量**：M。

### 4.3 静态 collider 每帧重复 broadphase 更新

- **现状**：`UpdateCollider()` 无条件 `setWorldTransform` + `updateSingleAabb`（`collision_world.cpp:430-431`）。SOLID_BSP 实体大多静止（func_wall）或低速（门、列车），实体数多的地图每帧做上千次无效 `updateSingleAabb`（Dbvt 叶子重插）。
- **方案**：`CBoxCollider` 缓存上次中心，位移平方 < epsilon² 时直接返回。client/monster 每帧都在动，不受影响。
- **风险**：低。**收益**：中（刷型密集地图显著）。**工作量**：S。

### 4.4 每次查询构造 `btBoxShape`

- **现状**：`WouldProjectileHit()` 每弹每帧构造栈上 `btBoxShape projectileShape(halfExtents)`（`collision_world.cpp:229`）——其构造要初始化 15 个顶点数组与 margin；弹雨场景（几十上百弹/帧）下开销可观。
- **方案**：在 `CCollisionWorld` 内做形状缓存：halfExtents 量化到 0.5 为 key，`unordered_map` 缓存 `btBoxShape`（容量封顶，超出回退现构造）；或每 tracked 弹缓存上次 halfExtents，变化才重建。查询用 `const btBoxShape*`。
- **风险**：低（Bullet 形状无状态、只读使用）。**收益**：中，弹越多越明显。**工作量**：S。

### 4.5 sweep 命中后继续做剩余候选的窄相

- **现状**：`CProjectileSweepCallback` 继承 `ClosestConvexResultCallback`（`collision_world.cpp:84`），只用于布尔判定却保留"最近命中"语义——已命中后，后续 broadphase 候选仍会进入窄相三角测试。
- **方案**：布尔化提前终止——`needsCollision` 开头 `if (hasHit()) return false;`（基类 `hasHit()` 即 `m_closestHitFraction < 1`）。一次命中后所有剩余候选零窄相成本。
- **风险**：极低，纯减法。**收益**：中。**工作量**：S。

### 4.6 `GetDebugLevel()` 每次走引擎回调

- **现状**：每次 `DebugLog`（含每帧必发的 `DebugLog(2, "Projectile frame: ...")` 与多处 `DebugLog(1)`）都先 `CVAR_GET_FLOAT` → `pfnCVarGetFloat` 引擎字符串查表（`debug_log.cpp:32-40`）。
- **方案**：`CVAR_REGISTER` 后引擎持有 `g_debugCvar` 并直接回写其 `value` 字段，注册完直接读 `g_debugCvar.value`，彻底去掉引擎调用与字符串查找。
- **风险**：低（HLSDK 标准 cvar 生命周期）。**收益**：低-中（热路径上的固定开销归零）。**工作量**：S。

### 4.7 suppressed / tracked 双 map 合一

- **现状**：`m_suppressedProjectiles` 与 `m_trackedProjectiles` 都以实体索引为 key（`projectile_gate.h:38-39`），同一弹体每帧要做 4–6 次独立哈希查找（`Update` 里 find/emplace/erase 穿插）；且 `m_trackedProjectiles[entityIndex]`（`projectile_gate.cpp:88, 101`）用 `operator[]`，若前置逻辑变动可能静默插入 `{entity=nullptr, initialSolid=0}` 的脏条目（`initialSolid=0` 恰好等于 SOLID_NOT，会变成"恢复成 SOLID_NOT"的隐性错误）。
- **方案**：合并为单张 `unordered_map<int, CTrackedProjectile>`，条目内含 `suppressed` 标志；恢复/剔除只是改标志。哈希查找降为每弹 1–2 次，同时从结构上消除 `operator[]` 隐患（改 `find()` + 断言）。
- **风险**：低-中（状态机重排，需按 §9 清单回归）。**收益**：中。**工作量**：M。

### 4.8 零碎分配

- `std::vector<int> staleProjectiles`（`projectile_gate.cpp:37`）每帧构造 → 复用成员缓冲 `clear()`。工作量 S，收益低，顺手做。

---

## 5. P1/P2：剔除率与覆盖面（决定收益上限）

> 热路径优化降低"做剔除这件事的成本"；本节决定"能剔除掉多少"。建议 §6.1 的统计先落地，用数据定位收益缺口。

### 5.1 碰撞网剔除 SKY / TURB / UNDERWATER 面形成"洞"（正确性）

- **现状**：`IsIgnoredWorldSurface()` 把 `SKY|TURB|UNDERWATER` 三类面全部排除出 Bullet 网格（`collision_world.cpp:102-108`）。这套面过滤源自可见性网格思路，但本网格的用途是**碰撞预测**，语义不同：
  - **SURF_UNDERWATER（0x80）**：水下朝外的墙面恰被标记为此类 → 被排除 → 水下弹道预测漏判，弹体保持 SOLID_NOT **穿墙**。这是最现实的一类错误（SC 水下区域常见）。
  - **SKY**：引擎里 CONTENTS_SKY 对移动是实心的（弹体打天空会 Touch/爆）。网格里没有天空面 → 朝天空的弹体永不恢复 solid，可能一路飞出地图且永不自毁（长局 edict 泄漏）。
  - **TURB**：水面 visual 面缺失影响相对小（水体本就允许穿越），但与 UNDERWATER 一并处理更干净。
- **方案**：碰撞网改为**包含全部面**（或最低限度：非 sky 全包含 + sky 单独验证后包含）。三角量略增，BVH 构建一次、查询不变。若担心弹幕打天空的表现差异，可加 cvar 分阶段放开。
- **风险**：中（改变命中预测的召回率，需在含水下/天空场景的地图回归）。**收益**：正确性修复 + 这些区域的剔除率反而可信。**工作量**：M。

### 5.2 SOLID_BSP 实体用 AABB 近似，刷型密集区剔除率归零

- **现状**：brush 实体 collider 是 origin 中心、`absmin/absmax` 全包盒（`collision_world.cpp:397-432, 456-457`）。大门、旋转体、移动列车这类"实际刷型瘦、AABB 胖"的实体，扫掠几乎必"命中"→ 恢复 solid → **附近区域永远无法剔除**。
- **方案**：按 brush model（`modelindex`）懒构建真正的 Bullet 三角网格（`model_t` 本身就是 BSP，`BuildWorldGeometry` 的采集逻辑可直接复用），缓存于 `modelindex → mesh`（与地图同生命周期）；运行时按 `v.origin + v.angles` 刚体摆位（用 `btQuaternion` 支持旋转门）。同 modelindex 的多个实体共享 mesh、仅 transform 不同。
- **收益**：刷型密集地图（SC 常态）的整体剔除率上限抬升，是覆盖面方向最大的一项。**风险**：中（网格摆放坐标系需与引擎一致，建议先做 2–3 张典型地图的对照验证）。**工作量**：L。

### 5.3 SOLID_TRIGGER / SOLID_BBOX 不在预测范围（行为面确认项）

- **现状**：collider 目标只有 client/monster/SOLID_BSP（`collision_world.cpp:447-457`）。被剔除的弹体本帧不再参与引擎碰撞，因此**穿过触发区、SOLID_BBOX 箱体时不再产生 Touch**。若目标模组的子弹依赖触发区计数/引信，这是可感知的行为差异。
- **方案**：不急于实现——先在文档与 README 里明示该边界；如需覆盖，可把 `SOLID_TRIGGER`（可选 `SOLID_BBOX`）加入 collider 目标并用 cvar 控制。
- **风险**：中（trigger 大量重叠时 sweep 变贵）。**工作量**：M。

### 5.4 每帧 Think 的弹体永远走 fallback，剔除率≈0

- **现状**：`nextthink <= time` 即回退（`projectile_gate.cpp:85-97`）。若目标模组的子弹按帧 Think（引信/拖尾类实现很常见），则**所有弹体每帧都回退，插件只付扫描成本、拿不到剔除收益**。此项是否成立决定整个插件对目标模组的实际价值，**优先用 §6.1 统计验证**。
- **方案**：配置行内可选信任标志（如 `bdsc_bullet_proj trust`），命中后跳过 nextthink 回退、照常 sweep。理由：sweep 用的是本帧 StartFrame 时的最新速度；仅当 Think 会改写轨迹时才有错判风险，信任标志即把该风险显式交给配置者。
- **风险**：中（错判一帧 = 该帧该弹免碰撞）。**收益**：高（若命中此场景）。**工作量**：M。

### 5.5 多帧前瞻（实验性，P3）

- **思路**：一次扫 K 帧位移走廊，全程无命中则 K-1 帧免 sweep（collider 仍每帧同步）。走廊是"全路径 AABB 对所有 collider 当前位置"的判定，对**中途走进走廊**的玩家会漏判（K=2~4、约 100ms 窗口，概率低但存在）。
- **方案**：`bvh_lookahead_frames`（默认 1=关闭），仅在 5.1/5.2 落地、统计基线稳定后再试。
- **风险**：中-高。**工作量**：M。

---

## 6. P2/P3：构建期、可观测性与运维

### 6.1 `bvh_status`：先把度量立起来（P1）

- **现状**：只有 `DebugLog(2)` 的逐帧计数（`projectile_gate.cpp:121`），无聚合、无耗时、无剔除率，无法回答"优化到底有没有用"。
- **方案**：环形累计（滑动窗口）：scanned / managed / culled / collision / fallback / sweep 次数、`Update()` 耗时（`QueryPerformanceCounter`/`clock_gettime`，仅统计开启时计量），新命令 `bvh_status` 输出最近 N 秒均值与峰值。**这是后续每一项收益验证的前置。**
- **工作量**：M，风险低。

### 6.2 世界网格构建 `removeDuplicateVertices=true`（P2）

- **现状**：`addTriangle(..., true)`（`collision_world.cpp:354-355`）让 `btTriangleMesh` 对每个顶点做内部哈希查重——大地图数万三角形时构建时间与内存都显著上升；重复三角形对碰撞完全无害。
- **方案**：改 `false`。大地图加载卡顿直接改善。工作量 S。

### 6.3 16 位索引（P3）

- `btTriangleMesh(true, false)`（`:308`）恒用 32 位索引；顶点 < 65536 的地图可按 `numvertexes` 预判改用 16 位，索引内存减半。小收益，顺手做。

### 6.4 日志与命令（P3）

- **状态切换日志节流**：`DebugLog(1, "Projectile %d BVH-culled...")`（`projectile_gate.cpp:116` 等）在弹幕下会刷屏，按 `(entityIndex, 事件)` 做最短间隔节流。
- **`bvh_reload`**：运行时重读 classname 配置（当前需重启插件，README 也这么写的）。
- **`bvh_enabled`**：一键总开关（排障时绕过本插件，不必卸载）。

### 6.5 工程面（P3）

- HLSDK 头文件引发 `-Wdeprecated-copy`（`cc4.log`）：在 CMake 里对该告警 `-Wno-deprecated-copy`（第三方头，不可修），保持构建输出干净、真实告警可见。

---

## 7. 明确不建议做的方向

1. **多线程 sweep**：调用点在引擎单线程主循环内，跨帧并行收益不成比例，还要为 Bullet world 加锁或复制；不值得。
2. **替换引擎移动逻辑/自建权威判定**：插件边界是"Bullet 只决定引擎碰撞是否运行"（README 已声明）。越界即失去"失败即回退引擎"的安全网，回归风险不可控。
3. **索引级 string_t 整数比较**：引擎 `string_t` 等值仅在同一分配上成立，配置类名与实体类名是不同分配，此路不通（已评估，避免后人踩坑）。

---

## 8. 建议实施批次

| 批次 | 内容 | 预估 | 出口标准 |
|------|------|------|----------|
| 1（先做） | §3.1、§3.2、§4.5、§4.6、§4.8、§6.2 | 约半天 | WSL 语法检查全绿；构建无新增告警 |
| 2 | §6.1 统计、§4.1、§4.2、§4.3、§4.7 | 1–2 天 | `bvh_status` 可用；弹幕基准下 Update 耗时对比数据 |
| 3 | §4.4、§5.1、§6.4、§6.5 | 1–2 天 | 水下/天空场景回归通过；剔除率数据更新 |
| 4 | §5.2（按模型网格）、§5.4（按统计结论）、§5.5（实验分支） | 按需 | 目标地图剔除率提升 + 无行为回归 |

---

## 9. 回归验证清单（每批次共用）

1. **基准场景**：弹幕地图（大量受管弹体）、刷型密集地图、含水下区域地图、朝天空射击。
2. **指标**：`bvh_status` 的 culled/managed 比例、Update 每帧耗时；服务器帧率/帧时间对比。
3. **行为正确性**：
   - 预测命中恢复 solid 后，Touch/伤害/贴花/爆点与未装插件一致；
   - 被剔除期间穿越触发区、箱体的表现符合 §5.3 声明的边界；
   - 水下墙面、天空命中不再穿墙/飞出地图（§5.1 落地后）。
4. **状态机健壮性**：`bvh_debug=1` 下验证——弹体删除/索引复用、脚本中途改 solid、地图切换与插件重载后无 solid 泄漏（全部恢复初值）、map 长跑 edict 数稳定。
5. **双平台**：Windows（VS2022 Win32）+ WSL g++ 语法检查均通过。

---

## 10. 实施状态（四批次已全部落地）

| 批次 | commit | 内容 |
|------|--------|------|
| 1 | `ceb3ccf` | §3.1 编译错误、§3.2 脚本+小写 `linux` 宏、§4.5 首中即停、§4.6 cvar 直读、§4.8 缓冲复用、§6.2 关闭顶点去重 |
| 2 | `802aba8` | §6.1 `bvh_status` 统计、§4.1 透明查找+movetype 预过滤、§4.2 单遍扫描+代际戳、§4.3 静态 collider 跳过、§4.7 双 map 合一 |
| 3 | `04f71da` | §4.4 形状缓存、§5.1 碰撞网纳入 SKY/UNDERWATER 面、§6.4 `bvh_reload`/`bvh_enabled`/日志节流、§6.5 告警压制 |
| 4 | `e270bb0` | §5.2 按 brush model 建网格+yaw 旋转、§5.4 `trust` 标志、§5.5 `bvh_lookahead_frames`（默认关闭） |

**与原方案的偏差（评审注意）**

1. **§4.1**：透明查找最终采用 `std::map<std::string, std::less<>>` 而非 `unordered_set` 透明哈希——libstdc++ 仅在 C++20 模式提供 unordered 透明查找（P0919R3 未回移 C++17）；类名集合极小，有序查找与哈希等价且任何工具链可用。
2. **§5.1**：最终只排除 TURB（水面边界，引擎可穿越）；SKY 与 UNDERWATER 面全部纳入网格。比"全包含"更精确——水边界若纳入会使开阔水域上空永远判"命中"、剔除率归零。
3. **§5.2**：网格路径仅支持 yaw 旋转（常见门/转台）；出现 pitch/roll 时回退 AABB 盒，保持预测保守正确。
4. **§3.1**：采用"补一层解引用"而非文档原稿的"改指针层级"——与同文件 Windows 分支（读指针变量的值）意图一致。两种写法语义互斥，Linux 实机需按 §9 清单验证 `sv + kSvModelsOffset` 处确为指针槽。
5. **行为微调**：fallback/命中恢复从"无条件写 initialSolid"改为"仅在被剔除（suppressed）时恢复"，不再覆盖脚本对未被剔除弹体的 solid 修改。
6. **顺带强化**：配置解析升级为 `<classname> [flags...]` 并原子换表（解析失败保留旧表）；扫描预过滤对已跟踪实体不生效，保证 movetype 变更仍能走恢复路径。

**遗留验证（需要在实机完成，静态检查无法覆盖）**：§9 全部条目，重点第 1、2、3 项（天空/水下/刷型场景）与 Linux 模型解析（偏差 4）。`bvh_status` 的剔除率与 Update 耗时数据需在弹幕基准地图上采集前后对比。
