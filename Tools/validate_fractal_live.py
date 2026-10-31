"""Run inside the connected UE editor after starting PIE; exports its actual height grid."""
import json
import math
from pathlib import Path
import struct
import zlib

import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if not world:
    raise RuntimeError("Start PIE before validating the fractal terrain")
terrain_actor = unreal.GameplayStatics.get_actor_of_class(world, unreal.VerdantWorld)
player = unreal.GameplayStatics.get_player_character(world, 0)
ground = terrain_actor.get_component_by_class(unreal.ProceduralMeshComponent)
vertices, triangles, normals, uvs, tangents = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(ground, 0)
row = math.isqrt(len(vertices))
assert row * row == len(vertices), "Terrain must be a square height grid"
heights = [v.z for v in vertices]
assert all(math.isfinite(h) for h in heights), "Non-finite terrain height"
assert len(triangles) == (row - 1) ** 2 * 6, "Unexpected terrain topology"
step = vertices[1].x - vertices[0].x
x0, y0 = vertices[0].x, vertices[0].y
hmin, hmax = min(heights), max(heights)
assert hmax - hmin > 1500, "Terrain relief is still too flat"


def height_at(x, y):
    gx = max(0, min(row - 1.00001, (x - x0) / step))
    gy = max(0, min(row - 1.00001, (y - y0) / step))
    ix, iy = math.floor(gx), math.floor(gy)
    u, v = gx - ix, gy - iy
    a, b = heights[iy * row + ix], heights[iy * row + ix + 1]
    c, d = heights[(iy + 1) * row + ix], heights[(iy + 1) * row + ix + 1]
    return a * (1 - u) + b * (u - v) + d * v if u >= v else a * (1 - v) + d * u + c * (v - u)


# Validate the actual cooked collision against the rendered triangle surface.
trace_checks = []
ignore = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.VerdantCharacter))
for x, y in [(-1000, -5800), (0, 1000), (-4500, -1500), (4200, -1000), (500, 6500),
             (3000, -5500), (-8500, 3500), (7600, 7600), (-12000, -12000)]:
    expected = height_at(x, y)
    hit = unreal.SystemLibrary.line_trace_single(
        world, unreal.Vector(x, y, expected + 20), unreal.Vector(x, y, expected - 60),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignore, unreal.DrawDebugTrace.NONE)
    assert hit, "Missing terrain collision at %s" % ((x, y),)
    details = hit.to_dict()
    assert details["blocking_hit"], "Trace did not block"
    assert details["hit_component"] == ground, "Trace hit scenery instead of terrain"
    impact = details["impact_point"]
    error = abs(impact.z - expected)
    trace_checks.append({"xy_cm": [x, y], "height_error_cm": error})
    assert error < 1.0, "Collision and height grid disagree at %s: %s" % ((x, y), error)

# Along-route and transverse grades matter more than an overall noise amplitude.
routes = [((-1000, -6200), (0, 1000)), ((0, 1000), (-4500, -1500)),
          ((0, 1000), (4200, -1000)), ((0, 1000), (0, 3600)),
          ((0, 3600), (500, 6500))]
route_checks = []
for a, b in routes:
    distance = math.dist(a, b)
    count = math.ceil(distance / 50)
    samples = [(a[0] + (b[0] - a[0]) * i / count, a[1] + (b[1] - a[1]) * i / count) for i in range(count + 1)]
    zs = [height_at(x, y) for x, y in samples]
    max_grade = max(abs(zs[i + 1] - zs[i]) / (distance / count) for i in range(count))
    route_checks.append({"from_cm": a, "to_cm": b, "max_grade": max_grade})
    assert max_grade < math.tan(math.radians(45)), "A main route exceeds a safe walking grade"

# Second differences detect real resolved relief at several scales, not texture detail.
multiscale = []
for span_m in [2, 8, 32, 64]:
    offset = round(span_m * 100 / step)
    differences = []
    for iy in range(40, row - 40, 19):
        for ix in range(offset, row - offset, 23):
            center = iy * row + ix
            differences.append(abs(heights[center - offset] - 2 * heights[center] + heights[center + offset]))
    multiscale.append({"span_m": span_m, "mean_abs_second_difference_cm": sum(differences) / len(differences)})
assert all(m["mean_abs_second_difference_cm"] > .1 for m in multiscale)

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
output = root / "Saved" / "Validation" / "fractal-live"
output.mkdir(parents=True, exist_ok=True)
height_dir = root / "Assets" / "Terrain"
height_dir.mkdir(parents=True, exist_ok=True)


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


# 16-bit lossless grayscale, with exact world bounds and decoding metadata beside it.
scanlines = bytearray()
for y in range(row):
    scanlines.append(0)
    for h in heights[y * row:(y + 1) * row]:
        scanlines.extend(struct.pack(">H", round((h - hmin) / (hmax - hmin) * 65535)))
png = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">IIBBBBB", row, row, 16, 0, 0, 0, 0))
png += png_chunk(b"IDAT", zlib.compress(bytes(scanlines))) + png_chunk(b"IEND", b"")
(height_dir / "fractal-heightmap-v01.png").write_bytes(png)
metadata = {"width": row, "height": row, "grid_spacing_cm": step, "origin_xy_cm": [x0, y0],
            "min_height_cm": hmin, "max_height_cm": hmax, "row_order": "increasing world Y",
            "decode": "z_cm = min_height_cm + pixel_uint16 / 65535 * (max_height_cm-min_height_cm)",
            "source": "Actual live ProceduralMeshComponent section 0 after C++ fractal generation",
            "note": "This export is evidence/reference; gameplay generates the same grid from code, not this PNG."}
(height_dir / "fractal-heightmap-v01.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
report = {"pass": True, "vertices": len(vertices), "triangles": len(triangles) // 3,
          "heightmap": metadata, "collision": trace_checks, "routes": route_checks,
          "multiscale_relief": multiscale, "player_body_size": player.get_editor_property("body_size"),
          "player_movement_mode": str(player.get_component_by_class(unreal.CharacterMovementComponent).movement_mode)}
(output / "terrain-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print("FRACTAL_LIVE_VALIDATION", json.dumps(report))
