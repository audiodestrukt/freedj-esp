#!/usr/bin/env python3
"""See it run: our main board image on the emulated CDJ-2000, its front panel
in cdj-nxs2-qemu's virtual deck app (click keys, drag the jog and the tempo
slider; the lamps are lit by our firmware), its console in this terminal, and
an XDJ-1000MK2 (played from a real one's packets) beside it on a private
network segment so Link has a tempo master to follow.  The screen stays dark:
drawing it is milestone 6.  Close the app window or Ctrl-C to stop.

  python3 tools/demo.py [build/main.bin]
"""
import os, select, socket, struct, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_link import QEMU, Segment, udp_frame, STATUS, BEAT, OUR_MAC, OUR_IP, PEER_MAC, PEER_IP, MAGIC

EMU = os.path.expanduser(os.environ.get("CDJ_NXS2", "~/sandbox/cdj-nxs2"))


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]; s.close(); return p


def main(image):
    keys, state = free_port(), free_port()
    seg = Segment()
    env = dict(os.environ, CDJ_PANEL_KEYSOCK=str(keys), CDJ_PANEL_STATESOCK=str(state))
    os.makedirs("build", exist_ok=True)
    emu = subprocess.Popen([QEMU, "-M", "cdj2000", "-kernel", image, "-display", "none", "-monitor", "none",
                            "-serial", "stdio", "-serial", "null",
                            "-netdev", f"socket,id=djlink,mcast={seg.group}:{seg.port},localaddr=127.0.0.1"],
                           stdout=subprocess.PIPE, stderr=open("build/demo.qemu.log", "w"), stdin=subprocess.DEVNULL, env=env)
    app = subprocess.Popen([os.path.join(EMU, ".venv/bin/python"), "-m", "app.virtual_deck", "--model", "cdj2000",
                            "--panel-port", str(keys), "--state-port", str(state), "--screen", "face"],
                           cwd=EMU, stdout=open("build/demo.app.log", "w"), stderr=subprocess.STDOUT)
    os.set_blocking(emu.stdout.fileno(), False)
    print(f"CDJ-2000 (our firmware) on a private segment {seg.group}:{seg.port}; "
          f"an XDJ-1000MK2 joins as player 1 in a few seconds.\n")
    our_ka, t0 = None, time.time()
    next_ka = next_st = next_beat = t0 + 4.0
    counter, beat = 0, 1
    try:
        while emu.poll() is None and app.poll() is None:
            r, _, _ = select.select([emu.stdout, seg.rx], [], [], 0.02)
            if emu.stdout in r:
                sys.stdout.write((emu.stdout.read() or b"").decode(errors="replace")); sys.stdout.flush()
            while True:
                try:
                    f = seg.rx.recv(2048)
                except BlockingIOError:
                    break
                if f[6:12] != OUR_MAC:
                    continue
                et = struct.unpack("!H", f[12:14])[0]
                if et == 0x0806 and struct.unpack("!H", f[20:22])[0] == 1 and f[38:42] == PEER_IP:
                    seg.send(f[6:12] + PEER_MAC + b"\x08\x06" + f[14:20] + b"\x00\x02" + PEER_MAC + PEER_IP + f[22:32])
                elif et == 0x0800 and f[23] == 17:
                    u = f[14 + (f[14] & 15) * 4:]
                    if struct.unpack("!H", u[2:4])[0] == 50000 and u[8:18] == MAGIC and u[8 + 0x0a] == 0x06:
                        our_ka = u[8:8 + 0x36]
            now = time.time()
            if not our_ka:
                continue
            if now >= next_ka:
                next_ka = now + 1.5
                k = bytearray(our_ka)
                k[0x0c:0x20] = b"XDJ-1000MK2".ljust(20, b"\0"); k[0x24] = 1
                k[0x26:0x2c] = PEER_MAC; k[0x2c:0x30] = PEER_IP
                seg.send(udp_frame(b"\xff" * 6, bytes([169, 254, 255, 255]), 50000, 50000, bytes(k)))
            if now >= next_st:
                next_st = now + 0.2
                counter += 1
                s = bytearray(STATUS)
                s[0xa0:0xa4] = struct.pack("!I", beat); s[0xc8:0xcc] = struct.pack("!I", counter)
                seg.send(udp_frame(OUR_MAC, OUR_IP, 50002, 50002, bytes(s)))
            if now >= next_beat:
                next_beat = now + 60 / 126.05
                beat += 1
                b = bytearray(BEAT); b[0x5c] = (beat - 1) % 4 + 1
                seg.send(udp_frame(b"\xff" * 6, bytes([169, 254, 255, 255]), 50001, 50001, bytes(b)))
    except KeyboardInterrupt:
        pass
    finally:
        for p in (app, emu):
            if p.poll() is None:
                p.terminate()
        for p in (app, emu):
            try:
                p.wait(3)
            except subprocess.TimeoutExpired:
                p.kill()
    print("\nstopped.")


if __name__ == "__main__":
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    main(sys.argv[1] if len(sys.argv) > 1 else "build/main.bin")
