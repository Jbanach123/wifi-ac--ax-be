"""
plot_results.py
Analyzes ns-3 VoIP simulation results: 802.11ac vs 802.11ax
Usage: python3 plot_results.py results_bg.csv
"""

import sys
import pandas as pd
import matplotlib.pyplot as plt

# ── QoS thresholds (ITU-T G.114) ─────────────────────────────────────────────
QOS_DELAY_MS  = 150.0
QOS_JITTER_MS =  30.0
QOS_LOSS_PCT  =   1.0

# ── Load CSV (skip repeated header rows) ─────────────────────────────────────
file = sys.argv[1] if len(sys.argv) > 1 else "~/results_bg.csv"

df = pd.read_csv(file, header=None, names=[
    "standard","nVoip","nBg","seed","srcAddr","dstAddr","protocol",
    "txPkts","rxPkts","lostPkts","lossPct",
    "meanDelayMs","meanJitterMs","throughputKbps"
])

# Drop repeated header rows (each simulation run prepends a header line)
df = df[df["standard"] != "standard"].reset_index(drop=True)

# Cast numeric columns
for col in ["nVoip","nBg","seed","txPkts","rxPkts","lostPkts",
            "lossPct","meanDelayMs","meanJitterMs","throughputKbps"]:
    df[col] = pd.to_numeric(df[col], errors="coerce")

# ── Keep only VoIP UDP flows, drop TCP background flows ──────────────────────
voip = df[df["protocol"] == "UDP"].copy()

# Each station has uplink + downlink flow — average both into one value per run
per_run = voip.groupby(["standard","nVoip","seed"], as_index=False).agg(
    delay  = ("meanDelayMs",    "mean"),
    jitter = ("meanJitterMs",   "mean"),
    loss   = ("lossPct",        "mean"),
    tput   = ("throughputKbps", "mean"),
)

# Average across seeds → one row per (standard, nVoip)
agg = per_run.groupby(["standard","nVoip"], as_index=False).agg(
    delay_mean  = ("delay",  "mean"),
    jitter_mean = ("jitter", "mean"),
    loss_mean   = ("loss",   "mean"),
    tput_mean   = ("tput",   "mean"),
)

ac = agg[agg["standard"] == "ac"].sort_values("nVoip")
ax_df = agg[agg["standard"] == "ax"].sort_values("nVoip")

# ── Plot 2x2 grid ─────────────────────────────────────────────────────────────
fig, axes = plt.subplots(2, 2, figsize=(11, 7))
fig.suptitle("VoIP QoS — 802.11ac vs 802.11ax (5 GHz, 80 MHz, G.711)",
             fontsize=13, fontweight="bold")

AC_STYLE = dict(color="#E85D24", marker="o", linewidth=2, markersize=7, label="802.11ac")
AX_STYLE = dict(color="#1D9E75", marker="s", linewidth=2, markersize=7, label="802.11ax")

def draw(ax_obj, metric, ylabel, title, threshold=None):
    ax_obj.plot(ac["nVoip"].values,    ac[metric].values,    **AC_STYLE)
    ax_obj.plot(ax_df["nVoip"].values, ax_df[metric].values, **AX_STYLE)
    if threshold is not None:
        ax_obj.axhline(threshold, color="crimson", linestyle=":",
                       linewidth=1.5, label=f"QoS limit = {threshold}")
    ax_obj.set_title(title)
    ax_obj.set_xlabel("Number of VoIP stations")
    ax_obj.set_ylabel(ylabel)
    ax_obj.legend()
    ax_obj.grid(alpha=0.3, linestyle="--")
    ax_obj.spines["top"].set_visible(False)
    ax_obj.spines["right"].set_visible(False)

draw(axes[0,0], "delay_mean",  "Mean delay [ms]",   "End-to-end delay",  QOS_DELAY_MS)
draw(axes[0,1], "jitter_mean", "Mean jitter [ms]",  "Jitter",            QOS_JITTER_MS)
draw(axes[1,0], "loss_mean",   "Packet loss [%]",   "Packet loss ratio", QOS_LOSS_PCT)
draw(axes[1,1], "tput_mean",   "Throughput [kbps]", "VoIP throughput per station")

plt.tight_layout()
plt.savefig("voip_qos_comparison.png", dpi=150, bbox_inches="tight")
print("Saved: voip_qos_comparison.png")

# ── Capacity summary printed to console ───────────────────────────────────────
print("\n── VoIP capacity (max stations within all QoS limits) ──")
for std, data in [("ac", ac), ("ax_df", ax_df)]:
    label = std.replace("_df", "")
    ok = data[
        (data["delay_mean"]  < QOS_DELAY_MS)  &
        (data["jitter_mean"] < QOS_JITTER_MS) &
        (data["loss_mean"]   < QOS_LOSS_PCT)
    ]
    cap = int(ok["nVoip"].max()) if not ok.empty else 0
    print(f"  802.11{label}: {cap} stations")

plt.show()
