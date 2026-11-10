"""Create Verdant's own materials and launch map after its C++ module compiles.

Run with UnrealEditor-Cmd Verdant.uproject -run=pythonscript -script=<this file>.
The landscape and living world are built by the original runtime GameMode.
Authors shared materials, /Game/Maps/Valley and the project-local Fable cloud
adaptation. Requires the selected Titan sky dependencies to be migrated first.
"""
from pathlib import Path
import json
import runpy
import unreal as u

ROOT = Path(u.Paths.project_dir())
MATERIALS = '/Game/Art/Materials'
LEVEL = '/Game/Maps/Valley'
assets = u.AssetToolsHelpers.get_asset_tools()
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)


def make_material(name, terrain=False):
    path = MATERIALS + '/' + name
    material = u.load_asset(path)
    if material:
        return material
    material = assets.create_asset(name, MATERIALS, u.Material, u.MaterialFactoryNew())
    assert material, 'Could not create ' + path
    material.set_editor_property('two_sided', False)
    color = u.MaterialEditingLibrary.create_material_expression(
        material, u.MaterialExpressionVertexColor if terrain else u.MaterialExpressionVectorParameter, -450, -100)
    if not terrain:
        color.set_editor_property('parameter_name', 'Tint')
        color.set_editor_property('default_value', u.LinearColor(.3, .5, .2, 1))
    roughness = u.MaterialEditingLibrary.create_material_expression(material, u.MaterialExpressionScalarParameter, -450,
                                                                    100)
    roughness.set_editor_property('parameter_name', 'Roughness')
    roughness.set_editor_property('default_value', .92 if terrain else .88)
    glow = u.MaterialEditingLibrary.create_material_expression(material, u.MaterialExpressionScalarParameter, -450, 240)
    glow.set_editor_property('parameter_name', 'Glow')
    glow.set_editor_property('default_value', .08 if terrain else 0)
    emission = u.MaterialEditingLibrary.create_material_expression(material, u.MaterialExpressionMultiply, -180, 100)
    u.MaterialEditingLibrary.connect_material_expressions(color, '', emission, 'A')
    u.MaterialEditingLibrary.connect_material_expressions(glow, '', emission, 'B')
    u.MaterialEditingLibrary.connect_material_property(color, '', u.MaterialProperty.MP_BASE_COLOR)
    u.MaterialEditingLibrary.connect_material_property(roughness, '', u.MaterialProperty.MP_ROUGHNESS)
    u.MaterialEditingLibrary.connect_material_property(emission, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    u.MaterialEditingLibrary.recompile_material(material)
    assert u.EditorAssetLibrary.save_loaded_asset(material), 'Material save failed: ' + path
    return material


make_material('M_Verdant')
make_material('M_Terrain', terrain=True)
mode = u.load_class(None, '/Script/Verdant.VerdantGameMode')
assert mode, 'Compile the Verdant module before creating its map.'
if u.EditorAssetLibrary.does_asset_exist(LEVEL):
    assert levels.load_level(LEVEL)
    for actor in actors.get_all_level_actors():
        if actor.actor_has_tag('VerdantMapSetup'):
            actors.destroy_actor(actor)
else:
    assert levels.new_level(LEVEL)
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', mode)


def spawn(cls, label, position=(0, 0, 0), rotation=(0, 0, 0)):
    actor = actors.spawn_actor_from_class(cls, u.Vector(*position), u.Rotator(*rotation))
    assert actor, label
    actor.set_actor_label(label)
    actor.tags = ['VerdantMapSetup']
    actor.set_folder_path('Verdant/Lighting')
    return actor


# Keep map regeneration on the same environment profile as the authored map.
# Run Tools/migrate_titan_sky.py --apply once before rebuilding the launch map.
runpy.run_path(str(ROOT / 'Tools/setup_fable_environment.py'))['apply'](save=False)
runpy.run_path(str(ROOT / 'Tools/setup_sunset_sky.py'))['apply'](save=False)
start = spawn(u.PlayerStart, 'TravelerArrival', (-1000, -5000, 210), (0, 78, 0))
start.set_folder_path('Verdant/Start')
assert levels.save_current_level(), 'Map save failed'
u.EditorAssetLibrary.save_directory(MATERIALS, only_if_is_dirty=True, recursive=True)
u.EditorAssetLibrary.save_directory('/Game/Art/Environment', only_if_is_dirty=True, recursive=True)
report = {'map': LEVEL, 'game_mode': mode.get_path_name(),
          'materials': [MATERIALS + '/M_Verdant', MATERIALS + '/M_Terrain'],
          'runtime_landscape': True, 'setup_actor_count': 8}
output = ROOT / 'Saved' / 'Validation'
output.mkdir(parents=True, exist_ok=True)
(output / 'map_setup.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('VERDANT_MAP_READY ' + LEVEL)
