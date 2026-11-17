# Python vs C++ 搜索速度 Benchmark

![Benchmark](benchmark.png)

## 测试条件

- 机器：Intel Core Ultra 5 125H，Windows 10 Home China（10.0.26300），屏幕 1440×960 @ 200%
- 同一台机器、同一网络、同一时间段
- 目标：柯洁，UID `6757425`
- 搜索内容：H5 最新 101 局 + 官方棋谱库列表 41 页 + 棋谱库详情 39 页
- 最终结果：818 局，双方返回的 `101 / 717 / 1153 / 818` 计数完全一致
- 双方使用相同的接口、相同的 UA、相同的分页顺序
- 只测搜索核心，不包含 GUI 绘制；C++ 与 Python 都是单线程顺序请求
- 交替执行 Python / C++，取中位数，降低网络波动影响
- 时间单位：毫秒；表中换算为秒

### 两种对比

1. **无额外延迟（0 ms/页）**：测接口、解析、合并本身的差距。
2. **相同节流（80 ms/页）**：Python `SearchService` 默认每页 sleep 80 ms；给 C++ 加同样的 sleep，做公平对比。

## 结果一：无额外延迟

| 阶段 | Python | C++ | C++ 加速 |
|---|---:|---:|---:|
| 账号解析 | 0.858 s | 0.513 s | 1.67× |
| H5 最新 100 局 | 0.674 s | 0.395 s | 1.71× |
| 棋谱库列表（41 页） | 42.482 s | 18.448 s | **2.30×** |
| 棋谱库详情（39 页） | 36.644 s | 13.107 s | **2.80×** |
| 合并 / 解析 | 0.199 s | 0.013 s | **15.3×** |
| **总计（818 局）** | **81.595 s** | **32.587 s** | **2.50×** |

Python 三次总耗时：59.0 / 92.1 / 81.6 秒  
C++ 三次总耗时：31.8 / 32.8 / 32.6 秒

## 结果二：相同 80 ms/页节流

| 阶段 | Python | C++ | C++ 加速 |
|---|---:|---:|---:|
| 账号解析 | 1.082 s | 0.473 s | 2.29× |
| H5 最新 100 局 | 0.919 s | 0.427 s | 2.15× |
| 棋谱库列表（41 页） | 45.569 s | 23.676 s | **1.93×** |
| 棋谱库详情（39 页） | 39.777 s | 16.996 s | **2.34×** |
| 合并 / 解析 | 0.391 s | 0.014 s | **28.9×** |
| **总计（818 局）** | **87.739 s** | **41.586 s** | **2.11×** |

Python 两次总耗时：87.6 / 87.9 秒  
C++ 两次总耗时：40.3 / 42.9 秒

## 结果三：实际 Python `SearchService`

用 Python 版真实入口 `SearchService.fetch_all_user_games()` 跑了 3 次：

- 101.4 / 102.4 / 108.0 秒，中位数 **102.4 秒**
- 同样得到 818 局
- 该入口自带每页 80 ms 的 `QIPU_PAGE_DELAY`，并包含回调与合并

C++ 发布版搜索没有人为 sleep，同条件下核心搜索中位数约 **32.6 秒**；实际使用时约为 **3 倍** 差距。不过这个 3× 混入了网络时段差异，正式公平结论以上面的 **2.11×（同为 80 ms/页）** 为准。

## 结论

- **公平同节流：C++ 约快 2.11 倍**（87.7 s → 41.6 s）。
- **不额外限速：C++ 约快 2.50 倍**（81.6 s → 32.6 s）。
- 搜索阶段本身（棋谱库列表 + 详情）C++ 快 **2.0–2.8 倍**。
- 纯合并/解析阶段 C++ 快 **15–29 倍**，但这部分只占几十到几百毫秒，网络才是主要瓶颈。
- C++ 的优势来自 WinHTTP 请求开销更低、无 GIL、手工字符串/HTML 解析更快；但总时间仍主要受野狐服务器响应速度影响。

## 复现方式

C++ 侧可独立复现：

```bat
cpp\build\Release\FoxKifuBench.exe --runs 3 --delay 0
cpp\build\Release\FoxKifuBench.exe --runs 2 --delay 80
```

Python 侧脚本 `bench_python.py` / `bench_python_service.py` 依赖原 Python 版
`fox` 包，本仓库不包含 Python 实现；原始数据已随仓库附带。Python 对照命令：

```bat
python benchmark\bench_python.py --runs 3 --delay 0
python benchmark\bench_python.py --runs 2 --delay 80
python benchmark\bench_python_service.py
```

原始数据：

- [raw_no_delay.json](raw_no_delay.json)
- [raw_throttle.json](raw_throttle.json)
- [raw_python_service.json](raw_python_service.json)
- [results.csv](results.csv)

## 注意

- 野狐接口响应时间波动较大，绝对值会随网络、时段、服务器负载变化；表中采用中位数。
- Python 和 C++ 都走匿名公开接口，818 局包含 101 局最新对局 + 717 局棋谱库历史，两边数据完全一致。
- C++ 搜索默认没有每页 sleep；如果担心触发风控，可以在 `startSearch` 的翻页循环中加一个可配置延迟，代价是耗时增加。
