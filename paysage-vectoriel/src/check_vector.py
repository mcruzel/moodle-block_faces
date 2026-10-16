# check_vector.py fichier.svg — vérifie qu'un SVG ne contient aucun élément matriciel
# et compte les primitives vectorielles.
import re, sys
import xml.etree.ElementTree as ET

fn = sys.argv[1]
txt = open(fn, encoding='utf-8').read()
interdits = {
    '<image': 'élément <image> (bitmap)',
    'data:': 'URI data: (contenu embarqué)',
    '<filter': 'filtre (traitement par pixels)',
    '<fe': 'primitive de filtre',
    '<pattern': 'motif <pattern>',
    '<foreignObject': 'contenu étranger (HTML)',
    '<script': 'script',
    '.png': 'référence à un fichier PNG',
    '.jpg': 'référence à un fichier JPEG',
}
problemes = [desc for k, desc in interdits.items() if k in txt]
root = ET.fromstring(txt)          # le fichier doit être du XML valide
ns = '{http://www.w3.org/2000/svg}'
tags = {}
for el in root.iter():
    t = el.tag.replace(ns, '')
    tags[t] = tags.get(t, 0) + 1
n_tri = len(re.findall(r'z', ' '.join(el.get('d', '') for el in root.iter(ns + 'path'))))
print('fichier :', fn, '(%.2f Mo)' % (len(txt.encode('utf-8')) / 1e6))
print('éléments :', ', '.join('%s=%d' % kv for kv in sorted(tags.items(), key=lambda kv: -kv[1])))
print('triangles (sous-chemins fermés) :', n_tri)
print('éléments matriciels ou filtres :', 'AUCUN' if not problemes else problemes)
sys.exit(1 if problemes else 0)
