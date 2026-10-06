"""Independent exact-rational reference for float32 * 255, ties to even."""
from fractions import Fraction
from pathlib import Path
import struct

def bits(value): return struct.unpack('<I', struct.pack('<f', value))[0]
def exact(value):
    exponent = ((value >> 23) & 255) - 127
    mantissa = (value & 0x7fffff) | 0x800000
    if value == 0: return Fraction(0)
    return Fraction(mantissa, 1 << 23) * Fraction(2) ** exponent
values = {0, bits(1.0), bits(.25), bits(.5), bits(.75)}
for step in range(255):
    boundary = bits((step + .5) / 255)
    values.update([boundary-1, boundary, boundary+1])
for step in range(256): values.add(bits(step/255))
rows=[]
for value in sorted(values):
    scaled=exact(value)*255
    integer,remainder=divmod(scaled.numerator,scaled.denominator)
    expected=integer + (remainder*2>scaled.denominator or (remainder*2==scaled.denominator and integer%2))
    rows.append(f'    {{0x{value:08x}UL, {expected}}},')
out=Path(__file__).parent/'include/bitmap_rounding_vectors.h'
out.write_bytes(('/* Generated exact rational expectations; do not calculate with target rint. */\n'
    'static const struct { unsigned long bits, expected; } bitmap_rounding_vectors[] = {\n'
    +'\n'.join(rows)+'\n};\n').encode())
print(len(rows), 'reference vectors')
