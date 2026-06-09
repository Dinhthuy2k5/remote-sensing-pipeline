import requests
import time
import sys

BASE_URL = "http://localhost:8080"
FILE_PATH = "data/samples/test_large.tif"  # dùng file gốc 3 lần

def run_session(session_num: int) -> dict:
    print(f"\n{'='*50}")
    print(f"Session {session_num}/3")
    print(f"{'='*50}")

    # Upload
    t0 = time.time()
    with open(FILE_PATH, "rb") as f:
        r = requests.post(
            f"{BASE_URL}/upload",
            data=f,
            headers={
                "Content-Type": "application/octet-stream",
                "X-Filename": "test.tif"
            }
        )
    sid = r.json()["session_id"]
    upload_time = time.time() - t0
    print(f"  Upload: {upload_time:.2f}s  session_id={sid}")

    # Start
    requests.post(f"{BASE_URL}/sessions/{sid}/start")

    # Poll status
    t1 = time.time()
    while True:
        status = requests.get(f"{BASE_URL}/sessions/{sid}/status").json()
        pct  = status.get("progress", 0)
        done = status.get("tile_done", 0)
        total= status.get("tile_total", 0)
        state= status.get("status", "?")
        print(f"  [{state}] {done}/{total} ({pct}%)", end="\r")

        if state in ("DONE", "ERROR"):
            break
        time.sleep(0.5)

    pipeline_time = time.time() - t1
    print(f"\n  Pipeline: {pipeline_time:.2f}s  state={state}")

    # Results count
    results = requests.get(f"{BASE_URL}/sessions/{sid}/results").json()
    count = len(results.get("features", []))
    print(f"  Detections: {count}")

    return {
        "session": session_num,
        "session_id": sid,
        "upload_s": round(upload_time, 2),
        "pipeline_s": round(pipeline_time, 2),
        "detections": count,
        "state": state
    }

def main():
    print("Remote Sensing Pipeline — Stress Test")
    print(f"File: {FILE_PATH}")
    print(f"Runs: 3")

    results = []
    for i in range(1, 4):
        r = run_session(i)
        results.append(r)
        if r["state"] == "ERROR":
            print("ERROR: pipeline failed, stopping.")
            sys.exit(1)
        time.sleep(1)  # nhỏ thôi, không cần delay nhiều

    print(f"\n{'='*50}")
    print("STRESS TEST SUMMARY")
    print(f"{'='*50}")
    print(f"{'Run':<6} {'SessionID':<12} {'Upload(s)':<12} {'Pipeline(s)':<14} {'Detections':<12} {'State'}")
    print("-" * 60)
    for r in results:
        print(f"{r['session']:<6} {r['session_id']:<12} "
              f"{r['upload_s']:<12} {r['pipeline_s']:<14} "
              f"{r['detections']:<12} {r['state']}")

    # Verify consistency
    det_counts = [r["detections"] for r in results]
    if len(set(det_counts)) == 1:
        print(f"\n✅ PASS: All 3 runs produced {det_counts[0]} detections (consistent)")
    else:
        print(f"\n⚠️  WARN: Inconsistent detection counts: {det_counts}")

    all_done = all(r["state"] == "DONE" for r in results)
    print(f"{'✅ PASS' if all_done else '❌ FAIL'}: All sessions reached DONE state")

if __name__ == "__main__":
    main()