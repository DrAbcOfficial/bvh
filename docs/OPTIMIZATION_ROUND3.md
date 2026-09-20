# BVH Projectile Optimizer 优化点挖掘（第三轮评审稿）

- 代码基线：`5c78f77`（工作树干净），Sven Co-op MetaMod 插件
- 前置文档：`docs/OPTIMIZATION.md`（第二轮，四批次已全部落地）
- 方法：第二轮覆盖了插件自身五个维度；本轮以"**受控实体的完整生命周期**"为主线，走读
  **bdsc**（AngelScript 侧注册 `ProjBullet::CProjBullet`）、**bdsccpp**（C++ 侧
  `SetupProjBullet` 初始化、`FireBullets` 钩子、`SV_SingleClipMoveToEntity` 引擎钩子）
  与 bvh 现源码三方交叉验证，并用 [ReHLDS 引擎源码](https://github.com/dreamstalker/rehlds/blob/master/rehlds/engine/sv_phys.cpp)
  逐条校准每个判定所依据的引擎语义。
- 每项标注：优先级（P0 最高）、预期收益、风险、工作量（S/M/L）

---

## 1. 总览

| # | 项 | 维度 | 优先级 | 收益 | 风险 | 工作量 |
|---|----|------|--------|------|------|--------|
| 1 | sweep 用 1×1×1 盒凸扫，而实体是零尺寸点实体——应改 rayTest 对齐引擎 | 热路径+语义 | **P1** | 高（每三角窄相降一个数量级） | 低-中 | S-M |
| 2 | `thinkDue` 判定窗口与引擎 `SV_RunThink` 不一致，漏一帧转向 | 正确性 | **P1** | 修正确性 | 低 | S |
| 3 | 实体级"轨迹静态位"协议（bdsc/bdsccpp 写，bvh 读） | 覆盖面 | **P1** | 高（interval-Think 弹剔除率 0→全程） | 中 | M |
| 4 | sweep 不排除 owner，首帧枪口必伪命中 | 热路径 | P2 | 中（统计污染+首帧恢复） | 低 | S |
| 5 | lookahead 窗口可跨越 `nextthink`，中途 Think 被跳过 | 正确性 | P2 | 低-中 | 低 | S |
| 6 | lookahead 命中后双 sweep，可由 hitFraction 合一 | 热路径 | P2 | 中（开启时 sweep 减半） | 低 | S |
| 7 | 扫描上界用 `maxEntities`，可用实体数高水位收紧 | 热路径 | P2 | 中（大容量地图） | 低 | S |
| 8 | `bvh_status` 缺 fallback 原因/trust 生效/查询方式分解 | 可观测性 | P2 | 支撑 #1/#3 验证 | 低 | S |
| 9 | `ForgetExpiredProjectiles` 每帧全量复核 | 热路径 | P3 | 低 | 低 | S |
| 10 | sweep 向量忽略 `basevelocity`（水流区漂移） | 正确性 | P3 | 低 | 低 | S |
| 11 | brush mesh collider 无运动膨胀，快速 pusher 可穿越预测 | 正确性 | P3 | 低（快速列车地图） | 低 | M |
| 12 | SOLID_BBOX/SOLID_SLIDEBOX 杂项不在候选集（pushable 等） | 覆盖面 | P2 | 行为面确认 | 中 | M |
| 13 | FLYMISSILE 类名的 `MOVE_MISSILE` 语义未覆盖；`spawnflags=8910422` 仓库内无读者 | 澄清 | P3 | 文档 | 低 | S |

**明确不建议做**（§7）：bdsccpp→bvh 直接 ABI 调用、弹簇共享 sweep、多线程、修改 bdsccpp 的弹体幽灵钩子。

---

## 2. 实体生命周期三方对照（本轮新证据链）

```
[bdsccpp] NewFireBullets (engine_api.cpp:48, vftable 钩子, mp_projshot>0 时把 hitscan 换成弹体)
   └─ 每枪 shotCount 个 CREATE_NAMED_ENTITY("bdsc_bullet_proj") + 字段装配 + MDLL_Spawn
      （velocity/owner/euser4/weapons/spawnflags 等全部在 DispatchSpawn 前定稿）
[bdsc]   CBullet.as:166-198（AS 武器路径）同样先装配后 DispatchSpawn，两路同归
   └─ AS CProjBullet.Spawn() (lib/Entity/CProjBullet.as:116)
      ├─ g_EntityFuncs.SetupProjBullet(self, iBulletShape)   ← bdsccpp C++ 实现
      └─ Think 结构二选一：
          flThinkInterve<=0（多数弹）: 全程仅一次 DestoryThink，nextthink=flDestoryTime
          flThinkInterve> 0（升级件/技能注入）: 每帧 Think，可改写 velocity（制导/环绕真实存在）
```

**bdsccpp `SetupProjBullet`（AngelScriptRegistration.cpp:41-127）关键事实**：

- `movetype = MOVETYPE_FLY`、`solid = SOLID_SLIDEBOX`；
- **`pfnSetSize(g_vecZero, g_vecZero)` 且 `pev->maxs = pev->pev->mins = g_vecZero`** ——
  弹体是**零尺寸点实体**，模型仅为视觉（scale=2）；
- `GetGameObject(...)->m_bIgnoreTraceLine = true`；
- 路径上写 `spawnflags = 8910422`（五个生成点一致），**bdsc/bdsccpp 仓库内无任何读者**。

**bdsccpp 引擎钩子（engine_api.cpp:141-155）**：钩 `SV_SingleClipMoveToEntity`，候选实体是
弹体（`m_bIgnoreTraceLine`）时清空 trace —— **一切移动/TraceLine 均穿过弹体**（弹-弹互穿、
buckshot 同膛不互消的机制）。即：引擎对弹体的唯一碰撞语义 = 弹体**自身**的移动裁剪。

**bdsc `CProjBullet` 关键事实**：

- `flThinkInterve` 是**武器级**变量（`lib/Weapon/CBaseCustomWeapon.as:160`），由技能字符串
  解析注入（`:394`）、升级件覆写（`upgradepart/level/Special.as:327` 强制 0.02s）；
- 制导弹真实存在：`Special.as:363-368/961-1051/1187-1188` 在 Think 回调里改写
  `pev.velocity`（改向、环绕、反弹）；
- 默认弹 `nextthink = flDestoryTime = time + maxspeed/max(1,speed)`（`CProjBullet.as:176-180`），
  全程只有一次"自毁"Think —— 第二轮的 fallback 逻辑对其**已经是最优**，`trust` 配置无增益。

---

## 3. 引擎语义校准（ReHLDS sv_phys.cpp 引证）

第三轮所有判定依据的引擎行为，逐条对源：

1. **剔除生效机制**：`MOVETYPE_FLY/FLYMISSILE` 走 `SV_Physics_Bounce` → `SV_PushEntity`，
   其中 `solid == SOLID_NOT → moveType = MOVE_NOMONSTERS`（只裁剪 bmodel/世界）。即剔除的
   实际效果 = 弹体自身移动不再裁剪 client/monster/SOLID_BBOX 实体；且 `SV_Impact` 对
   `solid == SOLID_NOT` 的一方不调用 Touch。与 README 声明的行为一致。
2. **弹体移动 = 单段线段**：`SV_PushEntity` 一次 `SV_Move(origin, mins, maxs, end)`，
   零 hull → **点对体积的线段 trace**，命中即停（不滑移）。bvh 的单次线性预测与引擎
   每帧位移**天然同构**——这正是 #1 改 rayTest 的语义依据。
3. **Think 时机**：`SV_RunThink` 触发条件是 `thinktime <= g_psv.time + host_frametime`
   （Think 在同帧移动**之前**执行）。bvh 现用 `nextthink <= gpGlobals->time` 判定，
   窗口少算了 `(time, time+frametime]` 这一段 → #2。
4. **位移向量**：`SV_Physics_Bounce` 对 FLY 同样执行
   `velocity += basevelocity → move = velocity*dt → velocity -= basevelocity`，
   即**该帧实际位移含 basevelocity**（CONTENTS_CURRENT 水流区会给实体加 basevelocity）→ #10。
5. **时序**：`SV_Physics()` 先调 `pfnStartFrame()` 再遍历实体循环（玩家跳过）；
   brush pusher 的位移也发生在 StartFrame 之后 → #4、#11 的依据。
6. **实体遍历上界**：引擎自身用 `g_psv.num_edicts`（高水位）而非数组容量
   `g_psv.max_edicts` 遍历 → #7。

---

## 4. P1 项

### 4.1 零尺寸点实体的预测应使用 rayTest（#1）

- **现状**：`WouldProjectileHit()`（collision_world.cpp:320-352）经 `GetEntityBounds`
  发现 absmin==absmax（零尺寸）→ `IsUsableExtent` 失败 → 回退 **0.5 半径盒**
  （`kMinimumHalfExtent`，:26,329-331）→ `convexSweepTest(btBoxShape)`。每个 broadphase
  候选的三角形都走 15 顶点凸体扫掠窄相（`btConvexTriangleCallback`）。
- **引擎侧**：同一位移是零 hull 的线段 trace（§3.2）。bvh 的 1×1×1 盒比引擎"胖"约 1 单位，
  属于无谓的过度保守：擦角/贴墙时产生伪命中 → 伪恢复 → `collision` 计数污染 + 白付一帧引擎碰撞。
- **方案**：
  - 受管实体为点实体（`mins==maxs==0`）时改走 `btCollisionWorld::rayTest(from, to, cb)`，
    回调保持"首中即停 + userIndex 过滤"两个现有短路；窄相变为射线-三角形（每三角约 3 次
    点积），宽相走 ray 的 dbvt 视锥；形状缓存对点弹体零命中，保留给未来非零 hull 的泛用类名。
  - **实现时一并把 sweep 终点改为 `origin + (velocity + basevelocity) * frameTime`**
    （见 #10，一次向量加法）。
  - 非 point 实体（第三方配置扩容）保留盒路径：`mins==maxs==0` 作为路由条件，
    可加 `bvh_point_ray` cvar 便于对照。
- **收益**：弹幕场景 sweep 是第一热路径；刷型密集地图（mesh collider 三角多）每三角窄相
  成本降约一个数量级，同时消除伪恢复。
- **风险**：低-中。rayTest 无 margin，与引擎点 trace 完全一致（引擎同样无 margin）；
  需按 §9 清单做贴墙/擦角回归。
- **工作量**：S-M。

### 4.2 `thinkDue` 窗口与引擎不一致（#2，正确性）

- **现状**：`projectile_gate.cpp:109-110`：`thinkDue = nextthink > 0 && nextthink <= time`。
- **引擎**：`SV_RunThink` 在 `thinktime <= time + host_frametime` 时**本帧就会先 Think 再移动**
  （§3.3）。
- **后果**：`nextthink ∈ (time, time+frametime]` 的弹体（interval-Think 弹每个周期都会落入
  一次）会被 bvh 当作"本帧无 Think"而预测/剔除，引擎却在移动前执行了 Think——若该 Think
  改向，预测用的是旧速度，**该帧漏判且免碰撞**。`trust` 弹体不受影响（本来就不看 Think），
  影响的是未信任的 interval 弹，恰是最需要 fallback 保护的群体。
- **方案**：`thinkDue = nextthink > 0.0f && nextthink <= gpGlobals->time + gpGlobals->frametime`。
  与 `SV_RunThink` 精确对齐后，fallback 帧的语义 = "引擎本帧将 Think"。
- **收益**：修正确性；同时让 fallback 计数更真实（对 #8 的数据解释是前置）。
- **风险**：低（收紧方向正确，未信任弹的剔除率略降属预期）。
- **工作量**：S。

### 4.3 实体级"轨迹静态位"协议（#3）

- **问题**：受管类名只有一个（`bdsc_bullet_proj`），classname 级 `trust` 无法区分：
  - 拖尾/烟迹/伤害积累类 interval 弹（Think 不改轨迹）→ 配 trust 则**白拿剔除**；
  - 制导/环绕弹（`Special.as` 改向）→ 配 trust 则**穿墙穿人**，只能放弃。
  结果：`trust` 一旦配置就对全部弹种生效，实际不可用；interval 弹整体剔除率 ≈ 0。
- **有利条件**（本轮证实）：弹体由 bdsc 注册、bdsccpp 初始化，**生成点完全收敛**，
  两侧都精确知道每个弹体"Think 会不会改轨迹"：
  - AS 路径：`CProjBullet.Spawn`（`CProjBullet.as:177-185`）决策点之后，
    `pfnThinkCallBack == null && pfnLatencyThinkCallBack == null` 且
    武器的 `aryProjThinkCallBack/aryProjLatencyThinkCallBack` 均为空 → 轨迹静态；
  - bdsccpp 路径（`NewFireBullets`）：弹体无任何 AS 回调，恒为轨迹静态，
    可直接在 `SetupProjBullet` 末尾置位。
- **方案**（in-band，无 ABI）：
  1. 约定 `pev.iuser2 = 1` 表示"Think 不会改变轨迹/速度"（`iuser2` 在弹体生命周期内
     无任何使用者——bdsc 全仓 grep 证实；`iuser3`/`fuser1` 为备选。注意 `CRCLRocket.as:35`
     等对**其它类名**用了 `iuser2=2`，语义不冲突，但文档需写明按类名隔离）。
  2. bdsc：`CProjBullet.Spawn` 装配 Think 回调后按上述判定置位/清零；
     bdsccpp：`SetupProjBullet` 对 C++ 路径弹体置位（AS 路径会在 Spawn 尾部覆写，两路不冲突）。
  3. bvh：think 门槛（gate :118,:135）与 lookahead 的 `thinkAllows`（:118）改为
     `thinkDue && !flags->trustThink && entity->v.iuser2 == 0`；
     实体位优先级高于 classname 配置（位=1 即信任，位=0 则仍受配置的 trust 影响）。
- **收益**：interval-Think 但不改轨迹的弹（升级件玩法下占比可观）剔除率 0 → 全程剔除；
  制导弹安全地保持 fallback。
- **风险**：中（错信任 = 该弹该帧免碰撞）。灰度依赖 #8 的 trust 生效计数 + §9 对照。
- **工作量**：M（三仓各一小块，bvh 侧约 3 行判定 + 文档）。

---

## 5. P2 项

### 5.1 sweep 排除 owner（#4）

- **现状**：callback（collision_world.cpp:107-128）只排除世界包围盒；弹体生成点为枪口/眼位，
  首帧与射手的 client collider 盒体（32×32×72）重叠或贴邻 → 首帧伪命中 → 恢复
  （`m_throttledRestoredHit`++、`collision` 计数 +1 帧无谓引擎碰撞）。每个弹体都中招。
- **方案**：`WouldProjectileHit` 读取 `projectile->v.owner` 的 ENTINDEX，回调内
  `object->getUserIndex() == ownerIndex` 时拒绝（collider 的 userIndex 已是实体索引，
  collision_world.cpp:568）。owner 为空则不过滤。
- **依据**：引擎移动 trace 以弹体自身为 `passed_entity` 跳过自体；游戏事实（弹体不会
  出生即 Touch owner 自毁，而 `CProjBullet.Touch` 对 owner 就是直接 Remove）表明引擎侧
  owner 命中并不发生。bvh 对齐后首帧即可剔除，且指标不再被首帧伪命中污染。
- **工作量**：S；**风险**：低。

### 5.2 lookahead 窗口建立时预判 `nextthink`（#5）

- **现状**：`lookaheadSkip` 生效期间仅检查"**本帧** thinkDue"（gate :114-129）。K>1 时若
  `nextthink` 落在窗口中后段（`time < nextthink <= time + K*frametime`），该 Think 帧仍被
  skip 且不 sweep —— 未信任弹的改向 Think 会被跳过一帧。
- **方案**：建立窗口时加条件 `nextthink == 0 || nextthink > time + frametime * K`
  （对齐 #2 修正后的窗口语义），否则不进入 skip。与 4.3 的实体位天然协同：置位弹体
  不受此限。
- **工作量**：S；**风险**：低（当前默认 K=1 不受影响）。

### 5.3 lookahead 双 sweep 合一（#6）

- **现状**：弹幕且开启 lookahead 时，clear 弹每帧先扫 1 帧、clear 后再扫 K 帧走廊
  （gate :146,163-172），sweep 成本 ×2。
- **方案**：一次扫 `frameTime * K`，用回调的 `m_closestHitFraction`（ray 回调同名字段）：
  `fraction <= 1/K` → 本帧命中；否则剔除且 `skip = max(0, floor(fraction*K) - 1)`。
  语义等价，sweep 次数减半。与 5.2 同批实施。
- **工作量**：S；**风险**：低。

### 5.4 扫描上界收紧（#7）

- **现状**：每帧扫 `1 .. gpGlobals->maxEntities`（gate :66）。SC 容量常为 4096-8192，
  而引擎自身遍历用高水位 `g_psv.num_edicts`（§3.6）。
- **方案**：每帧向 `g_engfuncs.pfnNumberOfEntities` 取一次上界（`min(ret, maxEntities)`，
  异常值 ≤0 时回退 `maxEntities`）。先在实机验证其返回语义恰为高水位（ReHLDS
  `PF_NumberOfEntities`）。
- **收益**：地图实体量常态几百，扫描 + INDEXENT 工作量按容量比例下降。
- **工作量**：S；**风险**：低（回退兜底；同帧新生成的弹体下一帧才被扫描，首帧引擎碰撞
  生效，fail-open 语义不变）。

### 5.5 `bvh_status` 分解统计（#8）

- fallback 计数拆 `thinkDue` / `movetype` 两源；新增：trust 位生效数、ray/box 查询数、
  owner 伪命中拦截数（5.1）。这是 4.1/4.3 前后对比的度量基础。
- **工作量**：S；**风险**：低。

---

## 6. P3 项

### 6.1 `ForgetExpiredProjectiles` 降频（#9）

`OnFreeEntPrivateData`（game_hooks.cpp:99-104）已覆盖正常释放，该函数只是防 edict 复用的
保险（gate :266-277）。可降频至每 16 帧。S。

### 6.2 sweep 向量纳入 `basevelocity`（#10）

见 4.1 实现要点。单独列出是因为它独立于 rayTest 也成立：CONTENTS_CURRENT 区引擎按
`velocity + basevelocity` 位移（§3.4），bvh 少加一项会预测偏移（每帧约 150/3*waterlevel*dt，
一帧内无碰撞但累积路径偏差）。S。

### 6.3 brush mesh collider 运动保守化（#11）

盒路径已按速度膨胀一帧（`GetEntityBounds(includePredictedMotion=true)`），mesh 路径
（collision_world.cpp:523-527,559-561）只按当前 origin 摆位。pusher（`SV_PushMove`）在
StartFrame 之后移动，快速列车/门可在一帧内切入弹道而预测无感。等价保守化：mesh collider
实体速度非零时，把 sweep 起止点沿其速度反向外推一帧位移（预测盒随动），或限制 mesh 路径
仅用于静止/低速实体（速度阈值 cvar）。M。

### 6.4 SOLID_BBOX/SOLID_SLIDEBOX 杂项候选（#12）

第二轮 §5.3 的遗留确认项，本轮给出精确机理：被剔除帧引擎走 `MOVE_NOMONSTERS`，弹体
本来就不会与它们碰撞，因此**只要预测集不含它们，被剔除弹体就会穿过 pushable、
武装武器、地面装备而不产生 Touch**。修复方向不变（cvar 控制纳入 SOLID_BBOX），但现在
可以确认这与引擎候选集严格一致、无过度保守问题。先文档声明边界，实测有依赖再实现。M。

### 6.5 澄清与文档（#13）

- `spawnflags = 8910422`（五个生成点一致写入）在 bdsc/bdsccpp 仓库内**无读者**——若为
  跨插件标记请在外部仓库注明；否则可与 `iuser2` 一样写入 bvh 的 README"弹体字段占用表"
  （iuser4=伤害类型、weapons=弹种、button=tracer、impulse=绘制模式、bInDuck=hull 形状、
  maxspeed=寿命、speed=速率、spawnflags=标记、iuser2=轨迹静态位【新】）。
- 泛用 FLYMISSILE 类名走 `MOVE_MISSILE`（引擎对其 hull/monster 裁剪语义与 MOVE_NORMAL
  不同），bvh 预测未模拟该差异；bdsc 弹体恒为 FLY 不受影响，文档注明即可。

---

## 7. 明确不建议做的方向（本轮新增）

1. **bdsccpp → bvh 直接函数导出/共享 `CGameObject`**：4.3 的 entvars in-band 位已完全覆盖
   需求，且不受 plugins.ini 加载顺序、插件缺位、双平台符号导出差异影响。为几行判定引入
   ABI 耦合不值。
2. **弹簇（buckshot）共享 sweep**：同膛弹散布角内命中分布各不相同，共享预测必然错判；
   rayTest 落地后单弹成本已低，无必要。
3. **修改 bdsccpp 的弹体幽灵钩子**（`SV_SingleClipMoveToEntity` 清空 trace）：它与 bvh
   无耦合（幽灵化作用于"弹体作为候选"的 trace，bvh 预测作用于"弹体作为移动者"），保持
   现状。
4. 多线程 sweep / 替换引擎移动：维持第二轮结论。

---

## 8. 建议实施批次

| 批次 | 内容 | 预估 | 出口标准 |
|------|------|------|----------|
| 1（先做，纯 bvh 小改） | 4.2、5.1、5.2、5.3、5.5、6.1 | 半天-1 天 | WSL 语法检查绿；`bvh_status` 出现分解指标；弹幕基准无回归 |
| 2（语义对齐） | 4.1（含 6.2） | 1 天 | 弹幕基准 Update 耗时对比数据；贴墙/擦角/门缝/水下/天空回归通过 |
| 3（三方协议） | 4.3 + 5.5 的 trust 计数 | 1-2 天（三仓联动） | interval-弹剔除率 >0 且制导弹无穿墙；灰度开关可回退 |
| 4（行为面，按需） | 5.4、6.3、6.4、6.5 | 按需 | 对应地图实测 + 无行为回归 |

依赖关系：批次 3 依赖批次 1 的统计分解（5.5）做灰度验证；批次 2 与批次 3 相互独立可并行。

---

## 9. 回归验证清单（在第二轮 §9 基础上的增量）

1. **thinkDue 修正（4.2）**：`Special` 系改向武器（未信任）转向行为与未装插件逐帧一致；
   `bvh_debug=2` 下观察 fallback 帧号与武器 Think 周期对齐。
2. **rayTest（4.1）**：贴墙射击、擦角、两扇门缝、水下墙面、朝天空——预测命中帧的
   Touch/伤害/贴花/爆点与未装插件一致；弹幕基准采集 sweep 耗时前后对比。
3. **owner 过滤（5.1）**：贴脸射击命中率与弹着点不变；`collision` 计数中首帧伪命中归零。
4. **trust 位（4.3）**：拖尾类置位弹全程剔除且爆点正确；制导弹未置位仍每帧 fallback；
   地图切换/弹体重用（`projectile_gate.cpp:91-97`）后位不残留（AS 路径每次 Spawn 重写）。
5. **lookahead（5.2/5.3）**：`bvh_lookahead_frames>1` 仅在 4.3 落地后启用；窗口跨越
   nextthink 的场景不再出现 skip 帧内 Think。
6. **双平台**：Windows（VS2022 Win32）+ WSL g++ 语法检查均通过。

---

## 10. 实施状态

| 批次 | commit | 内容 |
|------|--------|------|
| 前置 | `7149253` | 本评审文档（实体生命周期三方对照 + ReHLDS 语义校准） |
| 1 | `e979609` | 4.2 thinkDue 窗口对齐 SV_RunThink；5.1 sweep 排除 owner；5.2/5.3 lookahead 单查询化（hitFraction 判定）+ 窗口内 Think 预判；5.5 fallback 原因/trust/ownerFilter 分解统计；6.1 ForgetExpired 降频至每 16 帧 |
| 2 | `6ac313a` | 4.1 零 hull 弹体走 rayTest（点实体语义对齐 + 窄相降级为射线-三角形）；6.2 位移向量纳入 basevelocity；bvh_status 增列 ray/box 查询计数 |
| 3 | bvh `3f9937f`、bdsc `fa301cf`、bdsccpp `50a29b6` | 4.3 pev.iuser2 轨迹静态位三方协议（1=静态信任 / 2=动态强制回退 / 0=未标记沿用配置）；README 增补字段占用表与 trust 使用建议 |

**协议要点（实施时的定稿语义）**：`iuser2=1` 信任、`=2` 强制回退、`=0`（未标记的第三方类名）
沿用 classname `trust` 配置——实体位可双向覆盖配置，杜绝 `trust` 误伤制导弹。

**遗留（批次 4，按需，需实机验证）**：5.4 `pfnNumberOfEntities` 高水位扫描上界（先验证返回
语义）、6.3 mesh collider 运动保守化、6.4 SOLID_BBOX 候选纳入（cvar）。

**遗留验证（实机）**：§9 清单全部条目；重点为 thinkDue 修正后 interval 弹的转向帧对照
（`bvh_debug=2`）、rayTest 贴墙/擦角/门缝/水下/天空的预测命中帧对照、trust 位灰度期间
`bvh_status` 的 trust 计数与制导弹行为。
