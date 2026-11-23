#!/bin/sh
# live.sh BIN SECONDS: run the live harness, read its channel's xruns before it ends
"$1" "$2" &
pid=$!
sleep $(( $2 - 1 ))
sndctl --libxo json -f /dev/dsp4 -v | python3 -c '
import json,sys
d=json.load(sys.stdin)
def walk(o):
    if isinstance(o,dict):
        if "xruns" in o and str(o.get("pid"))==sys.argv[1]: print("pid", o["pid"], o.get("name"), "xruns", o["xruns"], "interrupts", o.get("interrupts"))
        for v in o.values(): walk(v)
    elif isinstance(o,list):
        for v in o: walk(v)
walk(d)' $pid
wait $pid
