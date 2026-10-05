#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Krita Mobile (unofficial fork): builds the inventory of everything a user can
# reach in Krita (actions, menus, dockers, tools, filters, generators, brush
# engines, layer/mask types, file formats). The phone UI is verified against
# this list: every row must get a "new location" and a test result.
#
# Usage: python3 mobilefork/tools/extract_inventory.py [--src .] [--out mobilefork/inventory]
# Re-run after every upstream merge and diff the CSVs to see what changed.

import argparse
import csv
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

SKIP_DIRS = ('/po/', '/tests/', '/test/', '/fixtures/', '/example/', '/.git/', '/mobilefork/')


def walk(src, exts):
    for root, dirs, files in os.walk(src):
        rel = '/' + os.path.relpath(root, src).replace(os.sep, '/') + '/'
        if any(s in rel for s in SKIP_DIRS):
            continue
        for f in files:
            if f.endswith(exts):
                yield os.path.join(root, f)


def rel(src, path):
    return os.path.relpath(path, src).replace(os.sep, '/')


def clean(text):
    if text is None:
        return ''
    return re.sub(r'\s+', ' ', text.replace('&', '')).strip()


# --------------------------------------------------------------------------
# Actions (.action files)
# --------------------------------------------------------------------------
def parse_actions(src):
    actions = {}
    for path in walk(src, ('.action',)):
        try:
            tree = ET.parse(path)
        except ET.ParseError as e:
            print(f'warning: cannot parse {path}: {e}', file=sys.stderr)
            continue
        root = tree.getroot()
        for cat in root.iter('Actions'):
            category = clean(cat.findtext('text')) or cat.get('category', '')
            for a in cat.findall('Action'):
                name = a.get('name')
                if not name:
                    continue
                entry = actions.setdefault(name, {
                    'name': name,
                    'text': clean(a.findtext('text')),
                    'tooltip': clean(a.findtext('toolTip')),
                    'category': category,
                    'shortcut': clean(a.findtext('shortcut')),
                    'checkable': clean(a.findtext('isCheckable')),
                    'source': rel(src, path),
                })
                if rel(src, path) not in entry['source']:
                    entry['source'] += ';' + rel(src, path)
    return actions


# --------------------------------------------------------------------------
# Menus and toolbars (.xmlgui / .rc)
# --------------------------------------------------------------------------
def parse_xmlgui(src):
    placement = defaultdict(list)
    ns = '{http://www.kde.org/standards/kxmlgui/1.0}'

    def tag(e):
        return e.tag.replace(ns, '')

    def visit(elem, path):
        for child in elem:
            t = tag(child)
            if t in ('Menu', 'ToolBar'):
                label = clean(child.findtext(ns + 'text')) or clean(child.findtext('text')) or child.get('name', '')
                prefix = 'Toolbar: ' if t == 'ToolBar' else ''
                visit(child, path + [prefix + label])
            elif t == 'MenuBar':
                visit(child, path)
            elif t == 'Action':
                n = child.get('name')
                if n:
                    placement[n].append(' > '.join(path) if path else '(top level)')
            elif t == 'DefineGroup' or t == 'Merge' or t == 'Separator' or t == 'text':
                continue
            else:
                visit(child, path)

    for path in walk(src, ('.xmlgui', '.rc')):
        if '/windows/' in path or path.endswith('.qrc'):
            continue
        try:
            txt = open(path, encoding='utf-8').read()
            # xmlgui files are configured by CMake; strip @VAR@ placeholders
            txt = re.sub(r'@[A-Z0-9_]+@', '0', txt)
            root = ET.fromstring(txt)
        except (ET.ParseError, UnicodeDecodeError):
            continue
        visit(root, [])
    return placement


# --------------------------------------------------------------------------
# Actions created in C++ without an .action entry
# --------------------------------------------------------------------------
CODE_ACTION_RE = re.compile(
    r'(?:createAction|createStandardAction|addAction|actionByName|action)\s*\(\s*"([a-zA-Z0-9_]+)"')
KSTD_RE = re.compile(r'KStandardAction::(\w+)\s*\(')


def parse_code_actions(src, known):
    found = defaultdict(set)
    std = defaultdict(set)
    for path in walk(src, ('.cpp', '.cc', '.h')):
        if '/libs/' not in path and '/plugins/' not in path and '/krita/' not in path:
            continue
        try:
            txt = open(path, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for m in CODE_ACTION_RE.finditer(txt):
            name = m.group(1)
            if name not in known and '_' in name:
                found[name].add(rel(src, path))
        for m in KSTD_RE.finditer(txt):
            std[m.group(1)].add(rel(src, path))
    return found, std


# --------------------------------------------------------------------------
# Plugin registries
# --------------------------------------------------------------------------
def grep_files(src, subdir, exts, regex, flags=0):
    rx = re.compile(regex, flags)
    out = []
    base = os.path.join(src, subdir)
    for path in walk(base, exts):
        try:
            txt = open(path, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for m in rx.finditer(txt):
            out.append((m, rel(src, path), txt))
    return out


def parse_dockers(src):
    res = {}
    for m, path, txt in grep_files(src, '', ('.cpp', '.h'),
                                   r'QString\s+id\(\)\s*const\s*(?:override)?\s*\{\s*return\s+(?:QString\s*\(\s*)?(?:QLatin1String\s*\()?\s*"([^"]+)"'):
        if 'DockFactory' not in txt and 'KoDockFactoryBase' not in txt:
            continue
        did = m.group(1)
        # Best effort title: setWindowTitle(i18n("...")) in the same plugin dir
        title = ''
        d = os.path.dirname(os.path.join(src, path))
        for f in os.listdir(d):
            if f.endswith(('.cpp', '.cc')):
                t = open(os.path.join(d, f), encoding='utf-8', errors='replace').read()
                mt = re.search(r'setWindowTitle\(\s*i18n[c]?\(\s*(?:"[^"]*"\s*,\s*)?"([^"]+)"', t)
                if mt:
                    title = mt.group(1)
                    break
        res[did] = {'id': did, 'title': title, 'source': path}
    return res


def parse_tools(src):
    res = {}
    for m, path, txt in grep_files(src, '', ('.cpp', '.h', '.cc'),
                                   r'\b(?:Ko|Kis)\w*FactoryBase\s*\(\s*(?:QLatin1String\s*\(\s*)?"([^"]+)"'):
        tid = m.group(1)
        # tooltip set after this factory id in the same file
        mt = re.search(r'setToolTip\s*\(\s*i18n[c]?\(\s*(?:"[^"]*"\s*,\s*)?"([^"]+)"', txt[m.end():])
        if tid in ('MyTool', 'MyShape', 'KisShapeSelection'):
            continue
        res[tid] = {'id': tid, 'title': mt.group(1) if mt else '', 'source': path}
    res.setdefault('InteractionTool', {'id': 'InteractionTool (KoInteractionTool_ID)', 'title': 'Shape Selection Tool', 'source': 'plugins/tools/defaulttool'})
    return res


def parse_koids(src, subdir, kind):
    res = {}
    for m, path, txt in grep_files(src, subdir, ('.cpp', '.h', '.cc'),
                                   r'KoID\(\s*"([^"]+)"\s*,\s*(?:ki18n|i18n[c]?)\(\s*(?:"[^"]*"\s*,\s*)?"([^"]+)"'):
        res.setdefault(m.group(1), {'id': m.group(1), 'title': m.group(2), 'kind': kind, 'source': path})
    return res


def parse_paintops(src):
    res = {}
    rx = r'Factory<[^>]*>\s*\(\s*"([^"]+)"\s*,\s*(?:i18n[c]?|ki18n)\(\s*(?:"[^"]*"\s*,\s*)?"([^"]+)"'
    for m, path, txt in grep_files(src, 'plugins/paintops', ('.cpp', '.cc', '.h'), rx, re.S):
        res[m.group(1)] = {'id': m.group(1), 'title': m.group(2), 'source': path}
    # factories with their own id() (mypaint, default paintops)
    for m, path, txt in grep_files(src, 'plugins/paintops', ('.cpp', '.cc', '.h'),
                                   r'QString\s+\w*::?id\(\)\s*const\s*(?:override)?\s*\{\s*return\s+"([^"]+)"'):
        res.setdefault(m.group(1), {'id': m.group(1), 'title': '', 'source': path})
    return res


def parse_impex(src):
    res = []
    for path in walk(os.path.join(src, 'plugins/impex'), ('.json',)):
        try:
            j = json.load(open(path, encoding='utf-8'))
        except Exception:
            continue
        if 'Krita/FileFilter' not in json.dumps(j.get('X-KDE-ServiceTypes', '')):
            continue
        direction = 'export' if 'X-KDE-Export' in j else 'import'
        mimes = j.get('X-KDE-Export') or j.get('X-KDE-Import') or ''
        res.append({'id': j.get('Id', ''), 'direction': direction, 'mimetypes': mimes,
                    'extensions': j.get('X-KDE-Extensions', ''), 'source': rel(src, path)})
    return res


def parse_layer_types(src):
    # Layer/mask creation is exposed as actions; list node classes for coverage
    res = {}
    for m, path, txt in grep_files(src, 'libs', ('.h',),
                                   r'class\s+KRITA\w*_EXPORT\s+(Kis\w*(?:Layer|Mask))\s*:\s*public'):
        res[m.group(1)] = {'id': m.group(1), 'source': path}
    return res


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default='.')
    ap.add_argument('--out', default='mobilefork/inventory')
    a = ap.parse_args()
    src = os.path.abspath(a.src)
    os.makedirs(a.out, exist_ok=True)

    actions = parse_actions(src)
    placement = parse_xmlgui(src)
    code_actions, std_actions = parse_code_actions(src, actions)

    rows = []
    for name, e in sorted(actions.items(), key=lambda kv: (kv[1]['category'], kv[0])):
        rows.append({**e, 'menu': ' | '.join(sorted(set(placement.get(name, [])))),
                     'origin': 'action-file', 'new_location': '', 'tested': ''})
    for name, files in sorted(code_actions.items()):
        rows.append({'name': name, 'text': '', 'tooltip': '', 'category': '(code-defined)',
                     'shortcut': '', 'checkable': '', 'source': ';'.join(sorted(files))[:300],
                     'menu': ' | '.join(sorted(set(placement.get(name, [])))),
                     'origin': 'code', 'new_location': '', 'tested': ''})
    for name, files in sorted(std_actions.items()):
        rows.append({'name': 'KStandardAction::' + name, 'text': '', 'tooltip': '',
                     'category': '(KStandardAction)', 'shortcut': '(standard)', 'checkable': '',
                     'source': ';'.join(sorted(files))[:300], 'menu': '',
                     'origin': 'kstandardaction', 'new_location': '', 'tested': ''})

    fields = ['origin', 'category', 'name', 'text', 'tooltip', 'shortcut', 'checkable',
              'menu', 'source', 'new_location', 'tested']
    with open(os.path.join(a.out, 'actions.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, '') for k in fields})

    def dump(name, items, fields):
        with open(os.path.join(a.out, name), 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=fields + ['new_location', 'tested'])
            w.writeheader()
            for it in items:
                w.writerow({k: it.get(k, '') for k in fields})

    dockers = parse_dockers(src)
    tools = parse_tools(src)
    filters = parse_koids(src, 'plugins/filters', 'filter')
    filters.update({k: v for k, v in parse_koids(src, 'libs/image/filter', 'filter').items() if k not in filters})
    generators = parse_koids(src, 'plugins/generators', 'generator')
    paintops = parse_paintops(src)
    impex = parse_impex(src)
    layers = parse_layer_types(src)

    dump('dockers.csv', sorted(dockers.values(), key=lambda d: d['id']), ['id', 'title', 'source'])
    dump('tools.csv', sorted(tools.values(), key=lambda d: d['id']), ['id', 'title', 'source'])
    dump('filters.csv', sorted(filters.values(), key=lambda d: d['id']), ['id', 'title', 'kind', 'source'])
    dump('generators.csv', sorted(generators.values(), key=lambda d: d['id']), ['id', 'title', 'kind', 'source'])
    dump('brush_engines.csv', sorted(paintops.values(), key=lambda d: d['id']), ['id', 'title', 'source'])
    dump('file_formats.csv', sorted(impex, key=lambda d: (d['direction'], d['id'])),
         ['id', 'direction', 'mimetypes', 'extensions', 'source'])
    dump('layer_and_mask_types.csv', sorted(layers.values(), key=lambda d: d['id']), ['id', 'source'])

    in_menu = sum(1 for r in rows if r['menu'])
    summary = {
        'actions_from_action_files': len(actions),
        'actions_defined_only_in_code': len(code_actions),
        'kstandard_actions': len(std_actions),
        'actions_placed_in_menus_or_toolbars': in_menu,
        'dockers': len(dockers),
        'tools': len(tools),
        'filters': len(filters),
        'generators': len(generators),
        'brush_engines': len(paintops),
        'file_filters': len(impex),
        'layer_and_mask_classes': len(layers),
    }
    json.dump(summary, open(os.path.join(a.out, 'summary.json'), 'w'), indent=2)
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
