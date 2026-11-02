#!/bin/zsh
set -eu
spacesmart_root=${0:A:h}
spacesmart_pidfile="$spacesmart_root/runtime/bridge.pid"
if [[ -f "$spacesmart_pidfile" ]]; then
  spacesmart_pid=$(cat "$spacesmart_pidfile")
  spacesmart_command=$(/bin/ps -p "$spacesmart_pid" -o command= 2>/dev/null || true)
  if [[ "$spacesmart_command" == "$spacesmart_root/bin/SpaceSmartBridge $spacesmart_root/agent_events.py"* ]]; then
    kill "$spacesmart_pid"
  fi
  rm -f "$spacesmart_pidfile"
fi
print 'SpaceSmart 연결 프로그램을 종료했습니다.'
