#!/bin/sh
# Close the running Telink SWS app (if any), then rebuild + install + launch it.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT/tools" && python3 -c "
import fcli, time
s = fcli.open_cli()
if 'Telink' in fcli.cmd(s, 'loader info'):
    for t in ('press', 'short', 'release'):
        fcli.cmd(s, 'input send back ' + t)
    time.sleep(1)
"
cd "$ROOT/telink_sws" && UFBT_HOME="$ROOT/.ufbt" ufbt launch
