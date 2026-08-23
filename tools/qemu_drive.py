import json
import os
import socket
import sys
import time


class Qmp:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(240):
            try:
                self.sock.connect(path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.5)
        else:
            raise SystemExit("cannot connect to QMP socket " + path)
        self.f = self.sock.makefile("rw")
        self._read()  # greeting

    def _read(self):
        while True:
            line = self.f.readline()
            if not line:
                raise SystemExit("qmp closed")
            msg = json.loads(line)
            if "event" in msg:
                continue
            return msg

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}) + "\n")
        self.f.flush()
        return self._read()


def main():
    sock_path, script_path = sys.argv[1], sys.argv[2]
    qmp = Qmp(sock_path)
    qmp.cmd("qmp_capabilities")
    with open(script_path) as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            op, _, arg = line.partition(" ")
            if op == "sleep":
                time.sleep(float(arg))
            elif op == "shot":
                r = qmp.cmd("screendump", filename=arg)
                print("shot", arg, r)
            elif op == "down":
                events = [{"type": "key",
                           "data": {"down": True,
                                    "key": {"type": "qcode", "data": k}}}
                          for k in arg.split(",")]
                print("down", qmp.cmd("input-send-event", events=events))
            elif op == "up":
                events = [{"type": "key",
                           "data": {"down": False,
                                    "key": {"type": "qcode", "data": k}}}
                          for k in arg.split(",")]
                print("up", qmp.cmd("input-send-event", events=events))
            elif op == "quit":
                try:
                    qmp.cmd("quit")
                except Exception:
                    pass
                return
    # idle forever if script has no quit
    while True:
        time.sleep(3600)


if __name__ == "__main__":
    main()
