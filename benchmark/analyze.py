import json
import os
import statistics

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    path = os.path.join(HERE, name)
    if not os.path.exists(path):
        return []
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def median(values):
    return statistics.median(values) if values else 0


def summarize(rows, engine):
    subset = [r for r in rows if r.get("engine") == engine and "total_ms" in r]
    return {
        "n": len(subset),
        "total": median([r["total_ms"] for r in subset]),
        "resolve": median([r.get("resolve_ms", 0) for r in subset]),
        "h5": median([r.get("h5_ms", 0) for r in subset]),
        "search": median([r.get("search_ms", 0) for r in subset]),
        "meta": median([r.get("meta_ms", 0) for r in subset]),
        "merge": median([r.get("merge_ms", 0) for r in subset]),
        "count": median([r.get("total_count", 0) for r in subset]),
    }


def main():
    no_delay = load("raw_no_delay.json")
    throttle = load("raw_throttle.json")

    print("## No delay (0 ms/page)")
    print("| engine | runs | total | resolve | H5 | qipu list | meta | merge | games |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for eng in ("python", "cpp"):
        s = summarize(no_delay, eng)
        print(f"| {eng} | {s['n']} | {s['total']} | {s['resolve']} | {s['h5']} | "
              f"{s['search']} | {s['meta']} | {s['merge']} | {s['count']} |")

    print("\n## 80 ms/page throttle")
    print("| engine | runs | total | resolve | H5 | qipu list | meta | merge | games |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for eng in ("python", "cpp"):
        s = summarize(throttle, eng)
        print(f"| {eng} | {s['n']} | {s['total']} | {s['resolve']} | {s['h5']} | "
              f"{s['search']} | {s['meta']} | {s['merge']} | {s['count']} |")

    print("\n## Speedup (median total)")
    for label, rows in (("no_delay", no_delay), ("throttle_80ms", throttle)):
        p = summarize(rows, "python")["total"]
        c = summarize(rows, "cpp")["total"]
        print(f"- {label}: python {p} ms, cpp {c} ms, speedup {p / c:.2f}x")

    print("\n## Raw totals")
    for label, rows in (("no_delay", no_delay), ("throttle_80ms", throttle)):
        for eng in ("python", "cpp"):
            vals = [r["total_ms"] for r in rows
                    if r.get("engine") == eng and "total_ms" in r]
            print(f"- {label} {eng}: {vals}")


if __name__ == "__main__":
    main()
