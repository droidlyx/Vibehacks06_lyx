"""Make a project-local, smoke-free version of Titan's soft cloud material.

Run in Unreal Python. Original imported Titan packages are read-only inputs.
This only edits /Game/Art/Environment/M_FableCloudBase and MI_FableClouds.
"""
from pathlib import Path
import json
import unreal as u

SOURCE_BASE = '/Game/Environment/_Global/Materials/Sky/M_Clouds'
SOURCE_SOFT = '/Game/Environment/_Global/Materials/Sky/M_Clouds_Soft'
BASE = '/Game/Art/Environment/M_FableCloudBase'
INSTANCE = '/Game/Art/Environment/MI_FableClouds'


def prepare():
    if u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError('Finish gameplay before preparing cloud materials.')
    registry = u.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(['/Game/Environment/_Global/Materials/Sky'], True)
    source = u.load_asset(SOURCE_SOFT)
    if not source:
        raise RuntimeError('Run migrate_titan_sky.py --apply first.')
    material = u.load_asset(BASE)
    if not material:
        material = u.EditorAssetLibrary.duplicate_asset(SOURCE_BASE, BASE)
    if not material:
        raise RuntimeError('Could not duplicate cloud base material.')
    nodes = {node.get_name(): node for node in u.MaterialEditingLibrary.get_material_expressions(material)}
    # Verified source graph: Power_5 is the ordinary-cloud albedo; Add_4 adds
    # smoke density to Multiply_0 and feeds ConservativeDensity. Bypassing both
    # smoke branches removes the Titan volcano from this world's sky.
    albedo = nodes['MaterialExpressionPower_5']
    density = nodes['MaterialExpressionMultiply_0']
    output = nodes['MaterialExpressionVolumetricAdvancedMaterialOutput_1']
    if not u.MaterialEditingLibrary.connect_material_property(albedo, '', u.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError('Could not bypass volcanic smoke colour.')
    if not u.MaterialEditingLibrary.connect_material_expressions(density, '', output, 'ConservativeDensity'):
        raise RuntimeError('Could not bypass volcanic smoke density.')
    # Ground-view shadow maps are cheaper than secondary ray marching. One
    # scattering octave is retained; actual GPU cost still needs user review.
    output.set_editor_property('ray_march_volume_shadow', False)
    u.MaterialEditingLibrary.recompile_material(material)

    instance = u.load_asset(INSTANCE)
    if not instance:
        instance = u.AssetToolsHelpers.get_asset_tools().create_asset(
            'MI_FableClouds', '/Game/Art/Environment', u.MaterialInstanceConstant,
            u.MaterialInstanceConstantFactoryNew())
    if not instance:
        raise RuntimeError('Could not create cloud material instance.')
    u.MaterialEditingLibrary.set_material_instance_parent(instance, material)
    scalars = {}
    vectors = {}
    for name in u.MaterialEditingLibrary.get_scalar_parameter_names(source):
        value = u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(source, name)
        if str(name) == 'CloudSpeed':
            value = .06
        u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, name, value)
        scalars[str(name)] = value
    for name in u.MaterialEditingLibrary.get_vector_parameter_names(source):
        value = u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(source, name)
        if str(name) == 'Albedo':
            value = u.LinearColor(1., .98, .93, 1.)
        u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, name, value)
        vectors[str(name)] = [value.r, value.g, value.b, value.a]
    for name in u.MaterialEditingLibrary.get_texture_parameter_names(source):
        value = u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(source, name)
        if value:
            u.MaterialEditingLibrary.set_material_instance_texture_parameter_value(instance, name, value)
    u.MaterialEditingLibrary.update_material_instance(instance)
    assert u.MaterialEditingLibrary.get_material_property_input_node(material,
                                                                     u.MaterialProperty.MP_BASE_COLOR) == albedo
    assert u.MaterialEditingLibrary.get_inputs_for_material_expression(material, output)[-1] == density
    assert not output.get_editor_property('ray_march_volume_shadow')
    for asset in (material, instance):
        if not u.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError('Could not save ' + asset.get_path_name())

    report = {
        'source_base': SOURCE_BASE, 'source_soft': SOURCE_SOFT,
        'base': BASE, 'instance': INSTANCE,
        'adaptation': 'Ordinary cloud colour/density bypass volcano smoke; Beer shadow maps; warm albedo; slower cloud motion.',
        'cloud_height_units': 'Normalized cloud profile lookup offset, not kilometres; source values retained.',
        'scalar_parameters': scalars, 'vector_parameters': vectors,
        'ray_march_volume_shadow': False,
        'validation': 'Graph and saved-asset checks only; visual/performance review pending user Play.'
    }
    path = Path(u.Paths.project_dir()) / 'Assets/TitanSky/cloud-material-report.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('VERDANT_FABLE_CLOUD_MATERIAL ' + INSTANCE)
    return instance


if __name__ == '__main__':
    prepare()
