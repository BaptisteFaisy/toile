#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Toile — fabrique un GIF animé à partir d'une série d'images PNG.

Stdlib uniquement : ni PIL, ni installation, ni réseau.  Le script écrit
lui-même le décodeur PNG (signature, IHDR/IDAT/IEND, zlib, annulation des
cinq filtres de ligne) et l'encodeur GIF89a (palette globale, LZW,
en-têtes, extension NETSCAPE2.0 pour la boucle infinie).

Usage :

    python3 tools/make-gif.py --out site/gif/toile.gif --fps 10 a.png b.png

Les images d'entrée sont des PNG 8 bits RGB ou RGBA, non entrelacés, de
même taille ; si les tailles diffèrent, message sur stderr et sortie 2.
La quantification est 6x6x6 (216 couleurs web-safe), sans tramage ; l'alpha
est ignoré et le fond du GIF est noir.

Sortie : une ligne de résumé sur stdout ; code 0 si le GIF est écrit, 2 si
les entrées sont invalides (taille, format PNG, fichier absent).
"""

import argparse
import os
import struct
import sys
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
WEB_SAFE = (0, 51, 102, 153, 204, 255)
PALETTE_SIZE = 216
GCT_SIZE = 256
LZW_MAX_CODE = 4096
WEIGHTS = (0.299, 0.587, 0.114)


class ImageError(Exception):
    """Entrée invalide : PNG illisible, format non supporté, taille différente."""


def read_chunks(data):
    """Découpe un flux PNG en une liste de (type, charge utile)."""
    if not data.startswith(PNG_SIGNATURE):
        raise ImageError("signature PNG absente")
    chunks, pos = [], len(PNG_SIGNATURE)
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        chunks.append((kind, payload))
        pos += 12 + length
    return chunks


def parse_ihdr(payload):
    """(largeur, hauteur, profondeur, type de couleur, entrelacement) de IHDR."""
    if len(payload) != 13:
        raise ImageError("IHDR mal formé")
    width, height, depth, color, comp, filt, interlace = struct.unpack(
        ">IIBBBBB", payload)
    if depth != 8:
        raise ImageError("profondeur %d bits non supportée (8 bits attendu)" % depth)
    if color not in (2, 6):
        raise ImageError("type de couleur PNG %d non supporté (2 ou 6)" % color)
    if comp != 0 or filt != 0:
        raise ImageError("méthode de compression PNG non supportée")
    if interlace != 0:
        raise ImageError("PNG entrelacé non supporté")
    return width, height, color


def paeth(a, b, c):
    """Prédicteur Paeth : le voisin le plus proche de a + b - c."""
    estimate = a + b - c
    da, db, dc = abs(estimate - a), abs(estimate - b), abs(estimate - c)
    if da <= db and da <= dc:
        return a
    if db <= dc:
        return b
    return c


def unfilter_line(ftype, line, previous, bpp):
    """Annule le filtre d'une ligne de scan (en place) et la renvoie."""
    for i in range(len(line)):
        left = line[i - bpp] if i >= bpp else 0
        up = previous[i]
        upper_left = previous[i - bpp] if i >= bpp else 0
        if ftype == 1:
            line[i] = (line[i] + left) & 0xFF
        elif ftype == 2:
            line[i] = (line[i] + up) & 0xFF
        elif ftype == 3:
            line[i] = (line[i] + ((left + up) >> 1)) & 0xFF
        elif ftype == 4:
            line[i] = (line[i] + paeth(left, up, upper_left)) & 0xFF
        elif ftype != 0:
            raise ImageError("filtre de ligne PNG inconnu : %d" % ftype)
    return line


def decode_png(data):
    """(largeur, hauteur, octets RGB) d'un PNG 8 bits RGB ou RGBA."""
    chunks = read_chunks(data)
    header = [payload for kind, payload in chunks if kind == b"IHDR"]
    if not header:
        raise ImageError("chunk IHDR absent")
    width, height, color = parse_ihdr(header[0])
    idat = b"".join(payload for kind, payload in chunks if kind == b"IDAT")
    if not idat:
        raise ImageError("chunk IDAT absent")
    try:
        raw = zlib.decompress(idat)
    except zlib.error as exc:
        raise ImageError("flux IDAT illisible : %s" % exc)
    bpp = 4 if color == 6 else 3
    stride = width * bpp
    if len(raw) < height * (stride + 1):
        raise ImageError("données PNG tronquées")
    pixels, previous, pos = bytearray(), bytearray(stride), 0
    for _ in range(height):
        ftype = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        unfilter_line(ftype, line, previous, bpp)
        pixels += line
        previous = line
    if color == 6:
        rgb = bytearray(len(pixels) // 4 * 3)
        for i in range(len(pixels) // 4):
            rgb[3 * i:3 * i + 3] = pixels[4 * i:4 * i + 3]
        pixels = rgb
    return width, height, bytes(pixels)


def read_png(path):
    """(largeur, hauteur, octets RGB) du fichier PNG `path`."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as exc:
        raise ImageError("%s : %s" % (path, exc.strerror or exc))
    return decode_png(data)


def build_palette():
    """Palette web-safe 6x6x6 : 216 entrées (r, g, b), index = 36r + 6g + b."""
    return [(r, g, b) for r in WEB_SAFE for g in WEB_SAFE for b in WEB_SAFE]


def build_level_tables():
    """Trois tables 256 -> niveau (0..5), au plus proche pondéré (luma)."""
    tables = []
    for weight in WEIGHTS:
        table = bytearray(256)
        for value in range(256):
            best, best_cost = 0, None
            for level, sample in enumerate(WEB_SAFE):
                cost = weight * (value - sample) ** 2
                if best_cost is None or cost < best_cost:
                    best, best_cost = level, cost
            table[value] = best
        tables.append(bytes(table))
    return tables


def quantize(rgb, tables):
    """Indices de palette (0..215) d'une image RGB, sans tramage."""
    red, green, blue = tables
    levels_r = rgb[0::3].translate(red)
    levels_g = rgb[1::3].translate(green)
    levels_b = rgb[2::3].translate(blue)
    return bytes(r * 36 + g * 6 + b for r, g, b in zip(levels_r, levels_g, levels_b))


class BitWriter:
    """Écriture de codes LZW en bits LSB-first, comme l'exige le format GIF."""

    def __init__(self):
        self.data = bytearray()
        self.acc = 0
        self.nbits = 0

    def write(self, code, size):
        """Ajoute un code de `size` bits au flux."""
        self.acc |= code << self.nbits
        self.nbits += size
        while self.nbits >= 8:
            self.data.append(self.acc & 0xFF)
            self.acc >>= 8
            self.nbits -= 8

    def flush(self):
        """OCTets du flux, avec le dernier octet partiel bourré de zéros."""
        if self.nbits:
            self.data.append(self.acc & 0xFF)
            self.acc = 0
            self.nbits = 0
        return bytes(self.data)


def lzw_compress(indices, min_code_size):
    """Compresse des indices de palette en codes LZW (variante GIF)."""
    clear_code = 1 << min_code_size
    eoi_code = clear_code + 1
    writer = BitWriter()
    table = {}
    code_size = min_code_size + 1
    next_code = eoi_code + 1
    prefix = None
    writer.write(clear_code, code_size)
    for value in indices:
        if prefix is None:
            prefix = value
            continue
        key = (prefix, value)
        found = table.get(key)
        if found is not None:
            prefix = found
            continue
        writer.write(prefix, code_size)
        if next_code < LZW_MAX_CODE:
            table[key] = next_code
            next_code += 1
            if next_code > (1 << code_size) and code_size < 12:
                code_size += 1
        else:
            writer.write(clear_code, code_size)
            table.clear()
            next_code = eoi_code + 1
            code_size = min_code_size + 1
        prefix = value
    if prefix is not None:
        writer.write(prefix, code_size)
    writer.write(eoi_code, code_size)
    return writer.flush()


def sub_blocks(data):
    """Découpe un flux en sous-blocs GIF de 255 octets au maximum."""
    blocks = bytearray()
    for start in range(0, len(data), 255):
        chunk = data[start:start + 255]
        blocks.append(len(chunk))
        blocks += chunk
    blocks.append(0)
    return bytes(blocks)


def write_gif(path, frames, width, height, fps, delay_cs):
    """Écrit un GIF89a (palette globale, LZW, boucle NETSCAPE2.0)."""
    palette = build_palette()
    out = bytearray(b"GIF89a")
    out += struct.pack("<HHBBB", width, height, 0xF7, 0, 0)
    for index in range(GCT_SIZE):
        out += bytes(palette[index] if index < PALETTE_SIZE else (0, 0, 0))
    out += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00"
    for indices in frames:
        out += b"\x21\xF9\x04\x04" + struct.pack("<H", delay_cs) + b"\x00\x00"
        out += b"\x2C" + struct.pack("<HHHHB", 0, 0, width, height, 0x00)
        out += bytes([8]) + sub_blocks(lzw_compress(indices, 8))
    out += b"\x3B"
    with open(path, "wb") as handle:
        handle.write(out)
    return len(out)


def load_frames(paths):
    """(largeur, hauteur, [indices de palette]) des images PNG de `paths`."""
    frames, size = [], None
    for path in paths:
        width, height, rgb = read_png(path)
        if size is None:
            size = (width, height)
        elif (width, height) != size:
            raise ImageError("tailles différentes : %s est %dx%d, attendu %dx%d"
                             % (path, width, height, size[0], size[1]))
        frames.append(quantize(rgb, build_level_tables()))
    return size[0], size[1], frames


def parse_args(argv):
    """Options de la ligne de commande : --out, --fps, images d'entrée."""
    parser = argparse.ArgumentParser(
        description="Fabrique un GIF animé à partir d'images PNG (stdlib seule).")
    parser.add_argument("--out", required=True, help="fichier GIF à écrire")
    parser.add_argument("--fps", type=int, default=10, help="images par seconde")
    parser.add_argument("images", nargs="+", help="images PNG d'entrée")
    return parser.parse_args(argv)


def main(argv=None):
    """Point d'entrée : écrit le GIF et résume sur stdout. Retourne le code."""
    args = parse_args(argv)
    if args.fps <= 0:
        sys.stderr.write("make-gif: --fps doit être positif\n")
        return 2
    try:
        width, height, frames = load_frames(args.images)
    except ImageError as exc:
        sys.stderr.write("make-gif: %s\n" % exc)
        return 2
    directory = os.path.dirname(os.path.abspath(args.out))
    if directory and not os.path.isdir(directory):
        os.makedirs(directory, exist_ok=True)
    delay_cs = max(1, int(round(100.0 / args.fps)))
    size = write_gif(args.out, frames, width, height, args.fps, delay_cs)
    print("%d images, %dx%d, %d octets, %d im/s -> %s"
          % (len(frames), width, height, size, args.fps, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
