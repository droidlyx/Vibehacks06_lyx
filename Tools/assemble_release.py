from pathlib import Path
import hashlib
import zipfile

folder = Path("Downloads")
target = folder / "Grow_up_final-Windows.zip"
expected = (folder / "SHA256SUMS.txt").read_text().split()[0]
whole = hashlib.sha256()
entries = (folder / "parts.sha256").read_text().splitlines()
assert len(entries) == 17, "Unexpected number of parts"
with target.open("wb") as out:
    for index, line in enumerate(entries, 1):
        digest, name = line.split()
        assert name == f"Grow_up_final-Windows.zip.{index:03d}", "Unexpected part name/order"
        part_hash = hashlib.sha256()
        with (folder / name).open("rb") as source:
            while chunk := source.read(1024 * 1024):
                part_hash.update(chunk)
                whole.update(chunk)
                out.write(chunk)
        assert part_hash.hexdigest() == digest, f"Invalid or missing LFS content: {name}"
assert whole.hexdigest() == expected, "Complete archive checksum mismatch"
with zipfile.ZipFile(target) as archive:
    for name in archive.namelist():
        components = [part.lower() for part in name.split("/")]
        assert "local" not in components and "saved" not in components, "Private/runtime data in archive"
        assert not name.lower().endswith((".key", ".pem")), "Credential file in archive"
print(f"Verified {target.name}: {target.stat().st_size} bytes, SHA256 {expected}")
