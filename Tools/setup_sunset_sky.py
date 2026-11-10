"""Woodland sunrise: distinct sun above the ridge, warm dawn band and direct rays.

No terrain, NPC, gameplay, or global post-process edits. Uses a procedural
Unlit/IsSky dome behind the existing volumetric clouds. No Play or screenshots.
"""
from pathlib import Path
from datetime import datetime, timezone
import json
import math
import shutil
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
MATERIAL = '/Game/Art/Environment/M_StorybookSunsetSky'
TAG = 'VerdantSunsetSky'
# Terrain-only authoring measured a ~22.6 degree ridge in this direction.
# The former azimuth 48 / elevation 8 sun sat behind a ~36 degree ridge.
# Keep the visible disk just above the local mountain horizon at arrival.
SUN_AZIMUTH = 96.0
SUN_ELEVATION = 25.0
SUN_COLOR = (1., .86, .68, 1.)
CLOUD_ALBEDO = (1., .94, .87, 1.)
SUN_INTENSITY = 12.0
SKY_FILL_INTENSITY = .60

# Linear HDR values, composed before the existing project's tone mapping.
# The camera-vector input avoids equirectangular seams and texture stretching.
SKY_CODE = r'''
float3 D = -normalize(ViewRay);
float3 S = normalize(SunDirection);
float height = saturate(D.z);
float3 azimuthD = normalize(float3(D.xy, 0.0001));
float3 azimuthS = normalize(float3(S.xy, 0.0001));
float towardsSun = pow(saturate(dot(azimuthD, azimuthS) * 0.5 + 0.5), 5.0);
float nearHorizon = exp(-height * 5.0);

float3 horizon = lerp(float3(0.12, 0.20, 0.25), float3(0.60, 0.30, 0.13), towardsSun);
float3 upperSky = float3(0.055, 0.095, 0.17);
float3 midSky = lerp(float3(0.12, 0.20, 0.28), float3(0.32, 0.29, 0.28), towardsSun);
float3 sky = lerp(horizon, midSky, smoothstep(0.0, 0.32, height));
sky = lerp(sky, upperSky, smoothstep(0.20, 0.90, height));
sky += float3(0.025, 0.022, 0.016) * towardsSun * nearHorizon;
// A narrow dawn band follows the visible mountain sunrise, not the whole sky.
float dawnBand = exp(-pow((D.z - S.z) * 10.0, 2.0)) * towardsSun * 0.70;
sky = lerp(sky, float3(0.80, 0.38, 0.16), dawnBand);

// A quiet moss-lit morning; warm colour stays local to the rising sun.
sky = lerp(sky, lerp(float3(0.12, 0.18, 0.19), float3(0.26, 0.28, 0.24), towardsSun),
           smoothstep(0.0, 0.28, -D.z));

float cosineAngle = dot(D, S);
float disc = smoothstep(cos(radians(0.86)), cos(radians(0.72)), cosineAngle);
float halo = pow(saturate(cosineAngle), 180.0);
float broadGlow = pow(saturate(cosineAngle), 18.0);
sky += float3(0.45, 0.25, 0.07) * halo + float3(0.025, 0.018, 0.012) * broadGlow;
sky = lerp(sky, float3(7.0, 5.0, 2.8), disc);
return max(sky, 0.0);
'''


def apply(save=True):
    editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError('Finish the current Play session before applying the sky.')
    if save and u.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError('Save existing map edits before the sky backup.')
    world = editor.get_editor_world()
    if world.get_path_name() != '/Game/Maps/Valley.Valley':
        raise RuntimeError('Open Valley first.')
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    existing = actors.get_all_level_actors()

    def managed(cls):
        found = [a for a in existing if isinstance(a, cls) and a.actor_has_tag('VerdantFableEnvironment')]
        if len(found) != 1:
            raise RuntimeError('Expected one managed ' + cls.__name__)
        return found[0]

    sun = managed(u.DirectionalLight)
    sky = managed(u.SkyLight)
    clouds = managed(u.VolumetricCloud)
    fog = managed(u.ExponentialHeightFog)
    instance = clouds.get_component_by_class(u.VolumetricCloudComponent).get_editor_property('material')
    if not instance or instance.get_path_name() != '/Game/Art/Environment/MI_FableClouds.MI_FableClouds':
        raise RuntimeError('Expected project-local Fable cloud instance.')
    backup = ROOT / 'Saved/EnvironmentBackup' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ_sunset')
    backup.mkdir(parents=True)
    for relative in ['Content/Maps/Valley.umap', 'Content/Art/Environment/MI_FableClouds.uasset',
                     'Content/Art/Environment/M_StorybookSunsetSky.uasset']:
        source = ROOT / relative
        if source.exists():
            target = backup / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)

    material = u.load_asset(MATERIAL)
    if material:
        u.MaterialEditingLibrary.delete_all_material_expressions(material)
    else:
        material = u.AssetToolsHelpers.get_asset_tools().create_asset(
            'M_StorybookSunsetSky', '/Game/Art/Environment', u.Material, u.MaterialFactoryNew())
    material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('is_sky', True)
    camera = u.MaterialEditingLibrary.create_material_expression(material, u.MaterialExpressionCameraVectorWS, -600, 0)
    direction = u.MaterialEditingLibrary.create_material_expression(material, u.MaterialExpressionVectorParameter, -600,
                                                                    160)
    direction.set_editor_property('parameter_name', 'SunDirection')
    azimuth, elevation = math.radians(SUN_AZIMUTH), math.radians(SUN_ELEVATION)
    sun_vector = (math.cos(elevation) * math.cos(azimuth), math.cos(elevation) * math.sin(azimuth), math.sin(elevation))
    direction.set_editor_property('default_value', u.LinearColor(*sun_vector, 1.))
    custom = u.MaterialEditingLibrary.create_material_expression(material, u.MaterialExpressionCustom, -250, 0)
    custom.set_editor_property('description', 'Ridge sunrise / warm dawn band / aligned sun disc')
    custom.set_editor_property('code', SKY_CODE)
    custom.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ['ViewRay', 'SunDirection']:
        slot = u.CustomInput()
        slot.set_editor_property('input_name', name)
        inputs.append(slot)
    custom.set_editor_property('inputs', inputs)
    assert u.MaterialEditingLibrary.connect_material_expressions(camera, '', custom, 'ViewRay')
    assert u.MaterialEditingLibrary.connect_material_expressions(direction, 'RGB', custom, 'SunDirection')
    assert u.MaterialEditingLibrary.connect_material_property(custom, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    u.MaterialEditingLibrary.recompile_material(material)

    domes = [a for a in existing if a.actor_has_tag(TAG)]
    if len(domes) > 1:
        raise RuntimeError('Multiple sunset domes found.')
    dome = domes[0] if domes else actors.spawn_actor_from_class(u.StaticMeshActor, u.Vector(0, 0, 0))
    dome.set_actor_label('Sunset_PeachAndVioletSky')
    dome.set_folder_path('Verdant/Lighting')
    dome.set_editor_property('tags', [TAG, 'VerdantMapSetup'])
    component = dome.get_component_by_class(u.StaticMeshComponent)
    component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Sphere'))
    component.set_material(0, material)
    # Persist the profile as well as the live enabled flag: BlockAll can be
    # reapplied when the map is reloaded or duplicated for PIE.
    dome.set_actor_enable_collision(False)
    component.set_collision_profile_name('NoCollision')
    component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    component.set_editor_property('generate_overlap_events', False)
    component.set_cast_shadow(False)
    component.set_editor_property('affect_distance_field_lighting', False)
    # Radius 200 km: farther than cloud traces so opaque depth does not cut off
    # clouds at the horizon. Camera-vector material remains stable during growth.
    dome.set_actor_scale3d(u.Vector(400000., 400000., 400000.))

    # DirectionalLight points along the incoming rays, opposite the solar disc.
    # Python Rotator positional arguments use roll/pitch/yaw; name each axis.
    sun.set_actor_rotation(u.Rotator(pitch=-SUN_ELEVATION, yaw=SUN_AZIMUTH + 180., roll=0.), False)
    incoming = sun.get_actor_forward_vector()
    assert max(abs(actual - expected) for actual, expected in
               zip((-incoming.x, -incoming.y, -incoming.z), sun_vector)) < .00001
    light = sun.get_component_by_class(u.DirectionalLightComponent)
    # Morning key is restrained; keep sky fill and exposure for readable shade.
    light.set_intensity(SUN_INTENSITY)
    light.set_light_color(u.LinearColor(*SUN_COLOR))
    light.set_editor_property('light_source_angle', 1.6)
    light.set_editor_property('cast_cloud_shadows', False)
    light.set_editor_property('cast_shadows', True)
    light.set_editor_property('cast_dynamic_shadows', True)
    # View-dependent sunlight shafts around the actual solar direction, without
    # reintroducing global volumetric fog. These are not world-space fog beams.
    light.set_enable_light_shaft_bloom(True)
    light.set_bloom_scale(.15)
    light.set_bloom_threshold(1.2)
    light.set_bloom_max_brightness(2.5)
    light.set_bloom_tint(u.Color(255, 230, 190, 255))
    light.set_enable_light_shaft_occlusion(False)
    light.set_light_shaft_override_direction(u.Vector(0, 0, 0))
    sun.set_actor_label('Sunset_GoldenSideSun')
    fill = sky.get_component_by_class(u.SkyLightComponent)
    fill.set_intensity(SKY_FILL_INTENSITY)
    fill.set_light_color(u.LinearColor(1., 1., 1., 1.))
    fill.set_editor_property('real_time_capture', True)
    fill.set_editor_property('cloud_ambient_occlusion', False)
    sky.set_actor_label('Sunset_MatchedSkyFill')
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, 'Albedo',
                                                                          u.LinearColor(*CLOUD_ALBEDO))
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, 'Extinction',
                                                                          u.LinearColor(1., 1., 1., 1.))
    u.MaterialEditingLibrary.update_material_instance(instance)
    clouds.set_actor_label('Sunset_PeachCloudBanks')
    # Existing thin ground haze stays; the far skydome must not be washed gray.
    fog.get_component_by_class(u.ExponentialHeightFogComponent).set_fog_cutoff_distance(200000.)

    if save:
        for asset in [material, instance]:
            assert u.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
        assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
    report = {
        'profile': 'Woodland ridge sunrise with direct sunlight and subtle screen-space shafts', 'backup': str(backup),
        'material': MATERIAL, 'sun_elevation_degrees': SUN_ELEVATION,
        'sun_azimuth_degrees': SUN_AZIMUTH, 'sky_radius_km': 200,
        'sun_direction': sun_vector, 'sun_lux': SUN_INTENSITY, 'sky_fill': SKY_FILL_INTENSITY,
        'light_shaft_bloom': True, 'light_shaft_bloom_scale': .15, 'global_volumetric_fog': False,
        'scope': 'sky, clouds, matching sun/sky fill, fog far cutoff only; no ground geometry/material or post-process changes',
        'visual_acceptance': 'pending user review; no PIE or screenshot capture',
    }
    (ROOT / 'Assets/TitanSky/sunset-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('VERDANT_SUNSET_SKY ' + json.dumps(report))
    return report


if __name__ == '__main__':
    apply()
