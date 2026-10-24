"""아두이노에 명령을 직접 보낸다.

  python3 -m hwtest.send ARD1 "LED FILL 255 0 0"            # 한 번 보내고 종료
  python3 -m hwtest.send ARD2 "SERVO 0" "SERVO 180"          # 여러 개 연달아
  python3 -m hwtest.send                                     # 대화형 콘솔

대화형 콘솔에서는 "ARD1 LED FILL 0 0 255" 처럼 보드 이름 + 명령을 입력한다.
보드 이름을 빼면 직전에 쓴 보드로 보낸다. 종료는 quit 또는 Ctrl+D.
"""
import sys

from .discovery import find_devices
from .link import DeviceError, DeviceTimeout

BOARDS = ("ARD1", "ARD2")


def normalize(cmd):
    """명령어는 대문자로, LCD PRINT 의 글자는 입력한 그대로."""
    parts = cmd.split(" ", 3)
    if len(parts) == 4 and parts[0].upper() == "LCD" and parts[1].upper() == "PRINT":
        return f"LCD PRINT {parts[2]} {parts[3]}"
    return cmd.upper()


def send(links, board, cmd):
    if board not in links:
        print(f"ERR {board} 미연결")
        return False
    # 스테핑 모터 이동은 오래 걸리므로 넉넉히 기다린다.
    cmd = normalize(cmd)
    timeout = 30 if cmd.startswith("STEP MOVE") else 3
    try:
        resp = links[board].command(cmd, timeout=timeout)
        print(" ".join(["OK"] + resp) if resp else "OK")
        return True
    except (DeviceError, DeviceTimeout) as e:
        print(e)
        return False


def console(links):
    print("보드 이름 + 명령 입력 (예: ARD1 LED FILL 255 0 0). 종료: quit")
    board = "ARD1" if "ARD1" in links else "ARD2"
    while True:
        try:
            line = input(f"{board}> ").strip()
        except EOFError:
            break
        if not line:
            continue
        if line.lower() in ("quit", "exit", "q"):
            break
        head, _, rest = line.partition(" ")
        if head.upper() in BOARDS:
            board = head.upper()
            if not rest:
                continue
            line = rest
        send(links, board, line)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    links = find_devices()
    try:
        if not argv:
            console(links)
            return 0
        board = argv[0].upper()
        if board not in BOARDS or len(argv) < 2:
            print(__doc__)
            return 2
        ok = all([send(links, board, c) for c in argv[1:]])
        return 0 if ok else 1
    finally:
        for link in links.values():
            link.close()


if __name__ == "__main__":
    sys.exit(main())
