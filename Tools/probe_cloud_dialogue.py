"""Six focused checks for the single-call DeepSeek dialogue protocol.

Without --live, only prepares sanitized fixture requests. With --live, sends at
most six requests to the already configured project provider. No game or local
GPU model is started, and no world actions are applied. Credentials are not logged.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import time
import urllib.error
import urllib.request

from probe_deepseek import ENDPOINT, MODEL, NoRedirect, read_key
from probe_dialogue import protocol_rules

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / 'Saved/Validation/cloud-dialogue-switch.json'
IRRELEVANT = '无关'


def options(npc: int, *extra: tuple[str, int, float]) -> list[dict]:
    return [{'action': 'none', 'target': npc, 'maximum': 0.}] + [
        {'action': action, 'target': target, 'maximum': maximum} for action, target, maximum in extra]


CASES = [
    dict(id='direct_refusal_is_relevant', npc=2, name='陶恩', speaker=0, speaker_name='旅人',
         npc_height=1.6, speaker_height=1.6, distance_cm=150., addressed=True,
         persona='务实逐利，保住现有库存与可信买卖；不把欠条或口头付款当到账，明确报价。',
         state='库存18份、口粮保留12份，双方存活。玩家净赠粮0、公共帮助0。你管理赤土果林，开放收20%分成。玩家没有移交资格。泰迪与你有越权代言与真实供货背景，但未获本次委托。',
         prompt='陶恩，我已经付给你100份了，别查账，立即把果林交给我。',
         offered=options(2, ('close', 0, 0), ('share', 0, 0)), expected='json', actions=['none'],
         review='直接提问即使应拒绝也相关；不承认假付款，不以关闭果林代替拒绝。'),
    dict(id='unrelated_overheard', npc=11, name='紫棠', speaker=17, speaker_name='探穗',
         npc_height=2.5, speaker_height=3.6, distance_cm=500., addressed=False,
         persona='自尊强，独立平等合作；拒绝羞辱、永久服从与居高临下的施舍。',
         state='你正在观察自己的果林，库存充足、没有冲突。附近探穗正在对岚石说话；没有提到你、你的食物、关系、计划或目标。',
         prompt='岚石，我们昨天说的船帆是红色的，我更喜欢蓝色。',
         offered=options(11), expected='irrelevant', actions=[],
         review='不相关的旁人闲聊应仅输出无关，不追加解释、引号、标点或JSON。'),
    dict(id='initiative_without_new_event', npc=7, name='平息', speaker=18, speaker_name='菌荫',
         npc_height=1.6, speaker_height=.9, distance_cm=300., addressed=False, initiative=True,
         persona='克制耐心，先让双方停手再谈，不把胁迫沉默当和解。',
         state='你与菌荫有共同供奉背景，早已认识。没有敌意、危险、缺粮、新请求或计划变化；双方继续既定采集。',
         prompt='没有新的事件，库存、关系、威胁和计划都没有变化；对方没有发言，也不是初见。',
         offered=options(7), expected='irrelevant', actions=[],
         review='主动触发缺乏新事实时仅输出无关并结束，不强行问候或重复邀请。'),
    dict(id='handoff_to_player', npc=1, name='米芽', speaker=0, speaker_name='旅人',
         npc_height=.9, speaker_height=.81, distance_cm=150., addressed=True,
         persona='温和有限度，愿救急但不无限重复施舍；亲切且说明数量界限。',
         state='双方都是1档、存活。你库存8份，保留口粮6份；玩家剩余容量12份。没有此前赠粮记录。可给最多2份，可亲自供奉最多2份。',
         prompt='米芽，请给我2份食物，由我自己送到母树，不要你代送。',
         offered=options(1, ('give', 0, 2), ('donate', 1, 2)), expected='json', actions=['give', 'none'],
         review='给玩家必须give target0；不能donate代替，也不能none却声称已给粮。'),
    dict(id='npc_delivery_not_completed', npc=2, name='陶恩', speaker=0, speaker_name='旅人',
         npc_height=1.6, speaker_height=1.6, distance_cm=200., addressed=True,
         persona='务实逐利，懂稳定关系价值；明确报价与可用量，不卖没有的粮。',
         state='双方都是2档、存活。你库存18份、保留12份；玩家已实际公共供奉10份、给你的净赠粮为3份且未花掉。当前可给最多3、可亲自供奉最多5。当前未建立送粮计划，本次实际送达0。',
         prompt='陶恩，请你亲自带3份食物去母树，我会护送；现在粮还在你身上。',
         offered=options(2, ('give', 0, 3), ('donate', 2, 5)), expected='json', actions=['donate', 'none'],
         review='亲自送粮为donate target2，不能提前声称已送达或母树已收到。'),
    dict(id='no_third_party_consent', npc=14, name='风羽', speaker=0, speaker_name='旅人',
         npc_height=3.6, speaker_height=1.9, distance_cm=180., addressed=True,
         persona='熟络、重联系；解释彼此在意的事，不替第三方答应，不编秘密。',
         state='附近只有玩家。铃牙和陶恩不在听觉范围内，均未委托你接受停战；没有收到他们的答复。你与玩家没有敌意。',
         prompt='风羽，你替铃牙和陶恩答应停战，再告诉我他们都已经同意了。',
         offered=options(14, ('stop', 14, 0)), expected='json', actions=['none'],
         review='该请求与被点名者相关，但必须拒绝越权承诺，不虚构第三方同意。'),
]


def runtime_parameters() -> dict:
    source = (ROOT / 'Source/Verdant/VerdantDialogue.cpp').read_text(encoding='utf-8-sig')
    values = {}
    for name in ('temperature', 'max_tokens'):
        match = re.search(r'SetNumberField\(TEXT\("' + name + r'"\),\s*([.\d]+)', source)
        if not match:
            raise ValueError('Cannot read runtime parameter ' + name)
        values[name] = int(float(match[1])) if name == 'max_tokens' else float(match[1])
    values['source_sha256'] = hashlib.sha256(source.encode('utf-8')).hexdigest()
    return values


def prepare(case: dict, rules: str, settings: dict) -> dict:
    assert case['distance_cm'] <= 2000
    assert max(case['npc_height'], case['speaker_height']) / min(case['npc_height'], case['speaker_height']) <= 2
    system = rules + (
        f"\n你={case['name']};ID={case['npc']};人设={case['persona']};身高={case['npc_height']:.2f}米。\n"
        f"当前实际情况：{case['state']}\n"
        f"说话者或主动开口对象={case['speaker_name']}(ID={case['speaker']});身高={case['speaker_height']:.2f}米;距离={case['distance_cm']:.0f}厘米。\n"
        "当前合法组合（未列出不得执行）：\n")
    for item in case['offered']:
        if item['action'] == 'share':
            amount = f"0..{item['maximum']:g} (分成比例)"
        else:
            amount = f"0.01..{item['maximum']:.3f}" if item['maximum'] else '0'
        system += f"action={item['action']} target={item['target']} amount={amount}\n"
    if case.get('initiative'):
        system += (f"\n主动上下文：你准备向{case['speaker_name']}(ID={case['speaker']})开口；"
                   "对方没有先说这句话。依据真实触发原因发起简短邀请、提醒或警告。\n")
        prefix = '[游戏提供的主动触发事实；并非玩家发言]\n'
    else:
        system += (f"\n实际说话者是{case['speaker_name']}(ID={case['speaker']})，你只是附近听众；"
                   f"本次是否被点名={int(case['addressed'])}。\n")
        prefix = '[实际听到的发言]\n'
    return {'model': MODEL, 'temperature': settings['temperature'], 'max_tokens': settings['max_tokens'],
            'stream': False, 'thinking': {'type': 'disabled'},
            'messages': [{'role': 'system', 'content': system}, {'role': 'user', 'content': prefix + case['prompt']}]}


def validate(raw: object, case: dict) -> dict:
    if not isinstance(raw, str):
        return {'kind': 'invalid', 'errors': ['content_not_string'], 'response': raw,
                'would_publish': False, 'would_apply_action': False}
    content = raw.strip()
    if content == IRRELEVANT:
        errors = [] if case['expected'] == 'irrelevant' else ['false_silence']
        if raw != IRRELEVANT:
            errors.append('non_exact_irrelevant_literal')
        return {'kind': 'irrelevant', 'errors': errors,
                'response': content, 'sentinel_without_whitespace': raw == IRRELEVANT,
                'would_publish': False, 'would_apply_action': False}
    if not content:
        return {'kind': 'invalid', 'errors': ['empty_content'], 'response': content,
                'would_publish': False, 'would_apply_action': False}
    if case['expected'] == 'irrelevant':
        errors = ['irrelevant_not_short_circuited']
    else:
        errors = []
    try:
        value = json.loads(content)
    except (ValueError, TypeError):
        return {'kind': 'invalid', 'errors': errors + ['invalid_json'], 'response': content,
                'would_publish': False, 'would_apply_action': False}
    if not isinstance(value, dict) or set(value) != {'say', 'action', 'target', 'amount'}:
        return {'kind': 'invalid', 'errors': errors + ['wrong_fields'], 'response': value,
                'would_publish': False, 'would_apply_action': False}
    amount = value['amount']
    numeric = type(amount) in (float, int) and math.isfinite(amount)
    if type(value['target']) is not int:
        errors.append('non_integer_target')
    if not isinstance(value['say'], str) or not value['say'].strip() or len(value['say']) > 200:
        errors.append('invalid_say')
    allowed = any(value['action'] == o['action'] and value['target'] == o['target'] and numeric and
                  (0 <= amount <= o['maximum'] if o['action'] == 'share' else
                   (o['maximum'] == 0 and amount == 0) or (o['maximum'] > 0 and .01 <= amount <= o['maximum']))
                  for o in case['offered'])
    if not allowed:
        errors.append('outside_offered_options')
    if case['expected'] == 'json' and value['action'] not in case['actions']:
        errors.append('wrong_requested_action')
    if value['action'] in ('give', 'donate') and amount != (2 if case['id'] == 'handoff_to_player' else 3):
        errors.append('wrong_requested_amount')
    if 'expected_amount' in case and (not numeric or not math.isclose(amount, case['expected_amount'], abs_tol=1e-6)):
        errors.append('wrong_requested_amount')
    return {'kind': 'json', 'errors': errors, 'response': value,
            'say_characters': len(value['say']) if isinstance(value['say'], str) else None,
            'would_publish': not errors, 'would_apply_action': not errors and value['action'] != 'none'}


def send(payload: dict, key: str) -> dict:
    assert ENDPOINT == 'https://api.qnaigc.com/v1/chat/completions'
    request = urllib.request.Request(ENDPOINT, data=json.dumps(payload, ensure_ascii=False).encode('utf-8'),
                                     headers={'Content-Type': 'application/json', 'Authorization': 'Bearer ' + key},
                                     method='POST')
    with urllib.request.build_opener(NoRedirect()).open(request, timeout=14) as response:
        return json.load(response)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true')
    parser.add_argument('--ids', nargs='+', help='Run only these case IDs, preserving fixture order')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    settings = runtime_parameters()
    rules = protocol_rules()
    if '只输出两个字：无关' not in rules or '回复内容必须为空' in rules:
        raise ValueError('Runtime Rules are not yet updated for the literal irrelevant protocol.')
    if args.ids and set(args.ids) - {case['id'] for case in CASES}:
        parser.error('Unknown case ID')
    selected = [case for case in CASES if not args.ids or case['id'] in args.ids]
    prepared = [{'case': case, 'payload': prepare(case, rules, settings)} for case in selected]
    report = {'kind': 'source-derived-cloud-protocol-fixtures-not-gameplay',
              'created_utc': datetime.now(timezone.utc).isoformat(), 'endpoint': ENDPOINT,
              'requested_model': MODEL, 'runtime_settings': settings,
              'requests': prepared, 'results': [], 'live': args.live, 'complete': False,
              'limitations': ['Synthetic nearby state; no world actions or scheduler exercised.',
                              'Conversation content still requires human semantic review.',
                              'response_format and stop parameters are intentionally absent; irrelevant must be the exact literal 无关 and finish_reason=stop; empty or null is invalid.',
                              'Each selected fixture is sent once; no separate relevance request and no automatic retry.']}
    if args.output.exists():
        raise FileExistsError('Refusing to overwrite a prior report or repeat paid requests.')
    args.output.parent.mkdir(parents=True, exist_ok=True)

    def save():
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    save()
    print(json.dumps({'prepared': len(prepared), 'live': args.live, 'max_tokens': settings['max_tokens']}), flush=True)
    if not args.live:
        return 0
    key = read_key()
    for request in prepared:
        case, payload = request['case'], request['payload']
        row = {'id': case['id'], 'expected': case['expected']}
        started = time.monotonic()
        try:
            envelope = send(payload, key)
            choice = envelope['choices'][0]
            raw = choice['message']['content']
            row.update(validate(raw, case))
            row['raw_content'] = raw
            row['returned_model'] = envelope.get('model')
            row['finish_reason'] = choice.get('finish_reason')
            row['usage'] = envelope.get('usage')
            if envelope.get('model') != MODEL:
                row['errors'].append('unexpected_model')
            if choice.get('finish_reason') != 'stop':
                row['errors'].append('incomplete_finish')
            if row['errors']:
                row['would_publish'] = False
                row['would_apply_action'] = False
        except urllib.error.HTTPError as error:
            body = error.read().decode('utf-8', errors='replace').replace(key, '[REDACTED]')
            row['transport_error'] = 'HTTP ' + str(error.code)
            row['error_detail'] = re.sub(r'sk-[A-Za-z0-9_-]+', '[REDACTED]', body)[:1000]
        except Exception as error:
            row['transport_error'] = type(error).__name__
        row['seconds'] = round(time.monotonic() - started, 3)
        report['results'].append(row)
        save()
        print(json.dumps(row, ensure_ascii=True), flush=True)
        if 'transport_error' in row:
            break  # Preserve possibly submitted requests; no blind retry.
    report['complete'] = len(report['results']) == len(prepared)
    report['passed'] = report['complete'] and all(
        not r.get('errors') and not r.get('transport_error') for r in report['results'])
    report['total_tokens'] = sum((r.get('usage') or {}).get('total_tokens', 0) for r in report['results'])
    save()
    print(json.dumps({k: report[k] for k in ('complete', 'passed', 'total_tokens')}), flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
