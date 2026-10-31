# 快快长大（Grow up）

**从18厘米高的小生物开始，在山谷中寻找食物、不断长大，与居民交谈、合作或战斗。**

一个使用 **Unreal Engine 5.8.3 / C++** 开发的单人游戏原型。体型改变你的视角、行动能力和与他人的关系；食物既是成长的养分，也是分享、协商和复苏母树的资源。

本文介绍当前本地开发版。旧 v0.3 可玩包与历史验证记录不包含全部新功能，最新玩法与画面仍在迭代。

## 山谷与居民

![快快长大：多尺度山谷美术概念](Assets/Concepts/fractal-world-v01.png)

*美术概念图，非实机画面：展示嵌套山脊、盆地与自然植被的视觉方向。当前实现是约400米宽的有限山谷，不是图中延伸到地平线的完整世界。*

当前场景包含三片果林、一棵母树、**20位不同模型的居民**与160个初始食物点。地形使用五层尺度的侵蚀基底叠加，搭配植被、山间日出、云层与森林配乐。主角是程序化种子／嫩芽小生物，居民使用选定的 Project Titan 角色素材。

| 长颈鸭商人 | 菜商 | 企鹅 | 沙漠鼬 |
|:---:|:---:|:---:|:---:|
| <img src="Docs/CharacterCatalog/Models/T36_064_SKM_clifftops_NoodleDuck_shopowner.jpg" width="180" alt="长颈鸭商人模型预览"> | <img src="Docs/CharacterCatalog/Models/T37_065_SMK_npc_cabbage_merchant.jpg" width="180" alt="菜商模型预览"> | <img src="Docs/CharacterCatalog/Models/T49_078_Penguin_LWC.jpg" width="180" alt="企鹅模型预览"> | <img src="Docs/CharacterCatalog/Models/T31_058_SKM_DesertFerrret.jpg" width="180" alt="沙漠鼬模型预览"> |

*当前选用角色素材预览，取自原始资产缩略图，不是游戏内截图。完整[角色设计库](Docs/CharacterCatalog/README.md)收录53种候选类型，其中20种已接入当前场景。*

## 现在可以怎么玩

- **寻找食物，逐渐长大。** 玩家从18厘米起步，每5秒消耗当时库存的20%用于成长，常规上限4.5米；空间不足时暂缓消耗。NPC的体型保持不变。
- **体型决定获取能力。** 食物分四档，分别提供1、3、8、20份养分；高档食物需要相应身高，携带容量也随身高增加。每30秒尝试补回采空或缺失的初始食物点，未采空的食物不会直接补满，落点不安全时延后补充。
- **与有个性的居民交往。** 20位居民拥有各自的人设、动机、库存与实例关系，可采集、分享、跟随、保护、战斗或撤退。承诺送粮需要实际走到目标，口头答应不会让资源隔空到账。
- **开口协商，也听他们主动交谈。** 可以向附近居民广播，或指定一位对象。全局每10秒尝试安排一组主动交谈，受距离、体型和请求状态限制；不是每位NPC都持续调用模型。
- **让母树复苏。** 将100份养分送到母树，同时决定食物用于自身成长、分享还是供奉。果林可以免费开放、收取分成或限制采集；生态手记记录本季的资源、居民与归属变化。
- **失败后继续生活。** 玩家死亡后约2.5秒回到安全起点，保留85%的身高且不低于起始身高；超过新容量的食物留在死亡处。复活有3秒保护，世界与已有关系不因此重置。

### 对话如何影响世界

每个符合条件的听众由一次 **DeepSeek** 请求同时判断相关性并生成对白与行动。无关内容不显示回复；涉及赠粮、跟随或攻击等动作时，游戏会重新检查距离、生命状态、权限和真实资源。

当前默认交谈距离为 **40米**，双方身高比不超过 **5倍**；广播最多选择3位候选，主动对话链最多两句实际回复。NPC回复通过头顶气泡显示，回复期间短暂停步并面向交谈对象，随后恢复行动。模型可能拒绝或误解，任意口头承诺并不会自动变成新玩法。

当前链路无需本地 StartLux 或 Qwen。云端服务缺失时，移动、成长、战斗和居民的规则行为仍可运行。

## 操作

| 输入 | 行为 |
|---|---|
| WASD / 鼠标 / Shift | 移动 / 观察 / 奔跑 |
| 鼠标左键 / 右键 / 空格 | 轻击 / 重击 / 闪避 |
| 按住 E | 连续获取附近食物或尸体食物，受档位与容量限制 |
| Q | 查看自动成长提示，不额外消耗食物 |
| F / Shift + F | 分享或供奉 / 在母树附近批量供奉 |
| T | 打开对话输入并暂停场景，可选广播或指定对象 |
| Enter / Esc | 发送并恢复场景 / 取消输入 |
| B / G | 养护土壤 / 接管或管理果林 |
| Tab / PageUp、PageDown | 打开生态手记 / 翻阅记录 |
| H | 操作帮助 |
| R | 手动重新开始整局 |

**开发模式体型预览：** 按住 `[` / `]` 连续缩小／放大，小键盘 `-` / `+` 等效；每秒按÷1.5／×1.5变化，松开停止。按 `\` 恢复起始大小并退出预览。预览范围约0.18–36米，保留碰撞检查并暂停自动成长；此功能不进入 Shipping 构建，也不代表正常玩法的成长上限。

## 从源码启动

需要 Windows、Unreal Engine **5.8.3**、对应 C++ 编译工具链及 Git LFS。工程入口仍为 `Verdant.uproject`。

```powershell
git clone git@github.com:droidlyx/Vibehacks06_lyx.git
cd Vibehacks06_lyx
git lfs install
git lfs pull
```

在启动环境设置 `DEEPSEEK_API_KEY`，或将密钥单独保存在 `Local/Secrets/deepseek.key`。当前使用七牛兼容接口 `https://api.qnaigc.com/v1` 和模型 `deepseek/deepseek-v4.1-flash-1017`，详见[AI配置](Docs/LocalAI.md)。密钥不进入 Git，也不随便携包分发。

关闭编辑器后编译，再打开项目并手动点击 Play：

```powershell
# 可通过 UE_ROOT 指定本机引擎目录
powershell -ExecutionPolicy Bypass -File Tools/Build.ps1
.\Open-Editor.cmd
```

也可使用 `Start-Game.ps1 -Editor -NoAI` 启动不发送模型请求的编辑器进程。部分授权音效只保存在本机，其他机器需按[音频说明](Assets/Audio/README.md)重建；素材准备工具中的本机路径也需按环境配置。

如需独立构建，运行 `Tools/Build.ps1 -Package -Version my-build`，再用 `Tools/Stage-Portable.ps1 -Version my-build` 整理对应版本。历史 `Builds/v0.3/Windows` 属于旧版本，不能代替当前源码构建。

## 当前边界与资料

这是持续迭代的原型，尚无存档、多人联机或完整剧情战役。当前地形、对话、移动和界面的实际体验以人工试玩为准；历史测试结果只适用于各自记录的版本。

- [玩法与接入记录](Docs/RuntimeIntegration.zh-CN.md)：人口、食物、成长与复活。
- [对话架构](Docs/ConversationRuntime.zh-CN.md)：调度、回应与行为边界。
- [分形地形](Docs/FractalTerrain.md) · [环境与天空](Docs/EnvironmentLook.md)：地形规则和视觉迭代。
- [开发记录](Docs/DevelopmentLog.md)：各阶段修改、验证与待办。
- [素材说明](Assets/README.md) · [音频来源](Assets/Audio/README.md)：Project Titan、Quaternius、Kenney及音乐的来源与使用范围。Titan素材不属于CC0，项目中的不同素材不共享同一许可。

历史工程标识为 **Verdant Accord / Verdant**；本项目展示名为 **快快长大（Grow up）**。
