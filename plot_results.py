"""
Scenario 1 visualization — RTS/CTS
Usage: python3 visualize_s1.py --input results_scenario1.csv
"""

import argparse
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# ── argumenty ────────────────────────────────────────────────────────────────
parser = argparse.ArgumentParser()
parser.add_argument("--input", default="results_scenario1.csv")
args = parser.parse_args()

# ── load data ───────────────────────────────────────────────────────────────
df = pd.read_csv(args.input)
df = df[df["scenario"].astype(str).str.isnumeric()]
df = df[(df["scenario"].astype(int) == 1) & (df["protocol"] == "UDP")]

for col in ["nVoip", "lossPct", "meanDelayMs", "meanJitterMs"]:
    df[col] = pd.to_numeric(df[col], errors="coerce")
df["nVoip"] = df["nVoip"].astype(int)

# ── aggregate: mean, std and sample size by standard/mode/nVoip ───────────
agg = df.groupby(["standard", "rtsCtsMode", "nVoip"]).agg(
    delay_mean  = ("meanDelayMs",  "mean"),
    delay_std   = ("meanDelayMs",  "std"),
    jitter_mean = ("meanJitterMs", "mean"),
    jitter_std  = ("meanJitterMs", "std"),
    loss_mean   = ("lossPct",      "mean"),
    loss_std    = ("lossPct",      "std"),
    n           = ("meanDelayMs",  "count"),
).reset_index()

# 95% CI for each metric
z = 1.96
agg["delay_ci"] = z * agg["delay_std"] / np.sqrt(agg["n"])
agg["jitter_ci"] = z * agg["jitter_std"] / np.sqrt(agg["n"])
agg["loss_ci"] = z * agg["loss_std"] / np.sqrt(agg["n"])

# ── colors and line styles ─────────────────────────────────────────────────
COLORS = {"off": "#3266ad", "all": "#e07b39", "tcponly": "#2d9e75"}
DASH   = {"off": "-",       "all": "--",      "tcponly": ":"}
MODES  = ["off", "all", "tcponly"]
STDS   = ["ac", "ax", "be"]
NV     = [2, 5, 10, 15, 20]

# ── figure: 3 rows × 3 columns ─────────────────────────────────────────────
fig, axes = plt.subplots(3, 3, figsize=(14, 10))
fig.suptitle("Scenario 1 — RTS/CTS impact on VoIP QoS", fontsize=13)

METRICS = [
    ("delay",  "Delay [ms]",          150, "ITU-T 150 ms"),
    ("jitter", "Jitter [ms]",         30, "ITU-T 30 ms"),
    ("loss",   "Packet loss [%]",     1,  "ITU-T 1 %"),
]

for row, (metric, ylabel, threshold, tlabel) in enumerate(METRICS):
    for col, std in enumerate(STDS):
        ax = axes[row][col]
        sub = agg[agg["standard"] == std]

        for mode in MODES:
            m = sub[sub["rtsCtsMode"] == mode].sort_values("nVoip")
            if m.empty:
                continue
            ci = 1.96 * m[f"{metric}_std"] / np.sqrt(m["n"])
            ax.plot(m["nVoip"], m[f"{metric}_mean"],
                    color=COLORS[mode], linestyle=DASH[mode],
                    linewidth=2, marker="o", markersize=5,
                    label=f"RTS/CTS {mode}")
            ax.fill_between(m["nVoip"],
                            m[f"{metric}_mean"] - ci,
                            m[f"{metric}_mean"] + ci,
                            color=COLORS[mode], alpha=0.12, linewidth=0)

        # set Y range based on data (not the ITU-T threshold)
        vals = sub[sub["rtsCtsMode"].isin(MODES)][f"{metric}_mean"].dropna()
        if not vals.empty:
            ymax = vals.max() * 1.25
            ymin = max(0, vals.min() * 0.8)
            ax.set_ylim(ymin, ymax)

        ax.axhline(threshold, color="crimson", linewidth=1,
                   linestyle="--", alpha=0.7)
        # threshold label only if it fits in the current y range
        ylim = ax.get_ylim()
        if ylim[0] <= threshold <= ylim[1]:
            ax.text(NV[-1], threshold * 1.04, tlabel,
                    color="crimson", fontsize=7.5, ha="right")

        if row == 0:
            ax.set_title(f"802.11{std.upper()}", fontsize=10)
        if col == 0:
            ax.set_ylabel(ylabel)
        ax.set_xlabel("Number of VoIP stations")
        ax.set_xticks(NV)
        ax.grid(True, alpha=0.25)

# ── shared legend ─────────────────────────────────────────────────────────────
handles = [
    plt.Line2D([0], [0], color=COLORS[m], linestyle=DASH[m],
               linewidth=2, marker="o", markersize=5,
               label=f"RTS/CTS: {m}")
    for m in MODES
]
fig.legend(handles=handles, loc="lower center", ncol=3,
           fontsize=10, bbox_to_anchor=(0.5, 0.01))

plt.tight_layout(rect=[0, 0.07, 1, 1])
plt.savefig("s1_plots.png", dpi=130, bbox_inches="tight")
print("Saved: s1_plots.png")
plt.show()
