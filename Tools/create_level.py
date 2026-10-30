"""Create Verdant's own materials and launch map after its C++ module compiles.

Run with UnrealEditor-Cmd Verdant.uproject -run=pythonscript -script=<this file>.
The landscape and living world are built by the original runtime GameMode.
Only /Game/Art/Materials/M_Verdant, M_Terrain and /Game/Maps/Valley are authored.
"""
from pathlib import Path
import json
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
    roughness = u.MaterialEditingLibrary.create_material_expression(material, u.MaterialExpressionScalarParameter, -450, 100)
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


atmosphere = spawn(u.SkyAtmosphere, 'ValleyBlueSky')
atmosphere.get_component_by_class(u.SkyAtmosphereComponent).set_editor_property('rayleigh_scattering_scale', .75)
sun = spawn(u.DirectionalLight, 'WarmAfternoonSun', (0, 0, 10000), (-38, -38, 0))
sunlight = sun.get_component_by_class(u.DirectionalLightComponent)
sunlight.set_mobility(u.ComponentMobility.MOVABLE)
sunlight.set_intensity(4.2)
sunlight.set_light_color(u.LinearColor(1, .88, .67))
sunlight.set_editor_property('atmosphere_sun_light', True)
sky = spawn(u.SkyLight, 'OpenSkyFill')
skylight = sky.get_component_by_class(u.SkyLightComponent)
skylight.set_mobility(u.ComponentMobility.MOVABLE)
skylight.set_intensity(1.1)
skylight.set_editor_property('real_time_capture', True)
fog = spawn(u.ExponentialHeightFog, 'DistantValleyHaze')
fog_component = fog.get_component_by_class(u.ExponentialHeightFogComponent)
fog_component.set_fog_density(.009)
fog_component.set_fog_height_falloff(.15)
fog_component.set_fog_inscattering_color(u.LinearColor(.57, .68, .72))
start = spawn(u.PlayerStart, 'TravelerArrival', (-1000, -5000, 210), (0, 78, 0))
start.set_folder_path('Verdant/Start')
assert levels.save_current_level(), 'Map save failed'
u.EditorAssetLibrary.save_directory(MATERIALS, only_if_is_dirty=True, recursive=True)
report = {'map': LEVEL, 'game_mode': mode.get_path_name(),
          'materials': [MATERIALS + '/M_Verdant', MATERIALS + '/M_Terrain'],
          'runtime_landscape': True, 'setup_actor_count': 5}
output = ROOT / 'Saved' / 'Validation'
output.mkdir(parents=True, exist_ok=True)
(output / 'map_setup.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('VERDANT_MAP_READY ' + LEVEL)
