"""Targeted import of accepted, measured runtime audio. No PIE or screenshot calls.

Run through the connected editor Python API after prepare_runtime_audio.py.
Only the manifest-listed SoundWaves and four new SoundClasses are saved.
"""
from pathlib import Path
import hashlib
import json
import math
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
assert ROOT == Path(__file__).resolve().parents[1], 'Run in the Verdant project.'
manifest = json.loads((ROOT / 'Assets/Audio/runtime-audio-manifest.json').read_text(encoding='utf-8'))
report_path = ROOT / 'Saved/Audio/runtime-import.json'
report_path.parent.mkdir(parents=True, exist_ok=True)
report = {'success': False, 'classes': [], 'sounds': [], 'errors': [],
          'user_audition_approval': manifest['approval'], 'gameplay_test_performed': False,
          'screenshot_performed': False}
asset_tools = u.AssetToolsHelpers.get_asset_tools()


def verify_close(value, expected, label, tolerance=1e-6):
    if not math.isclose(value, expected, abs_tol=tolerance):
        raise RuntimeError(f'{label}: {value} != {expected}')


try:
    classes = {}
    for name, row in manifest['sound_classes'].items():
        path = row['path']
        folder, asset_name = path.rsplit('/', 1)
        u.EditorAssetLibrary.make_directory(folder)
        sound_class = u.load_asset(path)
        if sound_class is None:
            sound_class = asset_tools.create_asset(asset_name, folder, u.SoundClass, u.SoundClassFactory())
        if not isinstance(sound_class, u.SoundClass):
            raise RuntimeError('Expected SoundClass: ' + path)
        properties = sound_class.get_editor_property('properties')
        properties.set_editor_property('volume', row['linear_gain'])
        sound_class.set_editor_property('properties', properties)
        actual = float(sound_class.get_editor_property('properties').get_editor_property('volume'))
        verify_close(actual, row['linear_gain'], path)
        if not u.EditorAssetLibrary.save_loaded_asset(sound_class):
            raise RuntimeError('Could not save ' + path)
        classes[name] = sound_class
        report['classes'].append({'path': path, 'gain_db': row['gain_db'], 'verified_linear_gain': actual})

    for row in manifest['assets']:
        source = (ROOT / row['prepared_file']).resolve()
        if not source.is_relative_to((ROOT / 'Local/AudioRuntime').resolve()):
            raise RuntimeError('Unexpected source location: ' + str(source))
        if hashlib.sha256(source.read_bytes()).hexdigest() != row['prepared_sha256']:
            raise RuntimeError('Prepared audio changed: ' + row['id'])
        folder = row['destination']
        if folder not in ('/Game/Art/Audio/Runtime', '/Game/Art/Audio/LocalLicensed'):
            raise RuntimeError('Unexpected import destination: ' + folder)
        u.EditorAssetLibrary.make_directory(folder)
        task = u.AssetImportTask()
        task.filename = str(source)
        task.destination_path = folder
        task.destination_name = row['id']
        task.automated = True
        task.replace_existing = True
        task.replace_existing_settings = True
        task.save = True
        factory = u.SoundFactory()
        factory.set_editor_property('auto_create_cue', False)
        task.factory = factory
        asset_tools.import_asset_tasks([task])
        sound = u.load_asset(row['ue_path'])
        if not isinstance(sound, u.SoundWave):
            raise RuntimeError('Expected SoundWave: ' + row['ue_path'])
        sound.set_editor_property('sound_class_object', classes[row['bus']])
        sound.set_editor_property('looping', row['looping'])
        sound.set_editor_property('volume', 1.0)
        sound.set_editor_property('pitch', 1.0)
        if row['bus'] in ('Music', 'Ambience'):
            sound.set_editor_property('virtualization_mode', u.VirtualizationMode.PLAY_WHEN_SILENT)
        sound.set_editor_property('loading_behavior', getattr(u.SoundWaveLoadingBehavior, row['loading_behavior']))
        sound.set_editor_property('compression_quality', 80 if row['bus'] in ('Music', 'Ambience') else 60)
        duration = float(sound.get_editor_property('duration'))
        channels = int(sound.get_editor_property('num_channels'))
        rate = int(sound.get_editor_property('imported_sample_rate'))
        if channels != row['channels'] or rate != 48000:
            raise RuntimeError('Unexpected imported format: ' + row['id'])
        verify_close(duration, row['measured']['duration_seconds'], row['id'] + ' duration', .002)
        if sound.get_editor_property('sound_class_object') != classes[row['bus']]:
            raise RuntimeError('Incorrect class: ' + row['id'])
        if bool(sound.get_editor_property('looping')) != row['looping']:
            raise RuntimeError('Incorrect loop setting: ' + row['id'])
        loading = sound.get_editor_property('loading_behavior')
        if loading != getattr(u.SoundWaveLoadingBehavior, row['loading_behavior']):
            raise RuntimeError('Incorrect loading behavior: ' + row['id'])
        if row['bus'] in ('Music', 'Ambience') and sound.get_editor_property(
                'virtualization_mode') != u.VirtualizationMode.PLAY_WHEN_SILENT:
            raise RuntimeError('Incorrect virtualization mode: ' + row['id'])
        if not u.EditorAssetLibrary.save_loaded_asset(sound):
            raise RuntimeError('Could not save ' + row['ue_path'])
        report['sounds'].append({'path': row['ue_path'], 'channels': channels, 'sample_rate_hz': rate,
                                 'duration_seconds': duration, 'looping': row['looping'],
                                 'sound_class': row['sound_class'], 'loading_behavior': str(loading),
                                 'virtualization_mode': str(sound.get_editor_property('virtualization_mode')),
                                 'source_sha256': row['prepared_sha256']})
    report['success'] = len(report['sounds']) == len(manifest['assets'])
except Exception as exc:
    report['errors'].append(type(exc).__name__ + ': ' + str(exc))
    raise
finally:
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    u.log('VERDANT_RUNTIME_AUDIO_IMPORT ' + json.dumps(
        {'success': report['success'], 'sounds': len(report['sounds']), 'errors': report['errors']}))
