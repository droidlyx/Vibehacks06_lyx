"""Prepare six licensed nature additions only; no editor/game/screenshot automation.

python Tools/prepare_ecology_assets.py
Uses the existing local Standard archive and Blender. Existing imported meshes and
the broader character manifest are not touched. Runtime decides world dimensions.
"""
from pathlib import Path
import argparse
import hashlib
import json
import posixpath
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = Path(r'D:/Workspace/vscode_workspace/EMBER_GENERATIONS_LOCAL_SOURCE_20260829')
ARCHIVE = LIBRARY / 'vendor-free-assets/quaternius/stylized-nature-megakit/Stylized Nature MegaKit[Standard].zip'
BLENDER = LIBRARY / '_tools/blender-4.5.13-windows-x64/blender.exe'
RAW = ROOT / 'Assets/Source/Quaternius/Ecology'
OUT = ROOT / 'Assets/Prepared/Ecology'
MANIFEST = ROOT / 'Assets/Environment/ecology-assets.json'
SPECS = [('SM_Clover01', 'Clover_1'), ('SM_Pebble01', 'Pebble_Round_1'),
         ('SM_Pebble02', 'Pebble_Round_2'), ('SM_Bush01', 'Bush_Common'),
         ('SM_Fern01', 'Fern_1'), ('SM_Flowers01', 'Flower_3_Group')]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage():
    records = []
    selected = set()
    RAW.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(ARCHIVE) as z:
        for asset, source in SPECS:
            entry = f'glTF/{source}.gltf'
            data = json.loads(z.read(entry))
            selected.add(entry)
            for obj in data.get('buffers', []) + data.get('images', []):
                uri = obj.get('uri', '')
                if uri and not uri.startswith('data:'):
                    selected.add(posixpath.normpath(posixpath.join('glTF', uri)))
        selected.add('License_Standard.txt')
        for entry in sorted(selected):
            target = (RAW / entry).resolve()
            if not target.is_relative_to(RAW.resolve()):
                raise ValueError('Unsafe archive member: ' + entry)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(z.read(entry))
            records.append({'archive_member': entry, 'path': target.relative_to(ROOT).as_posix(),
                            'sha256': digest(target)})
    result = {'schema_version': 1, 'status': 'staged', 'source_archive': str(ARCHIVE),
              'source_archive_sha256': digest(ARCHIVE),
              'source_url': 'https://quaternius.com/packs/stylizednaturemegakit.html',
              'license': 'CC0-1.0', 'license_file': 'Assets/Source/Quaternius/Ecology/License_Standard.txt',
              'source_files': records, 'assets': [], 'gameplay_test_performed': False,
              'visual_acceptance': 'No new in-engine screenshot or gameplay review performed.'}
    MANIFEST.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


def blender_export():
    import bpy
    from mathutils import Vector
    from io_scene_fbx import parse_fbx
    result = json.loads(MANIFEST.read_text(encoding='utf-8'))
    result['assets'] = []
    for asset, source in SPECS:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        source_path = RAW / 'glTF' / f'{source}.gltf'
        gltf = json.loads(source_path.read_text(encoding='utf-8'))
        bpy.ops.import_scene.gltf(filepath=str(source_path))
        meshes = sorted([o for o in bpy.context.scene.objects if o.type == 'MESH'], key=lambda o: o.name)
        if not meshes:
            raise RuntimeError('No imported geometry: ' + source)
        bpy.ops.object.select_all(action='DESELECT')
        for obj in meshes:
            obj.hide_set(False)
            obj.select_set(True)
        mesh = meshes[0]
        bpy.context.view_layer.objects.active = mesh
        if len(meshes) > 1:
            bpy.ops.object.join()
        mesh.name = asset
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        bpy.context.view_layer.update()
        coords = [mesh.matrix_world @ Vector(corner) for corner in mesh.bound_box]
        minimum = [min(p[i] for p in coords) for i in range(3)]
        maximum = [max(p[i] for p in coords) for i in range(3)]
        size = [maximum[i] - minimum[i] for i in range(3)]
        if min(size) <= 0 or len(mesh.data.polygons) == 0:
            raise RuntimeError('Degenerate model: ' + source)
        descriptors = []
        for index, mat in enumerate(mesh.data.materials):
            name = mat.name if mat else 'None'
            # A fresh scene per mesh prevents Blender's numeric material suffixes.
            source_mat = next((m for m in gltf.get('materials', []) if m.get('name') == name), None)
            if source_mat is None:
                raise RuntimeError(f'Unmapped source material {source}: {name}')
            pbr = source_mat.get('pbrMetallicRoughness', {})
            tex = pbr.get('baseColorTexture')
            desc = {'slot': index, 'name': name,
                    'color': pbr.get('baseColorFactor', [1, 1, 1, 1]),
                    'masked': source_mat.get('alphaMode') == 'MASK',
                    'alpha_cutoff': source_mat.get('alphaCutoff', .333),
                    'two_sided': source_mat.get('doubleSided', False), 'roughness': .85}
            if tex:
                img = gltf['images'][gltf['textures'][tex['index']]['source']]
                texture_path = (source_path.parent / img['uri']).resolve()
                desc['texture'] = texture_path.relative_to(ROOT).as_posix()
                desc['texture_sha256'] = digest(texture_path)
            descriptors.append(desc)
        target = OUT / f'{asset}.fbx'
        bpy.ops.export_scene.fbx(filepath=str(target), use_selection=True, object_types={'MESH'},
                                 add_leaf_bones=False, bake_anim=False, axis_forward='-Y', axis_up='Z',
                                 apply_unit_scale=True, path_mode='AUTO', use_mesh_modifiers=True,
                                 mesh_smooth_type='FACE')
        tree, _ = parse_fbx.parse(str(target))
        if not any(c.id == b'Geometry' for e in tree.elems if e.id == b'Objects' for c in e.elems):
            raise RuntimeError('FBX has no geometry: ' + asset)
        result['assets'].append({'name': asset, 'source_mesh': source,
                                 'source_gltf': source_path.relative_to(ROOT).as_posix(),
                                 'fbx': target.relative_to(ROOT).as_posix(), 'fbx_sha256': digest(target),
                                 'ue_path': '/Game/Art/Nature/' + asset,
                                 'bounds_min_m': minimum, 'bounds_max_m': maximum, 'bounds_size_m': size,
                                 'vertices': len(mesh.data.vertices),
                                 'triangles': sum(len(p.vertices) - 2 for p in mesh.data.polygons),
                                 'materials': descriptors})
    result['status'] = 'prepared_for_targeted_import'
    result['blender_version'] = bpy.app.version_string
    MANIFEST.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('VERDANT_ECOLOGY_PREPARED', len(result['assets']))


if __name__ == '__main__':
    if '--blender' in sys.argv:
        blender_export()
    else:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--stage-only', action='store_true')
        args = parser.parse_args()
        stage()
        if not args.stage_only:
            log_path = OUT / 'blender-export.log'
            with log_path.open('w', encoding='utf-8') as log:
                subprocess.run([str(BLENDER), '--background', '--factory-startup', '--python-exit-code', '1',
                                '--python', str(Path(__file__).resolve()), '--', '--blender'],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            print('Prepared six ecology meshes; details:', log_path)
