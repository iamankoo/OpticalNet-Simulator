"""Latency of the real FastAPI server (uvicorn, loopback HTTP) for a few endpoints.

    python benchmarks/bench_server.py [--json FILE]

Starts `uvicorn api.main:app` on a free port, waits for /health, then times sequential keep-alive requests
(standard library http.client only). This measures the actual server stack, unlike the in-process TestClient used in
bench_api.py. Results are medians of 5 repetitions of N requests each.
"""
import argparse
import http.client
import json
import socket
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def grid_document(w, h):
    nodes = [{"id": i} for i in range(1, w * h + 1)]
    links, link = [], 1
    for y in range(h):
        for x in range(w):
            a = y * w + x + 1
            for b in ([a + 1] if x + 1 < w else []) + ([a + w] if y + 1 < h else []):
                links.append({"id": link, "source": a, "target": b, "length_km": 20.0 + ((x * 7 + y * 13 + b) % 50),
                              "capacity_channels": 24, "cost": 1.0 + ((x + y) % 5)})
                link += 1
    return {"nodes": nodes, "links": links}


class Client:
    def __init__(self, port):
        self.conn = http.client.HTTPConnection("127.0.0.1", port, timeout=300)

    def call(self, method, path, body=None):
        data = json.dumps(body) if body is not None else None
        self.conn.request(method, path, body=data, headers={"content-type": "application/json"})
        response = self.conn.getresponse()
        payload = response.read()
        return response.status, json.loads(payload) if payload else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json")
    args = parser.parse_args()
    port = free_port()
    server = subprocess.Popen([sys.executable, "-m", "uvicorn", "api.main:app", "--port", str(port), "--log-level", "warning"],
                              cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    results = []
    try:
        client = Client(port)
        for _ in range(100):
            try:
                if client.call("GET", "/health")[0] == 200:
                    break
            except OSError:
                client = Client(port)
                time.sleep(0.1)
        else:
            sys.exit("server did not start")

        def bench(name, workload, body, ops, repeats=5):
            samples = []
            for _ in range(repeats):
                start = time.perf_counter()
                body()
                samples.append((time.perf_counter() - start) * 1000.0)
            median = statistics.median(samples)
            row = {"name": name, "workload": workload, "median_ms": round(median, 3), "operations": ops,
                   "ms_per_request": round(median / ops, 4), "requests_per_s": round(ops / (median / 1000.0), 1)}
            results.append(row)
            print(f"{name:34s} {median:9.2f} ms  {row['ms_per_request']:.3f} ms/request  {row['requests_per_s']:.0f} req/s", flush=True)

        n = 300
        bench("server.GET /health", f"{n} sequential requests", lambda: [client.call("GET", "/health") for _ in range(n)], n)
        doc = grid_document(30, 30)
        status, created = client.call("POST", "/topologies", doc)
        assert status == 201
        tid = created["topology_id"]
        import random
        rng = random.Random(1)
        pairs = []
        while len(pairs) < n:
            a, b = rng.randint(1, 900), rng.randint(1, 900)
            if a != b:
                pairs.append((a, b))
        bench("server.POST /route", f"30x30 grid (900 nodes, 1740 links), {n} random pairs, distance",
              lambda: [client.call("POST", "/route", {"topology_id": tid, "source": a, "destination": b, "metric": "distance"})
                       for a, b in pairs], n)
        sim_doc = grid_document(20, 20)
        sim_tid = client.call("POST", "/topologies", sim_doc)[1]["topology_id"]
        cfg = {"seed": 1, "end_time": 500, "arrival_rate": 20.0, "mean_lifetime": 8.0, "max_demand": 2, "routing": {"metric": "distance"}}
        bench("server.POST /simulate", "20x20 grid (400 nodes, 760 links), end_time 500, rate 20 (~20k events)",
              lambda: client.call("POST", "/simulate", {"topology_id": sim_tid, "config": cfg}), 1)
        study = {"topology_id": sim_tid, "config": {**cfg, "end_time": 300}, "replications": 16, "base_seed": 7}
        for w in (1, 4):
            bench(f"server.POST /study workers={w}", "20x20 grid, 16 replications of end_time 300",
                  lambda w=w: client.call("POST", "/study", {**study, "workers": w}), 1, repeats=3)

        # concurrency over real HTTP: 8 client threads, each with its own connection
        small = {"topology_id": sim_tid, "config": {**cfg, "end_time": 200}}

        def one(i):
            c = Client(port)
            return c.call("POST", "/simulate", {**small, "config": {**small["config"], "seed": i + 1}})[0]

        bench("server.simulate x16 sequential", "20x20 grid, end_time 200", lambda: [one(i) for i in range(16)], 16, repeats=3)
        with ThreadPoolExecutor(8) as pool:
            bench("server.simulate x16 8 clients", "same requests from 8 concurrent clients", lambda: list(pool.map(one, range(16))), 16, repeats=3)
    finally:
        server.terminate()
        server.wait(timeout=20)
    if args.json:
        Path(args.json).write_text(json.dumps({"python": sys.version.split()[0], "results": results}, indent=2))
        print("wrote", args.json)


if __name__ == "__main__":
    main()
