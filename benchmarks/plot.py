import sys
import pandas as pd
import matplotlib
matplotlib.use("Agg")   # no display needed in WSL
import matplotlib.pyplot as plt

CSV = sys.argv[1] if len(sys.argv) > 1 else "build/results.csv"
OUTDIR = sys.argv[2] if len(sys.argv) > 2 else "benchmarks/results"
WORKLOADS = ["random", "seq", "mixed"]

df = pd.read_csv(CSV)
df["total"] = df["hits"] + df["misses"]
df["hit_rate"] = df["hits"] / df["total"].where(df["total"] > 0)

def summarize(col):
    g = df.groupby(["engine", "workload", "capacity"])[col]
    return pd.DataFrame({"median": g.median(), "min": g.min(), "max": g.max()}).reset_index()

tput = summarize("ops_per_sec")
print(tput.to_string(index=False))

fig, axes = plt.subplots(1, 3, figsize=(13, 4.2), sharey=True)
for ax, wl in zip(axes, WORKLOADS):
    s = tput[(tput.engine == "sentinel") & (tput.workload == wl)].sort_values("capacity")
    n = tput[(tput.engine == "naive") & (tput.workload == wl)].iloc[0]
    xs = range(len(s))
    yerr = [s["median"] - s["min"], s["max"] - s["median"]]
    ax.errorbar(xs, s["median"], yerr=yerr, fmt="o-", capsize=4, label="SentinelDB (median, min-max)")
    ax.axhline(n["median"], color="red", linestyle="--", label="Naive baseline (median)")
    ax.axhspan(n["min"], n["max"], color="red", alpha=0.12)
    ax.set_xticks(list(xs))
    ax.set_xticklabels([str(c) for c in s["capacity"]])
    ax.set_yscale("log")
    ax.set_title(wl)
    ax.set_xlabel("Buffer pool capacity (pages, file = 1024 pages)")
    ax.grid(True, which="both", alpha=0.3)
axes[0].set_ylabel("Throughput (ops/sec, log scale)")
axes[0].legend(fontsize=8, loc="lower right")
fig.suptitle("Throughput vs pool capacity (5 runs per point, WSL2)")
fig.tight_layout()
fig.savefig(OUTDIR + "/throughput_vs_capacity.png", dpi=150)
print("saved", OUTDIR + "/throughput_vs_capacity.png")

fig, axes = plt.subplots(1, 3, figsize=(13, 4.2), sharey=True)
for ax, wl in zip(axes, WORKLOADS):
    s = tput[(tput.engine == "sentinel") & (tput.workload == wl)].sort_values("capacity")
    n = tput[(tput.engine == "naive") & (tput.workload == wl)].iloc[0]
    xs = range(len(s))
    yerr = [s["median"] - s["min"], s["max"] - s["median"]]
    ax.errorbar(xs, s["median"], yerr=yerr, fmt="o-", capsize=4, label="SentinelDB (median, min-max)")
    ax.axhline(n["median"], color="red", linestyle="--", label="Naive baseline (median)")
    ax.axhspan(n["min"], n["max"], color="red", alpha=0.12)
    ax.set_xticks(list(xs))
    ax.set_xticklabels([str(c) for c in s["capacity"]])
    ax.set_yscale("log")
    ax.set_title(wl)
    ax.set_xlabel("Buffer pool capacity (pages, file = 1024 pages)")
    ax.grid(True, which="both", alpha=0.3)
axes[0].set_ylabel("Throughput (ops/sec, log scale)")
axes[0].legend(fontsize=8, loc="lower right")
fig.suptitle("Throughput vs pool capacity (5 runs per point, WSL2)")
fig.tight_layout()
fig.savefig(OUTDIR + "/throughput_vs_capacity.png", dpi=150)
print("saved", OUTDIR + "/throughput_vs_capacity.png")

hr = summarize("hit_rate")
hr = hr[hr.engine == "sentinel"]
fig2, ax2 = plt.subplots(figsize=(6.5, 4.2))
for wl in WORKLOADS:
    s = hr[hr.workload == wl].sort_values("capacity")
    xs = range(len(s))
    yerr = [s["median"] - s["min"], s["max"] - s["median"]]
    ax2.errorbar(xs, s["median"], yerr=yerr, fmt="o-", capsize=4, label=wl)
ax2.set_xticks([0, 1, 2])
ax2.set_xticklabels(["64", "256", "1024"])
ax2.set_ylim(0, 1.05)
ax2.set_xlabel("Buffer pool capacity (pages, file = 1024 pages)")
ax2.set_ylabel("Hit rate (measured phase)")
ax2.set_title("SentinelDB buffer pool hit rate vs capacity (5 runs per point)")
ax2.grid(True, alpha=0.3)
ax2.legend()
fig2.tight_layout()
fig2.savefig(OUTDIR + "/hit_rate_vs_capacity.png", dpi=150)
print("saved", OUTDIR + "/hit_rate_vs_capacity.png")
print(hr.to_string(index=False))
