#!/usr/bin/env python3
"""Package the universal app and tracked source tree, excluding local runtime data."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import plistlib
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'build/macos/SpaceSmart.app'
GUIDE = ROOT / 'docs/SpaceSmart-설치가이드.pdf'
OUT = ROOT / 'dist'

def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def source_files():
    if (ROOT / '.git').exists():
        result = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z'])
        names = result.decode().strip('\0').split('\0')
    else:
        names = (ROOT / 'SOURCE_MANIFEST.txt').read_text().splitlines()
    allowed_roots = {'Arduino', 'formwork', 'macos', 'docs'}
    allowed_files = {'README.md', 'LICENSE', '.gitignore', '.gitattributes'}
    excluded = {'backups', 'state', 'runtime', '__pycache__', 'bin', '.venv', '.DS_Store'}
    files = []
    for name in sorted(set(names)):
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts: raise ValueError('Invalid manifest path')
        if not relative.parts: continue
        if relative.parts[0] not in allowed_roots and name not in allowed_files: continue
        if any(part in excluded for part in relative.parts): raise ValueError('Private/generated path: '+name)
        if relative.suffix in {'.log', '.dmg', '.zip', '.pem', '.key', '.p12', '.bin', '.elf'}: raise ValueError('Unexpected source artifact: '+name)
        path = ROOT / name
        if path.is_symlink() or not path.is_file(): raise ValueError('Missing or linked source: '+name)
        if relative.suffix in {'.swift', '.py', '.sh', '.command', '.txt', '.md', '.json', '.h', '.cpp', '.ino', '.formwork'}:
            if (str(Path.home())+'/').encode() in path.read_bytes(): raise ValueError('Local home path: '+name)
        files.append(path)
    if not (ROOT / 'macos/AppMain.swift') in files: raise ValueError('Add all source files to Git before packaging')
    return files

def main():
    version = plistlib.loads((ROOT / 'macos/Info.plist').read_bytes())['CFBundleShortVersionString']
    assert APP.is_dir() and GUIDE.is_file(), 'Run macos/build.sh first'
    assert plistlib.loads((APP / 'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'] == version
    files = source_files()
    shutil.copy2(GUIDE, APP / 'Contents/Resources' / GUIDE.name)
    run('codesign', '--force', '--sign', '-', APP)
    run('codesign', '--verify', '--deep', '--strict', APP)
    for binary in ['Contents/MacOS/SpaceSmart', 'Contents/Resources/SpaceSmartAgent']:
        run('lipo', APP / binary, '-verify_arch', 'arm64', 'x86_64')
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / f'SpaceSmart-Source-{version}.zip'
    manifest = '\n'.join(str(path.relative_to(ROOT)) for path in files)+'\n'
    with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in files: archive.write(path, 'spacesmart/'+str(path.relative_to(ROOT)))
        archive.writestr('spacesmart/SOURCE_MANIFEST.txt', manifest)
    with zipfile.ZipFile(source) as archive: assert archive.testzip() is None
    readme = OUT / '먼저 읽어주세요.txt'
    readme.write_text(f'''SpaceSmart for Mac {version} / Arduino v2.3 / 케이스 RevU

1. SpaceSmart.app을 Applications 폴더로 드래그합니다.
2. 복사한 앱을 실행하고 Bluetooth 사용을 허용합니다.
3. 디바이스 전원을 켜고 약 10초 뒤 개요에서 내 기기를 선택해 연결합니다.

macOS 13 이상, Apple Silicon / Intel 공용입니다.
Python·Xcode·Arduino는 앱 실행에 필요 없습니다.
앱 설치는 ESP32에 펌웨어를 업로드하지 않습니다. 조명 기능은 v2.3 이상이 필요합니다.

왼쪽 개요 / 조명 / 커스텀 / AI 연동 / 설정에서 이동합니다.
Claude는 Code 탭을 지원합니다. Codex는 훅 설치 후 /hooks에서 직접 신뢰 설정하세요.
창을 닫아도 메뉴 막대에서 실행됩니다. 종료는 메뉴 막대 또는 Command+Q입니다.

이 배포본은 ad-hoc 서명이며 Apple 공증을 받지 않았습니다.
출처를 확인한 뒤 차단되면 시스템 설정 → 개인정보 보호 및 보안 → 확인 없이 열기를 사용하세요.

조명 RF 동작과 현장 자동 조절은 실물 검증 전입니다.
설치 PDF와 소스의 Arduino/README.md, formwork/README.md, macos/README.md를 참고하세요.
https://github.com/kakao-workshop/spacesmart
''')
    info = OUT / 'release-info.json'
    info.write_text(json.dumps({'macos':version, 'firmware':'2.3', 'enclosure':'RevU', 'minimum_macos':'13.0', 'architectures':['arm64','x86_64'], 'signature':'ad-hoc', 'apple_notarized':False, 'verification':'See docs/VERIFICATION.md', 'source_files':len(files)}, ensure_ascii=False, indent=2)+'\n')
    dmg = OUT / f'SpaceSmart-Mac-{version}.dmg'
    with tempfile.TemporaryDirectory(prefix='spacesmart-release-') as temp:
        stage = Path(temp) / 'SpaceSmart'; stage.mkdir()
        run('ditto', '--norsrc', '--noextattr', APP, stage / APP.name)
        (stage / 'Applications').symlink_to('/Applications', target_is_directory=True)
        for file in [GUIDE, source, readme, info]: shutil.copy2(file, stage / file.name)
        run('hdiutil', 'create', '-volname', 'SpaceSmart', '-srcfolder', stage, '-ov', '-format', 'UDZO', dmg)
    shutil.copy2(GUIDE, OUT / GUIDE.name)
    checks = [dmg, source, OUT / GUIDE.name]
    (OUT / 'SHA256SUMS.txt').write_text(''.join(sha(path)+'  '+path.name+'\n' for path in checks))
    print(json.dumps({path.name:{'bytes':path.stat().st_size, 'sha256':sha(path)} for path in checks}, indent=2))

if __name__ == '__main__': main()
