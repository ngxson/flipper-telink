#!/usr/bin/env python3
"""Run Flipper CLI commands and wait for each to finish (prompt), streaming output.
Usage: swscmd.py [--timeout S] "cmd1" ["cmd2" ...]"""
import sys, time
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import fcli

args = sys.argv[1:]
timeout = 600
if args and args[0] == '--timeout':
    timeout = float(args[1]); args = args[2:]
s = fcli.open_cli()
s.timeout = 0.2
for c in args:
    s.reset_input_buffer()
    s.write(c.encode() + b'\r\n')
    buf = b''; t0 = time.time()
    while time.time() - t0 < timeout:
        d = s.read(4096)
        if d:
            buf += d
            sys.stdout.write(d.decode(errors='replace')); sys.stdout.flush()
            if buf.rstrip().endswith(b'>:') and len(buf) > len(c) + 4:
                break
    else:
        print('\n[timeout waiting for prompt]')
