"""Migrate the six-package Titan soft-cloud family without copying its plugins.

Default: inspect sources, dependencies, hashes, and destination conflicts only.
--apply: copy missing files at original /Game paths and write provenance.
Existing files with differing hashes are never overwritten. Source is read-only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path

SKY = "Environment/_Global/Materials/Sky"
PACKAGES = tuple(f"{SKY}/{name}" for name in (
    "M_Clouds_Soft", "M_Clouds", "T_Clouds_Profile", "T_StylizedClouds_Weather",
    "T_StylizedClouds_Voronoi", "VT_StylizedClouds_Voronoi",
))
SIDECARS = (".uasset", ".uexp", ".ubulk", ".uptnl")
PATH_RE = re.compile(rb"/(?:Game|Engine|Script|Volumetrics|DayNightCycle)/[A-Za-z0-9_\-/]+")
STRINGS_RE = re.compile(rb"[A-Za-z][A-Za-z0-9_ ]{3,100}")


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


def inspect(source: Path) -> tuple[list[dict], dict[str, list[str]], dict[str, list[str]]]:
    entries, references, material_strings = [], {}, {}
    allowed = {"/Game/" + name for name in PACKAGES}
    for package in PACKAGES:
        primary = confined(source, package + ".uasset")
        if not primary.is_file():
            raise FileNotFoundError(primary)
        data = primary.read_bytes()
        refs = sorted({match.decode("ascii") for match in PATH_RE.findall(data)})
        references["/Game/" + package] = refs
        unexpected = [ref for ref in refs if ref.startswith("/Game/") and ref not in allowed]
        plugin_refs = [ref for ref in refs if not ref.startswith(("/Game/", "/Engine/", "/Script/"))]
        if unexpected or plugin_refs:
            raise RuntimeError(
                f"Dependency outside the reviewed six-package set in {package}: {unexpected + plugin_refs}")
        if primary.stem in ("M_Clouds", "M_Clouds_Soft"):
            words = sorted({match.decode("ascii") for match in STRINGS_RE.findall(data)})
            material_strings["/Game/" + package] = [word for word in words if any(
                token in word.lower() for token in
                ("cloud", "density", "erosion", "height", "speed", "thickness", "albedo", "extinction", "scattering"))]
        for extension in SIDECARS:
            path = confined(source, package + extension)
            if path.is_file():
                entries.append({"path": package + extension, "bytes": path.stat().st_size, "sha256": digest(path)})
    return sorted(entries, key=lambda row: row["path"]), references, material_strings


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-content", type=Path, default=Path("E:/UE_projects/ProjectTitan/Content"))
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    source = args.source_content.resolve(strict=True)
    project = args.project_root.resolve(strict=True)
    destination = (project / "Content").resolve(strict=True)
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("Source and destination must be separate content roots")
    entries, references, material_strings = inspect(source)
    total = sum(row["bytes"] for row in entries)
    if total > 32 * 1024 * 1024:
        raise ValueError("Reviewed six-package sky family unexpectedly exceeds 32 MiB")
    conflicts = []
    for row in entries:
        target = confined(destination, row["path"])
        if target.exists() and (not target.is_file() or digest(target) != row["sha256"]):
            conflicts.append(row["path"])
    if conflicts:
        raise RuntimeError("Refusing different existing files: " + ", ".join(conflicts))
    print(json.dumps({"apply": args.apply, "packages": len(PACKAGES), "files": len(entries),
                      "mib": round(total / 1024 / 1024, 2), "conflicts": conflicts}, indent=2))
    if not args.apply:
        return
    copied = 0
    for row in entries:
        origin = confined(source, row["path"])
        target = confined(destination, row["path"])
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            with origin.open("rb") as reader, target.open("xb") as writer:
                for block in iter(lambda: reader.read(1024 * 1024), b""):
                    writer.write(block)
            copied += 1
        if digest(target) != row["sha256"]:
            raise RuntimeError(f"Destination hash mismatch: {target}")
    archive = project / "Assets/TitanSky"
    archive.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema_version": 1,
        "copied_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_project": str(source.parent),
        "purpose": "Selected Titan soft stylized volumetric clouds for Verdant's woodland sky trial",
        "rights": "Applicable Epic / Project Titan terms retained; not CC0. No external service upload.",
        "method": "Explicit reviewed six-package set, original /Game paths, source unchanged, SHA-256 verified",
        "entry_material": "/Game/" + PACKAGES[0],
        "files": entries,
        "raw_package_references": references,
        "material_parameter_name_candidates": material_strings,
        "parameter_note": "Raw package strings are discovery hints, not confirmed editable parameters. Inspect through Unreal MaterialEditingLibrary before setting values.",
        "validation": "File integrity and bounded source dependency scan only. Shader compilation, cloud appearance and performance await editor/user inspection.",
    }
    (archive / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        f"Copied {copied} new files; verified {len(entries)} destination hashes. Manifest: {archive / 'manifest.json'}")


if __name__ == "__main__":
    main()
