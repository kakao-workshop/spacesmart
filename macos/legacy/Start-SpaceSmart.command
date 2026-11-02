#!/bin/zsh
set -eu
spacesmart_root=${0:A:h}
spacesmart_binary="$spacesmart_root/bin/SpaceSmartBridge"
spacesmart_script="$spacesmart_root/agent_events.py"
mkdir -p "$spacesmart_root/runtime" "$spacesmart_root/bin"
if [[ ! -x "$spacesmart_binary" || "$spacesmart_root/SpaceSmartBridge.swift" -nt "$spacesmart_binary" ]]; then
  /usr/bin/swiftc -O "$spacesmart_root/SpaceSmartBridge.swift" -o "$spacesmart_binary" \
    -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker "$spacesmart_root/Info.plist"
fi
spacesmart_pidfile="$spacesmart_root/runtime/bridge.pid"
if [[ -f "$spacesmart_pidfile" ]]; then
  spacesmart_pid=$(cat "$spacesmart_pidfile")
  spacesmart_command=$(/bin/ps -p "$spacesmart_pid" -o command= 2>/dev/null || true)
  if [[ "$spacesmart_command" == "$spacesmart_binary $spacesmart_script"* ]]; then
    print 'SpaceSmart 연결 프로그램이 이미 실행 중입니다.'
    exit 0
  fi
fi
/usr/bin/nohup "$spacesmart_binary" "$spacesmart_script" "$@" > "$spacesmart_root/runtime/bridge.log" 2>&1 < /dev/null &
print $! > "$spacesmart_pidfile"
print "SpaceSmart Bluetooth 연결을 시작했습니다. 로그: $spacesmart_root/runtime/bridge.log"
