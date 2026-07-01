#!/bin/bash
# =============================================================================
# run_scenario1.sh
# Scenario 1 — Impact of the RTS/CTS Mechanism (Collision Mitigation)
#
# Sweep:
#   standards  : ac ax be
#   modes      : off all tcponly
#   density    : nVoip = 2 5 10 15 20
#   background : nBackground = 3 (fixed)
#   seeds      : 1 2 3
#   Total runs : 3 × 3 × 5 × 3 = 135
# =============================================================================

set -euo pipefail

NS3_DIR="${NS3_DIR:-.}"
SCRIPT="scratch/voip-unified"
OUTPUT="results_scenario1.csv"
LOGFILE="log_scenario1.txt"

STANDARDS="ac ax be"
MODES="off all tcponly"
NVOIP_LIST="2 5 10 15 20"
NBACKGROUND=3
SEEDS="1 2 3"

if [[ ! -f "$NS3_DIR/ns3" ]]; then
    echo "ERROR: $NS3_DIR/ns3 not found — run from ns-3 directory or set NS3_DIR=/path/to/ns-3"
    exit 1
fi

cd "$NS3_DIR"

TOTAL=$(( $(echo $STANDARDS | wc -w) * $(echo $MODES | wc -w) * $(echo $NVOIP_LIST | wc -w) * $(echo $SEEDS | wc -w) ))
echo "Total runs: $TOTAL  →  $OUTPUT"

# Write CSV header once
echo "scenario,standard,rtsCtsMode,rtsThreshold,raaVariant,tcpVariant,\
nVoip,nBg,seed,srcAddr,dstAddr,protocol,\
txPkts,rxPkts,lostPkts,lossPct,\
meanDelayMs,meanJitterMs,throughputKbps" > "$OUTPUT"
> "$LOGFILE"

run=0
start_time=$(date +%s)

for std in $STANDARDS; do
    for mode in $MODES; do
        for nv in $NVOIP_LIST; do
            for seed in $SEEDS; do
                run=$(( run + 1 ))

                # Estimate remaining time
                now=$(date +%s)
                elapsed=$(( now - start_time ))
                if [[ $run -gt 1 ]]; then
                    eta=$(( elapsed * (TOTAL - run + 1) / (run - 1) ))
                    eta_str="$(( eta / 60 ))m $(( eta % 60 ))s"
                else
                    eta_str="---"
                fi

                msg="[$run/$TOTAL] std=$std mode=$mode nVoip=$nv seed=$seed  (ETA: $eta_str)"
                echo "$msg"
                echo "$msg" >> "$LOGFILE"

                ./ns3 run "$SCRIPT \
                    --scenario=1 \
                    --standard=$std \
                    --rtsCtsMode=$mode \
                    --nVoip=$nv \
                    --nBackground=$NBACKGROUND \
                    --runSeed=$seed" \
                    2>>"$LOGFILE" \
                    | tail -n +2 \
                    >> "$OUTPUT"

            done
        done
    done
done

end_time=$(date +%s)
echo "Done! $TOTAL runs in $(( (end_time - start_time) / 60 )) min  →  $OUTPUT"
