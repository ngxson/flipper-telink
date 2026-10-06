import serial, sys, time
def open_cli(port='/dev/cu.usbmodemflip_Ngxson1'):
    s = serial.Serial(port, 230400, timeout=0.3)
    s.write(b'\r\n'); time.sleep(0.3); s.read(65536)
    return s
def cmd(s, c, wait=0.5):
    s.reset_input_buffer()
    s.write(c.encode() + b'\r\n')
    out = b''; t = time.time()
    while time.time() - t < wait:
        d = s.read(4096)
        if d: out += d; t = time.time()
        if out.endswith(b'>: '): break
    return out.decode(errors='replace')
if __name__ == '__main__':
    s = open_cli()
    for c in sys.argv[1:]:
        print(cmd(s, c, 1.0))
