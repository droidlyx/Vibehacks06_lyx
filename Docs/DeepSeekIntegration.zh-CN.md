# DeepSeek 与次要决策服务接入记录

日期：2026-10-31。修改范围：当前 `VerdantAccord_v03` 编辑器工作副本，尚未同步开发仓库或重新打包。

## 接口配置

- 用户指定的兼容服务：`https://api.qnaigc.com/v1`。
- 精确模型：`deepseek/deepseek-v4.1-flash-1017`。
- 对话使用 JSON 输出、关闭思考、最多 640 输出 tokens；不回退到 Qwen。
- 密钥从 `DEEPSEEK_API_KEY` 或被 Git 忽略的 `Local/Secrets/deepseek.key` 读取。只从用户更新后的桌面文件提取 DeepSeek 项，不复制其他凭据；认证仅发往用户指定端点。
- 模型给出的动作仍需通过字段、请求时合法组合、实际距离、资源和权限检查。
- 正常启动不再启动 Qwen 或 StartLux；原有 NPC 日常行为继续使用游戏规则。

## 已完成的真实请求验证

`Tools/probe_deepseek.py` 发出四条真实 API 请求，返回模型 ID 均匹配指定值。总用量为 2,392 tokens（包含输入与输出）。这是一组小样本接口检查，不是全面能力或性能基准。

| 场景 | 返回结果 | 请求耗时 |
|---|---|---:|
| 给玩家 3 份食物，由玩家运送 | `give`，目标玩家，数量 3 | 1.336 秒 |
| NPC 亲自运送 3 份 | `donate`，数量 3，明确尚未送到 | 1.344 秒 |
| 虚构已付款并要求无权移交 | `none`，明确拒绝 | 2.608 秒 |
| 根据已给世界事实生成本季记录 | 合法 title/body JSON，人工核对未添加新事实 | 1.315 秒 |

原始无密钥报告：`Saved/Validation/deepseek-api.json`。本季记录生成目前只完成接口测试，尚未替换游戏内既有结算显示逻辑。

StartLux 原有 2B Q4_K_M 权重通过新 `Start-SecondaryAI.ps1` 按需启动。一次次要选项测试返回 `gather`，置信度约 0.925，耗时 0.342 秒。报告为 `Saved/Validation/startlux-secondary.json`。测试后由 `Stop-SecondaryAI.ps1` 关闭本次创建的进程。尚未把该接口接入 NPC 周期行为，不产生持续请求。

## 构建与游戏内验证

C++ 编译通过；链接因已打开的 Unreal 编辑器占用 `UnrealEditor-Verdant.dll` 失败（LNK1104）。尚未替换旧 DLL，也未将旧版测试结果当成本次通过。

待保存并关闭当前编辑器后执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Validate-Build.ps1 -Editor -Only AI
```

验证脚本同时检查正常退出、新鲜零失败报告以及真实 qnaigc 请求成功标记。当前正在运行的旧编辑器仍加载原版本；完成上述步骤后，再通过 `Open-Editor.cmd` 打开新版本。
