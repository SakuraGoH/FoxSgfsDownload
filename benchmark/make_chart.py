import json
import os
import statistics

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager

FONT_PATH = "C:/Windows/Fonts/simhei.ttf"
if os.path.exists(FONT_PATH):
    font_manager.fontManager.addfont(FONT_PATH)
    plt.rcParams["font.family"] = font_manager.FontProperties(fname=FONT_PATH).get_name()
plt.rcParams["axes.unicode_minus"] = False

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    with open(os.path.join(HERE, name), "r", encoding="utf-8") as f:
        return json.load(f)


def med(rows, engine, key):
    vals = [r[key] for r in rows if r.get("engine") == engine and key in r]
    return statistics.median(vals) if vals else 0


def values(rows, engine):
    setup = med(rows, engine, "resolve_ms") + med(rows, engine, "h5_ms")
    search = med(rows, engine, "search_ms")
    meta = med(rows, engine, "meta_ms")
    merge = med(rows, engine, "merge_ms")
    total = med(rows, engine, "total_ms")
    return [setup / 1000.0, search / 1000.0, meta / 1000.0, merge / 1000.0], total / 1000.0


no_delay = load("raw_no_delay.json")
throttle = load("raw_throttle.json")

fig, axes = plt.subplots(1, 2, figsize=(13, 5.8), facecolor="#0b0718")
phases = ["账号解析 + H5 最新", "棋谱库列表", "棋谱库详情", "合并"]
colors = ["#5ce3ff", "#f6c648", "#a474ff", "#5de09a"]
legend_handles = []
for ax, rows, title in (
    (axes[0], no_delay, "无额外延迟（0 ms/页）"),
    (axes[1], throttle, "相同节流（80 ms/页）"),
):
    ax.set_facecolor("#120d22")
    labels = ["Python", "C++"]
    data = [values(rows, "python")[0], values(rows, "cpp")[0]]
    totals = [values(rows, "python")[1], values(rows, "cpp")[1]]
    bottoms = [0.0, 0.0]
    for i, name in enumerate(phases):
        vals = [data[0][i], data[1][i]]
        bars = ax.barh(labels, vals, left=bottoms, color=colors[i], label=name,
                       height=0.52, edgecolor="#0b0718", linewidth=1.2)
        if ax is axes[0]:
            legend_handles.append(bars)
        bottoms = [bottoms[0] + vals[0], bottoms[1] + vals[1]]
    for y, total in enumerate(totals):
        ax.text(total + 1.2, y, f"{total:.1f}s", va="center", color="#eeeafa",
                fontsize=12, fontweight="bold")
    speedup = totals[0] / totals[1] if totals[1] else 0
    ax.text(0.98, 0.50, f"C++ 快 {speedup:.2f}×", transform=ax.transAxes,
            ha="right", va="center", color="#5de09a", fontsize=13,
            fontweight="bold")
    ax.set_title(title, color="#eeeafa", fontsize=13, pad=12)
    ax.set_xlabel("秒", color="#9a92b2")
    ax.tick_params(colors="#cfc8e6", labelsize=11)
    for spine in ax.spines.values():
        spine.set_color("#3d2f60")
    ax.grid(axis="x", color="#3d2f60", alpha=0.35, linewidth=0.8)
    ax.set_axisbelow(True)
    ax.set_xlim(0, max(totals) * 1.18)

fig.legend(legend_handles, phases, loc="lower center", ncol=4,
           facecolor="#19122e", edgecolor="#3d2f60", labelcolor="#eeeafa",
           fontsize=9, framealpha=0.95)
fig.suptitle("野狐棋谱全量搜索 Benchmark · 柯洁 UID 6757425 · 818 局",
             color="#f6c648", fontsize=15, fontweight="bold", y=0.98)
fig.tight_layout(rect=(0, 0.10, 1, 0.94))
out = os.path.join(HERE, "benchmark.png")
fig.savefig(out, dpi=160, facecolor=fig.get_facecolor())
print("saved", out)
