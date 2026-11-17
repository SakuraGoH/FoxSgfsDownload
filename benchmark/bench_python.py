import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from fox.api import (
    fetch_chess_list, fetch_qipu_page, fetch_qipu_search_page,
    query_user_by_name, _fox_uid,
)
from fox.net import new_session
from fox.services import _merge_qipu_search


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--query", default="柯洁")
    parser.add_argument("--uid", default="6757425")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--delay", type=int, default=0, help="ms per page")
    args = parser.parse_args()

    for run in range(1, args.runs + 1):
        session = new_session()
        total_start = time.perf_counter()
        try:
            resolve_start = time.perf_counter()
            info = query_user_by_name(session, args.query)
            if info is None:
                raise RuntimeError("user not found")
            uid = args.uid or _fox_uid(info)
            name = info.get("username") or info.get("name") or args.query
            resolve_ms = int((time.perf_counter() - resolve_start) * 1000)

            h5_start = time.perf_counter()
            data = fetch_chess_list(session, uid, "0")
            latest = data.get("chesslist") or []
            h5_ms = int((time.perf_counter() - h5_start) * 1000)

            search_start = time.perf_counter()
            search_items = []
            seen = {str(g.get("chessid", "")) for g in latest}
            search_pages = 0
            page = 1
            while page <= 500:
                items, _ = fetch_qipu_search_page(session, name, page)
                if not items:
                    break
                search_pages += 1
                for item in items:
                    cid = str(item.get("chessid", ""))
                    if not cid or cid in seen:
                        continue
                    seen.add(cid)
                    search_items.append(item)
                if args.delay:
                    time.sleep(args.delay / 1000.0)
                page += 1
            search_ms = int((time.perf_counter() - search_start) * 1000)

            meta_start = time.perf_counter()
            meta = []
            meta_pages = 0
            for list_type in (1, 6):
                last = "0"
                while meta_pages < 1000:
                    items, _ = fetch_qipu_page(session, uid, last, list_type)
                    if not items:
                        break
                    meta_pages += 1
                    meta.extend(items)
                    last = str(items[-1].get("chessid", ""))
                    if args.delay:
                        time.sleep(args.delay / 1000.0)
            meta_ms = int((time.perf_counter() - meta_start) * 1000)

            merge_start = time.perf_counter()
            merged = _merge_qipu_search(search_items, meta)
            merge_ms = int((time.perf_counter() - merge_start) * 1000)
            total_ms = int((time.perf_counter() - total_start) * 1000)

            print(json.dumps({
                "run": run,
                "resolve_ms": resolve_ms,
                "h5_ms": h5_ms,
                "h5_count": len(latest),
                "search_ms": search_ms,
                "search_pages": search_pages,
                "search_count": len(search_items),
                "meta_ms": meta_ms,
                "meta_pages": meta_pages,
                "meta_count": len(meta),
                "merge_ms": merge_ms,
                "total_ms": total_ms,
                "total_count": len(latest) + len(search_items),
                "merged_count": len(merged),
            }, ensure_ascii=False), flush=True)
        except Exception as e:
            print(json.dumps({"run": run, "error": str(e)}, ensure_ascii=False),
                  flush=True)


if __name__ == "__main__":
    sys.exit(main())
