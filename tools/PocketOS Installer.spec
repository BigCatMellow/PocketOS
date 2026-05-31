# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['installer.py'],
    pathex=[],
    binaries=[],
    datas=[
        ('../release/pocketOS-v1.0/.tmp_update/bin/pocketOS', '.tmp_update/bin'),
        ('../release/pocketOS-v1.0/.tmp_update/res/pocketos', '.tmp_update/res/pocketos'),
        ('openvgdb.sqlite', '.'),
        ('fix_unsorted.py', '.'),
        ('ui', 'ui'),
    ],
    hiddenimports=['webview', 'webview.platforms.gtk'],
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
    a.binaries,
    a.datas,
    [],
    name='PocketOS Installer',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
