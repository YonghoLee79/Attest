"""USB 포트를 훑어 아두이노 1번/2번을 찾는다.

/dev/ttyACM0, /dev/ttyACM1 의 순서는 부팅마다 바뀔 수 있으므로
PING 응답(PONG ARD1 / PONG ARD2)으로 어느 보드인지 판별한다.
"""
import glob
import time

from .link import ArduinoLink, DeviceError, DeviceTimeout

BAUD = 115200


def candidate_ports():
    return sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))


def open_link(port, boot_wait=3.0):
    import serial  # pyserial: 라즈베리파이에서만 필요

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
    for port in ports or candidate_ports():
        try:
            link = open_link(port)
        except Exception as exc:  # 권한 문제, 다른 프로그램이 사용 중 등
            print(f"  [!] {port} 열기 실패: {exc}")
            continue
        dev_id = identify(link)
        if dev_id in ("ARD1", "ARD2") and dev_id not in found:
            link.name = f"{dev_id}({port})"
            found[dev_id] = link
            print(f"  [+] {port} -> {dev_id}")
        else:
            print(f"  [?] {port} 응답: {dev_id}")
            link.close()
    return found
