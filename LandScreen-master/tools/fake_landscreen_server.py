#!/usr/bin/env python3
import json
import socketserver
import threading
import time


HOST = "127.0.0.1"
PORT = 8001


class LandScreenHandler(socketserver.BaseRequestHandler):
    def setup(self):
        print("client connected:", self.client_address)
        self.running = True
        self.sender = threading.Thread(target=self.send_demo_data, daemon=True)
        self.sender.start()

    def handle(self):
        buffer = b""
        while self.running:
            data = self.request.recv(4096)
            if not data:
                break
            buffer += data
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                if line.strip():
                    print("from ground:", line.decode("utf-8", errors="replace"))

    def finish(self):
        self.running = False
        print("client disconnected:", self.client_address)

    def send_demo_data(self):
        planner_sent = False
        targets = [
            {"tx": 1.2, "ty": 2.1, "tn": "elephant"},
            {"tx": 3.5, "ty": 1.0, "tn": "monkey"},
            {"tx": 4.1, "ty": 3.0, "tn": "wolf"},
        ]
        index = 0
        while self.running:
            payload = targets[index % len(targets)].copy()
            if not planner_sent:
                payload["planner"] = [
                    {"x": 0.5, "y": 0.5},
                    {"x": 1.5, "y": 1.0},
                    {"x": 2.5, "y": 1.5},
                    {"x": 3.5, "y": 2.0},
                ]
                planner_sent = True
            else:
                payload["planner"] = []

            try:
                self.request.sendall((json.dumps(payload) + "\n").encode("utf-8"))
            except OSError:
                return

            index += 1
            time.sleep(2)


class ReusableTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True


if __name__ == "__main__":
    with ReusableTCPServer((HOST, PORT), LandScreenHandler) as server:
        print("fake LandScreen server listening on {}:{}".format(HOST, PORT))
        server.serve_forever()
