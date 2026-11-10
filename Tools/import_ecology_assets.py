"""Import only the six new ecology meshes/materials. No Play, screenshots or reload.

Run with the standalone Python commandlet after prepare_ecology_assets.py.
Use -ExecCmds="Interchange.FeatureFlags.Import.Enable 0" for this import only.
The live Rider task-graph callback hit Interchange texture-import re-entry on 2026-10-31.
"""
from pathlib import Path
import hashlib
import json
import math
import re
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
assert ROOT == Path(__file__).resolve().parents[1], 'Run in Verdant.'
manifest = json.loads((ROOT / 'Assets/Environment/ecology-assets.json').read_text(encoding='utf-8'))
allowed = {'SM_Clover01', 'SM_Pebble01', 'SM_Pebble02', 'SM_Bush01', 'SM_Fern01', 'SM_Flowers01'}
assert {r['name'] for r in manifest['assets']} == allowed
assets = u.AssetToolsHelpers.get_asset_tools()
report = {'success': False, 'meshes': [], 'textures': [], 'materials': [], 'errors': [],
          'screenshots_taken': False, 'gameplay_tests_run': False}
report_path = ROOT / 'Saved/Environment/ecology-import.json'
report_path.parent.mkdir(parents=True, exist_ok=True)


def texture(desc):
    source = ROOT / desc['texture']
    if hashlib.sha256(source.read_bytes()).hexdigest() != desc['texture_sha256']:
        raise RuntimeError('Source texture changed: ' + str(source))
    name = 'T_Ecology_' + re.sub(r'[^A-Za-z0-9_]', '_', source.stem) + '_' + desc['texture_sha256'][:6]
    path = '/Game/Art/Textures/Ecology/' + name
    tex = u.load_asset(path)
    if tex is None:
        task = u.AssetImportTask()
        task.filename = str(source)
        task.destination_path = '/Game/Art/Textures/Ecology'
        task.destination_name = name
        task.automated = True
        task.factory = u.TextureFactory()
        task.save = True
        assets.import_asset_tasks([task])
        tex = u.load_asset(path)
    if not isinstance(tex, u.Texture2D):
        raise RuntimeError('Missing texture: ' + path)
    tex.set_editor_property('srgb', True)
    u.EditorAssetLibrary.save_loaded_asset(tex)
    if path not in report['textures']:
        report['textures'].append(path)
    return tex


def material(asset_name, desc):
    name = 'M_Ecology_' + asset_name + '_' + str(desc['slot']).zfill(2)
    folder = '/Game/Art/Materials/Ecology'
    path = folder + '/' + name
    mat = u.load_asset(path)
    if mat is None:
        mat = assets.create_asset(name, folder, u.Material, u.MaterialFactoryNew())
    if not isinstance(mat, u.Material):
        raise RuntimeError('Expected ecology Material: ' + path)
    u.MaterialEditingLibrary.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode', u.BlendMode.BLEND_MASKED if desc['masked'] else u.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property('two_sided', desc['two_sided'] or desc['masked'])
    if desc['masked']:
        mat.set_editor_property('opacity_mask_clip_value', desc['alpha_cutoff'])
    if desc.get('texture'):
        expr = u.MaterialEditingLibrary.create_material_expression(mat, u.MaterialExpressionTextureSample, -400, 0)
        expr.texture = texture(desc)
        u.MaterialEditingLibrary.connect_material_property(expr, 'RGB', u.MaterialProperty.MP_BASE_COLOR)
        if desc['masked']:
            u.MaterialEditingLibrary.connect_material_property(expr, 'A', u.MaterialProperty.MP_OPACITY_MASK)
    else:
        expr = u.MaterialEditingLibrary.create_material_expression(mat, u.MaterialExpressionConstant3Vector, -400, 0)
        expr.constant = u.LinearColor(*desc['color'][:3], 1)
        u.MaterialEditingLibrary.connect_material_property(expr, '', u.MaterialProperty.MP_BASE_COLOR)
    rough = u.MaterialEditingLibrary.create_material_expression(mat, u.MaterialExpressionConstant, -400, 200)
    rough.r = .85
    u.MaterialEditingLibrary.connect_material_property(rough, '', u.MaterialProperty.MP_ROUGHNESS)
    u.MaterialEditingLibrary.set_material_usage(mat, u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    u.MaterialEditingLibrary.recompile_material(mat)
    if not u.EditorAssetLibrary.save_loaded_asset(mat):
        raise RuntimeError('Save failed: ' + path)
    report['materials'].append(
        {'path': path, 'masked': bool(mat.get_editor_property('blend_mode') == u.BlendMode.BLEND_MASKED),
         'two_sided': bool(mat.get_editor_property('two_sided'))})
    return mat


try:
    for folder in ['/Game/Art/Nature', '/Game/Art/Materials/Ecology', '/Game/Art/Textures/Ecology']:
        u.EditorAssetLibrary.make_directory(folder)
    for row in manifest['assets']:
        source = ROOT / row['fbx']
        if hashlib.sha256(source.read_bytes()).hexdigest() != row['fbx_sha256']:
            raise RuntimeError('FBX changed: ' + row['name'])
        task = u.AssetImportTask()
        task.filename = str(source)
        task.destination_path = '/Game/Art/Nature'
        task.destination_name = row['name']
        task.automated = True
        task.replace_existing = True
        task.replace_existing_settings = True
        task.save = True
        task.factory = u.FbxFactory()
        opt = u.FbxImportUI()
        opt.automated_import_should_detect_type = False
        opt.import_materials = False
        opt.import_textures = False
        opt.mesh_type_to_import = u.FBXImportType.FBXIT_STATIC_MESH
        opt.import_as_skeletal = False
        opt.import_mesh = True
        opt.import_animations = False
        opt.static_mesh_import_data.set_editor_property('combine_meshes', True)
        opt.static_mesh_import_data.set_editor_property('auto_generate_collision', False)
        opt.static_mesh_import_data.set_editor_property('normal_import_method',
                                                        u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        task.options = opt
        assets.import_asset_tasks([task])
        mesh = u.load_asset(row['ue_path'])
        if not isinstance(mesh, u.StaticMesh):
            raise RuntimeError('Missing mesh: ' + row['ue_path'])
        slots = list(mesh.get_editor_property('static_materials'))
        assigned = []
        for index, slot in enumerate(slots):
            original = str(slot.get_editor_property('imported_material_slot_name'))
            normalized = re.sub(r'[^A-Za-z0-9_]', '_', original)
            desc = next((d for d in row['materials'] if re.sub(r'[^A-Za-z0-9_]', '_', d['name']) == normalized), None)
            if desc is None:
                raise RuntimeError('Unmapped material slot: ' + row['name'] + ' ' + original)
            mat = material(row['name'], desc)
            mesh.set_material(index, mat)
            assigned.append(mat.get_path_name())
        bounds = mesh.get_bounds()
        actual = [bounds.box_extent.x * 2, bounds.box_extent.y * 2, bounds.box_extent.z * 2]
        expected = [v * 100 for v in row['bounds_size_m']]
        # FBX axis conversion may swap X/Y; height and sorted footprint must agree.
        if not math.isclose(actual[2], expected[2], rel_tol=.02, abs_tol=.1):
            raise RuntimeError('Unexpected height/units: ' + row['name'] + ' ' + str(actual))
        if any(not math.isclose(a, b, rel_tol=.02, abs_tol=.1) for a, b in
               zip(sorted(actual[:2]), sorted(expected[:2]))):
            raise RuntimeError('Unexpected footprint/units: ' + row['name'])
        if not u.EditorAssetLibrary.save_loaded_asset(mesh):
            raise RuntimeError('Save failed: ' + row['ue_path'])
        report['meshes'].append({'path': row['ue_path'], 'bounds_size_cm': actual, 'material_slots': assigned,
                                 'source_sha256': row['fbx_sha256'], 'collision': 'disabled decorative mesh'})
    # Existing nature atlases now also render through instanced mesh components.
    existing_materials = set()
    for name in ('SM_Grass01', 'SM_Tree01', 'SM_Tree02', 'SM_Rock01', 'SM_Rock02', 'SM_Rock03'):
        mesh = u.load_asset('/Game/Art/Nature/' + name)
        if not isinstance(mesh, u.StaticMesh):
            raise RuntimeError('Missing existing nature mesh: ' + name)
        for slot in mesh.get_editor_property('static_materials'):
            mat = slot.get_editor_property('material_interface')
            if isinstance(mat, u.Material) and mat.get_path_name() not in existing_materials:
                u.MaterialEditingLibrary.set_material_usage(mat, u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
                u.MaterialEditingLibrary.recompile_material(mat)
                if not u.EditorAssetLibrary.save_loaded_asset(mat):
                    raise RuntimeError('Could not save instance usage: ' + mat.get_path_name())
                existing_materials.add(mat.get_path_name())
    report['existing_material_instancing_enabled'] = sorted(existing_materials)
    report['success'] = len(report['meshes']) == len(allowed)
except Exception as exc:
    report['errors'].append(type(exc).__name__ + ': ' + str(exc))
    raise
finally:
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    u.log('VERDANT_ECOLOGY_IMPORT ' + json.dumps(
        {'success': report['success'], 'meshes': len(report['meshes']), 'errors': report['errors']}))
