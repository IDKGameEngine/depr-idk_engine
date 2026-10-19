"""Regenerate the small, original glTF fixtures and demo model. Python stdlib only."""
import copy
import json
from pathlib import Path
import struct
import zlib

HERE = Path(__file__).resolve().parent
DATA = HERE / 'data'
DATA.mkdir(exist_ok=True)


def png():
    def chunk(kind, content):
        return struct.pack('>I', len(content)) + kind + content + struct.pack('>I', zlib.crc32(kind + content))
    # Top row red; bottom row blue. No image or UV flips are needed at upload.
    pixels = bytes([0, 255, 0, 0, 255, 255, 0, 0, 255,
                    0, 0, 0, 255, 255, 0, 0, 255, 255])
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b'')


def make(vertices, normals, uvs, indices, nodes):
    blob = bytearray()
    views, accessors = [], []
    for values, components, kind, fmt in [(vertices, 3, 'VEC3', 'f'), (normals, 3, 'VEC3', 'f'),
                                          (uvs, 2, 'VEC2', 'f'), (indices, 1, 'SCALAR', 'H')]:
        while len(blob) % 4:
            blob.append(0)
        packed = struct.pack('<' + fmt * len(values), *values)
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(packed)})
        accessor = {'bufferView': len(views) - 1, 'componentType': 5126 if fmt == 'f' else 5123,
                    'count': len(values) // components, 'type': kind}
        if not accessors:
            accessor['min'] = [min(values[c::3]) for c in range(3)]
            accessor['max'] = [max(values[c::3]) for c in range(3)]
        accessors.append(accessor)
        blob.extend(packed)
    doc = {'asset': {'version': '2.0', 'generator': 'idk original fixture generator'}, 'scene': 0,
           'scenes': [{'nodes': [0]}], 'nodes': nodes,
           'meshes': [{'name': 'shared mesh', 'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2},
                                                          'indices': 3, 'material': 0}]}],
           'materials': [{'name': 'checker', 'pbrMetallicRoughness': {'baseColorFactor': [0.8, 1, 1, 1],
                          'baseColorTexture': {'index': 0}, 'metallicFactor': 0.25, 'roughnessFactor': 0.75}}],
           'textures': [{'source': 0, 'sampler': 0}],
           'samplers': [{'magFilter': 9728, 'minFilter': 9728, 'wrapS': 33071, 'wrapT': 33648}],
           'images': [{'uri': 'texture%20test.png'}],
           'buffers': [{'uri': 'mesh.bin', 'byteLength': len(blob)}], 'bufferViews': views, 'accessors': accessors}
    return doc, blob


def glb(doc, blob):
    doc = copy.deepcopy(doc)
    blob = bytearray(blob)
    while len(blob) % 4:
        blob.append(0)
    doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(png())})
    doc['images'] = [{'bufferView': len(doc['bufferViews']) - 1, 'mimeType': 'image/png'}]
    blob.extend(png())
    doc['buffers'] = [{'byteLength': len(blob)}]
    js = json.dumps(doc, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    blob.extend(b'\0' * (-len(blob) % 4))
    return struct.pack('<III', 0x46546c67, 2, 12 + 8 + len(js) + 8 + len(blob)) + struct.pack('<II', len(js), 0x4e4f534a) + js + struct.pack('<II', len(blob), 0x004e4942) + blob


nodes = [{'name': 'parent', 'translation': [0.1, 0.05, 0], 'children': [1, 2]},
         {'name': 'left', 'translation': [-0.55, 0, 0], 'mesh': 0},
         {'name': 'right mirrored', 'translation': [0.35, 0, 0], 'scale': [-1, 1, 1], 'mesh': 0}]
doc, blob = make([-.18, -.18, 0, .18, -.18, 0, 0, .18, 0], [0, 0, 1] * 3,
                 [0, 0, 1, 0, .5, 1], [0, 1, 2], nodes)
(DATA / 'scene.gltf').write_text(json.dumps(doc, indent=2) + '\n')
(DATA / 'mesh.bin').write_bytes(blob)
(DATA / 'texture test.png').write_bytes(png())
(DATA / 'scene.glb').write_bytes(glb(doc, blob))
for name, field in [('required-extension', {'extensionsRequired': ['IDK_unsupported']}),
                    ('compressed', {'extensionsRequired': ['KHR_draco_mesh_compression']}),
                    ('animated', {'animations': [{}]}), ('skinned', {'skins': [{}]})]:
    bad = copy.deepcopy(doc)
    bad.update(field)
    (DATA / (name + '.gltf')).write_text(json.dumps(bad) + '\n')
bad = copy.deepcopy(doc)
bad['meshes'][0]['primitives'][0]['mode'] = 1
(DATA / 'lines.gltf').write_text(json.dumps(bad) + '\n')
bad = copy.deepcopy(doc)
bad['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['texCoord'] = 1
(DATA / 'uv1.gltf').write_text(json.dumps(bad) + '\n')
bad = copy.deepcopy(doc)
bad['images'][0]['uri'] = 'missing.png'
(DATA / 'missing-image.gltf').write_text(json.dumps(bad) + '\n')
(DATA / 'truncated.glb').write_bytes(b'glTF\x02\0\0\0\x20\0\0\0')

# A genuine 3D demo: six independent quad faces and two shared cube instances.
positions, normals, uvs, indices = [], [], [], []
faces = [([1, 0, 0], [[.5, -.5, -.5], [.5, .5, -.5], [.5, .5, .5], [.5, -.5, .5]]),
         ([-1, 0, 0], [[-.5, -.5, .5], [-.5, .5, .5], [-.5, .5, -.5], [-.5, -.5, -.5]]),
         ([0, 1, 0], [[-.5, .5, -.5], [-.5, .5, .5], [.5, .5, .5], [.5, .5, -.5]]),
         ([0, -1, 0], [[-.5, -.5, .5], [-.5, -.5, -.5], [.5, -.5, -.5], [.5, -.5, .5]]),
         ([0, 0, 1], [[-.5, -.5, .5], [.5, -.5, .5], [.5, .5, .5], [-.5, .5, .5]]),
         ([0, 0, -1], [[.5, -.5, -.5], [-.5, -.5, -.5], [-.5, .5, -.5], [.5, .5, -.5]])]
for normal, corners in faces:
    first = len(positions) // 3
    positions.extend(x for p in corners for x in p)
    normals.extend(normal * 4)
    uvs.extend([0, 1, 1, 1, 1, 0, 0, 0])
    indices.extend(first + x for x in [0, 1, 2, 0, 2, 3])
cube_nodes = [{'name': 'cubes', 'children': [1, 2]}, {'name': 'left cube', 'translation': [-.8, 0, 0], 'mesh': 0},
              {'name': 'mirrored cube', 'translation': [.8, 0, 0], 'scale': [-1, 1, 1], 'mesh': 0}]
cube, cube_blob = make(positions, normals, uvs, indices, cube_nodes)
out = HERE.parent / 'examples'
out.mkdir(parents=True, exist_ok=True)
(out / 'instanced-cubes.glb').write_bytes(glb(cube, cube_blob))
