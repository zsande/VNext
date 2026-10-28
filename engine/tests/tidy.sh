#!/bin/sh
# Runs clang-tidy (Homebrew's llvm: brew install llvm; or set CLANG_TIDY) on the engine's own files with
# engine/.clang-tidy, four at a time. It reads the build's compile_commands.json (configure the player
# first), for arm64 only: clang-tidy takes one target. What it finds goes in build.nosync/tidy/*.log.
H=$(cd "$(dirname "$0")" && pwd)
E=$(cd "$H/.." && pwd)
B=$E/build.nosync
CT=${CLANG_TIDY:-/opt/homebrew/opt/llvm/bin/clang-tidy}
[ -x "$CT" ] || { echo "No clang-tidy at $CT: brew install llvm, or set CLANG_TIDY."; exit 1; }
[ -f "$B/compile_commands.json" ] || { echo "No $B/compile_commands.json: configure the player first."; exit 1; }
mkdir -p "$B/tidy"
python3 - "$B" <<'PY'
import json, sys
b = sys.argv[1]
own = [e for e in json.load(open(b + "/compile_commands.json")) if "/engine/src/" in e["file"]]
for e in own:  # (one target: arm64)
    if "command" in e:
        e["command"] = e["command"].replace(" -arch x86_64", "")
    else:
        a, out, i = e["arguments"], [], 0
        while i < len(a):
            if a[i] == "-arch" and i + 1 < len(a) and a[i + 1] == "x86_64":
                i += 2
                continue
            out.append(a[i])
            i += 1
        e["arguments"] = out
json.dump(own, open(b + "/tidy/compile_commands.json", "w"), indent=1)
open(b + "/tidy/files.txt", "w").write("\n".join(e["file"] for e in own) + "\n")  # (what this build compiles: not Windows.cpp here)
PY
n=0
for f in $(cat "$B/tidy/files.txt"); do
  "$CT" -p "$B/tidy" --config-file="$E/.clang-tidy" "$f" > "$B/tidy/$(basename "$f").log" 2>&1 &
  n=$((n + 1))
  [ $((n % 4)) = 0 ] && wait
done
wait
total=0
for f in "$B"/tidy/*.log; do
  c=$(grep -c "warning:" "$f")
  total=$((total + c))
  printf "%-16s %s\n" "$(basename "$f" .log)" "$c"
done
echo "$total in all (the same one in a header counts once for each file that includes it)"
