# SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Krita Mobile (unofficial fork): writes translations.json in its compact
# one-entry-per-line format, so diffs stay small.
import json


def write(path, data):
    lines = []
    for key, value in data.items():
        lines.append('  ' + json.dumps(key, ensure_ascii=False) + ': ' + json.dumps(value, ensure_ascii=False, separators=(', ', ': ')))
    with open(path, 'w', encoding='utf-8') as f:
        f.write('{\n' + ',\n'.join(lines) + '\n}\n')


if __name__ == '__main__':
    import sys
    p = sys.argv[1]
    write(p, json.load(open(p, encoding='utf-8')))
