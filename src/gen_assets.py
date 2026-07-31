#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""纯标准库生成抽学号 Ultra 的资源：
  1) icon.ico  —— 用 0731.png 生成 16/32/48/256 多尺寸图标
  2) bg_blur.png —— 同一张图做大幅模糊(缩小再盒式模糊)，供程序作为模糊动漫背景
不依赖任何第三方库（仅 zlib/struct）。
"""
import zlib, struct, os

SRC = r"C:\Users\imjia\Pictures\Screenshots\0731.png"
OUTDIR = os.path.dirname(os.path.abspath(__file__))

# ----------------------- PNG 解码 -----------------------
def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc: return a
    if pb <= pc: return b
    return c

def defilter(raw, w, h, bpp):
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xff
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xff
        elif ft == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xff
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + paeth(a, b, c)) & 0xff
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return out

def decode_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', "not a png"
    pos = 8; idat = b''; w = h = bd = ct = inter = 0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, bd, ct, _, _, inter = struct.unpack('>IIBBBBB', data)
        elif typ == b'IDAT':
            idat += data
        elif typ == b'IEND':
            break
        pos += 12 + ln
    assert bd == 8 and inter == 0, "only 8-bit non-interlaced supported"
    raw = zlib.decompress(idat)
    ch = {2: 3, 6: 4, 0: 1, 4: 2}[ct]
    px = defilter(raw, w, h, ch)
    return w, h, ch, px

# ----------------------- PNG 编码 -----------------------
def pack_png(w, h, ch, pixels):
    stride = w * ch
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += pixels[y * stride:(y + 1) * stride]
    comp = zlib.compress(bytes(raw), 9)
    def chunk(typ, data):
        return struct.pack('>I', len(data)) + typ + data + struct.pack('>I', zlib.crc32(typ + data) & 0xffffffff)
    sig = b'\x89PNG\r\n\x1a\n'
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6 if ch == 4 else 2, 0, 0, 0)
    return sig + chunk(b'IHDR', ihdr) + chunk(b'IDAT', comp) + chunk(b'IEND', b'')

# ----------------------- 缩放 / 模糊 -----------------------
def box_downscale(px, w, h, ch, dw, dh):
    out = bytearray(dw * dh * ch)
    for y in range(dh):
        sy0 = y * h // dh; sy1 = (y + 1) * h // dh
        if sy1 <= sy0: sy1 = sy0 + 1
        for x in range(dw):
            sx0 = x * w // dw; sx1 = (x + 1) * w // dw
            if sx1 <= sx0: sx1 = sx0 + 1
            r = g = b = a = 0; n = 0
            for sy in range(sy0, sy1):
                base = sy * w * ch
                for sx in range(sx0, sx1):
                    i = base + sx * ch
                    r += px[i]; g += px[i + 1]; b += px[i + 2]
                    if ch == 4: a += px[i + 3]
                    n += 1
            o = (y * dw + x) * ch
            out[o] = r // n; out[o + 1] = g // n; out[o + 2] = b // n
            if ch == 4: out[o + 3] = a // n
    return out

def box_blur(px, w, h, ch, r):
    tmp = bytearray(len(px)); out = bytearray(len(px))
    for y in range(h):
        base = y * w * ch
        for c in range(ch):
            for x in range(w):
                s = n = 0
                for k in range(-r, r + 1):
                    xx = x + k
                    if 0 <= xx < w:
                        s += px[base + xx * ch + c]; n += 1
                tmp[base + x * ch + c] = s // n
    for x in range(w):
        for c in range(ch):
            for y in range(h):
                s = n = 0
                for k in range(-r, r + 1):
                    yy = y + k
                    if 0 <= yy < h:
                        s += tmp[(yy * w + x) * ch + c]; n += 1
                out[(y * w + x) * ch + c] = s // n
    return out

def center_crop_square(px, w, h, ch):
    side = min(w, h)
    x0 = (w - side) // 2; y0 = (h - side) // 2
    out = bytearray(side * side * ch)
    for y in range(side):
        src = (y0 + y) * w * ch + x0 * ch
        dst = y * side * ch
        out[dst:dst + side * ch] = px[src:src + side * ch]
    return out, side, side

# ----------------------- ICO 打包 -----------------------
def build_ico(images):
    # images: list of (size, png_bytes)
    header = struct.pack('<HHH', 0, 1, len(images))
    entries = bytearray()
    offset = 6 + len(images) * 16
    data = bytearray()
    for s, png in images:
        w = s if s < 256 else 0
        entries += struct.pack('<BBBBHHII', w, w, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
        data += png
    return header + bytes(entries) + bytes(data)

def main():
    w, h, ch, px = decode_png(SRC)
    print("decoded", w, "x", h, "ch", ch)

    # ---- 图标：中心裁方 -> 多尺寸 RGBA PNG -> ICO ----
    sq, sw, sh = center_crop_square(px, w, h, ch)
    icon_pngs = []
    for s in (16, 32, 48, 256):
        ds = box_downscale(sq, sw, sh, ch, s, s)
        icon_pngs.append((s, pack_png(s, s, ch, ds)))
    with open(os.path.join(OUTDIR, "icon.ico"), "wb") as f:
        f.write(build_ico(icon_pngs))
    print("wrote icon.ico")

    # ---- 背景图：高清(长边 ~1200) + 轻微模糊(半径2) -> RGB PNG ----
    # 轻微模糊保留画面内容，同时降低细节对文字的干扰；文字可读性由程序里的半透明白蒙版保证
    long_side = max(w, h)
    target = 1200
    scale = target / long_side
    bw = max(1, int(round(w * scale)))
    bh = max(1, int(round(h * scale)))
    small = box_downscale(px, w, h, ch, bw, bh)  # 保留原通道数
    # 若原图带 alpha，先与白色合成，避免半透明
    if ch == 4:
        comp = bytearray(bw * bh * 3)
        for i in range(bw * bh):
            a = small[i * 4 + 3] / 255.0
            comp[i * 3]     = int(small[i * 4]     * a + 255 * (1 - a))
            comp[i * 3 + 1] = int(small[i * 4 + 1] * a + 255 * (1 - a))
            comp[i * 3 + 2] = int(small[i * 4 + 2] * a + 255 * (1 - a))
        small = comp
    blurred = box_blur(small, bw, bh, 3, 2)
    with open(os.path.join(OUTDIR, "bg_blur.png"), "wb") as f:
        f.write(pack_png(bw, bh, 3, blurred))
    print("wrote bg_blur.png", bw, "x", bh)

if __name__ == "__main__":
    main()
