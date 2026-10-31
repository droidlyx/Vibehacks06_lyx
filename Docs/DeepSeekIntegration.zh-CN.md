# DeepSeek 接入与验证记录

日期：2026-10-31。修改范围：当前 `VerdantAccord_v03` 编辑器工作副本，尚未同步开发仓库或重新打包。

## 当前接口与运行架构

- 用户指定的兼容服务：`https://api.qnaigc.com/v1`。
- 精确模型：`deepseek/deepseek-v4.1-flash-1017`。
- 每个符合条件的候选NPC只调用一次DeepSeek，同时判断相关性和生成回应；没有独立相关性gate。
- 无关时只返回字面`无关`并静默结束。相关时返回严格的`say/action/target/amount`四字段JSON。
- 关闭思考，温度0.3，最多256输出tokens；不使用`stop`参数。服务不支持`response_format=json_object`，改由提示词约束和游戏内严格JSON校验。
- 候选双方实际三维距离不超过40米，默认`[Verdant.Dialogue] RadiusCm=4000`；较高/较矮身高比不超过5，默认`MaxHeightRatio=5`
  ，恰好5倍仍符合体型条件。指定NPC仅1次请求；广播最多3个初始候选，玩家线程最多3句回答，NPC主动链仍最多2句。
- 密钥从 `DEEPSEEK_API_KEY` 或被 Git 忽略的 `Local/Secrets/deepseek.key` 读取。只从用户更新后的桌面文件提取 DeepSeek
  项，不复制其他凭据；认证仅发往用户指定端点。
- 模型给出的动作仍需通过字段、请求时合法组合、实际距离、资源和权限检查。
- 正常启动不再启动Qwen或StartLux，便携打包不再复制本地推理运行时和模型；原有NPC日常行为继续使用游戏规则。已存在的旧构建不会自动更新。

## 当前改版验证范围

- 历史2倍身高比版本的距离和体型纯规则检查25/25通过：`Saved/Validation/cloud-dialogue-switch-policy.json`。
- 前一版字面`不相关`协议的6组真实API检查6/6通过，总用量3,620 tokens。
  报告为`Saved/Validation/cloud-dialogue-switch-live.json`。这是历史结果，不覆盖最终字面`无关`协议。
- 最终字面`无关`协议原生编译通过，记录为`Saved/cloud-dialogue-final-build.log`。
- 最终协议3组真实API检查3/3通过：`Saved/Validation/cloud-dialogue-switch-wuguan-live.json`。每例仅一次请求、无重试；无关闲聊和无新事件各只返回
  `无关`，各1个输出token；相关赠粮同次返回对白及`give/target=0/amount=2`，35个输出tokens。总用量1,584
  tokens，耗时1.03–1.83秒。使用合成近邻状态，未执行游戏调度器或世界动作。
- 当前工作副本的StartLux和旧同一游戏构建中的Qwen已按进程归属核验关闭；检查时8066、8081、8090端口均无监听。本地权重保留。
- 尚未完成全部20种NPC的32组内容评审。游戏内对话、动作和拥挤场景仍待用户验收。

完整架构见[按需对话与持续行为](ConversationRuntime.zh-CN.md)。

## 历史：最初接入的真实请求验证

`Tools/probe_deepseek.py` 发出四条真实 API 请求，返回模型 ID 均匹配指定值。总用量为 2,392
tokens（包含输入与输出）。这是一组小样本接口检查，不是全面能力或性能基准。

| 场景                         | 返回结果                                   | 请求耗时 |
|------------------------------|--------------------------------------------|---------:|
| 给玩家 3 份食物，由玩家运送  | `give`，目标玩家，数量 3                   | 1.336 秒 |
| NPC 亲自运送 3 份            | `donate`，数量 3，明确尚未送到             | 1.344 秒 |
| 虚构已付款并要求无权移交     | `none`，明确拒绝                           | 2.608 秒 |
| 根据已给世界事实生成本季记录 | 合法 title/body JSON，人工核对未添加新事实 | 1.315 秒 |

原始无密钥报告：`Saved/Validation/deepseek-api.json`。本季记录生成目前只完成接口测试，尚未替换游戏内既有结算显示逻辑。

当时使用原有2B Q4_K_M权重，通过`Start-SecondaryAI.ps1`按需启动StartLux。一次次要选项测试返回`gather`
，置信度约0.925，耗时0.342秒，报告为`Saved/Validation/startlux-secondary.json`。这是历史实验；StartLux已退出当前调用链，正常运行无需启动该脚本。

## 历史：最初接入时的构建状态

C++编译当时通过；链接因已打开的Unreal编辑器占用`UnrealEditor-Verdant.dll`失败（LNK1104）。以下为当时记录的后续命令，不代表当前编辑器状态或当前协议验证方法。

当时计划保存并关闭编辑器后执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Validate-Build.ps1 -Editor -Only AI
```

旧验证脚本用于当时的正常退出、报告与请求检查。当前协议验证应使用本页所列专项报告；历史玩法探针可能仍含旧协议假设，不能据此宣称最新游戏内链路通过。
