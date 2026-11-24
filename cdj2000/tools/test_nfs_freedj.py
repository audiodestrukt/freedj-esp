#!/usr/bin/env python3
"""freedj-3000's own NFS client (its nfs_probe example) against our deck's
NFS server on the emulated CDJ-2000.  QEMU's segment is private, so this
script relays: UDP to 127.0.0.1:<portmap> and 127.0.0.1:2049 goes onto the
segment as frames from a made-up peer to our deck's 111 and 2049, and the
replies (put back together from their IP fragments) come back to the
client.  Skipped when freedj-3000 is not beside this repository.

  python3 tools/test_nfs_freedj.py build/main.bin
"""
import os, select, socket, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_nfs
from test_link import QEMU, OUR_MAC, OUR_IP, Segment, udp_frame, MACHINE, IMAGE
from fatimage import make_image, anlz
from pdbimage import export_pdb

FREEDJ = os.environ.get("FREEDJ_3000", os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../freedj-3000"))


def main(image):
    if not os.path.isdir(os.path.join(FREEDJ, "crates/nfs")):
        print(f"skipped: no freedj-3000 at {FREEDJ}")
        return 0
    probe = subprocess.run(["cargo", "build", "-q", "-p", "opendeck-nfs", "--example", "nfs_probe"], cwd=FREEDJ)
    if probe.returncode: return 1
    exe = os.path.join(FREEDJ, "target/debug/examples/nfs_probe")

    tid, audio, where = test_nfs.TRACKS[0]
    pcm = bytes(range(256)) * 400
    files = {audio: b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, 44100, 176400, 4, 16)
                    + b"data" + struct.pack("<I", len(pcm)) + pcm,
             where: anlz(audio, 120.0),
             "/PIONEER/rekordbox/export.pdb": export_pdb([(tid, audio, where, "Title")])}
    stick = "build/test_nfs_freedj_stick.img"
    make_image(stick, files)
    seg = Segment()
    p = subprocess.Popen([QEMU, "-M", MACHINE, "-kernel", image, "-display", "none", "-monitor", "none",
                          "-serial", "stdio", "-serial", "null",
                          "-drive", f"if=none,id=usbstk,file={stick},format=raw,snapshot=on",
                          "-device", "usb-storage,drive=usbstk,port=1",
                          "-netdev", f"socket,id=djlink,mcast={seg.group}:{seg.port},localaddr=127.0.0.1"],
                         stdout=subprocess.PIPE, stderr=open("build/test_nfs_freedj.qemu.log", "w"), stdin=subprocess.DEVNULL)
    os.set_blocking(p.stdout.fileno(), False)
    out = []
    peer = test_nfs.Peer(seg, p, out)
    # Two local sockets stand for the deck's two ports.
    pm, rpc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM), socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pm.bind(("127.0.0.1", 0)); rpc.bind(("127.0.0.1", 2049))
    clients = {}                     # our peer's source port -> (local socket, client address)
    def on_reply(u):                 # a whole datagram from the deck to the peer
        dport, ulen = struct.unpack("!HH", u[2:6])
        if dport in clients:
            sock, addr = clients[dport]
            sock.sendto(u[8:ulen], addr)
    peer.udp_hook = on_reply
    fails = []
    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""))
        if not ok: fails.append(name)
    try:
        t0 = time.time()
        while time.time() - t0 < 20 and "nfsd: serving" not in "".join(out): peer.pump(0.1)
        cmd = subprocess.Popen([exe, "127.0.0.1", str(pm.getsockname()[1]), audio.lstrip("/")],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        while cmd.poll() is None and time.time() - t0 < 60:
            r, _, _ = select.select([pm, rpc], [], [], 0.01)
            for s, port in ((pm, 111), (rpc, 2049)):
                if s in r:
                    data, addr = s.recvfrom(65536)
                    sport = 900 + (addr[1] % 100) + (0 if port == 111 else 100)
                    clients[sport] = (s, addr)
                    seg.send(udp_frame(OUR_MAC, OUR_IP, sport, port, data))
            peer.pump(0.005)
        text = cmd.communicate(timeout=5)[0] if cmd.poll() is not None else "(timed out)"
        if cmd.poll() is None: cmd.kill()
    finally:
        p.kill(); p.wait()
    print(text)
    check("freedj's client lists our export as \"/C/\" and mounts it", '"/C/"' in text and 'mounted "/C/"' in text)
    check("and lists the stick's top folder", "Contents" in text and "PIONEER" in text)
    check(f"and reads the track whole ({len(files[audio])} bytes)", f"{len(files[audio])} bytes" in text)
    print(f"\n{'all passed' if not fails else str(len(fails)) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else IMAGE))
