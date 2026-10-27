"""Python / FastAPI benchmark for OpticalNet (Phase 10).

    python benchmarks/bench_api.py [--quick] [--repeats N] [--json FILE]

Measures the cost of each layer on deterministic workloads (fixed seeds, grid topologies generated here):
  * "binding"  = calling the pybind11 module directly (C++ work + dict conversion)
  * "http"     = the same operation through FastAPI + Pydantic via the in-process TestClient (no network)
so (http - binding) is the overhead of the API layer. Timings are medians of --repeats runs. Peak memory is the
process peak working set (Windows, via ctypes; reported as 0 elsewhere). Nothing is estimated.
"""
import argparse
import json
import platform
import random
import statistics
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from api import bootstrap  # noqa: E402,F401
import opticalnet  # noqa: E402
from api.main import app  # noqa: E402
from api.store import store  # noqa: E402
from fastapi.testclient import TestClient  # noqa: E402


def peak_working_set_mib():
    try:
        import ctypes
        from ctypes import wintypes

        class Counters(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                        ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                        ("PeakPagefileUsage", ctypes.c_size_t)]

        c = Counters()
        c.cb = ctypes.sizeof(c)
        handle = ctypes.windll.kernel32.GetCurrentProcess()
        ctypes.windll.psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
        if ctypes.windll.psapi.GetProcessMemoryInfo(handle, ctypes.byref(c), c.cb):
            return c.PeakWorkingSetSize / (1024 * 1024)
    except Exception:
        pass
    return 0.0


def grid_document(w, h, capacity=24):
    nodes = [{"id": i} for i in range(1, w * h + 1)]
    links, link = [], 1
    for y in range(h):
        for x in range(w):
            a = y * w + x + 1
            for b in ([a + 1] if x + 1 < w else []) + ([a + w] if y + 1 < h else []):
                links.append({"id": link, "source": a, "target": b, "length_km": 20.0 + ((x * 7 + y * 13 + b) % 50),
                              "capacity_channels": capacity, "cost": 1.0 + ((x + y) % 5)})
                link += 1
    return {"nodes": nodes, "links": links}


RESULTS = []
ONLY = ""


def bench(name, workload, body, repeats, unit=None):
    if ONLY and ONLY not in name:
        return
    samples, ops = [], 0
    for _ in range(repeats):
        start = time.perf_counter()
        ops = body()
        samples.append((time.perf_counter() - start) * 1000.0)
    median = statistics.median(samples)
    row = {"name": name, "workload": workload, "median_ms": round(median, 3), "min_ms": round(min(samples), 3),
           "max_ms": round(max(samples), 3), "operations": ops, "unit": unit or "", "peak_working_set_mib": round(peak_working_set_mib(), 1)}
    if ops and unit:
        row["throughput_per_s"] = round(ops / (median / 1000.0), 1)
        row["ms_per_op"] = round(median / ops, 4)
    RESULTS.append(row)
    tp = f"  {row['throughput_per_s']:>10.1f} {unit}/s  ({row['ms_per_op']:.3f} ms/op)" if "throughput_per_s" in row else ""
    print(f"{name:34s} {median:10.2f} ms (min {min(samples):.2f}, max {max(samples):.2f}){tp}  peakWS {row['peak_working_set_mib']:.0f} MiB", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--json")
    parser.add_argument("--only", default="", help="run only benchmarks whose name contains this text")
    args = parser.parse_args()
    global ONLY
    ONLY = args.only
    q = 0.2 if args.quick else 1.0
    r = args.repeats

    side = 12 if args.quick else 30
    doc = grid_document(side, side)
    text = json.dumps(doc)
    nodes = side * side
    desc = f"{side}x{side} grid, {nodes} nodes, {len(doc['links'])} links, {len(text) // 1024} KiB JSON"
    print(f"OpticalNet API benchmark | python {platform.python_version()} | {platform.platform()} | repeats {r} | "
          f"{'quick' if args.quick else 'full'} workload\n")

    store.clear()
    client = TestClient(app)

    def health():
        for _ in range(300):
            assert client.get("/health").status_code == 200
        return 300

    # The cost of an empty request through the in-process TestClient + FastAPI stack: the floor under every http.* row.
    bench("http.GET /health (request floor)", "300 trivial requests, no engine call", health, r, "requests")
    topo = opticalnet.topology_from_json(text)
    bench("binding.topology_from_json", desc, lambda: (opticalnet.topology_from_json(text), 1)[1], r, "topologies")
    created = []

    def post_topology():
        resp = client.post("/topologies", json=doc)
        assert resp.status_code == 201
        created.append(resp.json()["topology_id"])
        return 1

    bench("http.POST /topologies", desc + " (parse + validate + summary)", post_topology, r, "topologies")
    if not created:
        post_topology()
    tid = created[-1]

    pairs_rng = random.Random(1)
    n_routes = int(300 * (q if args.quick else 1.0)) or 30
    pairs = []
    while len(pairs) < n_routes:
        a, b = pairs_rng.randint(1, nodes), pairs_rng.randint(1, nodes)
        if a != b:
            pairs.append((a, b))

    def binding_routes():
        for a, b in pairs:
            opticalnet.route(topo, a, b, metric="distance")
        return len(pairs)

    def http_routes():
        for a, b in pairs:
            resp = client.post("/route", json={"topology_id": tid, "source": a, "destination": b, "metric": "distance"})
            assert resp.status_code == 200
        return len(pairs)

    bench("binding.route", f"{desc}; {n_routes} random pairs, distance", binding_routes, r, "routes")
    bench("http.POST /route", f"{desc}; {n_routes} random pairs, distance", http_routes, r, "routes")

    # /connections/scenario: establish many connections against a request-owned ConnectionManager
    ops = [{"op": "establish", "id": i + 1, "source": a, "destination": b, "demand": 1, "routing": {"metric": "distance"}}
           for i, (a, b) in enumerate(pairs)]

    def scenario():
        resp = client.post("/connections/scenario", json={"topology_id": tid, "operations": ops})
        assert resp.status_code == 200
        return len(ops)

    bench("http.POST /connections/scenario", f"{desc}; {len(ops)} establish operations", scenario, r, "operations")

    sim_side = 12 if args.quick else 20
    sim_doc = grid_document(sim_side, sim_side)
    sim_tid = client.post("/topologies", json=sim_doc).json()["topology_id"]
    sim_topo = opticalnet.topology_from_json(json.dumps(sim_doc))
    sim_desc = f"{sim_side}x{sim_side} grid, {sim_side * sim_side} nodes, {len(sim_doc['links'])} links"
    end_time = 150.0 if args.quick else 500.0
    cfg = {"seed": 1, "end_time": end_time, "arrival_rate": 20.0, "mean_lifetime": 8.0, "min_demand": 1, "max_demand": 2,
           "routing": {"metric": "distance"}}

    def make_config(extra=None):
        c = opticalnet.SimulationConfig()
        c.seed, c.end_time, c.arrival_rate, c.mean_lifetime, c.max_demand = 1, end_time, 20.0, 8.0, 2
        c.routing = opticalnet.RoutingConstraints(metric="distance")
        if extra:
            c.random_failures = opticalnet.RandomFailureConfig(*extra)
        return c

    events = {}

    def binding_sim():
        res = opticalnet.simulate(sim_topo, make_config())
        events["sim"] = res["metrics"]["events_processed"]
        return events["sim"]

    def http_sim():
        resp = client.post("/simulate", json={"topology_id": sim_tid, "config": cfg})
        assert resp.status_code == 200
        return resp.json()["metrics"]["events_processed"]

    bench("binding.simulate", f"{sim_desc}; end_time {end_time:.0f}, rate 20", binding_sim, r, "events")
    bench("http.POST /simulate", f"{sim_desc}; same workload ({events.get('sim', '?')} events)", http_sim, r, "events")

    fcfg = {**cfg, "random_failures": {"failure_rate": 0.3, "mean_repair_time": 6.0, "node_failure_probability": 0.25}}

    def http_sim_failures():
        resp = client.post("/simulate", json={"topology_id": sim_tid, "config": fcfg})
        assert resp.status_code == 200
        return resp.json()["metrics"]["events_processed"]

    bench("http.POST /simulate (failures)", f"{sim_desc}; random failures 0.3/unit, reroute", http_sim_failures, r, "events")

    reps = 8 if args.quick else 16
    study_end = 100.0 if args.quick else 300.0
    scfg = {**cfg, "end_time": study_end}

    def study(workers):
        def run():
            resp = client.post("/study", json={"topology_id": sim_tid, "config": scfg, "replications": reps,
                                               "base_seed": 7, "workers": workers})
            assert resp.status_code == 200
            return sum(x["metrics"]["events_processed"] for x in resp.json()["replications"])
        return run

    for w in (1, 4, 0):
        bench(f"http.POST /study workers={w or 'auto'}", f"{sim_desc}; {reps} replications of end_time {study_end:.0f}",
              study(w), r, "events")

    def validate():
        resp = client.post("/validate", json={"topology_id": sim_tid, "simulation": {"config": {**cfg, "end_time": 100.0}}})
        assert resp.status_code == 200 and resp.json()["valid"]
        return 1

    bench("http.POST /validate (topology+trace)", f"{sim_desc}; traced run of end_time 100", validate, r, "validations")

    # Concurrency: the GIL is released while C++ runs, so parallel HTTP requests overlap.
    n_req = 8 if args.quick else 16
    small = {**cfg, "end_time": 100.0 if args.quick else 200.0}

    def one_request(i):
        with TestClient(app) as c:
            resp = c.post("/simulate", json={"topology_id": sim_tid, "config": {**small, "seed": i + 1}})
            assert resp.status_code == 200
            return resp.json()["metrics"]["events_processed"]

    def sequential():
        return sum(one_request(i) for i in range(n_req))

    def parallel():
        with ThreadPoolExecutor(8) as pool:
            return sum(pool.map(one_request, range(n_req)))

    bench("http.simulate x" + str(n_req) + " sequential", f"{sim_desc}; end_time {small['end_time']:.0f}", sequential, r, "events")
    bench("http.simulate x" + str(n_req) + " 8 threads", f"{sim_desc}; same requests from 8 client threads", parallel, r, "events")
    if len(RESULTS) >= 2 and RESULTS[-1]["name"].endswith("8 threads"):
        seq, par = RESULTS[-2]["median_ms"], RESULTS[-1]["median_ms"]
        print(f"\nparallel/sequential speedup: {seq / par:.2f}x")
        RESULTS[-1]["note"] = f"speedup vs sequential: {seq / par:.2f}x"

    if args.json:
        Path(args.json).write_text(json.dumps({"python": platform.python_version(), "platform": platform.platform(),
                                               "repeats": r, "quick": args.quick, "results": RESULTS}, indent=2))
        print(f"wrote {args.json}")


if __name__ == "__main__":
    main()
