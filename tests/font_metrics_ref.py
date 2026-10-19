"""Independent reference for the font-metric noise derivation.

The component under test is native/font_metrics.cc; this file re-derives the
same stream from scratch in Python so the two implementations are pinned to one
another (the C++ test asserts against the literals printed here, and the
end-to-end smoke test imports this module instead of trusting the binary).

Run directly to print the frozen vectors:

    python3 -I tests/font_metrics_ref.py
"""
MASK64 = (1 << 64) - 1


def mix(x):
    """SplitMix64 finalizer, uint64 wrapping. Mirrors fp_overrides.cc::Mix."""
    x = (x + 0x9E3779B97F4A7C15) & MASK64
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & MASK64
    return (x ^ (x >> 31)) & MASK64


# The advance is perturbed in 1/64 px steps (the quantum native widths already
# use, so the result is exactly representable), within +/-4 steps = 1/16 px per
# glyph. Chosen from the measured native distribution (native/font_probe.py):
# native fractional jitter never exceeds 0.5 units, so 4 units is 8x it and
# unambiguously detectable, while 1/16 px per glyph stays sub-pixel (report §3).
_QUANTUM = 1.0 / 64.0
_MAX_STEPS = 4


def perturb_advance(seed, advance, enabled=True):
    """Returns (status, perturbed_advance).

    status is 'disabled' when the dimension is off, the seed is zero, or the
    advance is not a positive finite number -- in every such case the native
    value is returned unchanged.
    """
    if not enabled or seed == 0 or not (advance > 0.0):
        return 'disabled', advance
    # Salt the seed with the quantized native advance so two glyphs in one
    # string get independent noise, while the same glyph in the same profile
    # always gets the same noise.
    quantized = int(round(advance / _QUANTUM))
    stream = mix(seed ^ (quantized * 0x9E3779B97F4A7C15 & MASK64))
    steps = stream % (2 * _MAX_STEPS + 1) - _MAX_STEPS  # [-8, 8]
    if steps == 0:
        return 'disabled', advance
    return 'perturbed', advance + steps * _QUANTUM


if __name__ == '__main__':
    seed = 0x0123456789ABCDEF
    print('mix(0) = 0x%016X' % mix(0))
    print('mix(1) = 0x%016X' % mix(1))
    status, value = perturb_advance(seed, 8.0)
    print('perturb(0x%X, 8.0) = (%s, %.7f)  # 1/64 steps: %d'
          % (seed, status, value, round((value - 8.0) * 64)))
