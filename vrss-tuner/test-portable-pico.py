"""Offline ZIP checks and loopback-only exercise of the packaged AOT client."""
import argparse
import hashlib
import json
import os
import re
import posixpath
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import zipfile


def packet(sequence, valid=2):
    data = bytearray(83)
    data[:2] = b"E\x01"
    struct.pack_into("<qq", data, 3, 42, sequence)
    struct.pack_into("<I", data, 27, valid)
    struct.pack_into("<fff", data, 55, .2, -.1, -1)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    bundle = args.bundle.resolve()
    manifest = json.loads((bundle / "bundle-manifest.json").read_text(encoding="utf-8-sig"))
    entries = {entry["path"]: entry["sha256"].lower() for entry in manifest["files"]}
    for required in ("pico-gaze-bridge/PicoGazeBridge.exe", "Pico-Source.ps1", "Start-PICO.cmd", "PICO.md"):
        assert required in entries, required
    assert len(entries) == len(manifest["files"])
    with zipfile.ZipFile(bundle.with_suffix(".zip")) as archive:
        names = {info.filename for info in archive.infolist() if not info.is_dir()}
        assert names == {bundle.name + "/" + name for name in entries} | {bundle.name + "/bundle-manifest.json"}
        for name, expected in entries.items():
            source = (bundle / name).read_bytes()
            assert hashlib.sha256(source).hexdigest() == expected, name
            assert archive.read(bundle.name + "/" + name) == source, name
        assert (bundle / "Pico-Source.ps1").read_bytes().startswith(b"\xef\xbb\xbf")
        # Bundle docs are flattened from docs/; validate their packaged targets,
        # not paths that happen to exist only in the source checkout.
        links = 0
        for name in entries:
            if not name.endswith(".md"):
                continue
            text = archive.read(bundle.name + "/" + name).decode("utf-8-sig")
            for target in re.findall(r"\]\(([^\s)]+)\)", text):
                if target.startswith(("https://", "http://", "#", "mailto:")):
                    continue
                path = target.split("#", 1)[0]
                if not path:
                    continue
                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), path))
                assert resolved in entries, f"Broken packaged link: {name} -> {target}"
                if "#" in target and resolved.endswith(".md"):
                    fragment = target.split("#", 1)[1]
                    target_text = archive.read(bundle.name + "/" + resolved).decode("utf-8-sig")
                    anchors = {re.sub(r"\s+", "-", re.sub(r"[^\w\s-]", "", heading.lower()))
                               for heading in re.findall(r"^#+\s+(.+)$", target_text, re.MULTILINE)}
                    assert fragment in anchors, f"Broken packaged heading: {name} -> {target}"
                links += 1
    print(f"PICO_PORTABLE_PASS {len(entries)}_manifest_hashes exact_zip_contents scripts_bom")
    print(f"PICO_PORTABLE_LINKS_PASS {links}_relative_links")

    # All sockets have system-assigned loopback ports. Never send to the real
    # headset, PC9030, or a running provider's UDP5556.
    with tempfile.TemporaryDirectory(prefix="pico-aot-", dir=bundle.parent) as root:
        root = Path(root)
        geometry, calibration, stop = [root / name for name in ("geometry.txt", "calibration.txt", "stop.request")]
        geometry.write_text("projection0=-1,1,-1,1\nprojection1=-1,1,-1,1\neyeToHead0=1,0,0,0,1,0,0,0,1\neyeToHead1=1,0,0,0,1,0,0,0,1\n")
        calibration.write_text("x_gain=1\ny_gain=1\nleft_deg=0\nright_deg=0\n")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as headset, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as output:
            headset.bind(("127.0.0.1", 0))
            output.bind(("127.0.0.1", 0))
            headset.settimeout(5)
            output.settimeout(.3)
            environment = os.environ.copy()
            environment["DOTNET_ROOT"] = str(root / "no-runtime-installed")
            process = subprocess.Popen([str(bundle / "pico-gaze-bridge/PicoGazeBridge.exe"),
                "--headset-ip", "127.0.0.1", "--headset-port", str(headset.getsockname()[1]),
                "--geometry", str(geometry), "--config", str(calibration), "--port", str(output.getsockname()[1]),
                "--seconds", "12", "--stop-file", str(stop)], env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                subscribe, peer = headset.recvfrom(2048)
                assert subscribe.startswith(b"PXR_GAZE_SUB id=") and len(subscribe) == 32
                identity = subscribe[16:]
                assert peer[1] not in (9030, 5556)
                headset.sendto(packet(1), peer)
                try:
                    output.recvfrom(2048)
                    raise AssertionError("Unconfirmed gaze was forwarded")
                except socket.timeout:
                    pass
                headset.sendto(b"PXR_GAZE_ACK id=" + identity + b" epoch=000000000000002a enabled=1", peer)
                headset.sendto(packet(2), peer)
                data, sender = output.recvfrom(2048)
                magic, version, sequence, qpc, lx, ly, rx, ry, mask = struct.unpack("<IIqqffffI", data)
                assert magic == 0x3147564E and version == 1 and sequence == qpc and qpc > 0 and mask == 3
                assert abs(lx - .2) < 1e-6 and abs(rx - .2) < 1e-6 and abs(ly + .1) < 1e-6 and abs(ry + .1) < 1e-6
                assert sender[0] == "127.0.0.1"
                stop.write_text("stop")
                while True:
                    command, source = headset.recvfrom(2048)
                    assert source == peer
                    if command == subscribe:
                        continue
                    assert command == b"PXR_GAZE_STOP id=" + identity
                    break
                stdout, stderr = process.communicate(timeout=5)
                assert process.returncode == 0 and "PICO_GAZE_BRIDGE_STOP sent=1" in stdout, (stdout, stderr)
                print("PICO_AOT_PASS self_contained no_dotnet_root ephemeral_socket confirmed_gaze_44bytes dedicated_stop")
            finally:
                stop.write_text("stop")
                if process.poll() is None:
                    process.communicate(timeout=5)


if __name__ == "__main__":
    main()
