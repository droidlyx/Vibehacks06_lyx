"""Copy the selected Titan creature packages and their available /Game references.

Default is a read-only plan. --apply preserves original package paths, refuses
conflicting destination files, verifies SHA-256, and writes a provenance manifest.
The string scan intentionally over-approximates dependencies; engine validation
is still required, especially for soft references missing in the source project.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
from datetime import datetime, timezone
from pathlib import Path

CAT = "Characters/NPCs/Clifftop/Creatures/Jellybean_Cat"
PENGUIN = "Characters/NPCs/Arctic/Penguin"
FERRET = "Characters/NPCs/Desert/Creatures/Desert_Ferret"
KELPIE_ANIMS = "Characters/NPCs/MarshlandType/Kelpie/Animations"
SEEDS = [
    f"{CAT}/SKM_Jellybean",
    f"{CAT}/Animations/Jellybean_Idle",
    f"{CAT}/Animations/Jellybean_Walk",
    *[f"{CAT}/Materials/Variants/MI_Jellybean_{color}" for color in ("Green", "Blue", "Yellow")],
    f"{PENGUIN}/Rigged/Small/T-Pose_weight_color",
    *[f"{PENGUIN}/Rigged/Small/{name}" for name in ("Idle", "AS_Penguin_Small_Walk", "Happy_Idle", "Jumping")],
    f"{FERRET}/SKM_DesertFerrret",
    *[f"{KELPIE_ANIMS}/AS_Kelpie_{name}" for name in ("Idle", "Walk", "Run", "Sleep")],
]
PACKAGE_RE = re.compile(rb"/Game/[A-Za-z0-9_\-/]+")
SCRIPT_RE = re.compile(rb"/Script/[A-Za-z0-9_]+")
SIDECARS = (".uasset", ".uexp", ".ubulk", ".uptnl")


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def confined(root: Path, relative: str) -> Path:
    target = (root / relative).resolve()
    target.relative_to(root)
    return target


def collect(source: Path) -> tuple[list[dict], dict[str, list[str]], list[str]]:
    pending = list(SEEDS)
    visited: set[str] = set()
    entries: list[dict] = []
    missing: dict[str, list[str]] = {}
    referrers: dict[str, set[str]] = {}
    scripts: set[str] = set()
    while pending:
        package = pending.pop()
        if package in visited:
            continue
        visited.add(package)
        path = confined(source, package + ".uasset")
        if not path.is_file():
            if package in SEEDS:
                raise FileNotFoundError(f"Selected package is absent: {path}")
            missing[package] = []
            continue
        data = path.read_bytes()
        scripts.update(match.decode("ascii") for match in SCRIPT_RE.findall(data))
        references = sorted({match.decode("ascii")[6:] for match in PACKAGE_RE.findall(data)})
        for reference in references:
            if reference == package:
                continue
            referrers.setdefault(reference, set()).add(package)
            if reference not in visited:
                pending.append(reference)
        for extension in SIDECARS:
            item = confined(source, package + extension)
            if item.is_file():
                entries.append({"path": package + extension, "bytes": item.stat().st_size, "sha256": digest(item)})
    for name in missing:
        missing[name] = sorted(referrers.get(name, set()))
    return sorted(entries, key=lambda entry: entry["path"]), missing, sorted(scripts)


def extract_preview(source: Path, relative: str, destination: Path) -> bool:
    try:
        from PIL import Image
    except ImportError:
        return False
    data = confined(source, relative + ".uasset").read_bytes()
    offset = data.find(b"\xff\xd8\xff")
    end = data.find(b"\xff\xd9", offset)
    if offset < 0 or end < 0:
        return False
    try:
        thumbnail = Image.open(io.BytesIO(data[offset: end + 2]))
        thumbnail.load()
    except (OSError, ValueError):
        return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    thumbnail.convert("RGB").save(destination, quality=95)
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-content", type=Path, default=Path("E:/UE_projects/ProjectTitan/Content"))
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--max-mib", type=float, default=500)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    source = args.source_content.resolve(strict=True)
    project = args.project_root.resolve(strict=True)
    destination = (project / "Content").resolve(strict=True)
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("Source and destination must be separate content roots")
    entries, missing, scripts = collect(source)
    total = sum(entry["bytes"] for entry in entries)
    if total > args.max_mib * 1024 * 1024:
        raise ValueError(f"Dependency closure exceeds the {args.max_mib} MiB limit")
    conflicts = []
    for entry in entries:
        target = confined(destination, entry["path"])
        if target.exists() and (not target.is_file() or digest(target) != entry["sha256"]):
            conflicts.append(entry["path"])
    if conflicts:
        raise RuntimeError("Refusing conflicting destination files: " + ", ".join(conflicts))
    summary = {"apply": args.apply, "file_count": len(entries), "mib": round(total / 1024 / 1024, 2),
               "missing_source_references": missing, "script_modules": scripts}
    print(json.dumps(summary, indent=2))
    if not args.apply:
        return
    copied = 0
    for entry in entries:
        origin = confined(source, entry["path"])
        target = confined(destination, entry["path"])
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            # Exclusive creation: never silently replace assets created after preflight.
            with origin.open("rb") as reader, target.open("xb") as writer:
                for block in iter(lambda: reader.read(1024 * 1024), b""):
                    writer.write(block)
            copied += 1
        if digest(target) != entry["sha256"]:
            raise RuntimeError(f"Destination hash mismatch: {target}")
    archive = project / "Assets/Titan"
    archive.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema_version": 1,
        "copied_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_project": str(source.parent),
        "source": "Local Project Titan sample; selected by user for this Unreal Engine project",
        "rights": "Retain applicable Epic/Project Titan terms; these assets are not represented as CC0. No asset upload to external generation services.",
        "method": "Conservative /Game string-reference closure; original package paths; source unchanged; SHA-256 verified",
        "seeds": ["/Game/" + seed for seed in SEEDS],
        "files": entries,
        "missing_source_references": missing,
        "missing_reference_note": "Absent in the source before copying. May be editor preview/retarget references or directory strings. Validate load behavior in Unreal; the scanner does not distinguish hard/soft references.",
        "script_modules": scripts,
        "validation": "File integrity only. Mesh/skeleton compatibility, animation playback and material appearance require Unreal inspection.",
    }
    (archive / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    for name, relative in (("Jellybean", f"{CAT}/SKM_Jellybean"),
                           ("Penguin_small", f"{PENGUIN}/Rigged/Small/T-Pose_weight_color"),
                           ("DesertFerret", f"{FERRET}/SKM_DesertFerrret")):
        extract_preview(source, relative, archive / "Previews" / f"{name}.jpg")
    print(
        f"Copied {copied} new files; all {len(entries)} destination hashes verified. Manifest: {archive / 'manifest.json'}")


if __name__ == "__main__":
    main()
