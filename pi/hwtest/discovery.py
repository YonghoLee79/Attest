"""USB 포트를 훑어 아두이노 1번/2번을 찾는다.

/dev/ttyACM0, /dev/ttyACM1 (Windows 는 COM3, COM4 ...) 의 순서는 바뀔 수 있으므로
PING 응답(PONG ARD1 / PONG ARD2)으로 어느 보드인지 판별한다.
"""
import glob
import time

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


def open_link(port, boot_wait=3.0):
    import serial  # pyserial

    ser = serial.Serial(port, BAUD, timeout=0.1)
    link = ArduinoLink(ser, name=port)
    # 포트를 열면 아두이노가 리셋된다 -> READY 메시지를 기다린다.
    if link.wait_for("READY", boot_wait) is None:
        time.sleep(0.5)
    return link


def identify(link, tries=3):
    for _ in range(tries):
        try:
            resp = link.command("PING", timeout=1.0)
            if resp:
                return resp[0]
        except (DeviceTimeout, DeviceError):
            time.sleep(0.3)
    return None


def find_devices(ports=None):
    """{'ARD1': ArduinoLink, 'ARD2': ArduinoLink} 를 돌려준다(찾은 것만)."""
    found = {}
    candidates = [(p, "") for p in ports] if ports else candidate_ports()
    if not candidates:
        print("  [!] USB 시리얼 포트가 하나도 없습니다.")
        print("      - 아두이노가 이 컴퓨터에 USB 로 꽂혀 있는지 확인하세요.")
        print("      - 아두이노가 라즈베리파이에 꽂혀 있다면 라즈베리파이에서 실행해야 합니다.")
        return found

    for port, desc in candidates:
        label = f"{port} ({desc})" if desc else port
        try:
            link = open_link(port)
        except Exception as exc:  # 권한 문제, 다른 프로그램이 사용 중 등
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
            print(f"  [?] {label}: PING 응답 없음({dev_id})")
            print("      -> 이 저장소의 테스트 펌웨어(.ino)가 업로드되어 있는지 확인")
            link.close()
    return found
