#!/usr/bin/env python3
"""Fill i18n/shady_da.ts from i18n/da.py. Fails if a string has no Danish."""
import sys, xml.etree.ElementTree as ET, os
sys.path.insert(0, os.path.dirname(__file__))
from da import DA
path = os.path.join(os.path.dirname(__file__), 'shady_da.ts')
tree = ET.parse(path)
missing = []
for m in tree.iter('message'):
    src = m.find('source').text
    tr = m.find('translation')
    for c in list(tr): tr.remove(c)
    tr.text = None
    tr.attrib.pop('type', None)
    if src not in DA:
        missing.append(src); tr.set('type', 'unfinished'); continue
    v = DA[src]
    if m.get('numerus') == 'yes':
        forms = v if isinstance(v, tuple) else (v, v)
        for f in forms:
            ET.SubElement(tr, 'numerusform').text = f
    else:
        tr.text = v
tree.write(path, encoding='utf-8', xml_declaration=True)
if missing:
    print('missing Danish for:', *missing, sep='\n  ')
    sys.exit(1)
