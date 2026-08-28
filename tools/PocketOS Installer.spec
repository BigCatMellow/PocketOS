# -*- mode: python ; coding: utf-8 -*-

from pathlib import Path

project = Path(SPECPATH).parent
payload = project / 'payload'

a = Analysis(
    [str(project / 'tools' / 'installer.py')],
    pathex=[str(project / 'tools')],
    binaries=[],
    datas=[
        (str(payload / '.tmp_update' / 'bin' / 'pocketOS'), 'payload/.tmp_update/bin'),
        (str(payload / '.tmp_update' / 'res' / 'pocketos'), 'payload/.tmp_update/res/pocketos'),
    ],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='PocketOS Installer',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='PocketOS Installer',
)
