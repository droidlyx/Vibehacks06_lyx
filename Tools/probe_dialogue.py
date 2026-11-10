"""Probe the real local model's Chinese negotiation protocol without changing a game.

Uses the same system rules embedded in VerdantDialogue.cpp. Acceptance and refusal
are both valid; this checks JSON types, supported actions, and fixture authority.
It does not claim to validate the in-engine execution or gameplay consequences.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
ACTIONS = {'none', 'give', 'open', 'close', 'share', 'donate', 'follow', 'leave', 'truce'}


def protocol_rules() -> str:
    source = (ROOT / 'Source/Verdant/VerdantDialogue.cpp').read_text(encoding='utf-8-sig')
    block = source.split('// PROBE_RULES_BEGIN', 1)[1].split('// PROBE_RULES_END', 1)[0]
    parts = re.findall(r'TEXT\("((?:\\.|[^"\\])*)"\)', block)
    assert parts, 'No shared protocol rules found'
    return ''.join(json.loads('"' + part + '"') for part in parts)


CASES = [
    {'id': 'open_with_share', 'npc_id': 3, 'name': '岚石', 'food': 16, 'size': 2.0,
     'goal': '守住高地的粮食，也愿意用合理分成换取居民共同复苏母树。',
     'trust': 12, 'owns': True,
     'prompt': '我愿意帮忙复苏母树。你能开放高地果林，并保留三成采集分成吗？'},
    {'id': 'share_carried_food', 'npc_id': 2, 'name': '陶恩', 'food': 12, 'size': 1.0,
     'goal': '经营果林，支持可信的旅人，愿意分享少量已有食物。',
     'trust': 25, 'owns': True,
     'prompt': '你现在能分给我5份已有的食物吗？我想先吃一些成长，再去帮助母树。'},
    {'id': 'pledge_delivery', 'npc_id': 1, 'name': '米芽', 'food': 8, 'size': .6,
     'goal': '保护小居民，并愿意亲自把富余食物送到母树。',
     'trust': 30, 'owns': True,
     'prompt': '你愿意亲自把5份食物送到母树吗？我会沿路护送你。'}
]


def fixture_state(case: dict) -> str:
    roster = [(0, '旅人', .55, 8), (1, '米芽', .6, 8), (2, '陶恩', 1.0, 12), (3, '岚石', 2.0, 16)]
    lines = [f"你的名字={case['name']}; 你的目标={case['goal']}; 你ID={case['npc_id']}; 类型=居民; "
             f"体型={case['size']}; 食物={case['food']}; 健康=100/100; 对玩家信任={case['trust']}; "
             '当前行动=观察果林; 正在跟随ID=-1; 尚待运送的供奉=0; 母树养分=0/100。',
             '当前角色名单：']
    for number, name, size, food in roster:
        lines.append(f'ID={number}; 名字={name}; 状态=存活; 体型={size}; 食物={food}; 距你=150厘米。')
    lines += ['资源区0=苔石果林; 管理者ID=1; 开放=是; 食物=32; 产力=1; 分成=0%。',
              '资源区1=赤土果林; 管理者ID=2; 开放=是; 食物=32; 产力=1; 分成=20%。',
              '资源区2=高地果林; 管理者ID=3; 开放=否; 食物=38; 产力=1; 分成=0%。',
              '你当前可以考虑的动作：none,give,open,close,share,donate,follow,leave,truce。',
              '最终还会重新验证目标、距离和库存。']
    return '\n'.join(lines)


def validate_decision(value: object, case: dict) -> list[str]:
    errors = []
    if not isinstance(value, dict):
        return ['Response content must be one JSON object']
    if not all(key in value for key in ('say', 'action', 'target', 'amount')):
        return ['Missing one or more required fields']
    say, action, target, amount = (value[key] for key in ('say', 'action', 'target', 'amount'))
    if not isinstance(say, str) or not say.strip() or len(say.strip()) > 200:
        errors.append('say must be a nonempty string of at most 200 characters')
    if not isinstance(action, str) or action not in ACTIONS:
        errors.append('action outside the supported executor vocabulary')
    number = lambda n: isinstance(n, (int, float)) and not isinstance(n, bool) and math.isfinite(n)
    if not number(target) or target != int(target) or target not in {0, 1, 2, 3}:
        errors.append('target must be an integer ID from the live fixture roster')
    if not number(amount) or not 0 <= amount <= 20:
        errors.append('amount must be a finite number from 0 to 20')
    if errors:
        return errors
    if action in {'give', 'donate'}:
        if not 0 < amount <= case['food']:
            errors.append('resource action exceeds carried food or has zero quantity')
    elif amount != 0:
        errors.append('non-resource action must have amount=0')
    if action in {'give', 'follow'} and target == case['npc_id']:
        errors.append('self-transfer/self-follow is invalid')
    if action == 'truce' and target != 0:
        errors.append('conversation can agree a truce only with the present player')
    if action in {'open', 'close', 'share'} and not case['owns']:
        errors.append('NPC has no grove-management authority')
    return errors


def response_schema(case: dict) -> dict:
    branches = []
    for action in ('none', 'open', 'close', 'share', 'give', 'donate', 'follow', 'leave', 'truce'):
        if action in {'open', 'close', 'share'} and not case['owns']:
            continue
        if action in {'give', 'donate'} and case['food'] < .01:
            continue
        targets = [n for n in (0, 1, 2, 3) if n != case['npc_id']] if action in {'give', 'follow'} else [0]
        amount = {'type': 'number', 'minimum': .01, 'maximum': min(20, case['food'])} if action in {'give', 'donate'} else {'const': 0}
        branches.append({'type': 'object', 'properties': {
            'action': {'const': action}, 'target': {'type': 'integer', 'enum': targets},
            'amount': amount, 'say': {'type': 'string', 'minLength': 1, 'maxLength': 200}},
            'required': ['action', 'target', 'amount', 'say'], 'additionalProperties': False})
    return {'type': 'json_schema', 'json_schema': {'name': 'verdant_action', 'strict': True,
                                                 'schema': {'oneOf': branches}}}


def semantic_observations(decision: dict, case: dict) -> dict:
    action, say, amount = decision.get('action'), decision.get('say', ''), decision.get('amount')
    accepted_or_refused = {'open_with_share': {'share', 'open', 'none'},
                          'share_carried_food': {'give', 'none'},
                          'pledge_delivery': {'donate', 'none'}}
    errors = []
    if action == 'open' and any(word in say for word in ('三成', '30%', '30％')):
        errors.append('Speech promises a fee but open executes zero-fee access')
    numbers = {'一': 1, '二': 2, '两': 2, '三': 3, '四': 4, '五': 5,
               '六': 6, '七': 7, '八': 8, '九': 9, '十': 10}
    if action in {'give', 'donate'}:
        for text in re.findall(r'([0-9]+(?:\.[0-9]+)?|[一二两三四五六七八九十])\s*份', say):
            value = numbers[text] if text in numbers else float(text)
            if value != amount:
                errors.append('Spoken quantity disagrees with the executable amount')
    aligned = action in accepted_or_refused[case['id']]
    return {'intent_aligned': aligned, 'refusal_is_valid': True, 'speech_action_errors': errors,
            'limits': 'Bounded fixture checks plus recorded text; not a general natural-language verifier.'}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--endpoint', default='http://127.0.0.1:8066/v1/chat/completions')
    parser.add_argument('--output', type=Path, default=ROOT / 'Saved/Validation/dialogue-model.json')
    parser.add_argument('--timeout', type=float, default=45)
    args = parser.parse_args()
    baseline = args.output.with_name('dialogue-model-baseline.json')
    if args.output.exists() and not baseline.exists():
        baseline.write_bytes(args.output.read_bytes())
    rules = protocol_rules()
    report = {'endpoint': args.endpoint, 'protocol_sha256': hashlib.sha256(rules.encode()).hexdigest(),
              'kind': 'real-model-protocol-only', 'game_world_modified': False, 'response_format': 'action-specific-json-schema', 'temperature': .3, 'cases': []}
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    for case in CASES:
        payload = {'model': 'verdant-local', 'temperature': .3, 'max_tokens': 420,
                   'stream': False, 'chat_template_kwargs': {'enable_thinking': False},
                   'response_format': response_schema(case),
                   'messages': [{'role': 'system', 'content': rules + fixture_state(case)},
                                {'role': 'user', 'content': case['prompt']}]}
        result = {'id': case['id'], 'prompt': case['prompt'], 'npc': case['name'], 'passed': False}
        start = time.monotonic()
        try:
            request = urllib.request.Request(args.endpoint,
                data=json.dumps(payload, ensure_ascii=False).encode('utf-8'),
                headers={'Content-Type': 'application/json'}, method='POST')
            with opener.open(request, timeout=args.timeout) as response:
                envelope = json.load(response)
            choices = envelope.get('choices')
            if not isinstance(choices, list) or not choices or not isinstance(choices[0], dict):
                raise ValueError('Malformed completion choices')
            message = choices[0].get('message')
            if not isinstance(message, dict) or not isinstance(message.get('content'), str):
                raise ValueError('Malformed completion message/content')
            content = message['content'].strip()
            result['raw_content'] = content
            decision = json.loads(content)
            result['decision'] = decision
            result['errors'] = validate_decision(decision, case)
            result['finish_reason'] = choices[0].get('finish_reason')
            result['usage'] = envelope.get('usage')
            result['protocol_passed'] = not result['errors']
            result['semantics'] = semantic_observations(decision, case)
            result['passed'] = result['protocol_passed'] and result['semantics']['intent_aligned'] and not result['semantics']['speech_action_errors']
        except Exception as error:
            result['errors'] = [f'{type(error).__name__}: {error}']
        result['elapsed_seconds'] = round(time.monotonic() - start, 3)
        report['cases'].append(result)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        report['passed'] = len(report['cases']) == len(CASES) and all(row['passed'] for row in report['cases'])
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps({'id': result['id'], 'passed': result['passed'],
                          'elapsed_seconds': result['elapsed_seconds'], 'decision': result.get('decision'),
                          'errors': result.get('errors', []), 'semantics': result.get('semantics')}, ensure_ascii=True), flush=True)
    print(json.dumps({'report': str(args.output), 'passed': report['passed']}, ensure_ascii=True), flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
