"""Developer-only: regenerate asset-free format schemas and supported-file hashes.

Usage: python prepare_metadata.py INVADER ORIGINAL_MAPS VERIFIED_MAPS
No game bytes, strings, images or relocation records are copied into the tool.
"""
import hashlib, json, pathlib, sys
root = pathlib.Path(__file__).resolve().parents[1]
from relocate_cache import Schemas
invader, original, verified = map(pathlib.Path, sys.argv[1:])
schemas = Schemas(invader / 'src/tag/hek/definition')
out = root / 'Resources'
out.mkdir(exist_ok=True)
types = {}
for name, fields in schemas.layouts.items():
    types[name] = {'Size': schemas.size(name), 'Fields': [
        {'Offset': offset, 'Name': field['name'], 'Type': field['type'], 'Struct': field.get('struct')}
        for offset, field in fields]}
(out/'schemas.json').write_text(json.dumps({'Types': types, 'Roots': schemas.roots}), encoding='utf-8')
titles = dict(zip('ui a10 a30 a50 b30 b40 c10 c20 c40 d20 d40 beavercreek sidewinder damnation ratrace prisoner hangemhigh chillout carousel boardingaction bloodgulch wizard putput longest'.split(),
    ['Main menu','The Pillar of Autumn','Halo','The Truth and Reconciliation','The Silent Cartographer','Assault on the Control Room','343 Guilty Spark','The Library','Two Betrayals','Keyes','The Maw','Battle Creek','Sidewinder','Damnation','Rat Race','Prisoner',"Hang Em High",'Chill Out','Derelict','Boarding Action','Blood Gulch','Wizard','Chiron TL-34','Longest']))
def sha(path):
    with path.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
catalog = {}
for name,title in titles.items():
    src, dst = original/(name+'.map'), verified/(name+'.map')
    sidecars = [verified/(name+'.nrl'), *sorted(verified.glob(name+'-bsp*.nrl'))]
    catalog[name] = {'Title': title, 'Bytes': dst.stat().st_size, 'Sha256': sha(dst),
        'SourceBytes': src.stat().st_size, 'SourceSha256': sha(src),
        'Sidecars': {p.name: sha(p) for p in sidecars}}
    print(name, flush=True)
(out/'catalog.json').write_text(json.dumps(catalog, indent=2), encoding='utf-8')
