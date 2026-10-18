"""Independent Python reference for the canvas_noise v1 contract.

Implements docs/canvas-component-contract.md literally with Python
arbitrarily-precision ints masked to 64 bits. Used ONLY to freeze
native/canvas_noise_golden.inc; the C++ tests must never call this to
compute expectations at runtime. The parity checker reruns this module
and compares against the frozen fixture.
"""
M = (1 << 64) - 1
CONST_A = 0x9E3779B97F4A7C15
CONST_B = 0xC2B2AE3D27D4EB4F
CONST_MIX_SEED = 0xA24BAED5
CONST_HI = 0x517CC1B727220A95
CONST_LO = 0x4CF8E355


def rotl64(x, k):
    return ((x << k) | (x >> (64 - k))) & M


def mix(a, b):
    a = (a + b) & M
    a = rotl64(a, 23)
    a = (a + b + CONST_A) & M
    b = (b + a) & M
    b = rotl64(b, 45)
    b = (b + a + CONST_B) & M
    a = (a + rotl64(b, 31)) & M
    return a, b


def derive(seed, slot, width, height, stride):
    hi, lo = mix(seed, ((slot << 32) & M) ^ CONST_MIX_SEED)
    hi = (hi ^ seed ^ CONST_HI ^ (width << 16) ^ height) & M
    lo = (lo + seed + (stride << 3)) & M
    lo = lo ^ CONST_LO ^ ((slot * 0x9E3779B9) & M)
    return mix(hi, lo)


def apply_v1(seed, slot, width, height, stride, src):
    """src/bytearray in, returns new bytes; never mutates src (immutable source)."""
    data = bytes(src)  # snapshot: the reference reads only this copy
    out = bytearray(data)
    hi, lo = derive(seed, slot, width, height, stride)
    for row in range(height):
        base = row * stride
        for col in range(width):
            p = base + col * 4
            hi, lo = mix(hi, lo)
            v1 = hi ^ lo
            hi, lo = mix(hi, lo)
            v2 = hi ^ lo
            if data[p + 3] == 0:
                continue  # fully transparent: all four bytes preserved
            d = (v1 % 27) - 13  # signed delta in [-13,13]
            alpha = data[p + 3]
            k = ((v2 & 0xFF) * alpha) // 255
            for c in range(3):
                value = data[p + c] + d * k
                out[p + c] = 0 if value < 0 else (255 if value > 255 else value)
            out[p + 3] = data[p + 3]
    return bytes(out)


def src_bytes(length, salt):
    """Deterministic pseudo-random input, independent of the algorithm."""
    state = (salt * 0x9E3779B97F4A7C15 + 0xBF58476D1CE4E5B9) & M
    out = bytearray(length)
    for i in range(length):
        state ^= (state << 13) & M
        state ^= state >> 7
        state ^= (state << 17) & M
        state &= M
        out[i] = state & 0xFF
    return out


# (name, seed, slot, width, height, stride, src_generator)
VECTORS = [
    ("v1_1x1", 0x0123456789ABCDEF, 0, 1, 1, 4, 1),
    ("v2_2x2", 0xFEDCBA9876543210, 1, 2, 2, 8, 2),
    ("v3_3x2_pad", 0x0011223344556677, 2, 3, 2, 16, 3),
    ("v4_4x1_alpha", 0xFFFFFFFFFFFFFFFF, 2, 4, 1, 16, 4),
    ("v5_16x16", 0x0809_0A0B_0C0D_0E0F, 3, 16, 16, 64, 5),
]


def build_cases():
    """Yield (name, seed, slot, w, h, stride, src_bytes, expected_bytes)."""
    for name, seed, slot, w, h, stride, salt in VECTORS:
        src = src_bytes(stride * h, salt)
        if name == "v3_3x2_pad":
            for row in range(h):  # padding bytes must be zero, not noise
                for b in range(w * 4, stride):
                    src[row * stride + b] = 0
        if name == "v4_4x1_alpha":
            src[3] = 0            # fully transparent pixel
            src[7] = 0            # fully transparent pixel
            src[11] = 128         # partial alpha near saturated blue
            src[10] = 250
            src[15] = 255         # opaque, red near 0 => clamp-low path
            src[12] = 2
        yield name, seed, slot, w, h, stride, bytes(src), apply_v1(
            seed, slot, w, h, stride, src)


def fixture_text():
    lines = ["// Frozen canvas_noise v1 vectors from tests/canvas_ref/"
             "gen_canvas_ref.py.", "// Not produced by the C++ "
             "implementation under test.",
             "struct CanvasGoldenCase {",
             "  const char* name; std::uint64_t seed; std::uint32_t slot;",
             "  std::uint32_t width; std::uint32_t height;",
             "  std::uint64_t stride; std::size_t size;",
             "  const std::uint8_t* src; const std::uint8_t* expected;",
             "};"]
    for name, seed, slot, w, h, stride, src, expected in build_cases():
        lines.append(f"inline constexpr std::uint8_t kSrc_{name}[] = {{")
        lines.append(",\n    ".join(
            ",".join(str(b) for b in src[i:i + 8])
            for i in range(0, len(src), 8)))
        lines.append("};")
        lines.append(f"inline constexpr std::uint8_t kExp_{name}[] = {{")
        lines.append(",\n    ".join(
            ",".join(str(b) for b in expected[i:i + 8])
            for i in range(0, len(expected), 8)))
        lines.append("};")
        lines.append("inline constexpr CanvasGoldenCase kCase_{} = {{".format(name))
        lines.append('    "{}", {}ULL, {}U, {}U, {}U, {}ULL, {}U,'.format(
            name, seed, slot, w, h, stride, len(src)))
        lines.append("    kSrc_{}, kExp_{}}};".format(name, name))
    lines.append("inline constexpr const CanvasGoldenCase* kGoldenCases[] = {")
    lines.append(", ".join(f"&kCase_{n}" for n, *_ in VECTORS) + "};")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    import sys
    sys.stdout.write(fixture_text())
