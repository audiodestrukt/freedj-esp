#!/usr/bin/env python3
"""Milestone 3, end to end on the emulated CDJ-2000 (cdj-nxs2-qemu): our main
board image on a private Ethernet segment (QEMU's socket netdev, multicast on
the loopback interface only, so nothing reaches a real network), with a
made-up XDJ-1000MK2 beside it, played by this script from packets of a real
one (freedj-3000's reference capture). Checks that our deck answers ARP,
announces itself as a Pro DJ Link player, sends the peer its status, and
hears the peer as the tempo master.

  python3 tools/test_link.py build/main.bin      (CDJ_QEMU= another qemu)
"""
import os, random, re, select, socket, struct, subprocess, sys, time

QEMU = os.environ.get("CDJ_QEMU", os.path.expanduser("~/sandbox/cdj-nxs2-build/qemu-build/qemu-system-sh4"))
# The board under test (BOARD=xdj1000mk2 for that image; main/board.h): its
# emulated machine, the name it announces, and the address it makes up.
BOARD = os.environ.get("BOARD", "cdj2000")
MACHINE, NAME, OUR_MAC, OUR_IP = {
    "cdj2000":    ("cdj2000",    b"CDJ-2000",    bytes.fromhex("02cd20002001"), bytes([169, 254, 32, 1])),
    "xdj1000mk2": ("xdj1000mk2", b"XDJ-1000MK2", bytes.fromhex("02cd10001001"), bytes([169, 254, 16, 1])),
}[BOARD]
IMAGE = "build/main.bin" if BOARD == "cdj2000" else f"build-{BOARD}/main.bin"
PEER_MAC, PEER_IP = bytes.fromhex("020000000002"), bytes([169, 254, 0, 2])
MAGIC = b"Qspt1WmJOL"
# A playing tempo master's status and one of its beats: XDJ-1000MK2 firmware
# 1.45, 126.05 BPM, flags 0xe4 (playing, master, sync, on air).
STATUS = bytes.fromhex(
    "5173707431576d4a4f4c0a58444a2d313030304d4b3200000000000000000001050101000100000001030100000000010000000100000004"
    "00000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000ffff0100060400000000"
    "000000040100000000000003312e3435000000000000000100e4fffa001000008000313d7fffffff00100000000901ff0000000001ff0400"
    "00000000000000000000000000000100000000000000000000100000001000000000026b1f03000012345678000000010101010100000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000500000000000000000003000000000000")
BEAT = bytes.fromhex(
    "5173707431576d4a4f4c2858444a2d313030304d4b32000000000000000000010001003c000001dc000003b8000007700000077000000ee0"
    "00000ee0ffffffffffffffffffffffffffffffffffffffffffffffff001000000000313d01000001")


def csum(b):
    if len(b) % 2: b += b"\0"
    s = sum(struct.unpack("!%dH" % (len(b) // 2), b))
    while s >> 16: s = (s & 0xffff) + (s >> 16)
    return ~s & 0xffff


def udp_frame(dst_mac, dst_ip, sport, dport, payload):
    u = struct.pack("!HHHH", sport, dport, 8 + len(payload), 0) + payload
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(u), 0, 0x4000, 64, 17, 0, PEER_IP, dst_ip)
    ip = ip[:10] + struct.pack("!H", csum(ip)) + ip[12:]
    return dst_mac + PEER_MAC + b"\x08\x00" + ip + u


class Segment:
    def __init__(self):
        self.group, self.port = f"239.77.{random.randint(80, 250)}.{random.randint(1, 250)}", random.randint(46000, 47000)
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton("127.0.0.1"))
        self.tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
        self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.rx.bind(("", self.port))
        self.rx.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(self.group) + socket.inet_aton("127.0.0.1"))
        self.rx.setblocking(False)

    def send(self, frame):
        self.tx.sendto(frame, (self.group, self.port))


def main(image):
    seg = Segment()
    p = subprocess.Popen([QEMU, "-M", MACHINE, "-kernel", image, "-display", "none", "-monitor", "none",
                          "-serial", "stdio", "-serial", "null",
                          "-netdev", f"socket,id=djlink,mcast={seg.group}:{seg.port},localaddr=127.0.0.1"],
                         stdout=subprocess.PIPE, stderr=open("build/test_link.qemu.log", "w"), stdin=subprocess.DEVNULL)
    os.set_blocking(p.stdout.fileno(), False)
    out, ours = "", []          # our deck's UDP: (time, dst ip, dst port, payload)
    arp_replies, our_keepalive = 0, None
    t0 = time.time()
    next_ka = next_st = next_beat = t0 + 3.0
    asked_arp, counter, beat = False, 0, 1
    try:
        while time.time() - t0 < 14:
            r, _, _ = select.select([p.stdout, seg.rx], [], [], 0.02)
            if p.stdout in r:
                out += (p.stdout.read() or b"").decode(errors="replace")
            while True:
                try:
                    f = seg.rx.recv(2048)
                except BlockingIOError:
                    break
                if f[6:12] != OUR_MAC:
                    continue
                et = struct.unpack("!H", f[12:14])[0]
                if et == 0x0806:
                    op = struct.unpack("!H", f[20:22])[0]
                    if op == 1 and f[38:42] == PEER_IP:          # who has the peer: answer for it
                        seg.send(f[6:12] + PEER_MAC + b"\x08\x06" + f[14:20] + b"\x00\x02" + PEER_MAC + PEER_IP + f[22:32])
                    elif op == 2 and f[28:32] == OUR_IP and f[32:38] == PEER_MAC:
                        arp_replies += 1
                elif et == 0x0800 and f[23] == 17:
                    ihl = (f[14] & 15) * 4
                    u = f[14 + ihl:]
                    dport, ulen = struct.unpack("!HH", u[2:6])
                    ours.append((time.time(), f[30:34], dport, u[8:ulen]))
                    if dport == 50000 and u[8:18] == MAGIC and u[8 + 0x0a] == 0x06:
                        our_keepalive = u[8:ulen]
            now = time.time()
            if now > t0 + 3 and not asked_arp:                  # ask who has our deck's address
                asked_arp = True
                seg.send(b"\xff" * 6 + PEER_MAC + b"\x08\x06" + struct.pack("!HHBBH", 1, 0x0800, 6, 4, 1)
                         + PEER_MAC + PEER_IP + b"\0" * 6 + OUR_IP)
            if our_keepalive and now >= next_ka:                # the peer announces itself
                next_ka = now + 1.5
                k = bytearray(our_keepalive)
                k[0x0c:0x20] = b"XDJ-1000MK2".ljust(20, b"\0")
                k[0x24] = 1
                k[0x26:0x2c] = PEER_MAC
                k[0x2c:0x30] = PEER_IP
                seg.send(udp_frame(b"\xff" * 6, bytes([169, 254, 255, 255]), 50000, 50000, bytes(k)))
            if our_keepalive and now >= next_st:                # its status, to our deck
                next_st = now + 0.2
                counter += 1
                s = bytearray(STATUS)
                s[0xa0:0xa4] = struct.pack("!I", beat)
                s[0xc8:0xcc] = struct.pack("!I", counter)
                seg.send(udp_frame(OUR_MAC, OUR_IP, 50002, 50002, bytes(s)))
            if our_keepalive and now >= next_beat:              # and its beats, broadcast
                next_beat = now + 60 / 126.05
                beat += 1
                b = bytearray(BEAT)
                b[0x5c] = (beat - 1) % 4 + 1
                seg.send(udp_frame(b"\xff" * 6, bytes([169, 254, 255, 255]), 50001, 50001, bytes(b)))
    finally:
        p.kill(); p.wait()

    fails = []
    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""))
        if not ok: fails.append(name)
    kas = [(t, d) for t, ip, port, d in ours if port == 50000 and d[:10] == MAGIC and d[0x0a] == 0x06]
    sts = [(t, ip, d) for t, ip, port, d in ours if port == 50002 and d[:10] == MAGIC and d[0x0a] == 0x0a]
    gaps = [b[0] - a[0] for a, b in zip(kas, kas[1:])]
    check("boots and comes up on the network", f"on the network as {'.'.join(map(str, OUR_IP))}, player 2" in out)
    check("answers ARP for its address", arp_replies >= 1)
    k = kas[0][1] if kas else b""
    check(f"announces as player 2 \"{NAME.decode()}\", its MAC and IP in the packet",
          len(k) == 0x36 and k[0x0c:0x0c + len(NAME) + 1] == NAME + b"\0" and k[0x24] == 2 and k[0x26:0x2c] == OUR_MAC and k[0x2c:0x30] == OUR_IP)
    check("a keep-alive every 1.5 s", len(gaps) >= 4 and all(1.3 < g < 1.7 for g in gaps),
          ", ".join(f"{g:.2f}" for g in gaps))
    check("sends the peer its status (to 169.254.0.2:50002, as player 2)",
          len(sts) >= 10 and all(ip == PEER_IP and d[0x21] == 2 for _, ip, d in sts), f"{len(sts)} sent")
    check("hears the peer", re.search(r"link: .*peers 1", out) is not None)
    check("follows it as the tempo master at 126.05 BPM", re.search(r"master 1 @ 126\.05 BPM", out) is not None)
    check("the link light is on (PSR read the right way round)", "(no link)" not in out.split("on the network")[-1])
    print("\n--- console\n" + out.strip())
    print(f"\n{'all passed' if not fails else str(len(fails)) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else IMAGE))
