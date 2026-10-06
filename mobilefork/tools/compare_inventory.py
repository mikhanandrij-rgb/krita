#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Krita Mobile (unofficial fork): compares the action inventory extracted from
# the sources (mobilefork/inventory/actions.csv) with the runtime inventory
# written by the phone interface (runtime-inventory.csv). Every action of the
# source inventory must be reachable at runtime.
#
# Usage: compare_inventory.py <actions.csv> <runtime-inventory.csv> [--out merged.csv]
import argparse
import csv
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source')
    ap.add_argument('runtime')
    ap.add_argument('--out')
    ap.add_argument('--walk', help='walk.csv written by the automated action walk')
    a = ap.parse_args()
    walk = {}
    if a.walk:
        try:
            walk = {r['id']: r for r in csv.DictReader(open(a.walk, encoding='utf-8'))}
        except FileNotFoundError:
            print('walk.csv missing: the action walk did not run or did not finish')

    try:
        runtime = list(csv.DictReader(open(a.runtime, encoding='utf-8')))
    except FileNotFoundError:
        print('runtime inventory missing: the phone interface did not run')
        return 1
    source = list(csv.DictReader(open(a.source, encoding='utf-8')))

    rt_actions = {r['id']: r for r in runtime if r['kind'] == 'action'}
    reachable = {k for k, r in rt_actions.items() if r['reachable'] == 'yes'}
    docks = [r for r in runtime if r['kind'] == 'docker']
    tools = [r for r in runtime if r['kind'] == 'tool']

    missing = []      # in sources, not present at runtime at all
    unreachable = []  # present at runtime but not listed anywhere
    merged = []
    for row in source:
        name = row['name']
        if row['origin'] == 'kstandardaction':
            continue
        r = rt_actions.get(name)
        if r is None:
            status = 'not created at runtime'
            missing.append(row)
        elif name in reachable:
            status = 'reachable'
        else:
            status = 'present but not listed'
            unreachable.append(row)
        w = walk.get(name)
        tested = ''
        if w:
            tested = w['result'] + (f" ({w['detail']})" if w['detail'] else '')
        merged.append({'name': name, 'text': row['text'], 'category': row['category'],
                       'desktop_menu': row['menu'], 'phone_location': r['location'] if r else '',
                       'status': status, 'tested': tested})

    print(f'source actions: {len(merged)}')
    print(f'runtime actions: {len(rt_actions)}, listed in the phone interface: {len(reachable)}')
    print(f'dockers hosted: {len(docks)}, tools: {len(tools)}')
    print(f'reachable: {sum(1 for m in merged if m["status"] == "reachable")}')
    print(f'present but not listed: {len(unreachable)}')
    for row in unreachable[:200]:
        print('  UNLISTED', row['name'], '|', row['text'], '|', row['source'])
    print(f'not created at runtime (plugin not built on this platform, created lazily, or test fixture): {len(missing)}')
    for row in missing[:300]:
        print('  MISSING', row['name'], '|', row['text'], '|', row['source'])

    if walk:
        from collections import Counter
        c = Counter(w['result'] for w in walk.values())
        print('action walk: ' + ', '.join(f'{k} {v}' for k, v in sorted(c.items())))
        listed_not_walked = [m['name'] for m in merged if m['status'] == 'reachable' and m['name'] not in walk]
        print(f'listed but not reached by the walk (walk stopped early?): {len(listed_not_walked)}')
        for n in listed_not_walked[:50]:
            print('  NOT-WALKED', n)

    if a.out:
        with open(a.out, 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(merged[0].keys()) if merged else ['name'])
            w.writeheader()
            w.writerows(merged)
    return 0


if __name__ == '__main__':
    sys.exit(main())
