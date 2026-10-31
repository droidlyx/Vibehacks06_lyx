# 长大以后 · Verdant Accord

当前编辑器工作副本的NPC对话通过`https://api.qnaigc.com/v1`调用`deepseek/deepseek-v4.1-flash-1017`。每个符合距离和体型条件的候选NPC只请求一次，由同一次推理判断相关性并生成回应：无关返回字面`无关`，游戏静默结束；相关返回对白和行动JSON。正常启动器不再运行StartLux或Qwen，GPU留给游戏。NPC日常行动仍由游戏规则驱动。密钥和启动说明见[AI配置](Docs/LocalAI.md)，详细边界见[对话架构](Docs/ConversationRuntime.zh-CN.md)。

当前源码已接入20名不同模型的NPC、11类关系、按需对话、四档食物、自动成长和缩小复活，详见[本轮接入记录](Docs/RuntimeIntegration.zh-CN.md)。最终冷编译及DLL链接已通过，可重新打开编辑器手动试玩。下文的v0.3可玩包及历史验证记录属于原始交付版本，尚未按当前源码重新打包。

设计、素材、生成预算与当前进展：[中文项目总览](Docs/ProjectOverview.zh-CN.md)。
一个原创 UE 5.8 原型：在不同体型居民共同生活的山谷中连续成长，用战斗或 AI 协商取得养分，让母树复苏，并改变果林的开放程度、控制者和居民生存状态。

![原型游戏画面](Docs/Images/v03/valley-start.png)

## 开始试玩

历史 **v0.3** 便携包通过包内 **Play.cmd** 启动，使用原版独立本地AI服务；首次模型加载需要几秒。完整便携包位于`Builds/v0.3/Windows`；v0.1保留在`Builds/Windows`。这些旧包尚未更新为当前的单次DeepSeek链路，新链路仍待用户手动试玩验收。

源码启动要求 Unreal Engine 5.8.3；可通过环境变量 `UE_ROOT` 指定引擎目录。仓库使用 Git LFS，克隆后执行 `git lfs pull`。

```powershell
powershell -ExecutionPolicy Bypass -File Tools/Build.ps1
powershell -ExecutionPolicy Bypass -File Start-Game.ps1 -Editor
```

独立构建：`powershell -ExecutionPolicy Bypass -File Tools/Build.ps1 -Package -Version my-build`。每个版本保存到独立目录，避免覆盖已有可玩版本。

打包后运行`Tools/Stage-Portable.ps1 -Version my-build`补齐启动脚本和许可；当前便携包不再携带本地模型或推理运行时，也不复制密钥。既有`Validate-Build.ps1`玩法检查包含历史协议假设，不能替代当前专项API检查或用户试玩。

本地模型权重和推理二进制不进入Git，当前运行也无需安装。云端密钥配置见[LocalAI.md](Docs/LocalAI.md)。未配置密钥或云端服务不可用时，移动、成长、战斗和NPC日常行为仍可运行，交谈会提示服务不可用。

## 操作

| 输入 | 行为 |
|---|---|
| WASD / 鼠标 / Shift | 移动 / 观察 / 奔跑 |
| 鼠标左键 / 右键 / 空格 | 轻击 / 重击 / 闪避 |
| 按住 E | 获取附近散落食物或NPC尸体食物；受档位和容量限制，剩余保留 |
| Q | 查看自动成长提示；每5秒自动消耗库存的20%并成长 |
| F / Shift+F | F在母树附近供奉5份、居民附近分享5份；Shift+F在母树附近批量供奉 |
| T | 与附近居民自由交谈；Enter发送，Esc退出 |
| B | 在允许采集的果林花费食物养护土壤 |
| G | 接管无主果林；已管理时切换免费、30%分成、限制采集 |
| R | 重新开始这一局 |

## 一局可以怎样玩

母树需要100份养分。食物可以用于自身成长、分享或供奉。你的体型直接影响生命、力量和攻击范围。

米芽允许自由采集，陶恩要求分成，岚石控制的高地果林起初受限。你可以协商开放、请求分享或运送，也可以通过战斗改变控制关系。NPC承诺供奉后必须实际走到树边；死亡会中断行动，食物不会隔空到账。

母树复苏后世界继续运行。右侧“本季记录”比较开局与复苏时的居民/野生生物存活、免税开放与领地归属，并记录每人实际送达的养分；同时保留当前世界状态。之后发生的变化不会改写这一季的记录，没有隐藏的结局选项按钮。

建议现场先展示一次逐渐长大，再向居民提出协商，最后展示果林政策和人物行动的变化。

## 当前边界

- 一个约400米宽的原创自然山谷，三片果林，20名NPC、四档各5名，及160个食物点。
- 玩家保留当前18厘米起步设置，上限约4.5米；NPC不成长。玩家死亡后缩小复活，世界不重置。
- 角色与基础动作来自原始CC0素材的新组装/导入；母树和营养植物为程序造型。
- NPC日常行动使用游戏规则；交谈要求实际三维距离≤20米、较高/较矮身高比≤2，每个候选由一次DeepSeek请求完成相关性和回复。每次发言最多3个候选，每条链最多2句实际NPC回复。主动交谈低频触发。
- 模型可以拒绝、误解或还价。支持的实际行动有范围，任意口头承诺不会自动成为新玩法。
- 原型无存档、多人联机或完整剧情战役。R会重置世界。

## 验证与来源

v0.3独立构建通过八类后台验证，包括正常资源下的公共开放与和平接管两条完整路线。已完成的验证与剩余限制见 [Validation.md](Docs/Validation.md)，现场展示参考 [DemoGuide.md](Docs/DemoGuide.md)。美术来源、许可和处理过程见 [Assets/README.md](Assets/README.md)。

三个3D服务的官方接口、价格和预算记录见 [GenerationAPIs.md](Docs/GenerationAPIs.md)。本轮3D生成花费为0：本地许可明确且风格合适的素材已满足原型。初次只读余额为Tripo 3000、V2fun 5100、Hyper3D 223。

这是独立新项目、新代码和新地图。Ember仅作为设计参考，没有迁入它的项目历史、游戏代码、地图或已制作UE资产。[DevelopmentLog.md](Docs/DevelopmentLog.md)记录阶段开发和实际发现的问题。

编辑器项目导出：`Tools/Export-EditorProject.ps1`。本机独立副本位于 `E:/UE_projects/VerdantAccord_v03/Verdant.uproject`，使用 UE 5.8.3 打开；不是打包后的 exe。
