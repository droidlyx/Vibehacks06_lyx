"""Copy only selected Titan model/animation package dependencies; never overwrite local edits."""
from __future__ import annotations
from pathlib import Path
import argparse, hashlib, json, re, shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(r'E:\UE_projects\ProjectTitan')
IDS = ['T36', 'T37', 'T05', 'T31', 'T28', 'T01', 'T03', 'T07', 'T17', 'T18', 'T22', 'T26', 'T30', 'T32', 'T33', 'T49',
       'T50', 'T51', 'T52', 'T53']
CORE = '/Game/Characters/_Core/Anims/'
ANIMS = {
    'T05': ('/Game/Characters/NPCs/Sulfur/DragonOgre/Pose_ogro_0_Open_A_UE4',
            '/Game/Characters/NPCs/Sulfur/DragonOgre/Troll_Animation_0_Open_A_UE4'),
    'T31': ('/Game/Characters/NPCs/Sulfur/Creatures/Sulfur_Ferret/Animations/AS_SulfurFerret_IdleV2',
            '/Game/Characters/NPCs/Sulfur/Creatures/Sulfur_Ferret/Animations/AS_SulfurFerret_WalkV2'),
    'T28': ('/Game/Characters/NPCs/Desert/LittleDemon/Animations/LittleDemon_Animation_Idle',
            '/Game/Characters/NPCs/Desert/LittleDemon/Animations/LittleDemon_Animation_Walk'),
    'T49': ('/Game/Characters/NPCs/Arctic/Penguin/Rigged/Breathing_Idle',
            '/Game/Characters/NPCs/Arctic/Penguin/Rigged/Dwarf_Walk')}


def cpp(text): return 'TEXT(' + json.dumps(str(text), ensure_ascii=False) + ')'


def pkg(rel): return '/Game/' + Path(rel).relative_to('Content').with_suffix('').as_posix()


def source_file(package): return SOURCE / 'Content' / (package.removeprefix('/Game/') + '.uasset')


def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser();
    parser.add_argument('--copy', action='store_true');
    args = parser.parse_args()
    rows = json.loads((ROOT / 'Docs/CharacterCatalog/characters.json').read_text(encoding='utf-8-sig'))
    assets = json.loads((ROOT / 'Saved/CharacterCatalog/all_asset_classes.json').read_text(encoding='utf-8-sig'))
    assetmap = {a['relative_path']: a for a in assets}
    selected = [];
    queue = []
    for i, cid in enumerate(IDS):
        c = next(x for x in rows if x['id'] == cid);
        model = pkg(c['relative_model_path'])
        idle, walk = ANIMS.get(cid, (CORE + 'oot_lostfang_idle_01', CORE + 'walk'))
        modelasset = assetmap[c['relative_model_path']]
        for anim in (idle, walk):
            rel = 'Content/' + anim.removeprefix('/Game/') + '.uasset'
            assert assetmap[rel]['tags']['Skeleton'] == modelasset['tags']['Skeleton'], (cid, anim)
        selected.append(dict(c, agent_id=i + 1, model_package=model, idle_package=idle, walk_package=walk))
        queue.extend((model, idle, walk))
    packages = {};
    unresolved = set();
    conflicts = [];
    total = 0
    while queue:
        package = queue.pop()
        if package in packages: continue
        source = source_file(package)
        if not source.is_file(): unresolved.add(package);continue
        target = ROOT / source.relative_to(SOURCE)
        # Preserve current local packages, and resolve the actual package that will be loaded.
        file = target if target.exists() else source
        if target.exists() and digest(target) != digest(source): conflicts.append(package)
        data = file.read_bytes();
        total += len(data)
        packages[package] = (source, target)
        refs = set(m.decode('ascii') for m in re.findall(rb'/Game/[A-Za-z0-9_./\-]+', data))
        for ref in refs:
            ref = ref.split('.')[0].rstrip('/')
            if source_file(ref).is_file() or (ROOT / 'Content' / (ref[6:] + '.uasset')).is_file(): queue.append(ref)
        if len(packages) > 1800 or total > 3_000_000_000: raise RuntimeError(
            'Dependency budget exceeded; inspect before copying.')
    report = {'selected_count': len(selected), 'package_count': len(packages), 'dependency_bytes': total,
              'preserved_local_changes': sorted(set(conflicts)), 'unresolved_roots': sorted(unresolved), 'copied': 0}
    if unresolved: raise RuntimeError('Required source package missing: ' + ','.join(sorted(unresolved)))
    if args.copy:
        for source, target in packages.values():
            if target.exists(): continue
            target.parent.mkdir(parents=True, exist_ok=True);
            shutil.copy2(source, target);
            report['copied'] += 1
            for suffix in ('.uexp', '.ubulk'):
                companion = source.with_suffix(suffix);
                dest = target.with_suffix(suffix)
                if companion.exists() and not dest.exists(): shutil.copy2(companion, dest)
        (ROOT / 'Source/Verdant/VerdantPopulationProfiles.inl').write_text(
            '// Generated from reviewed character catalog by Tools/stage_population_assets.py.\n'
            'static const FResidentProfile Profiles[] = {\n' + ''.join('    {' + ', '.join(cpp(v) for v in
                                                                                           [c['id'], c['sample_name'],
                                                                                            c['motive'],
                                                                                            c['persona'] + '；底线：' + c[
                                                                                                'boundary'] + '；说话方式：' +
                                                                                            c[
                                                                                                'speech_style'] + '；选择倾向：' +
                                                                                            c['behavioral_signature'],
                                                                                            ','.join(c['tags']),
                                                                                            c['model_package'],
                                                                                            c['idle_package'],
                                                                                            c['walk_package']]) + '},\n'
                                                                       for c in selected) + '};\n', encoding='utf-8')
        relations = json.loads((ROOT / 'Docs/CharacterCatalog/relations.json').read_text(encoding='utf-8-sig'))[
            'relations']
        enabled = {'R01', 'R03', 'R07', 'R10', 'R18', 'R19', 'R20', 'R21', 'R32', 'R34', 'R37'}
        rules = [r for r in relations if r['relation_id'] in enabled]
        (ROOT / 'Source/Verdant/VerdantPopulationRelations.inl').write_text(
            '// Reviewed allowed type pairs; unimplemented relation types are not sampled.\n'
            'static const FRelationRule RelationRules[] = {\n' + ''.join('    {' + ', '.join([
                cpp(r['relation_id']), cpp(r['name']), cpp(','.join(r['source_type_ids'])),
                cpp(','.join(r['target_type_ids'])), 'true' if r['direction'] == '双向' else 'false',
                str(float(r.get('relative_weight', 1))) + 'f', cpp(','.join(r['exclusive_with'])),
                str(r['max_per_source']), str(r['max_per_target'])]) + '},\n' for r in rules) + '};\n', encoding='utf-8')
        # Explicit package cook entries avoid cooking every possible Titan character.
        config = ROOT / 'Config/DefaultGame.ini';
        text = config.read_text(encoding='utf-8-sig')
        begin = '; BEGIN VERDANT POPULATION COOK';
        end = '; END VERDANT POPULATION COOK'
        text = re.sub(re.escape(begin) + r'.*?' + re.escape(end) + '\n?', '', text, flags=re.S)
        block = begin + '\n[/Script/UnrealEd.ProjectPackagingSettings]\n' + ''.join(
            '+DirectoriesToAlwaysCook=(Path="' + d + '")\n' for d in
            sorted({p.rsplit('/', 1)[0] for p in queue})) + end + '\n'
        # Soft-loaded model and animation roots; transitive hard dependencies are cooked by UE.
        dirs = sorted(
            {x[k].rsplit('/', 1)[0] for x in selected for k in ('model_package', 'idle_package', 'walk_package')})
        block = begin + '\n[/Script/UnrealEd.ProjectPackagingSettings]\n' + ''.join(
            '+DirectoriesToAlwaysCook=(Path="' + d + '")\n' for d in dirs) + end + '\n'
        config.write_text(text.rstrip() + '\n\n' + block, encoding='utf-8')
    out = ROOT / 'Saved/Population';
    out.mkdir(parents=True, exist_ok=True)
    (out / 'asset-staging.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    (out / 'selected-profiles.json').write_text(json.dumps(selected, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=True))


if __name__ == '__main__': main()
