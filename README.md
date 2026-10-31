# 长大以后 · Verdant Accord

一个原创 UE 5.8 原型：在不同体型居民共同生活的山谷中连续成长，用战斗或 AI 协商取得养分，让母树复苏，并改变果林的开放程度、控制者和居民生存状态。

![原型游戏画面](Docs/Images/v03/valley-start.png)

## 开始试玩

本机直接运行 **Play.cmd**，默认启动已验证的 **v0.3**，并在后台启动独立本地 AI 服务。首次模型加载需要几秒。完整便携包位于 `Builds/v0.3/Windows`；v0.1保留在 `Builds/Windows`。

源码启动要求 Unreal Engine 5.8.3；可通过环境变量 `UE_ROOT` 指定引擎目录。仓库使用 Git LFS，克隆后执行 `git lfs pull`。

```powershell
powershell -ExecutionPolicy Bypass -File Tools/Build.ps1
powershell -ExecutionPolicy Bypass -File Start-Game.ps1 -Editor
```

独立构建：`powershell -ExecutionPolicy Bypass -File Tools/Build.ps1 -Package -Version my-build`。每个版本保存到独立目录，避免覆盖已有可玩版本。

打包后运行 `Tools/Stage-Portable.ps1 -Version my-build` 补齐本地模型和许可；`Tools/Validate-Build.ps1 -Version my-build -FullJourney` 可重跑独立构建检查。

本地模型权重和推理二进制不进入 Git。安装见 [LocalAI.md](Docs/LocalAI.md)。缺少模型时，移动、成长、战斗和 NPC 日常行为仍可运行，交谈会明确提示服务不可用。

## 操作

| 输入 | 行为 |
|---|---|
| WASD / 鼠标 / Shift | 移动 / 观察 / 奔跑 |
| 鼠标左键 / 右键 / 空格 | 轻击 / 重击 / 闪避 |
| 按住 E | 在果林内连续采集，或取走倒下生物的战利品 |
| Q | 食用5份食物，逐渐吸收并长大 |
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

- 一个约400米宽的原创自然山谷，三片果林，三位主要居民和三位野外生物。
- 玩家约0.8米起步，原型成长上限约4.5米；没有无限尺度地形。
- 角色与基础动作来自原始CC0素材的新组装/导入；母树和营养植物为程序造型。
- NPC日常采集、避敌、迁移使用游戏规则；自由对话和协商选择使用真实本地模型。
- 模型可以拒绝、误解或还价。支持的实际行动有范围，任意口头承诺不会自动成为新玩法。
- 原型无存档、多人联机或完整剧情战役。R会重置世界。

## 验证与来源

v0.3独立构建通过八类后台验证，包括正常资源下的公共开放与和平接管两条完整路线。已完成的验证与剩余限制见 [Validation.md](Docs/Validation.md)，现场展示参考 [DemoGuide.md](Docs/DemoGuide.md)。美术来源、许可和处理过程见 [Assets/README.md](Assets/README.md)。

三个3D服务的官方接口、价格和预算记录见 [GenerationAPIs.md](Docs/GenerationAPIs.md)。本轮3D生成花费为0：本地许可明确且风格合适的素材已满足原型。初次只读余额为Tripo 3000、V2fun 5100、Hyper3D 223。

这是独立新项目、新代码和新地图。Ember仅作为设计参考，没有迁入它的项目历史、游戏代码、地图或已制作UE资产。[DevelopmentLog.md](Docs/DevelopmentLog.md)记录阶段开发和实际发现的问题。

编辑器项目导出：`Tools/Export-EditorProject.ps1`。本机独立副本位于 `E:/UE_projects/VerdantAccord_v03/Verdant.uproject`，使用 UE 5.8.3 打开；不是打包后的 exe。
