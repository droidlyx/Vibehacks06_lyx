# Grow_up · 快快长大

从 10 厘米高的小生物开始，在山谷中采集食物、逐渐长大，与居民交谈、分享、合作或战斗。当前新旅程默认生成 200 位居民；帮助居民完成心愿、获得 12 次祝福，并让身高超过 10 米，达成通关目标。

对话会结合角色身份、记忆和现场状态提出行动，由游戏检查可行性后执行。口头承诺不会直接变成实际资源或行动。支持角色查找、对话历史、望远镜、五个存档栏位及游戏内 API 配置。

## 下载与启动

**最终 Windows 包 · 2026-11-10 · Shipping（r4）**

### 下载最终游戏包（分卷，总计约 1.12 GB）

大文件网络传输不稳定，因此使用 17 个分卷保存同一个完整 ZIP，文件内容经 SHA-256 校验。

1. 下载下面全部分卷，以及 [Join-Game.cmd](Downloads/Join-Game.cmd)、[Join-Game.ps1](Downloads/Join-Game.ps1)、[parts.sha256](Downloads/parts.sha256)、[SHA256SUMS.txt](Downloads/SHA256SUMS.txt)，放在同一文件夹。脚本和校验文件在 GitHub 文件页选择 Download raw file 下载。
2. 双击 `Join-Game.cmd`，生成并校验 `Grow_up_final-Windows.zip`。约需 2.3 GB 空间同时保存分卷和 ZIP，解压另需空间。
3. 完整解压，双击 `Grow_up_final/Grow_up.exe`。保留 `Grow_up/`、`Engine/` 等配套目录，不能只复制 EXE。无需 Unreal Editor 或 Python。若缺少运行库，运行包内 `Engine/Extras/Redist/en-us/vc_redist.x64.exe`。

<details>
<summary>展开全部分卷下载链接</summary>

- [Grow_up_final-Windows.zip.001](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.001)
- [Grow_up_final-Windows.zip.002](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.002)
- [Grow_up_final-Windows.zip.003](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.003)
- [Grow_up_final-Windows.zip.004](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.004)
- [Grow_up_final-Windows.zip.005](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.005)
- [Grow_up_final-Windows.zip.006](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.006)
- [Grow_up_final-Windows.zip.007](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.007)
- [Grow_up_final-Windows.zip.008](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.008)
- [Grow_up_final-Windows.zip.009](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.009)
- [Grow_up_final-Windows.zip.010](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.010)
- [Grow_up_final-Windows.zip.011](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.011)
- [Grow_up_final-Windows.zip.012](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.012)
- [Grow_up_final-Windows.zip.013](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.013)
- [Grow_up_final-Windows.zip.014](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.014)
- [Grow_up_final-Windows.zip.015](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.015)
- [Grow_up_final-Windows.zip.016](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.016)
- [Grow_up_final-Windows.zip.017](https://media.githubusercontent.com/media/droidlyx/Vibehacks06_lyx/main/Downloads/Grow_up_final-Windows.zip.017)

</details>

也可以安装 Git LFS 后克隆本仓库，执行 `git lfs pull`，再运行 `Downloads/Join-Game.cmd`。GitHub 的“Download ZIP”源码快照不保证包含真实 LFS 文件，请使用上述下载方式。合并脚本会检查分卷和完整 ZIP，遇到缺失或 LFS 指针文件会停止并提示。

本仓库用于发布可玩成品；[最新源码、资源与开发文档](https://github.com/droidlyx/Grow_up)已迁移至独立项目仓库。历史提交仍保留。

## 最新版本

- Windows x64 **Shipping** 正式便携包，直接运行 `Grow_up.exe`。
- 全屏与无边框采用显示器原生分辨率；调整设置面板宽度，API 按钮使用单行文字。
- 新增 **0–200% 全局音量**，音乐和音效保留独立控制；点击“保存游戏设置”应用。
- 开局说明补齐望远镜操作。
- 新的 API 连接需先测试成功，再保存应用；无有效自定义连接时使用本机提供的默认七牛配置。
- 默认七牛接口直连，保留 HTTPS 校验；API 记录包含失败类别与耗时。
- 存档保存在 `%LOCALAPPDATA%\Grow_up\Saved\SaveGames`，更新游戏目录时继续保留。

## 操作

| 操作 | 按键 |
|---|---|
| 移动 / 观察 | WASD / 鼠标 |
| 跳跃 / 奔跑 / 闪避 | 空格 / Shift / 左 Ctrl |
| 采集 / 聊天 | 按住 E / T |
| 分享、归还 / 抢夺 | F / R |
| 轻击 / 重击 | 鼠标左键 / 右键 |
| 望远镜 / 调节 2–8 倍 | B / 滚轮 |
| 对话历史 / 查找居民 | J / N |
| 帮助 / 暂停、存读档、设置 | H / Esc |

## API 配置

公开包**不包含开发者 API key、SSH key、存档或本机配置**。默认服务预设为七牛 `https://api.qnaigc.com/v1/chat/completions`，模型为 `deepseek/deepseek-v4.1-flash-1017`。

进入 **Esc → API 设置**，选择服务，填写自己的模型和 key，点击“测试连接”，成功后“保存并应用”。需要下次启动继续使用时，勾选“在这台电脑记住密钥”。也可在游戏根目录自行创建 `Local/Secrets/deepseek.key`，只写入自己的七牛 key，然后直接启动 EXE。调用费用计入所用 key 的账户。

未配置 key 时可游玩规则驱动的部分，但远程模型对话不可用。新的配置未测试成功前，不替换当前有效连接；默认连接只有在本机提供默认 key 时才能发起请求。

## 存档与验证状态

存档文件为 `Journey_1.json` 等，`.bak` 为备份。完整游戏包不附带开发者存档。Shipping 编译与打包成功；默认 key 在本机的独立最小请求返回 HTTP 200，但不把该检查等同于打包游戏内全部对话或玩法验收。

## 素材与许可

请随游戏保留 [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) 和包内许可文件。Unreal、Project Titan、Quaternius 及音频等素材各自适用其原有条款；本项目未授予统一的素材再分发许可。
