"""Generate Verdant's six original short SFX without samples, libraries, or APIs.

python Tools/generate_sfx.py          # write PCM WAVs and a measured manifest
python Tools/generate_sfx.py --verify # compare stored bytes with deterministic synthesis

Waveform checks are technical checks, not listening approval or a runtime mix test.
"""
from pathlib import Path
import hashlib
import io
import json
import math
import random
import struct
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Assets/Audio'
RATE = 48000
TAU = 2.0 * math.pi
SPECS = (
    {'name': 'AFX_Swing', 'duration': .16, 'peak_target': .20, 'fade_in': .008, 'fade_out': .025,
     'seed': 603106, 'description': 'Soft band-limited air swish'},
    {'name': 'AFX_Hit', 'duration': .16, 'peak_target': .23, 'fade_in': .003, 'fade_out': .025,
     'seed': 603107, 'description': 'Low damped impact with a soft noisy transient'},
    {'name': 'AFX_Pickup', 'duration': .22, 'peak_target': .20, 'fade_in': .003, 'fade_out': .025,
     'seed': None, 'description': 'Two short clear harmonic chimes'},
    {'name': 'AFX_Grow', 'duration': .50, 'peak_target': .19, 'fade_in': .015, 'fade_out': .065,
     'seed': None, 'description': 'Warm rising fundamental and fifth'},
    {'name': 'AFX_Offer', 'duration': .35, 'peak_target': .20, 'fade_in': .008, 'fade_out': .045,
     'seed': None, 'description': 'Brief warm D-major offering chord'},
    {'name': 'AFX_Restore', 'duration': 1.0, 'peak_target': .23, 'fade_in': .012, 'fade_out': .100,
     'seed': None, 'description': 'Bright ascending D-major completion chime'},
)


def ramp(value):
    value = max(0.0, min(1.0, value))
    return .5 - .5 * math.cos(math.pi * value)


def chime(t, onset, frequency, decay, strength=1.0):
    age = t - onset
    if age < 0.0:
        return 0.0
    attack = ramp(age / .004)
    return strength * attack * math.exp(-age / decay) * (
        math.sin(TAU * frequency * age)
        + .18 * math.sin(TAU * frequency * 2.0 * age)
        + .045 * math.sin(TAU * frequency * 3.0 * age))


def synthesize(spec):
    count = round(spec['duration'] * RATE)
    rng = random.Random(spec['seed']) if spec['seed'] is not None else None
    low_noise = 0.0
    slow_noise = 0.0
    samples = []
    name = spec['name']
    for index in range(count):
        t = index / RATE
        progress = index / (count - 1)
        if name == 'AFX_Swing':
            # Two one-pole filters make a moving, restrained wind band.
            cutoff = 550.0 + 2100.0 * math.sin(math.pi * progress) ** 2
            alpha = 1.0 - math.exp(-TAU * cutoff / RATE)
            low_noise += alpha * (rng.uniform(-1.0, 1.0) - low_noise)
            slow_noise += (1.0 - math.exp(-TAU * 180.0 / RATE)) * (low_noise - slow_noise)
            value = (low_noise - slow_noise) * math.sin(math.pi * progress) ** 1.4
        elif name == 'AFX_Hit':
            low_noise += .16 * (rng.uniform(-1.0, 1.0) - low_noise)
            # Integrate a falling 145 -> 75 Hz pitch instead of stepping frequency.
            phase = TAU * (145.0 * t - 35.0 * t * t / spec['duration'])
            value = (.90 * math.sin(phase) * math.exp(-t / .042)
                     + .18 * math.sin(TAU * 310.0 * t) * math.exp(-t / .022)
                     + .22 * low_noise * math.exp(-t / .012))
        elif name == 'AFX_Pickup':
            value = chime(t, 0.0, 587.329536, .060) + chime(t, .052, 880.0, .065, .72)
        elif name == 'AFX_Grow':
            start, end = 146.832384, 293.664768
            phase = TAU * (start * t + .5 * (end - start) * t * t / spec['duration'])
            value = (math.sin(phase) + .34 * math.sin(phase * 1.5)
                     + .16 * math.sin(phase * 2.0)) * math.sin(math.pi * progress) ** .75
        elif name == 'AFX_Offer':
            value = sum(chime(t, 0.0, frequency, .145, amplitude)
                        for frequency, amplitude in [(293.664768, 1.0), (369.994423, .72), (440.0, .64)])
        elif name == 'AFX_Restore':
            notes = [(0.0, 293.664768, 1.0), (.075, 440.0, .72), (.15, 587.329536, .80),
                     (.225, 739.988845, .55), (.30, 880.0, .58)]
            value = sum(chime(t, onset, frequency, .31, amplitude)
                        for onset, frequency, amplitude in notes)
        else:
            raise ValueError(name)
        samples.append(value)

    # Remove synthesis DC before applying the final boundary envelope.
    mean = math.fsum(samples) / count
    for index in range(count):
        attack = ramp(index / RATE / spec['fade_in'])
        release = ramp((count - 1 - index) / RATE / spec['fade_out'])
        samples[index] = (samples[index] - mean) * attack * release
    peak = max(abs(value) for value in samples)
    assert peak > 0.0 and all(math.isfinite(value) for value in samples)
    gain = spec['peak_target'] / peak
    pcm = [round(value * gain * 32767.0) for value in samples]
    assert pcm[0] == pcm[-1] == 0
    assert max(abs(value) for value in pcm) <= 8192
    stream = io.BytesIO()
    with wave.open(stream, 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(struct.pack('<' + 'h' * len(pcm), *pcm))
    return stream.getvalue()


def measure(data, spec):
    with wave.open(io.BytesIO(data), 'rb') as sound:
        channels, width, rate, frames = sound.getnchannels(), sound.getsampwidth(), sound.getframerate(), sound.getnframes()
        compression = sound.getcomptype()
        raw = sound.readframes(frames)
    pcm = struct.unpack('<' + 'h' * frames, raw)
    peak = max(abs(sample) for sample in pcm) / 32768.0
    rms = math.sqrt(math.fsum((sample / 32768.0) ** 2 for sample in pcm) / frames)
    assert (channels, width, rate, compression) == (1, 2, RATE, 'NONE')
    assert frames == round(spec['duration'] * RATE)
    assert peak <= .25 and all(-32768 < value < 32767 for value in pcm)
    assert pcm[0] == pcm[-1] == 0
    return {'sample_rate_hz': rate, 'channels': channels, 'bits_per_sample': width * 8,
            'frames': frames, 'duration_seconds': frames / rate,
            'peak_amplitude': peak, 'peak_dbfs': 20.0 * math.log10(peak),
            'rms_amplitude': rms, 'rms_dbfs': 20.0 * math.log10(rms),
            'first_sample': pcm[0], 'last_sample': pcm[-1], 'clipped_samples': 0,
            'sha256': hashlib.sha256(data).hexdigest()}


def main():
    verify = '--verify' in sys.argv
    if not verify:
        OUTPUT.mkdir(parents=True, exist_ok=True)
    records = []
    for spec in SPECS:
        data = synthesize(spec)
        path = OUTPUT / (spec['name'] + '.wav')
        if verify:
            assert path.read_bytes() == data, 'Non-reproducible or modified WAV: ' + str(path)
        else:
            path.write_bytes(data)
        measurements = measure(path.read_bytes(), spec)
        records.append({'id': spec['name'], 'file': path.relative_to(ROOT).as_posix(),
                        'ue_path': '/Game/Art/Audio/' + spec['name'], 'description': spec['description'],
                        'source': 'Original deterministic mathematical synthesis in Tools/generate_sfx.py',
                        'external_samples': False, 'api_calls': 0, 'noise_seed': spec['seed'],
                        'fade_in_ms': spec['fade_in'] * 1000, 'fade_out_ms': spec['fade_out'] * 1000,
                        'measured': measurements})
        print(f"{spec['name']}: {measurements['duration_seconds']:.3f}s, "
              f"peak={measurements['peak_amplitude']:.5f}, RMS={measurements['rms_amplitude']:.5f}, "
              f"{measurements['peak_dbfs']:.2f} dBFS, endpoints=0, no clipped samples")
    manifest = {'schema': 1, 'generator': 'Tools/generate_sfx.py', 'algorithm_version': 1,
                'generator_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'assets': records, 'sound_class': {'ue_path': '/Game/Art/Audio/SC_VerdantSFX',
                  'gain_db': -4.0, 'linear_gain': 10.0 ** (-4.0 / 20.0)},
                'technical_validation': 'PCM bytes, format, duration, peak, RMS, endpoints and deterministic regeneration checked.',
                'listening_review': 'Pending real playback on headphones/speakers; waveform validation is not perceptual approval.',
                'runtime_mix_review': 'Pending Unreal import, event playback and overlapping-event balance.'}
    if not verify:
        (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('Deterministic byte verification passed.' if verify else 'Generated 6 WAVs and Assets/Audio/manifest.json.')


if __name__ == '__main__':
    main()
