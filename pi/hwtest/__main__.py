"""통합 테스트 실행기.

  python3 -m hwtest                      # 자동 포트 탐색, 비대화식
  python3 -m hwtest -i                   # 육안 확인 질문 포함
  python3 -m hwtest --port1 /dev/ttyACM0 --port2 /dev/ttyACM1
  python3 -m hwtest --only ARD2 통합      # 그룹 선택
  python3 -m hwtest --sim                # 하드웨어 없이 가상 보드로 실행
  python3 -m hwtest --json report.json   # 결과 저장
"""
import argparse
import datetime
import json
import sys

from . import suite
from .link import Arduino1, Arduino2, ArduinoLink


def connect(args):
    if args.sim:
        from .simulator import SimArduino1, SimArduino2
        scale = 0.05
        links = {"ARD1": ArduinoLink(SimArduino1(time_scale=scale), "ARD1(sim)"),
                 "ARD2": ArduinoLink(SimArduino2(time_scale=scale), "ARD2(sim)")}
        return links, scale

    from .discovery import find_devices
    ports = [p for p in (args.port1, args.port2) if p] or None
    print("포트 탐색 중...")
    return find_devices(ports), 1.0


def main(argv=None):
    p = argparse.ArgumentParser(prog="hwtest", description="라즈베리파이 + 아두이노 2대 통합 테스트")
    p.add_argument("--port1", help="아두이노 1번 포트(생략 시 자동 탐색)")
    p.add_argument("--port2", help="아두이노 2번 포트(생략 시 자동 탐색)")
    p.add_argument("-i", "--interactive", action="store_true", help="육안 확인 질문 포함")
    p.add_argument("--only", nargs="+", choices=["ARD1", "ARD2", "통합"], help="실행할 그룹")
    p.add_argument("--sim", action="store_true", help="가상 아두이노로 실행")
    p.add_argument("--json", metavar="FILE", help="결과를 JSON 으로 저장")
    args = p.parse_args(argv)

    links, scale = connect(args)
    for dev in ("ARD1", "ARD2"):
        if dev not in links:
            print(f"  [!] {dev} 를 찾지 못했습니다 - 관련 테스트는 SKIP 됩니다.")

    ctx = suite.Context(
        ard1=Arduino1(links["ARD1"]) if "ARD1" in links else None,
        ard2=Arduino2(links["ARD2"]) if "ARD2" in links else None,
        interactive=args.interactive,
        timing_scale=scale,
    )

    print("\n=== 통합 테스트 시작 ===")
    try:
        results = suite.run(ctx, only=set(args.only) if args.only else None)
    finally:
        for link in links.values():
            link.close()

    counts = {s: sum(r["status"] == s for r in results) for s in ("PASS", "WARN", "FAIL", "SKIP")}
    print("\n=== 결과: " + "  ".join(f"{k} {v}" for k, v in counts.items()) + " ===")
    for r in results:
        if r["status"] == "FAIL":
            print(f"  ✗ {r['group']} {r['name']}: {r['detail']}")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"time": datetime.datetime.now().isoformat(timespec="seconds"),
                       "summary": counts, "results": results}, f, ensure_ascii=False, indent=2)
        print(f"결과 저장: {args.json}")

    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
