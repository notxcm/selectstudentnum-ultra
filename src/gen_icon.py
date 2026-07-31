#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成抽学号 Ultra 的程序图标 (icon.ico)，包含 32/48/256 三种尺寸。
绘制：渐变圆角方块 + 白色卡片 + 骰子五点（象征随机）。纯标准库实现。"""
import zlib, struct, math, os

def png_bytes(rgba, size):
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        raw += rgba[y*size*4:(y+1)*size*4]
    comp = zlib.compress(bytes(raw), 9)
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", comp) + chunk(b"IEND", b"")

def lerp(a, b, t):
    return int(a + (b - a) * t)

def inside_round(x, y, x0, y0, x1, y1, rr):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    if x >= x0 + rr and x <= x1 - rr:
        return True
    if y >= y0 + rr and y <= y1 - rr:
        return True
    cx = x0 + rr if x < x0 + rr else x1 - rr
    cy = y0 + rr if y < y0 + rr else y1 - rr
    return (x - cx) ** 2 + (y - cy) ** 2 <= rr * rr

def draw(size):
    rgba = bytearray(size * size * 4)
    def setpx(x, y, r, g, b, a=255):
        if 0 <= x < size and 0 <= y < size:
            i = (y * size + x) * 4
            # 简单 alpha 混合到已绘制像素
            sa = a / 255.0
            dr, dg, db, da = rgba[i], rgba[i+1], rgba[i+2], rgba[i+3]
            oa = da / 255.0
            na = sa + oa * (1 - sa)
            if na <= 0:
                return
            nr = (r * sa + dr * oa * (1 - sa)) / na
            ng = (g * sa + dg * oa * (1 - sa)) / na
            nb = (b * sa + db * oa * (1 - sa)) / na
            rgba[i], rgba[i+1], rgba[i+2], rgba[i+3] = int(nr), int(ng), int(nb), int(na*255)
    def fill_round(x0, y0, x1, y1, rr, col_top, col_bot):
        for y in range(size):
            for x in range(size):
                if inside_round(x, y, x0, y0, x1, y1, rr):
                    t = (y - y0) / max(1, (y1 - y0))
                    setpx(x, y, lerp(col_top[0], col_bot[0], t),
                          lerp(col_top[1], col_bot[1], t),
                          lerp(col_top[2], col_bot[2], t))
    def fill_circle(cx, cy, rad, col):
        for y in range(size):
            for x in range(size):
                if (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad:
                    setpx(x, y, col[0], col[1], col[2])
    m = size * 0.06
    fill_round(m, m, size - m, size - m, size * 0.24, (99, 102, 241), (79, 70, 229))
    ci = size * 0.21
    fill_round(ci, ci, size - ci, size - ci, size * 0.18, (248, 250, 255), (232, 236, 255))
    # 骰子五点
    col = (91, 92, 220)
    pr = size * 0.075
    card = (ci, size - ci)
    fx = [0.30, 0.70, 0.30, 0.70, 0.50]
    fy = [0.30, 0.30, 0.70, 0.70, 0.50]
    for u, v in zip(fx, fy):
        fill_circle(card[0] + (card[1] - card[0]) * u,
                    card[0] + (card[1] - card[0]) * v, pr, col)
    return bytes(rgba)

def build_ico(sizes):
    images = []
    for s in sizes:
        images.append((s, png_bytes(draw(s), s)))
    header = struct.pack("<HHH", 0, 1, len(images))
    entries = bytearray()
    offset = 6 + len(images) * 16
    for s, png in images:
        w = s if s < 256 else 0
        entries += struct.pack("<BBBBHHII", w, w, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
    data = b"".join(p for _, p in images)
    return header + bytes(entries) + data

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon.ico")
    with open(out, "wb") as f:
        f.write(build_ico([32, 48, 256]))
    print("wrote", out)
