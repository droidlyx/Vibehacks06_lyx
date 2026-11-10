"""Bake four modest in-place loops from each imported creature mesh's own bind pose.

Run inside Unreal Editor. Never changes Titan skeletons or source animations.
The walk uses a small analytic two-bone diagonal gait; it is a prototype, not mocap.
"""
import json
import math
from pathlib import Path
import unreal as u

DEST = '/Game/Art/Creatures/Animations'
MESHES = {
    'Jellybean': '/Game/Characters/NPCs/Clifftop/Creatures/Jellybean_Cat/SKM_Jellybean',
    'Ferret': '/Game/Characters/NPCs/Desert/Creatures/Desert_Ferret/SKM_DesertFerrret',
}


def v(value):
    return (value.x, value.y, value.z)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def mul(a, s):
    return tuple(x * s for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def norm(a):
    return math.sqrt(dot(a, a))


def unit(a):
    return mul(a, 1 / max(1e-9, norm(a)))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def between(a, b):
    a, b = unit(a), unit(b)
    xyz = cross(a, b)
    w = 1 + dot(a, b)
    length = math.sqrt(dot(xyz, xyz) + w * w)
    if length < 1e-8:
        raise ValueError('Unexpected inverted leg in small gait')
    return u.Quat(*(x / length for x in xyz), w / length)


def bind_pose(mesh):
    comp = u.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    local, parents, world = {}, {}, {}
    for index in range(comp.get_num_bones()):
        name = str(comp.get_bone_name(index))
        parent = str(comp.get_parent_bone(name))
        local[name] = comp.get_ref_pose_transform(index)
        parents[name] = parent
        world[name] = local[name].multiply(world[parent]) if parent in world else local[name]
    return local, parents, world


def gait_rotations(local, parents, world, phase):
    rotations = {}
    for upper, lower, foot, shift in [
        ('front_thigh_l', 'front_shin_l', 'front_foot_l', 0),
        ('front_thigh_r', 'front_shin_r', 'front_foot_r', math.pi),
        ('thigh_l', 'shin_l', 'foot_l', math.pi),
        ('thigh_r', 'shin_r', 'foot_r', 0),
    ]:
        hip, knee, rest = (v(world[n].translation) for n in (upper, lower, foot))
        a, b = norm(sub(knee, hip)), norm(sub(rest, knee))
        cycle = phase + shift
        stride, lift = (a + b) * .10, (a + b) * .055
        target = add(rest, (0, math.sin(cycle) * stride, max(0, math.cos(cycle)) * lift))
        axis = unit(sub(target, hip))
        distance = min(norm(sub(target, hip)), (a + b) * .998)
        target = add(hip, mul(axis, distance))
        bend = unit(sub(sub(knee, hip), mul(axis, dot(sub(knee, hip), axis))))
        along = (a * a - b * b + distance * distance) / (2 * distance)
        height = math.sqrt(max(0, a * a - along * along))
        next_knee = add(hip, add(mul(axis, along), mul(bend, height)))
        upper_q = between(sub(knee, hip), sub(next_knee, hip)).multiply(world[upper].rotation)
        lower_q = between(sub(rest, knee), sub(target, next_knee)).multiply(world[lower].rotation)
        rotations[upper] = world[parents[upper]].rotation.inversed().multiply(upper_q)
        rotations[lower] = upper_q.inversed().multiply(lower_q)
        rotations[foot] = lower_q.inversed().multiply(world[foot].rotation)
    return rotations


def bake(label, mesh, mode):
    local, parents, world = bind_pose(mesh)
    name = f'A_{label}_{mode}'
    path = DEST + '/' + name
    skeleton = mesh.get_editor_property('skeleton')
    sequence = u.load_asset(path)
    if not sequence:
        factory = u.AnimSequenceFactory()
        factory.target_skeleton = skeleton
        factory.preview_skeletal_mesh = mesh
        factory.edit_after_new = False
        sequence = u.AssetToolsHelpers.get_asset_tools().create_asset(name, DEST, u.AnimSequence, factory)
    if sequence.get_skeleton() != skeleton:
        raise RuntimeError('Destination skeleton differs: ' + path)
    sequence.set_editor_property('enable_root_motion', False)
    sequence.set_editor_property('force_root_lock', True)
    frames = 60 if mode == 'Idle' else 30
    rotations = {bone: [] for bone in local}
    for index in range(frames + 1):
        phase = 2 * math.pi * index / frames
        animated = gait_rotations(local, parents, world, phase) if mode == 'Walk' else {}
        # Spine/pelvis stay at bind pose: no foot sliding caused by body bob.
        for bone in ('head', 'neck_02', 'tail_01', 'tail_02'):
            if bone in local:
                amplitude = 1.3 if bone.startswith(('head', 'neck')) else 3.5
                delta = u.Rotator(0, amplitude * math.sin(phase), 0).quaternion()
                animated[bone] = local[bone].rotation.multiply(delta)
        for bone, transform in local.items():
            rotations[bone].append(animated.get(bone, transform.rotation))
    controller = sequence.controller
    controller.open_bracket('Bake mesh-specific creature loop', False)
    try:
        controller.remove_all_bone_tracks(False)
        controller.set_frame_rate(u.FrameRate(30, 1), False)
        controller.set_number_of_frames(u.FrameNumber(frames), False)
        for bone, transform in local.items():
            if not controller.add_bone_curve(bone, False):
                raise RuntimeError('Cannot add bone ' + bone)
            if not controller.set_bone_track_keys(bone, [transform.translation] * (frames + 1), rotations[bone],
                                                  [transform.scale3d] * (frames + 1), False):
                raise RuntimeError('Cannot write bone ' + bone)
    finally:
        controller.close_bracket(False)
    if not u.EditorAssetLibrary.save_loaded_asset(sequence, only_if_is_dirty=False):
        raise RuntimeError('Could not save animation (finish PIE first): ' + path)
    model = sequence.controller.get_model_interface()
    assert model.get_num_bone_tracks() == len(local)
    assert abs(sequence.get_play_length() - frames / 30) < .001
    return {'asset': path, 'mesh': mesh.get_path_name(), 'skeleton': skeleton.get_path_name(),
            'seconds': sequence.get_play_length(), 'bones': model.get_num_bone_tracks(),
            'frames': model.get_number_of_frames(), 'root_motion': False,
            'reference': 'mesh local bind pose', 'visual_review': 'pending user gameplay review'}


def main():
    report = []
    for label, path in MESHES.items():
        mesh = u.load_asset(path)
        if not mesh:
            raise RuntimeError('Missing imported mesh: ' + path)
        for mode in ('Idle', 'Walk'):
            report.append(bake(label, mesh, mode))
    output = Path(u.Paths.project_dir()) / 'Assets' / 'Titan' / 'motion-report.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('VERDANT_TITAN_MOTION ' + json.dumps(report))


if __name__ == '__main__':
    main()
