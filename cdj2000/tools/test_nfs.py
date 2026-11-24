#!/usr/bin/env python3
"""Our deck's stick over the network, end to end on the emulated CDJ-2000:
a stick with a rekordbox export.pdb (tools/pdbimage.py) and two tracks; the
deck must name the loaded track by its export ID in its status, serve the
stick over NFSv2 (portmap → mount "/C/" → LOOKUP in UTF-16LE → READ in 8 KB
and 32 KB pieces, sent as IP fragments), list a folder, and refuse a TCP
connection to its dbserver port at once.  This script is the peer: an
XDJ-1000MK2's keep-alives, so our deck sends it status, and an NFS client
on raw frames, so nothing leaves QEMU's private segment.

  python3 tools/test_nfs.py build/main.bin      (CDJ_QEMU= another qemu)
"""
import os, random, re, select, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fatimage import make_image, anlz
from pdbimage import export_pdb
from test_link import QEMU, OUR_MAC, OUR_IP, PEER_MAC, PEER_IP, MAGIC, Segment, udp_frame, csum, MACHINE, IMAGE

TRACKS = [   # rekordbox ID, audio, analysis
    (41, "/Contents/Test Artist/A long track name, with spaces.wav", "/PIONEER/USBANLZ/P001/00000001/ANLZ0000.DAT"),
    (77, "/Contents/Other/second.wav", "/PIONEER/USBANLZ/P002/00000002/ANLZ0000.DAT"),
]


def wide(s): return s.encode("utf-16-le")
def xdr_opaque(b): return struct.pack("!I", len(b)) + b + b"\0" * (-len(b) % 4)


class Peer:
    """The XDJ beside our deck, and an RPC client from its address."""
    def __init__(self, seg, proc, out):
        self.seg, self.proc, self.out = seg, proc, out
        self.keepalive, self.statuses, self.frags, self.replies, self.tcp = None, [], {}, {}, []
        self.next_ka, self.xid, self.fragmented, self.udp_hook = 0, random.randint(1, 1 << 30), 0, None

    def pump(self, secs):
        end = time.time() + secs
        while True:
            left = end - time.time()
            if left <= 0: return
            r, _, _ = select.select([self.proc.stdout, self.seg.rx], [], [], min(left, 0.02))
            if self.proc.stdout in r:
                self.out.append((self.proc.stdout.read() or b"").decode(errors="replace"))
            while True:
                try: f = self.seg.rx.recv(4096)
                except BlockingIOError: break
                self.frame(f)
            if self.keepalive and time.time() >= self.next_ka:
                self.next_ka = time.time() + 1.5
                k = bytearray(self.keepalive)
                k[0x0c:0x20] = b"XDJ-1000MK2".ljust(20, b"\0"); k[0x24] = 1
                k[0x26:0x2c] = PEER_MAC; k[0x2c:0x30] = PEER_IP
                self.seg.send(udp_frame(b"\xff" * 6, bytes([169, 254, 255, 255]), 50000, 50000, bytes(k)))

    def frame(self, f):
        if f[6:12] != OUR_MAC: return
        et = struct.unpack("!H", f[12:14])[0]
        if et == 0x0806 and struct.unpack("!H", f[20:22])[0] == 1 and f[38:42] == PEER_IP:
            self.seg.send(f[6:12] + PEER_MAC + b"\x08\x06" + f[14:20] + b"\x00\x02" + PEER_MAC + PEER_IP + f[22:32])
            return
        if et != 0x0800: return
        ip = f[14:]
        ihl, tot = (ip[0] & 15) * 4, struct.unpack("!H", ip[2:4])[0]
        ident, fl = struct.unpack("!HH", ip[4:8])
        data = ip[ihl:tot]
        if ip[9] == 6:
            self.tcp.append(data); return
        if ip[9] != 17: return
        if fl & 0x3fff:                                         # a fragment: put the datagram back together
            parts = self.frags.setdefault(ident, {})
            parts[(fl & 0x1fff) * 8] = (data, bool(fl & 0x2000))
            whole, at = b"", 0
            while at in parts:
                d, more = parts[at]
                whole += d; at += len(d)
                if not more:
                    del self.frags[ident]
                    self.fragmented += 1
                    self.udp(whole); return
            return
        self.udp(data)

    def udp(self, u):
        dport, ulen = struct.unpack("!HH", u[2:6])
        p = u[8:ulen]
        if dport == 50000 and p[:10] == MAGIC and p[0x0a] == 0x06: self.keepalive = p
        elif dport == 50002 and p[:10] == MAGIC and p[0x0a] == 0x0a: self.statuses.append(p)
        elif dport == 800 and len(p) >= 4: self.replies[struct.unpack("!I", p[:4])[0]] = p
        elif self.udp_hook: self.udp_hook(u)            # tools/test_nfs_freedj.py relays the rest

    def call(self, port, prog, vers, proc, args, timeout=3.0):
        self.xid += 1
        msg = struct.pack("!IIIIIIIIII", self.xid, 0, 2, prog, vers, proc, 0, 0, 0, 0) + args
        self.seg.send(udp_frame(OUR_MAC, OUR_IP, 800, port, msg))
        end = time.time() + timeout
        while time.time() < end and self.xid not in self.replies: self.pump(0.02)
        r = self.replies.pop(self.xid, None)
        assert r is not None, f"no reply to prog {prog} proc {proc}"
        assert struct.unpack("!IIII", r[4:20])[0] == 1 and struct.unpack("!I", r[20:24])[0] == 0, "not accepted"
        return r[24:]

    def syn(self, port):
        t = struct.pack("!HHIIBBHHH", 40000, port, 1000, 0, 5 << 4, 0x02, 8192, 0, 0)
        pseudo = PEER_IP + OUR_IP + struct.pack("!BBH", 0, 6, len(t))
        t = t[:16] + struct.pack("!H", csum(pseudo + t)) + t[18:]
        ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(t), 0, 0x4000, 64, 6, 0, PEER_IP, OUR_IP)
        ip = ip[:10] + struct.pack("!H", csum(ip)) + ip[12:]
        self.seg.send(OUR_MAC + PEER_MAC + b"\x08\x00" + ip + t)


def main(image):
    files = {}
    for tid, audio, where in TRACKS:
        pcm = bytes(random.Random(tid).randrange(256) for _ in range(150_000 - 44))   # 74 clusters: a long chain
        files[audio] = (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, 44100, 176400, 4, 16)
                        + b"data" + struct.pack("<I", len(pcm)) + pcm)
        files[where] = anlz(audio, 120.0)
    files["/PIONEER/rekordbox/export.pdb"] = export_pdb([(t, a, w, os.path.basename(a)) for t, a, w in TRACKS])
    stick = "build/test_nfs_stick.img"
    os.makedirs("build", exist_ok=True)
    make_image(stick, files)
    seg = Segment()
    p = subprocess.Popen([QEMU, "-M", MACHINE, "-kernel", image, "-display", "none", "-monitor", "none",
                          "-serial", "stdio", "-serial", "null",
                          "-drive", f"if=none,id=usbstk,file={stick},format=raw,snapshot=on",
                          "-device", "usb-storage,drive=usbstk,port=1",
                          "-netdev", f"socket,id=djlink,mcast={seg.group}:{seg.port},localaddr=127.0.0.1"],
                         stdout=subprocess.PIPE, stderr=open("build/test_nfs.qemu.log", "w"), stdin=subprocess.DEVNULL)
    os.set_blocking(p.stdout.fileno(), False)
    out = []
    peer = Peer(seg, p, out)
    fails = []
    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""))
        if not ok: fails.append(name)
    try:
        t0 = time.time()
        while time.time() - t0 < 20 and not (peer.keepalive and "nfsd: serving" in "".join(out)): peer.pump(0.1)
        peer.pump(2.0)                                          # it hears us, sends status
        console = "".join(out)
        first = re.search(r"track 1: /usb(\S.*?)  \(.*id (\d+)\)", console)
        loaded_id = int(first.group(2)) if first else -1
        check("reads the IDs from export.pdb", "usb: export.pdb, 2 tracks" in console and all(f"id {t})" in console for t, _, _ in TRACKS))
        check("its status names the loaded track by its rekordbox ID, on its USB",
              bool(peer.statuses) and all(struct.unpack("!I", s[0x2c:0x30])[0] == loaded_id and s[0x28] == 2 and s[0x29] == 3 for s in peer.statuses[-5:]),
              f"id {loaded_id}, {len(peer.statuses)} statuses")

        port = struct.unpack("!I", peer.call(111, 100000, 2, 3, struct.pack("!IIII", 100005, 1, 17, 0)))[0]
        nport = struct.unpack("!I", peer.call(111, 100000, 2, 3, struct.pack("!IIII", 100003, 2, 17, 0)))[0]
        check("portmap on 111 names mountd and nfsd", port == nport == 2049, f"{port}, {nport}")
        ex = peer.call(port, 100005, 1, 5, b"")
        check("exports \"/C/\" (UTF-16LE) to 169.254/16", wide("/C/") in ex and b"169.254.0.0/255.255.0.0" in ex)
        r = peer.call(port, 100005, 1, 1, xdr_opaque(wide("/C/")))
        root = r[4:36]
        check("mounts it", struct.unpack("!I", r[:4])[0] == 0 and len(root) == 32)

        def lookup(fh, name):
            r = peer.call(nport, 100003, 2, 4, fh + xdr_opaque(wide(name)))
            st = struct.unpack("!I", r[:4])[0]
            return (r[4:36], struct.unpack("!I", r[36 + 20:36 + 24])[0]) if st == 0 else (None, st)

        def resolve(path):
            fh, size = root, 0
            for comp in path.strip("/").split("/"):
                fh, size = lookup(fh, comp)
                if fh is None: return None, size
            return fh, size

        def read_all(fh, size, chunk):
            data = b""
            while len(data) < size:
                r = peer.call(nport, 100003, 2, 6, fh + struct.pack("!III", len(data), chunk, chunk))
                assert struct.unpack("!I", r[:4])[0] == 0
                n = struct.unpack("!I", r[4 + 68:4 + 72])[0]
                if n == 0: break
                data += r[4 + 72:4 + 72 + n]
            return data

        pdb_fh, pdb_size = resolve("/PIONEER/rekordbox/export.pdb")
        pdb = read_all(pdb_fh, pdb_size, 8192) if pdb_fh else b""
        check("export.pdb reads back over NFS as it is on the stick", pdb == files["/PIONEER/rekordbox/export.pdb"], f"{len(pdb)} bytes")
        tid, audio, where = TRACKS[0]
        fh, size = resolve(audio)
        frags0 = peer.fragmented
        t1 = time.time()
        got = read_all(fh, size, 8192) if fh else b""
        secs = time.time() - t1
        check("a track (long names, spaces, a 74-cluster chain) reads back in 8 KB READs, each sent as IP fragments",
              got == files[audio] and peer.fragmented - frags0 >= len(got) // 8192,
              f"{len(got)} bytes in {secs:.2f} s, {peer.fragmented - frags0} fragmented replies")
        got32 = read_all(fh, size, 32768) if fh else b""
        check("and in 32 KB READs, as a player asks", got32 == files[audio])
        mid = peer.call(nport, 100003, 2, 6, fh + struct.pack("!III", 100_000, 8192, 8192)) if fh else b""
        check("a READ from the middle (a seek back after the end)", mid[4 + 72:4 + 72 + 8192] == files[audio][100_000:108_192])
        _, missing = resolve("/Contents/nothing.wav")
        check("a name that is not there is NFSERR_NOENT", missing == 2)
        _, upper = resolve(audio.upper())
        check("names match without regard to case, as on the deck", upper != 2 and _ is not None, str(upper))
        r = peer.call(nport, 100003, 2, 16, root + struct.pack("!II", 0, 1000))
        names, at = [], 4
        while struct.unpack("!I", r[at:at + 4])[0] == 1:
            n = struct.unpack("!I", r[at + 8:at + 12])[0]
            names.append(r[at + 12:at + 12 + n].decode("utf-16-le")); at += 12 + ((n + 3) & ~3) + 4
        check("READDIR lists the stick's top folder in UTF-16LE", {"Contents", "PIONEER"} <= set(names), ", ".join(names))
        peer.syn(12523)
        t1 = time.time()
        while time.time() - t1 < 2 and not peer.tcp: peer.pump(0.02)
        rst = peer.tcp[0] if peer.tcp else b""
        check("a TCP connection to the dbserver port is reset at once",
              len(rst) >= 20 and rst[13] & 0x04 and struct.unpack("!I", rst[8:12])[0] == 1001, f"{(time.time() - t1) * 1000:.0f} ms")
        peer.pump(1.0)
        check("the deck keeps sending status while serving", len(peer.statuses) >= 10, f"{len(peer.statuses)}")
    finally:
        p.kill(); p.wait()
    console = "".join(out)
    check("no fault", "fatal" not in console)
    print("\n--- console\n" + console.strip())
    print(f"\n{'all passed' if not fails else str(len(fails)) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else IMAGE))
