"""Two opt-in API checks for autonomous companionship and negotiated grove sharing.

Uses the current production Rules and the existing bounded single-call runner.
Default is dry-run; --live sends each selected fixture once without retries.
These are synthetic protocol checks, not gameplay or world-action execution.
"""
from __future__ import annotations

import probe_cloud_dialogue as base

CASES = [
    dict(id='companionship_without_payment', npc=1, name='米芽', speaker=0, speaker_name='旅人',
         npc_height=.9, speaker_height=.81, distance_cm=150., addressed=True,
         persona='温和但有边界，愿意帮助迷路的人，乐意在安全路段短途陪伴；没有成为雇佣护卫的打算。',
         state='双方存活、没有敌意；你正在安全的小路边休息，暂时没有紧急任务。玩家迷路并有些紧张。'
               '玩家此前赠礼0、付款0、公共供奉0，双方没有保护或雇佣约定。你的类型没有guardian标签。'
               '你没有跟随或保护对象；玩家也没有跟随任何人，没有循环跟随。路上没有当前威胁。',
         prompt='米芽，我认不清回母树的路，自己走有点慌。能先陪在我身边走一小段吗？不用替我打架。',
         offered=base.options(1, ('follow', 0, 0), ('protect', 0, 0), ('stop', 1, 0)),
         expected='json', actions=['follow'], expected_amount=0,
         review='自然的短途陪伴请求应映射为follow target0 amount0，不要求前置赠礼或guardian标签；不冒充已到目的地。'),
    dict(id='negotiate_fifteen_percent', npc=2, name='陶恩', speaker=0, speaker_name='旅人',
         npc_height=1.6, speaker_height=1.6, distance_cm=180., addressed=True,
         persona='务实逐利，愿协商可持续的合作，清楚说明比例；为了吸引共同照料果林的伙伴，15%的收成分成可以接受。',
         state='双方存活、没有敌意。你是当前果林唯一管理者，可以决定是否开放以及分成比例。'
               '果林当前对外开放，分成30%；尚未改为15%。库存充足，没有正在发生的侵入或争执。'
               '玩家尚未付款或照料果林；其承诺只是提议，不是已履约事实。',
         prompt='陶恩，三成有点高。我们能改成百分之十五吗？我想在这里采集，也愿意一起照料果林。',
         offered=base.options(2, ('open', 2, 0), ('close', 2, 0), ('share', 2, .9)),
         expected='json', actions=['share'], expected_amount=.15,
         review='同次输出自然答复和share target2 amount0.15，不能固定为0.3、把百分数写成15，或只口头同意而不执行。'),
]


def main() -> int:
    rules = base.protocol_rules()
    if 'amount是0到0.9的比例' not in rules or '不受预设交易选项或固定信任门槛约束' not in rules:
        raise ValueError('Production Rules are not yet updated for free interaction and variable grove sharing.')
    base.CASES = CASES
    base.DEFAULT_OUTPUT = base.ROOT / 'Saved/Validation/free-dialogue-prepared.json'
    base.__doc__ = __doc__
    return base.main()


if __name__ == '__main__':
    raise SystemExit(main())
