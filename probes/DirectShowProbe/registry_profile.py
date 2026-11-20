"""Read-only audit of the current user's PotPlayer profile against a portable INI."""
import argparse
import configparser
import ctypes
import hashlib
import json
import re
import subprocess
import tempfile
import uuid
import winreg
from collections import Counter
from pathlib import Path


def read_tree(key):
    nodes = {}

    def visit(handle, section):
        children, values, _ = winreg.QueryInfoKey(handle)
        for index in range(values):
            name, value, kind = winreg.EnumValue(handle, index)
            nodes[section, name] = kind, value
        for index in range(children):
            child = winreg.EnumKey(handle, index)
            with winreg.OpenKey(handle, child) as nested:
                visit(nested, section + '\\' + child if section else child)

    visit(key, '')
    return nodes


def installed_profile(daily):
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\DAUM\PotPlayer64') as install:
        path, kind = winreg.QueryValueEx(install, 'ProgramPath')
    if kind != winreg.REG_SZ or Path(path).resolve() != (daily / 'PotPlayerMini64.exe').resolve():
        raise ValueError('ProgramPath does not identify the requested installation')
    if (daily / 'PotPlayerMini64.ini').exists():
        raise ValueError('Adjacent INI exists; registry profile is not an established source')
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\DAUM\PotPlayerMini64') as profile:
        return read_tree(profile)


def load_ini(path):
    profile = configparser.RawConfigParser(delimiters=('=',), strict=True,
                                            empty_lines_in_values=False,
                                            default_section='__POTTRACE_NO_DEFAULTS__')
    profile.optionxform = str
    with path.open(encoding='utf-16') as stream:
        profile.read_file(stream)
    return profile


def user_sid():
    output = subprocess.check_output(['whoami.exe', '/user', '/fo', 'csv', '/nh'])
    match = re.search(rb'"(S-1-[0-9-]+)"', output)
    if not match:
        raise RuntimeError('Could not identify current Windows user')
    return match[1].decode('ascii')


def fixture_descriptor(daily, nodes):
    kind, renderer = nodes['Settings', 'VideoRen2']
    if kind != winreg.REG_DWORD:
        raise ValueError('Unknown renderer selection format')
    return dict(source_sid=user_sid(), renderer=renderer,
                hashes={name: hashlib.sha256((daily / name).read_bytes()).hexdigest()
                        for name in ('PotPlayerMini64.exe', 'PotPlayer64.dll')})


def trial(nodes):
    """Exercise a new adjacent INI in an empty directory without running PotPlayer."""
    counts = Counter()
    rows = {}
    for (section, name), (kind, value) in nodes.items():
        if not section or not name:
            counts['root_or_unnamed_unverified'] += 1
            continue
        if kind not in (winreg.REG_DWORD, winreg.REG_SZ):
            counts['binary_or_other_type_unverified'] += 1
            continue
        if not all(part.strip() == part and part for part in (section, name)) or any(
                c in section for c in '[]\r\n\0') or any(c in name for c in '=\r\n\0') or any(
                c in str(value) for c in '\r\n\0') or name.startswith((';', '#')):
            counts['ambiguous_ini_syntax'] += 1
            continue
        rows.setdefault(section, []).append((name, str(value)))

    read = ctypes.WinDLL('kernel32').GetPrivateProfileStringW
    string = ctypes.c_wchar_p
    read.argtypes = [string, string, string, ctypes.POINTER(ctypes.c_wchar), ctypes.c_uint32, string]
    read.restype = ctypes.c_uint32
    missing = uuid.uuid4().hex
    with tempfile.TemporaryDirectory(prefix='pottrace-profile-') as directory:
        path = Path(directory) / 'PotPlayerMini64.ini'
        try:
            with path.open('x', encoding='utf-16', newline='\r\n') as stream:
                for section, items in rows.items():
                    stream.write(f'[{section}]\n')
                    for name, value in items:
                        stream.write(f'{name}={value}\n')
            for section, items in rows.items():
                for name, value in items:
                    buffer = ctypes.create_unicode_buffer(max(len(value.encode('utf-16-le')) // 2,
                                                             len(missing)) + 2)
                    read(section, name, missing, buffer, len(buffer), str(path))
                    counts['win32_equal' if buffer.value == value else 'win32_different'] += 1
        finally:
            path.unlink(missing_ok=True)
        rolled_back = not path.exists()
    return dict(fields=dict(sorted(counts.items())), temporary_ini_removed=rolled_back)


def compare(nodes, profile):
    counts = Counter()
    for (section, name), (kind, value) in nodes.items():
        if not section or not name:
            counts['root_or_unnamed_unverified'] += 1
        elif not profile.has_option(section, name):
            counts['missing'] += 1
        elif kind == winreg.REG_BINARY:
            # A string comparison would not establish the player's binary decoding.
            counts['binary_unverified'] += 1
        elif kind in (winreg.REG_DWORD, winreg.REG_SZ):
            actual = profile.get(section, name)
            equal = actual == str(value)
            if kind == winreg.REG_DWORD and value >= 2**31:
                equal |= actual == str(value - 2**32)
            counts['equal' if equal else 'different'] += 1
        else:
            counts['unsupported_type'] += 1
    # An additional setting (for example an external filter) can change behavior.
    counts['ini_only'] = sum((section, name) not in nodes
                             for section in profile.sections() for name in profile[section])
    return dict(sorted(counts.items()))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('installation', type=Path)
    parser.add_argument('ini', type=Path, nargs='?', help='Compare an existing portable INI')
    parser.add_argument('--trial', action='store_true', help='Check portable field encoding in a temporary INI')
    parser.add_argument('--fixture-description', type=Path, help='Create a renderer-only test input for another Windows user')
    args = parser.parse_args()
    if not any((args.ini, args.trial, args.fixture_description)):
        parser.error('Choose a comparison, a trial, or a fixture description')
    nodes = installed_profile(args.installation)
    result = {}
    incomplete = False
    if args.ini:
        counts = compare(nodes, load_ini(args.ini))
        result['comparison'] = counts
        incomplete = any(value for key, value in counts.items() if key != 'equal')
    if args.trial:
        check = trial(nodes)
        result['trial'] = check
        incomplete |= any(value for key, value in check['fields'].items() if key != 'win32_equal')
    if args.fixture_description:
        descriptor = fixture_descriptor(args.installation, nodes)
        with args.fixture_description.open('x', encoding='utf-8') as out:
            json.dump(descriptor, out, indent=2)
    if nodes != installed_profile(args.installation):
        raise RuntimeError('Registry profile changed during the read-only audit')
    print(json.dumps(result))
    raise SystemExit(int(incomplete))
