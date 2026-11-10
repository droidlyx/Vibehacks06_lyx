"""Import the six original Verdant SFX and route them to a shared -4 dB sound class.
Run later in UnrealEditor-Cmd Verdant.uproject -run=pythonscript -script=<this file>.
This script does not modify the main asset manifest or hook gameplay events.
"""
from pathlib import Path
import hashlib
import json
import math
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
assert ROOT == Path(__file__).resolve().parents[1], 'Run this script in the Verdant project.'
AUDIO = ROOT / 'Assets/Audio'
MANIFEST = json.loads((AUDIO / 'manifest.json').read_text(encoding='utf-8'))
DEST = '/Game/Art/Audio'
CLASS_PATH = DEST + '/SC_VerdantSFX'
GAIN = 10.0 ** (-4.0 / 20.0)
report = {'success': False, 'sound_class': CLASS_PATH, 'gain_db': -4.0,
          'linear_gain': GAIN, 'sounds': [], 'errors': [],
          'listening_review': 'Not performed by import; requires actual listening.',
          'runtime_mix_review': 'Not performed by import; requires gameplay playback.'}
assets = u.AssetToolsHelpers.get_asset_tools()

try:
    u.EditorAssetLibrary.make_directory(DEST)
    sound_class = u.load_asset(CLASS_PATH)
    if sound_class is None:
        sound_class = assets.create_asset('SC_VerdantSFX', DEST, u.SoundClass, u.SoundClassFactory())
    if not isinstance(sound_class, u.SoundClass):
        raise RuntimeError('Could not create or load ' + CLASS_PATH)
    properties = sound_class.get_editor_property('properties')
    properties.set_editor_property('volume', GAIN)
    sound_class.set_editor_property('properties', properties)
    actual_gain = float(sound_class.get_editor_property('properties').get_editor_property('volume'))
    if not math.isclose(actual_gain, GAIN, abs_tol=1e-6):
        raise RuntimeError('SoundClass volume was not applied: ' + str(actual_gain))
    if not u.EditorAssetLibrary.save_loaded_asset(sound_class):
        raise RuntimeError('Could not save ' + CLASS_PATH)
    report['verified_class_gain'] = actual_gain

    for row in MANIFEST['assets']:
        source = ROOT / row['file']
        if hashlib.sha256(source.read_bytes()).hexdigest() != row['measured']['sha256']:
            raise RuntimeError('Source WAV differs from measured manifest: ' + row['id'])
        task = u.AssetImportTask()
        task.filename = str(source)
        task.destination_path = DEST
        task.destination_name = row['id']
        task.automated = True
        task.replace_existing = True
        task.replace_existing_settings = True
        task.save = True
        factory = u.SoundFactory()
        factory.set_editor_property('auto_create_cue', False)
        task.factory = factory
        assets.import_asset_tasks([task])
        sound = u.load_asset(row['ue_path'])
        if not isinstance(sound, u.SoundWave):
            raise RuntimeError('Expected SoundWave at ' + row['ue_path'] + '; imported=' + str(task.imported_object_paths))
        sound.set_editor_property('sound_class_object', sound_class)
        sound.set_editor_property('looping', False)
        sound.set_editor_property('volume', 1.0)
        sound.set_editor_property('pitch', 1.0)
        duration = float(sound.get_editor_property('duration'))
        channels = int(sound.get_editor_property('num_channels'))
        imported_rate = int(sound.get_editor_property('imported_sample_rate'))
        if channels != 1 or imported_rate != 48000:
            raise RuntimeError('Unexpected imported audio format: ' + row['id'])
        if not math.isclose(duration, row['measured']['duration_seconds'], abs_tol=.002):
            raise RuntimeError('Unexpected imported duration: ' + row['id'])
        actual_class = sound.get_editor_property('sound_class_object')
        if actual_class != sound_class:
            raise RuntimeError('SoundClass assignment failed: ' + row['id'])
        if not u.EditorAssetLibrary.save_loaded_asset(sound):
            raise RuntimeError('Could not save ' + row['ue_path'])
        report['sounds'].append({'path': row['ue_path'], 'sound_class': actual_class.get_path_name(),
                                 'duration_seconds': duration, 'channels': channels,
                                 'imported_sample_rate_hz': imported_rate,
                                 'source_sha256': row['measured']['sha256']})
    report['success'] = len(report['sounds']) == 6
except Exception as exc:
    report['errors'].append(type(exc).__name__ + ': ' + str(exc))
    raise
finally:
    (AUDIO / 'unreal_import_report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    u.log('VERDANT_SFX_IMPORT ' + json.dumps(report))
