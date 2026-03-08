#!/usr/bin/env python3
"""FIX 4.2 demo client for the Order Matching Engine.

Usage:
    python3 demo/fix_client.py [--port 9000] [--host 127.0.0.1] [--self-test]

In self-test mode, exits 0 if at least 2 fill ExecutionReports are received.

Price format: tag 44 is parsed by the engine as a decimal string via strtod()
and stored as Price (int64, PRICE_SCALE=10000). So 150.0000 => 1500000 ticks.

Note: the engine has self-trade prevention keyed on the TCP connection (fd).
The demo uses two connections so that the aggressive buy can cross against the
resting sells without being blocked by the self-trade check.
"""

import argparse
import socket
import sys
import time
import threading


SOH = "\x01"


def checksum(msg: str) -> int:
    return sum(msg.encode()) % 256


def build_fix(body: str) -> str:
    header = f"8=FIX.4.2{SOH}9={len(body)}{SOH}"
    pre = header + body
    ck  = checksum(pre)
    return f"{pre}10={ck:03d}{SOH}"


class FIXClient:
    def __init__(self, host: str, port: int, name: str = "CLIENT"):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.name = name
        self.seq  = 1
        self.buf  = ""
        self.received = []
        self._lock = threading.Lock()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self):
        while True:
            try:
                data = self.sock.recv(4096).decode(errors="replace")
                if not data:
                    break
                with self._lock:
                    self.buf += data
                    while "10=" in self.buf:
                        end = self.buf.find(SOH, self.buf.find("10=") + 3)
                        if end == -1:
                            break
                        msg = self.buf[:end + 1]
                        self.buf = self.buf[end + 1:]
                        self.received.append(msg)
                        self._print_msg(msg)
            except Exception:
                break

    def _print_msg(self, msg: str):
        fields = {f.split("=")[0]: f.split("=")[1]
                  for f in msg.split(SOH) if "=" in f}
        t = fields.get("35", "?")
        names = {"A": "Logon", "5": "Logout", "8": "ExecutionReport"}
        name  = names.get(t, t)
        detail = ""
        if t == "8":
            exec_type = fields.get("150", "?")
            etype = {"0": "New", "F": "Fill", "4": "Cancelled", "5": "Replaced"}
            detail = (f" [{etype.get(exec_type, exec_type)}]"
                      f" qty={fields.get('32','?')} px={fields.get('31','?')}")
        print(f"  [{self.name}] << {name}{detail}")

    def send(self, body: str):
        msg = build_fix(body)
        print(f"  [{self.name}] >> {body[:60].replace(SOH, '|')}...")
        self.sock.sendall(msg.encode())
        self.seq += 1
        time.sleep(0.05)

    def logon(self):
        self.send(
            f"35=A{SOH}49={self.name}{SOH}56=ENGINE{SOH}34=1{SOH}108=30{SOH}"
        )

    def logout(self):
        self.send(
            f"35=5{SOH}49={self.name}{SOH}56=ENGINE{SOH}34={self.seq}{SOH}"
        )

    def new_order(self, cl_ord_id: str, symbol: str, side: str,
                  qty: int, price: str):
        """price is a decimal string e.g. '150.0000' (engine parses via strtod,
        PRICE_SCALE=10000, so 150.0000 => 1500000 internal ticks)."""
        self.send(
            f"35=D{SOH}49={self.name}{SOH}56=ENGINE{SOH}34={self.seq}{SOH}"
            f"11={cl_ord_id}{SOH}55={symbol}{SOH}54={side}{SOH}"
            f"38={qty}{SOH}40=2{SOH}44={price}{SOH}"
        )

    def cancel(self, cl_ord_id: str, orig_order_id: str, symbol: str, side: str):
        """orig_order_id is the numeric OrderId from tag 37 of the New ack."""
        self.send(
            f"35=F{SOH}49={self.name}{SOH}56=ENGINE{SOH}34={self.seq}{SOH}"
            f"11={cl_ord_id}{SOH}41={orig_order_id}{SOH}55={symbol}{SOH}54={side}{SOH}"
        )

    def wait(self, seconds: float):
        time.sleep(seconds)

    def fill_count(self) -> int:
        with self._lock:
            return sum(1 for m in self.received if "150=F" in m)

    def get_order_id(self) -> str:
        """Return the OrderId (tag 37) from the most recent New ack (39=0)."""
        with self._lock:
            for msg in reversed(self.received):
                if "35=8" in msg and "39=0" in msg:
                    for field in msg.split(SOH):
                        if field.startswith("37="):
                            return field[3:]
        return ""

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--self-test", action="store_true",
                        help="Exit 0 if >=2 fills received, else exit 1")
    args = parser.parse_args()

    print(f"Connecting to {args.host}:{args.port}")

    # Two connections so the aggressive buy (client_b) does not share a
    # trader_id with the resting sells (client_a) and self-trade prevention
    # does not block the crosses.
    client_a = FIXClient(args.host, args.port, name="SELLER")
    client_b = FIXClient(args.host, args.port, name="BUYER")

    print("\n[1] Logon both clients")
    client_a.logon()
    client_b.logon()
    client_a.wait(0.2)

    print("\n[2] Resting sells (3 price levels) via SELLER")
    # Decimal price strings: engine parses via strtod(), PRICE_SCALE=10000
    # 150.0000 => 1500000 ticks, 151.0000 => 1510000 ticks, 152.0000 => 1520000 ticks
    client_a.new_order("SELL1", "AAPL", "2", 10, "150.0000")
    client_a.new_order("SELL2", "AAPL", "2", 10, "151.0000")
    client_a.new_order("SELL3", "AAPL", "2", 10, "152.0000")
    client_a.wait(0.3)

    print("\n[3] Aggressive buy via BUYER — sweeps 2 levels")
    client_b.new_order("BUY1", "AAPL", "1", 20, "151.0000")
    client_b.wait(0.3)

    last_order_id = client_a.get_order_id()
    if last_order_id:
        print(f"\n[4] Cancel remaining sell SELL3 (OrderId={last_order_id})")
        client_a.cancel("CANCEL1", last_order_id, "AAPL", "2")
        client_a.wait(0.2)

    print("\n[5] Logout both clients")
    client_a.logout()
    client_b.logout()
    client_a.wait(0.2)

    fills_a = client_a.fill_count()
    fills_b = client_b.fill_count()
    total_fills = fills_a + fills_b
    print(f"\nTotal fills received: {total_fills} (SELLER={fills_a}, BUYER={fills_b})")
    client_a.close()
    client_b.close()

    if args.self_test:
        if total_fills >= 2:
            print("SELF-TEST PASSED")
            sys.exit(0)
        else:
            print(f"SELF-TEST FAILED: expected >=2 fills, got {total_fills}")
            sys.exit(1)


if __name__ == "__main__":
    main()
