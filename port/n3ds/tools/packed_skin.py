"""Split the exact palette-row and float weight attributes in weighted shaders."""
import re


# Count weight operands in every weighted consumer. Refuse a changed
# program until its skin inputs have been audited against the packed layout.
WEIGHT_OPERANDS = {'skin': 5, 'skin_lit': 5, 'skin_reflect': 5, 'skin_depth': 3, 'skin_chicago': 3}
WEIGHTS = re.compile(r'\bv3\.([zw])\b')


def packed_skin_source(source, name):
    if name not in WEIGHT_OPERANDS:
        return source
    if source.count('mova a0.xy, v3') != 1 or re.search(r'\bv4\b', source):
        raise ValueError(f'Unexpected packed skin input registers in {name}')
    result, count = WEIGHTS.subn(lambda operand: 'v4.x' if operand[1] == 'z' else 'v4.y', source)
    # Optional second-term MAD retains the same weight operands.
    if count != WEIGHT_OPERANDS[name] or len(re.findall(r'\bv3\b', result)) != 1:
        raise ValueError(f'Unexpected packed skin influence operands in {name}')
    return result
