#!/bin/zsh
set -eu
spacesmart_root=${0:A:h}
print 'Codex에서 /hooks 를 입력해 새 SpaceSmart 훅을 검토하고 신뢰로 설정하세요.'
print '대상 명령: /usr/bin/python3 .../spacesmart/macos/legacy/agent_events.py codex'
print '이 훅은 작업 이벤트와 프로젝트 이름만 로컬에 기록하며 승인·실행 판단을 변경하지 않습니다.'
print '완료 후 Ctrl+C 또는 /quit 로 종료하세요. 신뢰 설정은 데스크톱 앱과 공유됩니다.'
exec '/Applications/ChatGPT.app/Contents/Resources/codex-cli/bin/codex' --no-alt-screen -C "$spacesmart_root/../.."
