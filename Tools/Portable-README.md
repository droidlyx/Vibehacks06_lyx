# 快快长大（Grow up）— Windows 游戏包

请完整解压文件夹后双击 `Play.cmd`。保留 `Verdant.exe`、`Verdant/`、`Engine/` 和其他随包文件的相对位置，不要只复制一个
EXE。无需安装 Unreal 编辑器、Python、Qwen 或 StartLux。

如果 Windows 提示缺少运行库，运行包内 `Engine/Extras/Redist/en-us/vc_redist.x64.exe` 安装或修复 x64 运行库，然后重新启动游戏。

## 对话服务

当前游戏使用七牛兼容接口 `https://api.qnaigc.com/v1` 的 `deepseek/deepseek-v4.1-flash-1017`。
先生成初步意愿，由游戏检查行动可行性，再生成最终对白并在发布时复验；相关回应可能使用两次顺序请求，无关内容可提前结束。
联网对话需要接收者自己的该服务 API 密钥，调用费用由接收者的服务账户承担；本包不含开发者密钥或本地推理模型。

在启动环境中设置 `DEEPSEEK_API_KEY`，或自行创建 `Local/Secrets/deepseek.key`，文件中只放密钥，然后通过 `Play.cmd`
启动。密钥只留在自己的电脑上；转交游戏包前不要带上自行创建的 `Local/` 目录。详细配置见 [Docs/LocalAI.md](Docs/LocalAI.md)。

无需模型对话时，在游戏包目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Start-Game.ps1 -NoAI
```

移动、战斗、成长和由规则驱动的居民行为可继续运行；缺少凭据或服务失败时，对话会显示对应状态，不会自动回退到本地模型。

## 游玩与版本边界

WASD 移动，鼠标观察，Shift 奔跑；其他操作和交互以游戏内提示为准。设计说明见 [交互说明](Docs/InteractionDesign.zh-CN.md)
与 [食物和成长](Docs/FoodGrowthDesign.zh-CN.md)。

这是当前源码的新构建，不是历史 v0.3 包。随附开发文档中的旧测试、截图或概念不能作为本包已通过人工试玩、视觉或混音验收的证明；具体构建与验收结果以本次交付说明为准。
`portable-files.json` 记录发行说明和来源文件的版本及哈希，不代表玩法测试通过。

## 素材来源

署名和来源范围见 `THIRD-PARTY-NOTICES.txt`，许可原文与素材记录位于 `Assets/`。游戏中的第三方资源以 cooked
游戏内容随包提供；本包不附原始音频、可编辑素材库或本地模型权重。请连同这些说明分发完整游戏包，不要把第三方游戏资源作为独立素材重新分发。
