#!/usr/bin/env python3
"""Export the saved RevU native meshes in the documented print orientations.

Requires NumPy. Writes only to build/formwork/RevU (or --output).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'formwork/RevU'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/formwork/RevU')
    args = parser.parse_args()
    output = args.output.resolve()
    if output == SOURCE.resolve() or SOURCE.resolve() in output.parents:
        parser.error('Choose a build output directory; preserve the published originals.')
    doc = json.loads((SOURCE / 'SpaceSmart_RevU.formwork').read_text())
    parts = json.loads((SOURCE / 'print-parts.json').read_text())
    objects = {obj['id']: obj for obj in doc['objects']}
    for part in parts:
        obj = objects[part['id']]
        vertices = np.array(obj['mesh']['vertices'], dtype=float)
        faces = np.array([[face[0], face[i], face[i+1]] for face in obj['mesh']['faces'] for i in range(1, len(face)-1)])
        angle = math.radians(part['rotationX'])
        rotation = np.array([[1, 0, 0], [0, math.cos(angle), -math.sin(angle)], [0, math.sin(angle), math.cos(angle)]])
        vertices = vertices @ rotation.T
        vertices -= np.array([(vertices[:,0].min()+vertices[:,0].max())/2, (vertices[:,1].min()+vertices[:,1].max())/2, vertices[:,2].min()])
        triangles = vertices[faces]
        normals = np.cross(triangles[:,1]-triangles[:,0], triangles[:,2]-triangles[:,0])
        normals /= np.maximum(np.linalg.norm(normals, axis=1)[:,None], 1e-15)
        relative = Path(part['relativePath'])
        if relative.is_absolute() or '..' in relative.parts: raise ValueError('Invalid part path')
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('wb') as stream:
            stream.write(b'Formwork SpaceSmart RevU / mm'.ljust(80,b' '))
            stream.write(struct.pack('<I', len(faces)))
            for normal, triangle in zip(normals, triangles):
                stream.write(struct.pack('<12fH', *normal, *triangle.flatten(), 0))
        part['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        part['triangles'] = len(faces)
        part['printBounds'] = np.ptp(vertices, axis=0).round(4).tolist()
        print(relative, len(faces), 'triangles')
    (output / 'print-parts.json').write_text(json.dumps(parts, ensure_ascii=False, indent=2)+'\n')

if __name__ == '__main__': main()
