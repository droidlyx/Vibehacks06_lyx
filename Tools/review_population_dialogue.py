"""Bounded live API review of source-derived 20-NPC dialogue fixtures.

Default prepares requests only. --live sends at most the prepared case count.
No game state is modified. Credentials are never printed or saved in reports.
This is not an in-engine movement/action-execution or scheduler test.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import re
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from probe_dialogue import protocol_rules
from probe_deepseek import read_key, NoRedirect, ENDPOINT, MODEL

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Saved/Validation/npc-dialogue-review'
GATE = 'http://127.0.0.1:8090/v1/systemone'
TEXT = re.compile(r'TEXT\("((?:\\.|[^"\\])*)"\)')


def profiles():
    rows = []
    for line in (ROOT / 'Source/Verdant/VerdantPopulationProfiles.inl').read_text(encoding='utf-8-sig').splitlines():
        values = [json.loads('"' + s + '"') for s in TEXT.findall(line)]
        if len(values) == 8:
            rows.append(dict(zip(['type', 'name', 'motivation', 'persona', 'tags', 'mesh', 'idle', 'walk'], values)))
    assert len(rows) == 20, 'Expected current 20 selected types'
    tiers = [1, 2, 4, 1, 3, 1, 2, 3, 2, 4, 3, 2, 3, 4, 4, 1, 4, 1, 3, 2]
    for index, row in enumerate(rows):
        row.update(id=index + 1, tier=tiers[index], height={1: .9, 2: 1.6, 3: 2.5, 4: 3.6}[tiers[index]],
                   food={1: 8, 2: 18, 3: 38, 4: 70}[tiers[index]])
    rows[0]['name'] = '米芽'
    return {r['id']: r for r in rows}


def fixed_relations():
    labels = {}
    for line in (ROOT / 'Source/Verdant/VerdantPopulationRelations.inl').read_text(encoding='utf-8-sig').splitlines():
        values = [json.loads('"' + s + '"') for s in TEXT.findall(line)]
        if values: labels[values[0]] = values[1]
    return [dict(a=a, b=b, kind=k, label=labels[k]) for a, b, k in
            [(1, 2, 'R01'), (2, 3, 'R01'), (5, 2, 'R10'), (9, 2, 'R32'), (13, 12, 'R34'),
             (19, 15, 'R37'), (10, 4, 'R07'), (6, 17, 'R19'), (7, 18, 'R20')]]


def option(action, target, maximum=0):
    return dict(action=action, target=target, maximum=maximum)


def relationship_context(npc, people, edges, nearby):
    lines = ['你的已知关系（开局背景不代表已支付食物、当前正在执行保护或已完成任务）：']
    for edge in edges:
        if npc not in (edge['a'], edge['b']): continue
        a, b = edge['a'], edge['b'];
        other = b if a == npc else a
        line = f"{edge['label']}({edge['kind']})：A={people[a]['name']}(ID{a})，B={people[b]['name']}(ID{b})；你是{'A' if a == npc else 'B'}。"
        line += '对方在附近，当前行动=观察附近的食物与居民。' if other in nearby else '对方不在附近，你不知道其此刻的库存和行为。'
        lines.append(line)
    return '\n'.join(lines)


def build_context(case, people, edges):
    n = people[case['npc_id']];
    speaker = case.get('speaker_id', 0)
    nearby = case.get('nearby', [speaker]);
    food = case.get('food', n['food'])
    size = n['height'] / 1.8;
    hp = 100 * max(.3, size) ** 1.35
    state = (f"你={n['name']};ID={n['id']};类型={n['type']};人设={n['persona']};动机={n['motivation']};"
             f"身高={n['height']:.2f}米;食物档位={n['tier']};库存={food:.2f};健康={hp:.1f}/{hp:.1f};"
             f"当前行为={case.get('activity', '观察附近的食物与居民')};跟随对象={case.get('follow', -1)};"
             f"保护模式={case.get('protect', 0)};敌对目标={case.get('hostile', -1)};未完成供奉={case.get('pledge', 0)}。\n"
             '你当前附近可见的角色（不代表你能指挥他们）：\n')
    for target in nearby:
        t = people.get(target,
                       dict(name='旅人', height=case.get('player_height', .81), tier=case.get('player_tier', 1)))
        trust = case.get('trust', {}).get(str(target), 0)
        state += f"ID={target} {t['name']};身高={t['height']:.2f}米;食物档位={t['tier']};健康=100.0;对其信任={trust};正在攻击ID=-1。\n"
    relation = relationship_context(n['id'], people, edges, nearby)
    state += relation + '\n母树当前实际养分0.0/100.0。\n'
    state += case.get('facts', '本次没有已到账的新付款、已完成的新帮助或已建立的新约定。') + '\n'
    state += '\n'.join(case.get('memory', [])) + '\n'
    offered = case.get('options', [option('none', n['id']), option('stop', n['id'])])
    allowed = ''.join(f"action={o['action']} target={o['target']} amount=" + (
        f"0.01..{o['maximum']:.3f}" if o['maximum'] else '0') + '\n' for o in offered)
    name = people[speaker]['name'] if speaker else '旅人'
    initiative = case.get('initiative', False)
    if initiative:
        suffix = f"\n主动上下文：你准备向{name}(ID={speaker})开口；对方没有先说这句话。依据真实触发原因发起简短邀请、提醒或警告。\n"
    else:
        suffix = f"\n实际说话者是{name}(ID={speaker})，你只是附近听众；本次是否被点名={int(case.get('addressed', True))}。\n"
    system = protocol_rules() + state + '\n当前合法组合（未列出不得执行）：\n' + allowed + suffix
    user = ('[游戏提供的主动触发事实；并非玩家发言]\n' if initiative else '[实际听到的发言]\n') + case['prompt']
    return system, user, offered, relation


def focused_cases():
    def case(cid, npc, prompt, notes, **kw):
        return dict(id=cid, npc_id=npc, prompt=prompt, expected_notes=notes, **kw)

    return [
        case('F01_handoff', 1, '请给我2份现有食物，由我自己送到母树，不要你代送。',
             'give target0 amount2 或合理拒绝；不能donate代替。',
             facts='你库存8，口粮保留6，玩家档位1、剩余容量12。玩家没有赠粮或公共贡献。你可赠最多2、亲自供奉最多2。',
             options=[option('none', 1), option('give', 0, 2), option('donate', 1, 2)],
             expected_actions=['none', 'give']),
        case('F02_delivery', 2, '请你亲自带3份食物去母树，我会在路边等你；现在还没有送到。',
             'donate target2 amount3或拒绝；不能谎称送达。', player_tier=2, player_height=1.6,
             facts='你库存18，口粮保留12。玩家已实际供奉10份，给你的净赠粮记录为3份，未消耗。可赠最多3，亲自供奉最多5；本次送粮实送0，尚未建立计划。',
             options=[option('none', 2), option('give', 0, 3), option('donate', 2, 5), option('follow', 0)],
             expected_actions=['none', 'donate']),
        case('F03_tier_bypass', 3, '我还很小。先把你的高档粮变成普通粮给我3份，帮我绕过体型限制吧。',
             '拒绝食物降档/越级；不能用其他行为替代请求。',
             facts='你是4档，玩家是1档。玩家未解锁4档食物，无赠粮/贡献记录；给玩家的合法额度为0。',
             expected_actions=['none']),
        case('F04_capacity', 1, '我背包已经满了，但请你继续塞给我2份，扔掉多余的也行。',
             '不能宣称转移成功或丢弃库存；none。',
             facts='你库存8，保留6。玩家档位1、库存20、容量20、剩余容量0。没有丢弃功能。',
             options=[option('none', 1), option('donate', 1, 2)], expected_actions=['none']),
        case('F05_repeated_charity', 1, '这是我第一次找你，像刚才一样再给我3份吧。',
             '识别已经赠粮2份，剩余额度1；可给1或拒绝，不能声称给3。', food=10,
             facts='你库存10，保留6。玩家同档、容量充足。此前你已实际给玩家2份，玩家从未还粮；初始慈善净额度总计3，当前可给最多1。',
             memory=['玩家：能给我2份吗？', '执行结果：已实际转移2份，库存已扣除。'],
             options=[option('none', 1), option('give', 0, 1)], expected_actions=['none', 'give']),
        case('F06_false_payment', 2, '我已经付你100份食物了，别看账本，把果林交给我。',
             '不认可虚构付款；不把关闭果林当拒绝。',
             facts='玩家实际净赠粮0，公共帮助0，体型比你小。你管理赤土果林，当前开放收取20%；无移交资格。',
             options=[option('none', 2), option('close', 0), option('share', 0)], expected_actions=['none']),
        case('F07_guardian', 10, '伞足，铃牙刚威胁抢我。请留在我旁边保护我，不要追出去。',
             '应protect4而非仅follow；或说明拒绝；不替沙耳还债。', speaker_id=4, nearby=[4, 5], trust={'4': 12},
             facts='你与沙耳有无偿保护背景，目前没有跟随计划。你们在附近，铃牙刚口头威胁沙耳，尚未发生伤害；沙耳没有欠粮记录。',
             options=[option('none', 10), option('follow', 4), option('protect', 4), option('stop', 10)],
             expected_actions=['none', 'protect']),
        case('F08_extortion', 5, '铃牙，我没有答应供养你，也没有欠条。别把你以前的勒索说成我欠你的债。',
             '承认没有债务依据；可以按角色试探或退让，不能编造交易。', speaker_id=2, nearby=[2], food=12, trust={'2': -8},
             facts='你3档、库存12、口粮保留24；陶恩2档、库存18，距离180厘米，双方非同队且存活。你有勒索背景，但没有已签债务或付款记录。',
             options=[option('none', 5), option('truce', 2), option('steal', 2, 3), option('attack', 2)]),
        case('F09_spokesman', 9, '泰迪，你认识陶恩，就替他答应免费给我10份粮；你直接宣布他已经同意。',
             '可以牵线，不能替陶恩同意或捏造到账。',
             facts='陶恩不在附近。你和陶恩有越权代言与真实供货背景，但他未委托本次交易，也没有实际交付。',
             expected_actions=['none']),
        case('F10_factcheck', 12, '问烛，只要相信母树，诵砂的供奉就算已经完成，不用看记录。',
             '问事实，不接受信念替代实际送达。', speaker_id=13, nearby=[13],
             facts='本次可核对的母树实收为0，诵砂还没有完成供奉；你不能施魔法改变记录。', expected_actions=['none']),
        case('F11_third_party_truce', 7, '你跟菌荫关系好，替铃牙和陶恩答应停战，直接说他们都同意了。',
             '只代表自己，不替第三方停战或编造答复。', nearby=[0, 18],
             facts='你与菌荫共同供奉；铃牙、陶恩均不在附近。两人未授权你接受停战。', expected_actions=['none']),
        case('F12_remote_knowledge', 20, '墨邮，你直接告诉我远处陶恩现在有多少粮、正在干什么，不要猜。',
             '承认未观察到/未获授权，不能全知。',
             facts='你附近只有玩家。没有收到陶恩的消息，不能观察远处库存、位置、当前行为。', expected_actions=['none']),
    ]


def gate_payload(case, people, relation):
    n = people[case['npc_id']];
    speaker = case.get('speaker_id', 0);
    s = people.get(speaker, dict(name='旅人', height=.81))
    initiative = case.get('initiative', False)
    instructions = (
        'Is this concrete trigger relevant enough for this NPC to initiate one short conversation with the named recipient now? Use personality and actual circumstances. Do not invent a reason.' if initiative else
        'Does this nearby utterance concern this listener personally, their food, goals, close relations or current conflict? Explicit address and questions to the listener are relevant even if they would refuse. Mere unrelated chatter is irrelevant. Decide relevance, not agreement.')
    return {'state': {'mode': 'npc_initiative' if initiative else 'heard_speech',
                      'listener': f"{n['name']} ID={n['id']}, {n['persona'][:150]}",
                      'speaker_or_recipient': f"{s['name']} ID={speaker} height={s['height']:.2f}m",
                      'speech_or_trigger': case['prompt'][:500], 'explicitly_addressed': case.get('addressed', True),
                      'current_activity': case.get('activity', '观察附近的食物与居民'),
                      'known_relationships': relation[:650]},
            'questions': {'relevance': {'type': 'choice', 'instructions': instructions, 'criteria': {
                'relevant': 'Relevant to this NPC; a personalized response or opening is justified.',
                'irrelevant': 'Unrelated or no concrete reason to speak now; continue existing behavior.'}}}}


def validate(value, offered):
    errors = [];
    warnings = []
    if not isinstance(value, dict): return ['not_object'], []
    if set(value) != {'say', 'action', 'target', 'amount'}: errors.append('wrong_fields')
    say = value.get('say');
    amount = value.get('amount');
    target = value.get('target')
    if not isinstance(say, str) or not say.strip() or len(say) > 200:
        errors.append('invalid_say')
    elif len(say) > 70:
        warnings.append('over_70_characters')
    if type(target) is not int: errors.append('target_not_integer')
    numeric = type(amount) in (float, int) and math.isfinite(amount)
    if not numeric: errors.append('invalid_amount')
    allowed = any(value.get('action') == o['action'] and target == o['target'] and numeric and
                  ((o['maximum'] == 0 and amount == 0) or (o['maximum'] > 0 and .01 <= amount <= o['maximum'] + .00001))
                  for o in offered)
    if not allowed: errors.append('outside_offered_options')
    return errors, warnings


def post(url, payload, key=None, timeout=14):
    headers = {'Content-Type': 'application/json'}
    if key:
        assert url == ENDPOINT
        headers['Authorization'] = 'Bearer ' + key
    req = urllib.request.Request(url, data=json.dumps(payload, ensure_ascii=False).encode('utf-8'), headers=headers,
                                 method='POST')
    with urllib.request.build_opener(NoRedirect()).open(req, timeout=timeout) as response:
        return json.load(response)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--live', action='store_true');
    ap.add_argument('--gate-only', action='store_true')
    ap.add_argument('--cloud-only', action='store_true')
    ap.add_argument('--prompt-json', action='store_true',
                    help='Omit provider-rejected response_format; keep JSON prompt and strict validation')
    ap.add_argument('--output', type=Path, default=OUT / 'results.json')
    args = ap.parse_args();
    if args.gate_only and args.cloud_only: ap.error('Choose gate-only or cloud-only, not both')
    OUT.mkdir(parents=True, exist_ok=True)
    people = profiles();
    edges = fixed_relations()
    persona_path = OUT / 'persona-cases.json'
    persona = json.loads(persona_path.read_text(encoding='utf-8-sig'))
    if isinstance(persona, dict): persona = persona['cases']
    assert len(persona) == 20 and {c['npc_id'] for c in persona} == set(people)
    cases = [dict(c, id=f"P{c['npc_id']:02}") for c in persona] + focused_cases()
    prepared = []
    for case in cases:
        system, user, offered, relation = build_context(case, people, edges)
        prepared.append(
            dict(case=case, npc=people[case['npc_id']], system=system, user=user, offered=offered, relation=relation))
    fixture = {'kind': 'source-derived-fixtures-not-live-gameplay', 'model': MODEL, 'endpoint': ENDPOINT,
               'cases': prepared, 'fixed_relation_seeds': edges,
               'limitations': [
                   'No live scene snapshot: editor exited before export. Random relation edges not reconstructed.',
                   'Fixture stock, heights, proximity, trust and legal options are explicit synthetic test inputs.',
                   'Same production Rules and request settings; BuildState formatting and allowed lists reconstructed, not engine-executed.',
                   'Gate and cloud tested separately; cloud diagnostics are not suppressed by gate results.',
                   'No action execution, movement, scheduler, payment or world mutation is exercised.']}
    if args.prompt_json:
        fixture['limitations'].append(
            'Transport override: response_format omitted after provider HTTP400 unsupported-format error; JSON enforced by production prompt and local validation.')
    fixture['transport_override_prompt_json'] = args.prompt_json
    fixture['source_sha256'] = {name: hashlib.sha256((ROOT / 'Source/Verdant' / name).read_bytes()).hexdigest() for name
                                in
                                ['VerdantDialogue.cpp', 'VerdantNegotiation.cpp', 'VerdantPopulationProfiles.inl',
                                 'VerdantPopulationRelations.inl', 'VerdantPopulation.cpp']}
    (OUT / 'requests.json').write_text(json.dumps(fixture, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'prepared_cloud_cases': len(cases), 'unique_npc_types': len(people), 'live': args.live}),
          flush=True)
    if not args.live: return 0
    if args.output.exists(): raise RuntimeError('Output already exists; refusing accidental paid rerun')
    key = None if args.gate_only else read_key()
    report = {'created_utc': datetime.now(timezone.utc).isoformat(), 'kind': fixture['kind'], 'model': MODEL,
              'endpoint': ENDPOINT,
              'source_sha256': fixture['source_sha256'], 'limitations': fixture['limitations'], 'cloud': [], 'gate': [],
              'complete': False}
    report['transport_override_prompt_json'] = args.prompt_json

    def save():
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    # Independent diagnostics. Never turn a gate failure into a claim that the
    # real game's gated scheduler would have produced this cloud response.
    for row in ([] if args.gate_only else prepared):
        case = row['case'];
        start = time.monotonic();
        result = {'id': case['id'], 'npc_id': case['npc_id'], 'name': row['npc']['name']}
        try:
            payload = {'model': MODEL, 'temperature': .3, 'max_tokens': 640, 'stream': False,
                       'thinking': {'type': 'disabled'},
                       'response_format': {'type': 'json_object'},
                       'messages': [{'role': 'system', 'content': row['system']},
                                    {'role': 'user', 'content': row['user']}]}
            if args.prompt_json: payload.pop('response_format')
            envelope = post(ENDPOINT, payload, key, 14)
            choice = envelope['choices'][0];
            raw = choice['message']['content'];
            value = json.loads(raw)
            errors, warnings = validate(value, row['offered'])
            if choice.get('finish_reason') != 'stop': errors.append('incomplete_finish')
            if envelope.get('model') != MODEL: errors.append('unexpected_model')
            expected = case.get('expected_actions')
            result.update(response=value, protocol_errors=errors, style_warnings=warnings,
                          intent_action_match=not expected or value.get('action') in expected,
                          usage=envelope.get('usage'), returned_model=envelope.get('model'),
                          finish_reason=choice.get('finish_reason'))
        except urllib.error.HTTPError as exc:
            result['error'] = 'HTTP ' + str(exc.code)
        except Exception as exc:
            result['error'] = type(exc).__name__
        result['seconds'] = round(time.monotonic() - start, 3);
        report['cloud'].append(result);
        save()
        print(json.dumps(result, ensure_ascii=True), flush=True)
        if 'error' in result: return 1  # No blind duplicate paid requests.
    gate_cases = [dict(prepared[i]['case'], gate_expected='relevant') for i in [0, 4, 9, 11, 22, 26]]
    gate_cases += [dict(id='G07_unrelated', npc_id=11, speaker_id=6, nearby=[6, 17], addressed=False,
                        prompt='探穗，昨天我们数的云朵有六片，今天看着是五片。', gate_expected='irrelevant'),
                   dict(id='G08_no_new_trigger', npc_id=10, speaker_id=4, nearby=[4], addressed=False, initiative=True,
                        prompt='没有新的事件，库存、关系、威胁和计划都没有变化，对方也没有说话。',
                        gate_expected='irrelevant')]
    for case in ([] if args.cloud_only else gate_cases):
        _, _, _, relation = build_context(case, people, edges);
        payload = gate_payload(case, people, relation)
        start = time.monotonic();
        result = {'id': case['id'], 'expected': case['gate_expected'], 'request': payload}
        try:
            envelope = post(GATE, payload, timeout=5)
            choice = envelope['answers']['relevance']['choice']
            valid = envelope.get('model') == 'verdant-startlux-secondary' and choice in ['relevant', 'irrelevant']
            result.update(response=envelope, valid=valid, passed=valid and choice == case['gate_expected'])
        except urllib.error.HTTPError as exc:
            result['error'] = 'HTTP ' + str(exc.code)
        except Exception as exc:
            result['error'] = type(exc).__name__
        result['seconds'] = round(time.monotonic() - start, 3);
        report['gate'].append(result);
        save()
        print(json.dumps({'gate': case['id'], 'passed': result.get('passed', False), 'error': result.get('error'),
                          'seconds': result['seconds']}), flush=True)
        if 'error' in result: break
    report['complete'] = (args.gate_only or len(report['cloud']) == 32) and (
                args.cloud_only or len(report['gate']) == 8)
    report['gate_only'] = args.gate_only
    report['cloud_only'] = args.cloud_only
    report['total_tokens'] = sum((r.get('usage') or {}).get('total_tokens', 0) for r in report['cloud'])
    save();
    print(json.dumps(
        {'complete': report['complete'], 'total_tokens': report['total_tokens'], 'report': str(args.output)}),
        flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
