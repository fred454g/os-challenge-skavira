#!/bin/sh
#
# Runs the client once and saves its full output, tagged with the settings and
# the git commit, to experiments/<EXP>/results/.
# Run it in the CLIENT VM, while the variant you name is running in the SERVER VM.
#
# Usage (defaults = run-client-final.sh, seed included):
#   EXP=concurrency VARIANT=baseline ./experiments/run-experiment.sh
#   EXP=concurrency VARIANT=pool SEED=42 DELAY_US=300000 RUN=2 ./experiments/run-experiment.sh
#   EXP=cache VARIANT=cache TOTAL=100 ./experiments/run-experiment.sh
#
set -eu

PATHTOCOMMON=${PATHTOCOMMON:-/home/vagrant/os-challenge-common}
SERVER=${SERVER:-192.168.101.10}
PORT=${PORT:-5003}
SEED=${SEED:-5041}
TOTAL=${TOTAL:-500}
START=${START:-0}
DIFFICULTY=${DIFFICULTY:-30000000}
REP_PROB_PERCENT=${REP_PROB_PERCENT:-20}
DELAY_US=${DELAY_US:-600000}
PRIO_LAMBDA=${PRIO_LAMBDA:-1.5}
EXP=${EXP:?set EXP to the experiment folder name, e.g. EXP=concurrency}
VARIANT=${VARIANT:?set VARIANT to the server running now, e.g. baseline, thread, pool, reference}
RUN=${RUN:-1}

if [ ! -d "$PATHTOCOMMON/experiments/$EXP" ]; then
    echo "No such experiment folder: experiments/$EXP" >&2
    exit 1
fi
RESULTS_DIR=$PATHTOCOMMON/experiments/$EXP/results
mkdir -p "$RESULTS_DIR"

# Which code was measured: commit hash, plus "-dirty" if tracked files had uncommitted changes
COMMIT=$(git -C "$PATHTOCOMMON" rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ -n "$(git -C "$PATHTOCOMMON" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    COMMIT="$COMMIT-dirty"
fi

OUT="$RESULTS_DIR/${VARIANT}_t${TOTAL}_d${DELAY_US}_s${SEED}_r${RUN}.txt"
if [ -e "$OUT" ]; then
    echo "Refusing to overwrite $OUT - use another RUN=" >&2
    exit 1
fi

echo "# exp=$EXP variant=$VARIANT run=$RUN commit=$COMMIT date=$(date -Iseconds) total=$TOTAL start=$START difficulty=$DIFFICULTY rep=$REP_PROB_PERCENT delay=$DELAY_US lambda=$PRIO_LAMBDA seed=$SEED" > "$OUT"

CLIENT="$PATHTOCOMMON/$(cd "$PATHTOCOMMON" && ./get-bin-path.sh)/client"
"$CLIENT" "$SERVER" "$PORT" "$SEED" "$TOTAL" "$START" "$DIFFICULTY" "$REP_PROB_PERCENT" "$DELAY_US" "$PRIO_LAMBDA" | tee -a "$OUT"

echo "Saved: $OUT"
grep '^Results:' "$OUT" || echo "WARNING: no 'Results:' line - the run did not finish" >&2