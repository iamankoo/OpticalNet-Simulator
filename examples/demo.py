"""OpticalNet end-to-end demo: topology -> routing -> connections -> simulation -> failure/recovery -> study -> validation.

    python examples/demo.py                      # runs against a server already started with: uvicorn api.main:app
    python examples/demo.py --url http://127.0.0.1:8000
    python examples/demo.py --in-process         # no server needed: uses FastAPI's TestClient (same code path)

Every number printed below comes from the C++ engine; this script only sends HTTP requests (standard library only,
except --in-process which needs the packages from requirements.txt).
"""
import argparse
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class Http:
    def __init__(self, base):
        self.base = base.rstrip("/")

    def call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.base + path, data=data, method=method, headers={"content-type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=300) as response:
                return response.status, json.loads(response.read() or b"null")
        except urllib.error.HTTPError as error:
            return error.code, json.loads(error.read() or b"null")


class InProcess:
    def __init__(self):
        sys.path.insert(0, str(ROOT))
        from api.main import app
        from fastapi.testclient import TestClient

        self.client = TestClient(app)

    def call(self, method, path, body=None):
        response = self.client.request(method, path, json=body)
        return response.status_code, (response.json() if response.content else None)


def show(title):
    print(f"\n=== {title}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8000")
    parser.add_argument("--in-process", action="store_true")
    args = parser.parse_args()
    api = InProcess() if args.in_process else Http(args.url)

    show("1. health")
    status, body = api.call("GET", "/health")
    print(status, body)
    if status != 200:
        sys.exit("API is not reachable. Start it with:  uvicorn api.main:app   (or use --in-process)")

    show("2. load a topology (NSFNET: 14 nodes, 21 links)")
    topology = json.loads((ROOT / "configs" / "nsfnet.json").read_text())
    status, body = api.call("POST", "/topologies", topology)
    tid = body["topology_id"]
    print(status, {k: body[k] for k in ("topology_id", "node_count", "link_count", "is_connected", "validation")})

    show("3. routing: node 1 -> node 14, three metrics")
    paths = {}
    for metric in ("hop_count", "distance", "administrative"):
        status, body = api.call("POST", "/route", {"topology_id": tid, "source": 1, "destination": 14, "metric": metric})
        paths[metric] = body["path"]
        p = body["path"]
        print(f"{metric:15s} nodes={p['nodes']} cost={p['cost']} hops={p['hop_count']}")

    show("4. constrained routing: forbid the first link of the distance path; then an impossible hop limit")
    first_link = paths["distance"]["links"][0]
    status, body = api.call("POST", "/route", {"topology_id": tid, "source": 1, "destination": 14, "metric": "distance",
                                               "blocked_links": [first_link]})
    print(f"blocked link {first_link}: nodes={body['path']['nodes']} cost={body['path']['cost']}")
    status, body = api.call("POST", "/route", {"topology_id": tid, "source": 1, "destination": 14, "max_hops": 1})
    print(status, body)

    show("5. connections and capacity (a small 3-node line with 1 channel on the first link)")
    line = {"nodes": [{"id": 1}, {"id": 2}, {"id": 3}], "links": [
        {"id": 1, "source": 1, "target": 2, "length_km": 10, "capacity_channels": 1},
        {"id": 2, "source": 2, "target": 3, "length_km": 10, "capacity_channels": 2}]}
    ops = [{"op": "establish", "id": 1, "source": 1, "destination": 3}, {"op": "establish", "id": 2, "source": 1, "destination": 3},
           {"op": "release", "id": 1}, {"op": "establish", "id": 2, "source": 1, "destination": 3}]
    status, body = api.call("POST", "/connections/scenario", {"topology": line, "operations": ops})
    for step in body["steps"]:
        print(step["index"], step["op"], "OK" if step["ok"] else f"REJECTED {step['error']['code']}")
    print("final:", body["final"]["totals"], "valid:", body["final"]["validation"]["valid"])

    show("6. simulation: 200 time units, ~2 requests per unit holding 8-16 channels for ~30 units, seed 42")
    config = {"seed": 42, "end_time": 200, "arrival_rate": 2.0, "mean_lifetime": 30.0, "min_demand": 8, "max_demand": 16,
              "routing": {"metric": "distance"}}
    status, body = api.call("POST", "/simulate", {"topology_id": tid, "config": config})
    m = body["metrics"]
    print(f"requests={m['total_requests']} accepted={m['accepted_requests']} blocked={m['rejected_requests']} "
          f"acceptance={m['acceptance_rate']:.3f} utilization={m['average_utilization']:.3f} events={m['events_processed']}")
    print("blocking reasons:", m["rejections_by_reason"])
    status, again = api.call("POST", "/simulate", {"topology_id": tid, "config": config})
    print("same seed again gives identical result:", again == body)

    show("7. failure and recovery: one connection 1 -> 14, its first link fails at t=5, recovers at t=20")
    arrival = {"time": 1, "connection_id": 1, "source": 1, "destination": 14, "demand": 1, "lifetime": 100,
               "routing": {"metric": "distance"}}
    schedule = [{"time": 5, "action": "fail_link", "id": first_link}, {"time": 20, "action": "recover_link", "id": first_link}]
    for policy in ("reroute", "no_recovery"):
        status, body = api.call("POST", "/simulate", {"topology_id": tid, "include_trace": True, "arrivals": [arrival],
                                                      "config": {"end_time": 60, "failure_schedule": schedule, "recovery_policy": policy}})
        m = body["metrics"]
        print(f"policy={policy:11s} affected={m['connections_affected']} rerouted={m['connections_rerouted']} lost={m['connections_lost']} "
              f"downtime={m['total_connection_downtime']}")
    print("trace of the last run:", [(e["time"], e["event"], e["kind"]) for e in body["trace"]])

    show("8. multithreaded study: 16 replications, 1 worker vs 4 workers")
    results = {}
    for workers in (1, 4):
        status, body = api.call("POST", "/study", {"topology_id": tid, "replications": 16, "base_seed": 7, "workers": workers,
                                                   "config": {**config, "end_time": 100}})
        results[workers] = body
        a = body["aggregate"]["acceptance_rate"]
        print(f"workers={workers}: threads used={body['workers_used']} acceptance mean={a['mean']:.4f} "
              f"95% CI=({a['confidence_interval_95'][0]:.4f}, {a['confidence_interval_95'][1]:.4f})")
    same = results[1]["replications"] == results[4]["replications"] and results[1]["aggregate"] == results[4]["aggregate"]
    print("results identical regardless of worker count:", same)

    show("9. validation (C++ invariant checkers; the simulation trace is replayed independently)")
    status, body = api.call("POST", "/validate", {"topology_id": tid, "simulation": {"config": {**config, "end_time": 100,
                                                  "random_failures": {"failure_rate": 0.2, "mean_repair_time": 5.0}}}})
    print(status, "valid:", body["valid"], "| trace entries replayed:", body["simulation"]["trace_entries"],
          "| failures:", body["simulation"]["failures"])

    print("\nDemo finished.")


if __name__ == "__main__":
    main()
