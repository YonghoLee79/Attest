"""USB 포트를 훑어 아두이노 1번/2번을 찾는다.

/dev/ttyACM0, /dev/ttyACM1 (Windows 는 COM3, COM4 ...) 의 순서는 바뀔 수 있으므로
PING 응답(PONG ARD1 / PONG ARD2)으로 어느 보드인지 판별한다.
"""
import glob
import time
from concurrent.futures import ThreadPoolExecutor

from .link import ArduinoLink, DeviceError, DeviceTimeout

BAUD = 115200


def candidate_ports():
    """USB 시리얼 포트 목록 [(port, 설명)]. 리눅스/윈도우/맥 공통."""
    try:
        from serial.tools import list_ports
        ports = [(p.device, p.description) for p in list_ports.comports() if p.vid is not None]
        if ports:
            return sorted(ports)
    except ImportError:
        pass
    return [(p, "") for p in sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))]


def _keep_dtr_on_close(ser):
    """포트를 닫아도 DTR 을 유지하게 해서 다음에 열 때 아두이노가 리셋되지 않게 한다.

    돌려주는 값: 이번에 열 때 리셋이 일어났는지(HUPCL 이 켜져 있었는지).
    리눅스 tty 설정은 장치가 연결돼 있는 동안 유지되므로, 부팅/재연결 후 첫 번째만 리셋된다.
    """
    try:
        import termios
        attrs = termios.tcgetattr(ser.fd)
        was_reset = bool(attrs[2] & termios.HUPCL)
        attrs[2] &= ~termios.HUPCL
        termios.tcsetattr(ser.fd, termios.TCSANOW, attrs)
        return was_reset
    except Exception:  # Windows 등
        return True


def open_link(port, boot_wait=3.0):
    import serial  # pyserial

    ser = serial.Serial(port, BAUD, timeout=0.1)
    link = ArduinoLink(ser, name=port)
    if _keep_dtr_on_close(ser):
        # 포트를 열면 아두이노가 리셋된다 -> READY 메시지를 기다린다.
        if link.wait_for("READY", boot_wait) is None:
            time.sleep(0.5)
    return link


def _try_open(port):
    try:
        return open_link(port), None
    except Exception as exc:
        return None, exc


def identify(link, tries=3):
    for _ in range(tries):
        try:
            resp = link.command("PING", timeout=1.0)
            if resp:
                return resp[0]
        except (DeviceTimeout, DeviceError):
            time.sleep(0.3)
    return None


def sniff(link, seconds=1.5):
    """PING 을 한 번 더 보내고 돌아오는 바이트를 그대로 모은다(진단용)."""
    link.transport.write(b"PING\n")
    data = b""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        data += link.transport.readline()
    return data


def find_devices(ports=None):
    """{'ARD1': ArduinoLink, 'ARD2': ArduinoLink} 를 돌려준다(찾은 것만)."""
    found = {}
    candidates = [(p, "") for p in ports] if ports else candidate_ports()
    if not candidates:
        print("  [!] USB 시리얼 포트가 하나도 없습니다.")
        print("      - 아두이노가 이 컴퓨터에 USB 로 꽂혀 있는지 확인하세요.")
        print("      - 아두이노가 라즈베리파이에 꽂혀 있다면 라즈베리파이에서 실행해야 합니다.")
        return found

    # 포트를 열면 보드가 리셋되어 2~3초씩 걸리므로 모든 포트를 동시에 연다.
    with ThreadPoolExecutor(max_workers=len(candidates)) as pool:
        opened = list(pool.map(_try_open, [p for p, _ in candidates]))

    for (port, desc), (link, exc) in zip(candidates, opened):
        label = f"{port} ({desc})" if desc else port
        if exc is not None:  # 권한 문제, 다른 프로그램이 사용 중 등
            print(f"  [!] {label} 열기 실패: {exc}")
            if "ermission" in str(exc):
                print("      -> sudo usermod -aG dialout $USER 후 다시 로그인")
            else:
                print("      -> 시리얼 모니터 등 다른 프로그램이 포트를 쓰고 있지 않은지 확인")
            continue
        dev_id = identify(link)
        if dev_id in ("ARD1", "ARD2") and dev_id not in found:
            link.name = f"{dev_id}({port})"
            found[dev_id] = link
            print(f"  [+] {label} -> {dev_id}")
        else:
            raw = sniff(link)
            print(f"  [?] {label}: PING 응답 없음({dev_id}), 받은 데이터: {raw[:80]!r}")
            if not raw:
                print("      -> 아무 데이터도 없음: 펌웨어 업로드 실패 또는 다른 프로그램이 포트 사용 중")
            elif any(b > 0x7E or (b < 0x20 and b not in b"\r\n") for b in raw):
                print("      -> 깨진 문자: 통신 속도 불일치(다른 펌웨어가 올라가 있음)")
            else:
                print("      -> 다른 펌웨어가 응답 중: 테스트 펌웨어를 다시 업로드하세요")
            link.close()
    return found
