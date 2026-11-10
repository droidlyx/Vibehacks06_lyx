# Grow_up · 快快长大

从 10 厘米高的小生物开始，在山谷中采集食物、逐渐长大，与有各自身份、心愿和关系的居民交谈、分享、合作或战斗。

游戏使用 Unreal Engine 5.8 / C++ 开发。对话结合角色资料、记忆和现场状态提出行动，再由游戏检查可行性；口头承诺不会直接变成实际资源或行动。

## 下载最新版本

**2026-11-10 · Characters.json 角色配置版 · Windows x64 Shipping**

### [下载完整游戏 ZIP（约 1.12 GB）](https://github.com/droidlyx/Vibehacks06_lyx/releases/download/v2026.11.10-characters/Grow_up-Windows-Characters.zip)

[Release 页面](https://github.com/droidlyx/Vibehacks06_lyx/releases/tag/v2026.11.10-characters) · [SHA-256 校验文件](https://github.com/droidlyx/Vibehacks06_lyx/releases/download/v2026.11.10-characters/SHA256SUMS.txt) · [全部版本](https://github.com/droidlyx/Vibehacks06_lyx/releases)

1. 下载并完整解压 `Grow_up-Windows-Characters.zip`。
2. 双击 `Grow_up_Characters/Grow_up.exe` 启动。
3. 保留 `Grow_up/`、`Engine/` 等配套目录，不能只复制 EXE。

无需安装 Unreal Editor、Python 或 Git LFS，也不需要合并分卷。若 Windows 提示缺少运行库，运行包内 `Engine/Extras/Redist/en-us/vc_redist.x64.exe`。GitHub 的 **Code → Download ZIP** 是本仓库源码快照，不是上述可玩游戏包。

## 最新版本内容

- **外部角色配置**：角色资料现在从 `Characters.json` 读取，本版附带 201 条角色记录。
- **成长与居民互动**：采集食物、成长、分享、抢夺和战斗；与居民交谈并帮助他们完成心愿、收集祝福。
- **探索与查找**：望远镜支持 2–8 倍调节，提供居民查找、对话历史和操作帮助。
- **保存进度**：五个存档栏位，存档与游戏安装目录分离，更新游戏包时可继续保留。
- **显示与声音**：全屏和无边框使用显示器原生分辨率；全局音量范围 0–200%，音乐与音效分别调节。
- **游戏内 API 设置**：修复按钮换行，新连接需测试成功并保存后生效，并记录网络失败类别和耗时。

## Characters.json 在哪里

角色文件已包含在 Release 的完整 ZIP 中。解压后的目录关系如下：

```text
Grow_up_Characters/
├─ Grow_up.exe
├─ Grow_up/
│  ├─ Characters.json
│  ├─ Binaries/
│  └─ Content/
└─ Engine/
```

文件应位于 **`Grow_up/Characters.json`**，与运行时代码的 `ProjectDir()/Characters.json` 对应；不要误放在外层 EXE 旁。私有源码工程中的对应位置是项目根目录 `Characters.json`。如自行编辑，请先备份并保持合法的 JSON 格式。

本版角色文件 SHA-256：

```text
95c5f9ed6d6bf5361c06e42205b77f43cc3be90a302d97ce2adc2067cbea64e0
```

## 操作

| 操作 | 按键 |
|---|---|
| 移动 / 观察 | WASD / 鼠标 |
| 跳跃 / 奔跑 / 闪避 | 空格 / Shift / 左 Ctrl |
| 攀爬 | 靠近陡坡并向前移动 |
| 采集 / 聊天 | 按住 E / T |
| 分享、归还 / 抢夺 | F / R |
| 轻击 / 重击 | 鼠标左键 / 右键 |
| 望远镜 / 调节倍率 | B / 滚轮 |
| 对话历史 / 查找居民 | J / N |
| 操作帮助 / 暂停、存读档、设置 | H / Esc |

音量修改后点击“保存游戏设置”应用并保存。具体交互条件以游戏内提示为准。

## 配置模型对话

公开包**不含开发者 API key、SSH key 或存档**。未配置自己的 key 时，可以体验规则驱动的玩法，远程模型对话不可用。

进入 **Esc → API 设置**，选择服务、填写模型与 key，点击“测试连接”，成功后“保存并应用”。需要下次启动继续使用时，勾选“在这台电脑记住密钥”。未通过测试的新配置不会替换当前有效连接。

默认七牛预设：

- 接口：`https://api.qnaigc.com/v1/chat/completions`
- 模型：`deepseek/deepseek-v4.1-flash-1017`

也可在解压后的游戏根目录自行创建 `Local/Secrets/deepseek.key`，文件中只填写自己的七牛 key，再直接运行 EXE。默认连接只有在本机提供了默认 key 时才能使用；清空远程 key 并保存会恢复该默认配置。接口调用费用计入所用 key 的账户。请勿将自己的密钥随游戏转发。

## 存档位置

```text
%LOCALAPPDATA%\Grow_up\Saved\SaveGames
```

存档文件为 `Journey_1.json` 等，`.bak` 是备份。更新游戏包时保留该目录即可；Release 不附带开发者的游戏进度。

## 源码与版本对应

本仓库的源码目录按此前约定保留历史版本；**最新可玩版本请以 Release 为准**。

最新代码和角色资料保存在私有仓库 [droidlyx/Grow_up](https://github.com/droidlyx/Grow_up)，需要仓库访问权限。本次 Release 对应源码提交 `e05898171fe6a61344b37ef7fc8c4c3750e4ebb2`。

本版已完成离线 Shipping 编译、打包，以及 ZIP 和角色文件哈希校验；未运行额外玩法测试。历史开发文档和旧测试记录不代表最新版的完整体验验收。

## 素材与许可

请保留游戏包内的 `THIRD-PARTY-NOTICES.txt` 和各项许可说明。Unreal、Project Titan、Quaternius、Animal Variety Pack、音频与字体等素材各自适用原有条款；本仓库不授予统一的第三方素材再分发许可。
