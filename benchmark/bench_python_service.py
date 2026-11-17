import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from fox.net import new_session
from fox.services import SearchService


def main():
    session = new_session()
    service = SearchService(session)
    items = []
    start = time.perf_counter()
    service.fetch_all_user_games(
        "6757425",
        lambda batch, hint: items.extend(batch),
        full_history=True,
        nickname="柯洁",
    )
    total_ms = int((time.perf_counter() - start) * 1000)
    print(json.dumps({
        "engine": "python_service",
        "condition": "shipped_80ms",
        "total_ms": total_ms,
        "total_count": len(items),
        "delay_ms": SearchService.QIPU_PAGE_DELAY * 1000,
    }, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
