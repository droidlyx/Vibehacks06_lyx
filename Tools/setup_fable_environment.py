"""Apply Verdant's clear-after-rain lighting to the open Valley map.

Run through Unreal Python; never starts PIE or takes screenshots. Reuses only
Verdant-managed actors, leaves unrelated map content intact, and backs up the
saved map before its first edit. No engine/RHI/project rendering switch required.
"""
from pathlib import Path
from datetime import datetime, timezone
import json
import runpy
import shutil
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
CLOUD_SOURCE = '/Game/Environment/_Global/Materials/Sky/M_Clouds_Soft'
CLOUD_INSTANCE = '/Game/Art/Environment/MI_FableClouds'
TAG = 'VerdantFableEnvironment'


def set_props(obj, **properties):
    for key, value in properties.items():
        obj.set_editor_property(key, value)


def apply(save=True):
    editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError('Finish the current gameplay session before changing the environment.')
    if save and u.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError('Save current map edits first so the environment backup includes them.')
    world = editor.get_editor_world()
    if world.get_path_name() != '/Game/Maps/Valley.Valley':
        raise RuntimeError('Open /Game/Maps/Valley before applying this look.')
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    registry = u.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(['/Game/Environment/_Global/Materials/Sky'], True)
    source = u.load_asset(CLOUD_SOURCE)
    if not source:
        raise RuntimeError('Run Tools/migrate_titan_sky.py --apply first.')

    backup = ROOT / 'Saved/EnvironmentBackup' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    backup.mkdir(parents=True, exist_ok=False)
    map_file = ROOT / 'Content/Maps/Valley.umap'
    if map_file.exists():
        shutil.copy2(map_file, backup / 'Valley.umap')
    (backup / 'README.txt').write_text(
        'Saved-map backup before Fable lighting. To restore, close Unreal Editor, copy Valley.umap to '
        'Content/Maps/Valley.umap, then reopen. No terrain or gameplay code changed.\n', encoding='utf-8')

    existing = actors.get_all_level_actors()

    def managed(cls, label):
        matches = [a for a in existing if isinstance(a, cls)
                   and (a.actor_has_tag(TAG) or a.actor_has_tag('VerdantMapSetup'))]
        if len(matches) > 1:
            raise RuntimeError('Multiple managed actors for ' + cls.__name__)
        if matches:
            actor = matches[0]
        else:
            if any(isinstance(a, cls) for a in existing):
                raise RuntimeError('Unmanaged ' + cls.__name__ + ' already exists; do not duplicate lighting.')
            actor = actors.spawn_actor_from_class(cls, u.Vector(0, 0, 0))
            existing.append(actor)
        actor.set_actor_label(label)
        actor.set_folder_path('Verdant/Lighting')
        actor.set_editor_property('tags', list(dict.fromkeys([str(t) for t in actor.tags] + [TAG, 'VerdantMapSetup'])))
        return actor

    with u.ScopedEditorTransaction('Verdant: clear-after-rain environment'):
        atmosphere = managed(u.SkyAtmosphere, 'Fable_AzureAtmosphere')
        air = atmosphere.get_component_by_class(u.SkyAtmosphereComponent)
        set_props(air, rayleigh_scattering_scale=.82,
                  rayleigh_scattering=u.LinearColor(.0050, .0125, .0275, 1),
                  # Coefficient in 1/km, not a normalized artistic multiplier.
                  # UE's Earth default is .003996; .55 caused severe white haze.
                  mie_scattering_scale=.0025, mie_absorption_scale=.0003, mie_anisotropy=.72,
                  ground_albedo=u.Color(100, 123, 86, 255),
                  aerial_pespective_view_distance_scale=1.0)

        sun = managed(u.DirectionalLight, 'Fable_WarmMorningSun')
        sun.set_actor_rotation(u.Rotator(pitch=-32, yaw=-45, roll=0), False)
        light = sun.get_component_by_class(u.DirectionalLightComponent)
        light.set_mobility(u.ComponentMobility.MOVABLE)
        light.set_intensity(5.5)
        light.set_light_color(u.LinearColor(1, .91, .76, 1))
        set_props(light, atmosphere_sun_light=True, light_source_angle=1.5,
                  dynamic_shadow_distance_movable_light=22000., dynamic_shadow_cascades=4,
                  shadow_bias=.4, contact_shadow_length=.04,
                  volumetric_scattering_intensity=.55,
                  cast_cloud_shadows=False, cloud_shadow_strength=.32,
                  cloud_shadow_on_surface_strength=.65, cloud_shadow_extent=4.,
                  cloud_shadow_map_resolution_scale=.5, cloud_shadow_ray_sample_count_scale=.5)

        sky = managed(u.SkyLight, 'Fable_CoolSkyFill')
        fill = sky.get_component_by_class(u.SkyLightComponent)
        fill.set_mobility(u.ComponentMobility.MOVABLE)
        fill.set_intensity(.90)
        fill.set_light_color(u.LinearColor(.94, .98, 1, 1))
        set_props(fill, source_type=u.SkyLightSourceType.SLS_CAPTURED_SCENE,
                  real_time_capture=True, cubemap_resolution=128,
                  lower_hemisphere_is_black=True, lower_hemisphere_color=u.LinearColor(.035, .045, .025, 1),
                  cloud_ambient_occlusion=False, cloud_ambient_occlusion_strength=.25,
                  cloud_ambient_occlusion_extent=4., cloud_ambient_occlusion_map_resolution_scale=.5)

        fog = managed(u.ExponentialHeightFog, 'Fable_LayeredValleyHaze')
        haze = fog.get_component_by_class(u.ExponentialHeightFogComponent)
        haze.set_fog_density(.0015)
        haze.set_fog_height_falloff(.22)
        haze.set_fog_inscattering_color(u.LinearColor(.48, .62, .69, 1))
        haze.set_fog_max_opacity(.25)
        haze.set_start_distance(4000.)
        haze.set_second_fog_density(0.)
        haze.set_second_fog_height_falloff(.32)
        haze.set_second_fog_height_offset(-600.)
        haze.set_directional_inscattering_color(u.LinearColor(.55, .43, .28, 1))
        haze.set_volumetric_fog(False)
        haze.set_volumetric_fog_albedo(u.Color(220, 232, 239, 255))
        haze.set_volumetric_fog_scattering_distribution(.35)
        haze.set_volumetric_fog_distance(20000.)
        haze.set_volumetric_fog_start_distance(800.)
        haze.set_volumetric_fog_near_fade_in_distance(1200.)

        # The Titan source also renders a volcano plume. Use our separate,
        # inspected cloud-only parent; never modify the original source graph.
        instance = u.load_asset(CLOUD_INSTANCE)
        if not instance:
            instance = runpy.run_path(str(ROOT / 'Tools/prepare_fable_clouds.py'))['prepare']()
        cloud = managed(u.VolumetricCloud, 'Fable_SoftDriftingClouds')
        volume = cloud.get_component_by_class(u.VolumetricCloudComponent)
        set_props(volume, material=instance, layer_bottom_altitude=1.2, layer_height=1.0,
                  tracing_start_max_distance=80., tracing_max_distance=30.,
                  ground_albedo=u.Color(122, 143, 99, 255),
                  view_sample_count_scale=1., shadow_view_sample_count_scale=.5,
                  reflection_view_sample_count_scale_value=.5,
                  shadow_reflection_view_sample_count_scale_value=.25)

        post = managed(u.PostProcessVolume, 'Fable_ColourAndExposure')
        set_props(post, unbound=True, priority=10., blend_weight=1.)
        settings = post.get_editor_property('settings')
        settings_values = {
            'auto_exposure_method': u.AutoExposureMethod.AEM_MANUAL,
            'auto_exposure_apply_physical_camera_exposure': False,
            'auto_exposure_bias': .35,
            'bloom_intensity': .18,
            'bloom_threshold': 1.5,
            'vignette_intensity': .08,
            'color_saturation': u.Vector4(1., 1., 1., 1.04),
            'color_contrast': u.Vector4(1., 1., 1., 1.035),
            'color_gain_highlights': u.Vector4(1.015, 1., .975, 1.),
            'color_gain_shadows': u.Vector4(.975, 1., 1.025, 1.),
            'ambient_occlusion_intensity': .55,
            'ambient_occlusion_radius': 70.,
            'ambient_occlusion_quality': 60.,
            'motion_blur_amount': 0.,
        }
        for name, value in settings_values.items():
            settings.set_editor_property('override_' + name, True)
            settings.set_editor_property(name, value)
        post.set_editor_property('settings', settings)

        if save:
            if not u.EditorAssetLibrary.save_loaded_asset(instance):
                raise RuntimeError('Cloud instance save failed')
            if not levels.save_current_level():
                raise RuntimeError('Valley map save failed')

    report = {
        'profile': 'clear woodland v2 - corrected atmospheric coefficients', 'map': world.get_path_name(),
        'map_backup': str(backup / 'Valley.umap'), 'cloud_source': CLOUD_SOURCE,
        'cloud_instance': CLOUD_INSTANCE, 'sun_lux': light.get_editor_property('intensity'),
        'sky_intensity': fill.get_editor_property('intensity'),
        'cloud_bottom_km': volume.get_editor_property('layer_bottom_altitude'),
        'cloud_height_km': volume.get_editor_property('layer_height'),
        'fog_density': haze.get_editor_property('fog_density'), 'post_process_overrides': list(settings_values),
        'mie_scattering_per_km': air.get_editor_property('mie_scattering_scale'),
        'volumetric_fog': False, 'cloud_shadows': False, 'cloud_ambient_occlusion': False,
        'renderer': 'existing DX11 / SM5; no Lumen or renderer migration',
        'actors': [a.get_actor_label() for a in existing if a.actor_has_tag(TAG)],
        'validation': 'Saved asset/property verification only; visual and performance review pending user Play',
    }
    out = ROOT / 'Assets/TitanSky/environment-report.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('VERDANT_FABLE_ENVIRONMENT ' + json.dumps(report))
    return report


if __name__ == '__main__':
    apply()
