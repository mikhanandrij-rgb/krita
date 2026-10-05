#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Krita Mobile (unofficial fork): extracts the phone interface's strings and
# writes po/<lang>/kritamobileui.po from mobilefork/i18n/translations.json.
# Krita's build compiles every po/<lang>/<domain>.po (ki18n_install), so the
# catalogs need no CMake changes. Requires xgettext and the polib module.
#
# Usage (from the repository root): python3 mobilefork/i18n/generate_po.py
# Exits with status 1 if a string is untranslated, listing it.
import glob
import json
import os
import subprocess
import sys
import tempfile

import polib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'plugins', 'extensions', 'mobileui')
LANGS = {
    'ru': ('Russian', 'nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);'),
    'uk': ('Ukrainian', 'nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);'),
}


def main():
    pot = os.path.join(tempfile.mkdtemp(), 'kritamobileui.pot')
    sources = sorted(glob.glob(os.path.join(SRC, '*.cpp')))
    subprocess.check_call(['xgettext', '--from-code=UTF-8', '-C', '--kde', '-ci18n',
                           '-ki18n:1', '-ki18nc:1c,2', '-ki18np:1,2', '-ki18ncp:1c,2,3',
                           '--package-name=kritamobileui', '-o', pot] + sources, cwd=SRC)
    template = polib.pofile(pot)
    translations = json.load(open(os.path.join(ROOT, 'mobilefork', 'i18n', 'translations.json'), encoding='utf-8'))

    missing = []
    for lang, (name, plural) in LANGS.items():
        po = polib.POFile()
        po.metadata = {
            'Project-Id-Version': 'kritamobileui',
            'Language': lang,
            'MIME-Version': '1.0',
            'Content-Type': 'text/plain; charset=UTF-8',
            'Content-Transfer-Encoding': '8bit',
            'Plural-Forms': plural,
            'X-Generator': 'mobilefork/i18n/generate_po.py',
        }
        po.header = f'{name} translation of the Krita Mobile (unofficial fork) phone interface.\n' \
                    'SPDX-License-Identifier: GPL-3.0-or-later'
        for entry in template:
            key = f'{entry.msgctxt or ""}|{entry.msgid}'
            value = translations.get(key, {}).get(lang)
            new = polib.POEntry(msgid=entry.msgid, msgctxt=entry.msgctxt, occurrences=[])
            if entry.msgid_plural:
                new.msgid_plural = entry.msgid_plural
                if isinstance(value, list) and len(value) == 3:
                    new.msgstr_plural = {0: value[0], 1: value[1], 2: value[2]}
                else:
                    new.msgstr_plural = {0: '', 1: '', 2: ''}
                    missing.append((lang, key))
            else:
                if isinstance(value, str):
                    new.msgstr = value
                else:
                    missing.append((lang, key))
            po.append(new)
        out_dir = os.path.join(ROOT, 'po', lang)
        os.makedirs(out_dir, exist_ok=True)
        po.save(os.path.join(out_dir, 'kritamobileui.po'))
        print(f'{lang}: {len(po)} strings, {len(po.translated_entries())} translated')

    for lang, key in missing:
        print(f'UNTRANSLATED [{lang}] {key}')
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
