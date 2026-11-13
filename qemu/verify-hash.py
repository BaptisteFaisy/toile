#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Preuve C — contrôle croisé de l'empreinte entre l'invité et l'hôte.

L'invité (guest/common/toile_anim_main.c, mode --hash-hold) peint une image,
publie l'empreinte FNV-1a 64 de son framebuffer sur la console série, puis
TIENT l'image 1,2 s.  Cet outil :

  1. attend une ligne « HASH: ... fnv=<hex> » dans le journal série ;
  2. demande une capture au moniteur QEMU (screendump) PENDANT la tenue ;
  3. reconstruit la suite d'octets que l'invité a hachée (B, G, R, X par
     pixel pour le format B8G8R8X8_UNORM) ;
  4. compare les deux empreintes.

Sortie : 0 si les empreintes sont identiques (le transit est prouvé), 1 sinon.
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK = (1 << 64) - 1


def fnv1a(data):
    h = FNV_OFFSET
    for byte in data:
        h = ((h ^ byte) * FNV_PRIME) & MASK
    return h


def parse_ppm(path):
    """Rend (largeur, hauteur, octets RGB) d'un PPM binaire P6."""
    with open(path, "rb") as fh:
        data = fh.read()
    if not data.startswith(b"P6"):
        raise ValueError("pas un PPM P6 : %s" % path)
    fields = []
    idx = 2
    while len(fields) < 3:
        while data[idx:idx + 1].isspace():
            idx += 1
        if data[idx:idx + 1] == b"#":
            while data[idx:idx + 1] not in (b"\n", b""):
                idx += 1
            continue
        start = idx
        while not data[idx:idx + 1].isspace():
            idx += 1
        fields.append(int(data[start:idx]))
    idx += 1
    w, h, _maxval = fields
    return w, h, data[idx:idx + w * h * 3]


def guest_bytes(w, h, rgb, fmt):
    """Reconstruit les octets du framebuffer invité (ordre des pixels B,G,R,X).

    fmt 2 = B8G8R8X8_UNORM : le mot 32 bits vaut R<<16|G<<8|B, donc en
    little-endian les octets sont B, G, R, X(0).
    """
    out = bytearray()
    for i in range(w * h):
        r = rgb[3 * i]
        g = rgb[3 * i + 1]
        b = rgb[3 * i + 2]
        if fmt == 2:
            out += bytes((b, g, r, 0))
        else:
            out += bytes((r, g, b, 0))
    return bytes(out)


def guest_bytes_alt(w, h, rgb, variant):
    """Variantes d'agencement, pour diagnostiquer un écart de format.

    0 = B,G,R,X(0)  1 = R,G,B,X(0)  2 = B,G,R,X(255)  3 = B,G,R,A(rgb)
    """
    out = bytearray()
    for i in range(w * h):
        r, g, b = rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]
        if variant == 0:
            out += bytes((b, g, r, 0))
        elif variant == 1:
            out += bytes((r, g, b, 0))
        elif variant == 2:
            out += bytes((b, g, r, 255))
        else:
            out += bytes((b, g, r, r))
    return bytes(out)


def screendump(monitor, target):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect(monitor)
    time.sleep(0.2)
    try:
        s.recv(4096)
    except OSError:
        pass
    s.sendall(("screendump " + target + "\n").encode())
    time.sleep(0.8)
    try:
        s.recv(4096)
    except OSError:
        pass
    s.close()


def wait_hash(serial, timeout, after=-1):
    """Attend la ligne HASH: la plus recente dont frame > `after`.

    On relit tout le journal (la preuve C tourne quelques minutes) : c'est le
    seul moyen de ne pas recapturer la première empreinte, déjà périmée.
    """
    deadline = time.time() + timeout
    best = (None, None)
    while time.time() < deadline:
        try:
            with open(serial, "rb") as fh:
                text = fh.read().decode("utf-8", "replace")
        except OSError:
            text = ""
        for m in re.finditer(r"HASH: frame=(\d+).*?fnv=([0-9a-fA-F]{16})", text):
            frame = int(m.group(1))
            if frame > after:
                best = (frame, m.group(2).lower())
        if best[0] is not None:
            return best
        time.sleep(0.1)
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--serial", required=True)
    ap.add_argument("--monitor", required=True)
    ap.add_argument("--fmt", type=int, default=2)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--tries", type=int, default=3)
    args = ap.parse_args()

    last_frame = -1
    for attempt in range(args.tries):
        frame, expected = wait_hash(args.serial, args.timeout, last_frame)
        if frame is not None:
            last_frame = frame
        if expected is None:
            print("PREUVE C: aucune ligne HASH dans le journal série")
            return 1
        tmp = tempfile.NamedTemporaryFile(suffix=".ppm", delete=False)
        tmp.close()
        try:
            screendump(args.monitor, tmp.name)
            w, h, rgb = parse_ppm(tmp.name)
        finally:
            os.unlink(tmp.name)
        got = fnv1a(guest_bytes(w, h, rgb, args.fmt))
        if got != int(expected, 16):
            for alt in (0, 1, 2, 3):
                probe = guest_bytes_alt(w, h, rgb, alt)
                if fnv1a(probe) == int(expected, 16):
                    print("PREUVE C: correspondance avec un autre agencement "
                          "d'octets (variant %d) -> corriger le format" % alt)
                    got = fnv1a(probe)
                    break
        print("PREUVE C: frame=%d invite=%s hote=%016x (%dx%d) %s"
              % (frame, expected, got, w, h,
                 "IDENTIQUES" if got == int(expected, 16) else "DIFFERENTS"))
        if got == int(expected, 16):
            return 0
        print("PREUVE C: nouvel essai (%d/%d)" % (attempt + 1, args.tries))
        time.sleep(1.0)
    return 1


if __name__ == "__main__":
    sys.exit(main())