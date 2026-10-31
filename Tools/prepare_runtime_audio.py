"""Prepare accepted audio sources for a targeted Unreal import (no game/editor run).

Requires numpy and the already installed Blender libsndfile DLL. Paid source WAVs
and all derived masters stay under ignored Local/. This script records provenance;
it neither grants a licence nor substitutes measurements for listening approval.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import math
import os
import subprocess
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LIBRARY = Path(r'D:/Workspace/vscode_workspace/EMBER_GENERATIONS_LOCAL_SOURCE_20260829')
RATE = 48000
DEST = '/Game/Art/Audio/Runtime'
LICENSED_DEST = '/Game/Art/Audio/LocalLicensed'
BUS_GAINS = {'Music': -16.0, 'Ambience': -6.0, 'SFX': -4.0, 'UI': -10.0}


class SFInfo(ctypes.Structure):
    _fields_ = [('frames', ctypes.c_longlong), ('samplerate', ctypes.c_int),
                ('channels', ctypes.c_int), ('format', ctypes.c_int),
                ('sections', ctypes.c_int), ('seekable', ctypes.c_int)]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def db(value):
    return round(20.0 * math.log10(float(value)), 5) if value > 0 else None


def load_decoder(dll_dir):
    directory = os.add_dll_directory(str(dll_dir))
    lib = ctypes.CDLL(str(dll_dir / 'sndfile.dll'))
    lib.sf_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(SFInfo)]
    lib.sf_open.restype = ctypes.c_void_p
    lib.sf_readf_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_longlong]
    lib.sf_readf_float.restype = ctypes.c_longlong
    lib.sf_close.argtypes = [ctypes.c_void_p]
    lib.sf_close.restype = ctypes.c_int
    lib.sf_strerror.argtypes = [ctypes.c_void_p]
    lib.sf_strerror.restype = ctypes.c_char_p
    lib.sf_version_string.restype = ctypes.c_char_p
    return lib, directory


def decode(lib, path):
    info = SFInfo()
    handle = lib.sf_open(str(path).encode('utf-8'), 0x10, ctypes.byref(info))
    if not handle:
        raise RuntimeError(f'{path}: {lib.sf_strerror(None).decode("utf-8", errors="replace")}')
    try:
        if info.frames <= 0 or info.channels not in (1, 2) or info.samplerate <= 0:
            raise ValueError(f'Unsupported audio format: {path}')
        samples = np.empty((info.frames, info.channels), dtype=np.float32)
        read = lib.sf_readf_float(handle, samples.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), info.frames)
        if read != info.frames or not np.isfinite(samples).all():
            raise ValueError(f'Incomplete or non-finite decode: {path}')
        return samples.astype(np.float64), int(info.samplerate)
    finally:
        lib.sf_close(handle)


def decode_mp3_with_blender(source, library, output):
    """Use the existing Blender FFmpeg-backed reader; this sndfile build lacks MP3."""
    helper = output / 'decode_music_blender.py'
    decoded = output / 'MosslitPaths.decoded.npy'
    metadata = output / 'MosslitPaths.decode.json'
    helper.write_text('''from pathlib import Path
import sys, json
import aud
import numpy as np
args = sys.argv[sys.argv.index('--') + 1:]
sound = aud.Sound.file(args[0]).cache()
data = sound.data()
np.save(args[1], data)
Path(args[2]).write_text(json.dumps({'sample_rate_hz': int(sound.specs[0]), 'channels': int(sound.specs[1]), 'frames': len(data)}), encoding='utf-8')
''', encoding='utf-8')
    blender = library / '_tools/blender-4.5.13-windows-x64/blender.exe'
    subprocess.run([str(blender), '-b', '--python', str(helper), '--', str(source), str(decoded), str(metadata)],
                   check=True, capture_output=True, text=True)
    info = json.loads(metadata.read_text(encoding='utf-8'))
    samples = np.load(decoded, allow_pickle=False).astype(np.float64)
    if samples.shape != (info['frames'], info['channels']) or not np.isfinite(samples).all():
        raise ValueError('Invalid Blender music decode')
    decoded.unlink()  # This intermediate is wholly owned by this preparation run.
    return samples, info['sample_rate_hz']


def resample(samples, source_rate):
    """64-tap Hann-windowed sinc, chunked to bound memory; no network dependencies."""
    if source_rate == RATE:
        return samples.copy()
    count = round(len(samples) * RATE / source_rate)
    result = np.empty((count, samples.shape[1]), dtype=np.float64)
    radius = 32
    offsets = np.arange(-radius + 1, radius + 1, dtype=np.int64)
    cutoff = min(1.0, RATE / source_rate) * .95
    for start in range(0, count, 8192):
        stop = min(count, start + 8192)
        positions = np.arange(start, stop, dtype=np.float64) * source_rate / RATE
        indices = np.floor(positions).astype(np.int64)[:, None] + offsets
        distances = positions[:, None] - indices
        window = .5 * (1.0 + np.cos(np.pi * distances / radius))
        window[np.abs(distances) >= radius] = 0.0
        weights = cutoff * np.sinc(cutoff * distances) * window
        weights /= np.sum(weights, axis=1, keepdims=True)
        valid = (indices >= 0) & (indices < len(samples))
        safe = np.clip(indices, 0, len(samples) - 1)
        result[start:stop] = np.sum(samples[safe] * (weights * valid)[:, :, None], axis=1)
    return result


def metrics(samples, rate):
    peak = float(np.max(np.abs(samples)))
    rms = float(np.sqrt(np.mean(samples * samples)))
    return {'sample_rate_hz': rate, 'channels': samples.shape[1], 'frames': len(samples),
            'duration_seconds': round(len(samples) / rate, 6), 'peak_amplitude': peak,
            'peak_dbfs': db(peak), 'rms_dbfs': db(rms),
            'samples_at_or_above_full_scale': int(np.count_nonzero(np.abs(samples) >= 1.0)),
            'edge_jump_amplitude': float(np.max(np.abs(samples[-1] - samples[0])))}


def specs(library):
    paid = library / 'vendor-paid-assets/tommusic/fantasy-sfx-and-music-bundle/WAV SFX/WAV SFX_'
    free = ROOT / 'Assets/Audio/ThirdParty/Kenney/RPG'
    result = [{'id': 'MUS_MosslitPaths', 'source': ROOT / 'Assets/Audio/Suno/Originals/VA01_Mosslit_Paths.mp3',
               'bus': 'Music', 'channels': 2, 'looping': False, 'edge_fade_seconds': 1.0,
               'license': 'User-supplied Suno download; see generation-session.json for recorded account context',
               'source_url': None, 'destination': DEST, 'peak_ceiling_dbfs': -3.0}]
    result.append({'id': 'AMB_ForestDay', 'source': paid / 'BGS Loops/Forest Day/Forest Day.wav',
                   'bus': 'Ambience', 'channels': 2, 'looping': True, 'destination': LICENSED_DEST})
    for surface in ('Dirt', 'Stone'):
        for index in range(1, 4):
            result.append({'id': f'SFX_{surface}Step_{index:02}',
                           'source': paid / f'SFX/Footsteps/{surface}/{surface} Walk {index}.wav',
                           'bus': 'SFX', 'channels': 1, 'looping': False, 'destination': LICENSED_DEST,
                           'peak_ceiling_dbfs': -12.0, 'match_footstep_peak': True})
    for name, filename in [('UI_Tick', 'System tick 1.wav'), ('UI_Select', 'System Select 1.wav')]:
        result.append({'id': name, 'source': paid / 'SFX/Inventory and System Sounds' / filename,
                       'bus': 'UI', 'channels': 1, 'looping': False, 'destination': LICENSED_DEST})
    for index in range(1, 5):
        result.append({'id': f'SFX_Cloth_{index:02}', 'source': free / f'Audio/cloth{index}.ogg',
                       'bus': 'SFX', 'channels': 1, 'looping': False, 'destination': DEST})
    for name, filename, bus in [('UI_JournalOpen', 'bookOpen', 'UI'),
                                ('UI_JournalClose', 'bookClose', 'UI'),
                                ('SFX_LeatherPick', 'handleSmallLeather', 'SFX')]:
        result.append({'id': name, 'source': free / f'Audio/{filename}.ogg',
                       'bus': bus, 'channels': 1, 'looping': False, 'destination': DEST})
    for row in result:
        if row['source'].is_relative_to(paid):
            row.update({'license': 'TomMusic royalty-free project use; no standalone asset redistribution',
                        'license_path': str(paid / 'ReadMe.rtf'), 'source_url': 'https://tommusic.itch.io/',
                        'source_relative_to_library': row['source'].relative_to(library).as_posix()})
        elif row['source'].is_relative_to(free):
            row.update({'license': 'CC0-1.0', 'license_path': str(free / 'License.txt'),
                        'source_url': 'https://kenney.nl/assets/rpg-audio'})
        row.setdefault('edge_fade_seconds', 0.0)
        row.setdefault('peak_ceiling_dbfs', -9.0)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=DEFAULT_LIBRARY)
    parser.add_argument('--decoder-dir', type=Path)
    args = parser.parse_args()
    library = args.library.resolve()
    decoder_dir = args.decoder_dir or library / '_tools/blender-4.5.13-windows-x64/blender.shared'
    output = ROOT / 'Local/AudioRuntime'
    output.mkdir(parents=True, exist_ok=True)
    rows = specs(library)
    for row in rows:
        if not row['source'].is_file():
            raise FileNotFoundError(row['source'])
    lib, directory = load_decoder(decoder_dir)
    report = {'schema_version': 1, 'prepared_on': '2026-10-31',
              'approval': {'user_statement': '我试过了，基本符合描述，可以直接按照描述接入游戏',
                           'meaning': 'User accepted the audition descriptions and authorized integration; not a claim that the preparation script listened.',
                           'runtime_mix_review': 'User will perform gameplay listening; no automated gameplay test requested.'},
              'decoder': lib.sf_version_string().decode() + ' for WAV/OGG; installed Blender Audaspace/FFmpeg for MP3',
              'resampler': '64-tap Hann-windowed sinc, 0.95 Nyquist cutoff, DC-normalized, 8192-frame chunks',
              'sound_classes': {name: {'path': f'{DEST}/SC_Verdant{name}', 'gain_db': gain,
                                       'linear_gain': 10.0 ** (gain / 20.0)} for name, gain in BUS_GAINS.items()},
              'assets': [],
              'source_distribution_note': 'TomMusic original and derived source files remain in Local or the existing external library. LocalLicensed imported assets must not be committed to a public source repository.'}
    try:
        for row in rows:
            original, source_rate = (decode_mp3_with_blender(row['source'], library, output)
                                     if row['source'].suffix.lower() == '.mp3' else decode(lib, row['source']))
            samples = original.mean(axis=1, keepdims=True) if row['channels'] == 1 else original.copy()
            if samples.shape[1] == 1 and row['channels'] == 2:
                samples = np.repeat(samples, 2, axis=1)
            samples = resample(samples, source_rate)
            fade = min(round(row['edge_fade_seconds'] * RATE), len(samples) // 2)
            if fade:
                ramp = np.sin(np.linspace(0, np.pi / 2, fade)) ** 2
                samples[:fade] *= ramp[:, None]
                samples[-fade:] *= ramp[::-1, None]
            ceiling = math.floor(10.0 ** (row['peak_ceiling_dbfs'] / 20.0) * 32768) / 32768
            peak = float(np.max(np.abs(samples)))
            match_step = row.get('match_footstep_peak', False)
            if match_step and peak < .001:
                raise RuntimeError('Refusing to amplify near-silent footstep: ' + row['id'])
            gain = (ceiling / peak if match_step else min(1.0, ceiling / peak)) if peak else 1.0
            samples *= gain
            pcm = np.rint(samples * 32768).astype('<i2')
            prepared = output / f'{row["id"]}.wav'
            with wave.open(str(prepared), 'wb') as writer:
                writer.setnchannels(row['channels'])
                writer.setsampwidth(2)
                writer.setframerate(RATE)
                writer.writeframes(pcm.tobytes())
            final = pcm.astype(np.float64) / 32768
            measured = metrics(final, RATE)
            if measured['samples_at_or_above_full_scale'] or measured['peak_amplitude'] > ceiling + 1e-8:
                raise RuntimeError(f'Unexpected output level: {row["id"]}')
            entry = {key: value for key, value in row.items() if key != 'source'}
            entry.update({'source_path': str(row['source']), 'source_sha256': sha256(row['source']),
                          'source_metrics': metrics(original, source_rate),
                          'prepared_file': prepared.relative_to(ROOT).as_posix(),
                          'prepared_sha256': sha256(prepared), 'measured': measured,
                          'applied_gain_db': db(gain), 'upward_normalization': gain > 1.0,
                          'ue_path': f'{row["destination"]}/{row["id"]}',
                          'sound_class': report['sound_classes'][row['bus']]['path'],
                          'loading_behavior': 'PRIME_ON_LOAD' if row['bus'] in ('Music',
                                                                                'Ambience') else 'RETAIN_ON_LOAD'})
            if row['id'] == 'MUS_MosslitPaths':
                entry[
                    'loop_note'] = 'Full song with one-second edge fades, not a seamless loop; runtime restarts the non-looping clip with fades.'
            report['assets'].append(entry)
            print(
                f'{row["id"]}: {measured["duration_seconds"]:.3f}s / {measured["channels"]}ch / peak {measured["peak_dbfs"]} dBFS')
    finally:
        directory.close()
    manifest = ROOT / 'Assets/Audio/runtime-audio-manifest.json'
    manifest.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Prepared {len(report["assets"])} WAV files; no Unreal import or game run performed.')


if __name__ == '__main__':
    main()
