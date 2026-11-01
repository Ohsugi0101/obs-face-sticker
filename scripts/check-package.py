#!/usr/bin/env python3
"""Validate packaged Windows ABI, required exports, runtime imports and model integrity."""
import hashlib
import struct
import sys
import zipfile
from pathlib import PurePosixPath

def inspect_pe(data):
    if data[:2] != b'MZ':
        raise ValueError('Missing Windows executable header')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Missing PE signature')
    machine, count = struct.unpack_from('<HH', data, pe+4)
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    optional = pe+24
    if machine != 0x8664 or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('DLL is not Windows x64 PE32+')
    characteristics = struct.unpack_from('<H', data, pe+22)[0]
    if not characteristics & 0x2000:
        raise ValueError('PE file is not marked as a DLL')
    sections = []
    for i in range(count):
        section = optional+optional_size+i*40
        virtual_size, address, raw_size, raw_offset = struct.unpack_from('<IIII', data, section+8)
        sections.append((address, max(virtual_size, raw_size), raw_offset))
    def offset(rva):
        for address, size, raw in sections:
            if address <= rva < address+size:
                return raw+rva-address
        raise ValueError(f'Unmapped RVA {rva:x}')
    def string(rva):
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii')
    export_rva = struct.unpack_from('<I', data, optional+112)[0]
    exports = set()
    if export_rva:
        export = offset(export_rva)
        names = struct.unpack_from('<I', data, export+24)[0]
        array = offset(struct.unpack_from('<I', data, export+32)[0])
        exports = {string(struct.unpack_from('<I', data, array+i*4)[0]) for i in range(names)}
    import_rva = struct.unpack_from('<I', data, optional+120)[0]
    imports = set()
    if import_rva:
        entry = offset(import_rva)
        while any(data[entry:entry+20]):
            imports.add(string(struct.unpack_from('<I', data, entry+12)[0]))
            entry += 20
    return exports, imports

def main(path):
    prefix = 'face-png-filter/'
    with zipfile.ZipFile(path) as archive:
        if archive.testzip():
            raise ValueError('ZIP CRC validation failed')
        for name in archive.namelist():
            normalized = PurePosixPath(name.replace('\\', '/'))
            if normalized.is_absolute() or '..' in normalized.parts:
                raise ValueError(f'Unsafe archive path: {name}')
        required = [prefix+'bin/64bit/face-png-filter.dll', prefix+'data/overlay.effect',
                    prefix+'data/locale/ja-JP.ini', prefix+'data/licenses/YuNet-LICENSE',
                    prefix+'data/licenses/GPL-3.0.txt', 'install-windows.ps1']
        for name in required:
            archive.getinfo(name)
        model = archive.read(prefix+'data/models/face_detection_yunet_2023mar.onnx')
        if hashlib.sha256(model).hexdigest() != '8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4':
            raise ValueError('Unexpected YuNet model hash')
        exports, imports = inspect_pe(archive.read(required[0]))
        missing = {'obs_module_load', 'obs_module_set_pointer', 'obs_module_ver'} - exports
        if missing:
            raise ValueError(f'Missing OBS module exports: {sorted(missing)}')
        unexpected = [name for name in imports if name.lower().startswith(('opencv', 'libgcc', 'libstdc++'))]
        if unexpected:
            raise ValueError(f'Unpackaged runtime dependencies: {unexpected}')
        print('Validated: Windows x64 DLL, OBS exports, ZIP contents, model SHA-256')
        print('DLL imports: '+', '.join(sorted(imports)))
        print('OBS exports: '+', '.join(sorted(name for name in exports if name.startswith('obs_module'))))
if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: check-package.py Windows-binary.zip')
    main(sys.argv[1])
