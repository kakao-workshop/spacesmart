# 이전 개발용 브리지

현재 제품은 [네이티브 macOS 앱](../README.md)을 사용합니다. 이 폴더는 이전 Swift 브리지와 Python 이벤트 수집·훅 설치 코드를 보존합니다. 현재 앱과 동시에 연결하면 디바이스 연결을 차지할 수 있습니다.

- `SpaceSmartBridge.swift`, `Start-SpaceSmart.command`, `Stop-SpaceSmart.command`: 이전 메뉴 막대/개발 연결 실행 도구
- `bridge.py`, `requirements.txt`: Python Bluetooth 브리지
- `agent_events.py`, `install_hooks.py`, `Review-Codex-Hooks.command`: 개발용 AI 이벤트 수집과 훅 도구
- `tests/`: 이벤트와 전송 데이터 테스트

실행하면 해당 폴더 아래 `runtime`, `state`, `bin` 등이 생길 수 있습니다. 이들은 Git에서 제외되며 다른 사람에게 공유하면 안 됩니다. 기존 훅을 사용하던 사람은 현재 앱으로 전환하기 전에 자신이 설치했던 훅을 확인하세요. 새 앱은 다른 연동을 임의로 지우지 않습니다.
