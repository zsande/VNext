#!/bin/sh
# Runs a check program ($1, with its arguments), killing it past 2 GB of memory or $LIMIT seconds (default 240).
LIMIT=${LIMIT:-240}
"$@" &
P=$!
T=0
while kill -0 $P 2>/dev/null; do
  R=$(ps -o rss= -p $P 2>/dev/null | tr -d ' ')
  if [ -n "$R" ] && [ "$R" -gt 2000000 ]; then kill -9 $P; echo "KILLED: over 2 GB of memory"; exit 99; fi
  if [ $T -ge $((LIMIT * 2)) ]; then kill -9 $P; echo "KILLED: over $LIMIT seconds"; exit 98; fi
  sleep 0.5; T=$((T + 1))
done
wait $P
