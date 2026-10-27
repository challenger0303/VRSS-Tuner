import pathlib
import socket
import struct
import subprocess
import sys
import tempfile


def main():
    bridge, mock = map(pathlib.Path, sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="psvr2-bridge-test-") as tmp:
        root = pathlib.Path(tmp)
        geometry = root / "geometry.txt"
        calibration = root / "calibration.txt"
        geometry.write_text(
            "format=1\nvendor=1\nproduct=2\n"
            "projection0=-1,1,-1,1\nprojection1=-1,1,-1,1\n"
            "eyeToHead0=1,0,0,0,1,0,0,0,1\n"
            "eyeToHead1=1,0,0,0,1,0,0,0,1\n",
            encoding="ascii",
        )
        calibration.write_text(
            "x_gain=1\ny_gain=1\nleft_deg=0\nright_deg=0\n", encoding="ascii"
        )
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
            receiver.bind(("127.0.0.1", 0))
            receiver.settimeout(3)
            port = receiver.getsockname()[1]
            exports = subprocess.run(
                [str(bridge), "--capi", str(mock), "--check-exports-only", "1"],
                capture_output=True,
                text=True,
                timeout=3,
            )
            assert exports.returncode == 0 and "PSVR2TK_CAPI_EXPORTS_READY" in exports.stdout, exports
            result = subprocess.run(
                [str(bridge), "--capi", str(mock), "--check-only", "1"],
                capture_output=True,
                text=True,
                timeout=3,
            )
            assert result.returncode == 0 and "PSVR2TK_CAPI_READY" in result.stdout, result
            process = subprocess.Popen(
                [str(bridge), "--capi", str(mock), "--geometry", str(geometry),
                 "--config", str(calibration), "--port", str(port), "--seconds", "1"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            try:
                packet, _ = receiver.recvfrom(256)
                assert len(packet) == 44, len(packet)
                magic, version, sequence, qpc, lx, ly, rx, ry, mask = struct.unpack(
                    "<IIQQffffI", packet
                )
                assert (magic, version, sequence, mask) == (0x3147564E, 1, 1, 3)
                assert qpc > 0
                assert -0.22 < lx < -0.19 and 0.09 < ly < 0.12, (lx, ly)
                assert 0.19 < rx < 0.22 and -0.12 < ry < -0.09, (rx, ry)
            finally:
                out, err = process.communicate(timeout=3)
                assert process.returncode == 0, (out, err)
    print("PSVR2_BRIDGE_MOCK_PASS exports_preflight capi_load binocular_mapping udp_packet")


if __name__ == "__main__":
    main()
