# Grow_up · 快快长大 — Windows 最终版

下载附件 **Grow_up_final-Windows.zip**（约 1.12 GB），完整解压后双击 `Grow_up_final/Grow_up.exe`。这是 Windows x64 Shipping 便携包，无需 Unreal Editor 或 Python；请保留配套资源目录。

## 本版内容

- 从 10 厘米开始成长，与居民交谈、合作、分享或战斗；默认新旅程 200 位居民。
- 完成心愿收集祝福，支持五个存档栏位、对话历史和居民查找。
- B 切换望远镜，滚轮调节 2–8 倍。
- 全屏与无边框采用显示器原生分辨率；修复设置面板及 API 按钮布局。
- 全局音量 0–200%，音乐和音效独立调节。
- 新 API 连接需测试成功并保存后生效，增加网络失败诊断。

## 启动与 API

公开包不含开发者 API key、SSH key 或存档。进入 Esc → API 设置，填写自己的服务、模型及 key，测试成功后保存。也可自行创建 `Local/Secrets/deepseek.key`，填写自己的七牛 key。没有 key 时仍可体验规则驱动玩法，远程模型对话不可用。

默认七牛预设：`https://api.qnaigc.com/v1/chat/completions`，模型 `deepseek/deepseek-v4.1-flash-1017`。调用费用由所用 key 的账户承担。

存档：`%LOCALAPPDATA%\Grow_up\Saved\SaveGames`。如缺少运行库，运行包内 `Engine/Extras/Redist/en-us/vc_redist.x64.exe`。

附件 `SHA256SUMS.txt` 可校验 ZIP。完整 ZIP 由已发布的 17 个分卷在 GitHub 服务器端合并，未重新构建或更换游戏内容。Shipping 编译与打包成功，不代表所有玩法或在线服务均完成人工验收。

[最新源码与开发文档](https://github.com/droidlyx/Grow_up) · 第三方许可见包内 `THIRD-PARTY-NOTICES.txt`。
