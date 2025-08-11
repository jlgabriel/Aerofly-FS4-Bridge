# Python App Tutorial: Shared Memory and TCP

Build two small Python examples to read live telemetry from Aerofly Bridge and send commands.

- Part 1: Shared Memory (fastest local access)
- Part 2: TCP (JSON streaming + command channel)

## Prerequisites

- Python 3.9+
- Aerofly FS 4 with the Bridge DLL running
- Files in this repo available (particularly `docs/api_reference.md` for offsets format)

## Part 1 — Shared Memory (read-only telemetry)

The Bridge exposes a memory-mapped file named `AeroflyBridgeData` with a fixed layout. A JSON offsets file (`AeroflyBridge_offsets.json`) accompanies the DLL describing the byte offsets for each variable.

Minimal client:

```python
import json
import mmap
import struct

class AeroflySharedMemory:
    def __init__(self, offsets_path='AeroflyBridge_offsets.json'):
        with open(offsets_path, 'r') as f:
            self.offsets = json.load(f)
        self.var_by_name = {v['name']: v for v in self.offsets['variables']}
        # Map read-only shared memory (Windows named mapping)
        self.mm = mmap.mmap(-1, 0, 'AeroflyBridgeData')

    def is_valid(self) -> bool:
        # valid flag at bytes [8..12)
        return struct.unpack('I', self.mm[8:12])[0] == 1

    def timestamp_us(self) -> int:
        return struct.unpack('Q', self.mm[0:8])[0]

    def get(self, name: str) -> float:
        v = self.var_by_name[name]
        off = v['offset']
        return struct.unpack('d', self.mm[off:off+8])[0]

    def get_by_index(self, index: int) -> float:
        base = self.offsets['array_base_offset']
        stride = self.offsets['stride_bytes']
        off = base + index * stride
        return struct.unpack('d', self.mm[off:off+8])[0]

if __name__ == '__main__':
    shm = AeroflySharedMemory()
    if shm.is_valid():
        alt = shm.get('Aircraft.Altitude')
        ias = shm.get('Aircraft.IndicatedAirspeed')
        hdg = shm.get('Aircraft.TrueHeading')
        print(f"ALT={alt:.0f} m  IAS={(ias*1.94384):.1f} kt  HDG={(hdg*180/3.14159265):.0f}°  t={shm.timestamp_us()}µs")
```

Notes:
- Shared memory is read-only from external apps.
- Use `get_by_index` for higher throughput loops.

## Part 2 — TCP (JSON streaming + commands)

The Bridge provides:
- Data stream on TCP `12345` (read-only JSON frames)
- Command channel on TCP `12346` (write-only JSON)

Read telemetry:

```python
import socket, json

def read_telemetry(host='localhost', port=12345):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    try:
        buf = b''
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            buf += chunk
            # frames delimited by newlines in this tutorial
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                if not line:
                    continue
                frame = json.loads(line.decode('utf-8'))
                if frame.get('data_valid') != 1:
                    continue
                v = frame.get('variables', {})
                print(f"ALT={v.get('Aircraft.Altitude', 0):.0f} m  IAS={(v.get('Aircraft.IndicatedAirspeed', 0)*1.94384):.1f} kt")
    finally:
        sock.close()

if __name__ == '__main__':
    read_telemetry()
```

Send a command:

```python
import socket, json

def send_command(name, value, host='localhost', port=12346):
    cmd = { 'variable': name, 'value': value }
    data = (json.dumps(cmd) + '\n').encode('utf-8')
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    try:
        sock.sendall(data)
    finally:
        sock.close()

if __name__ == '__main__':
    # Set throttle to 0.6
    send_command('Aircraft.Throttle', 0.6)
```

## Troubleshooting

- Connection refused: ensure Aerofly is running and the Bridge started successfully.
- Firewall: allow inbound on ports 12345, 12346 (TCP).
- No data: you must be in an active flight; check `data_valid == 1`.
- Performance: for high-rate loops, prefer Shared Memory and use indices.

## Next steps

- Explore variable names in the [Variables Reference](variables_reference.md)
- See protocol details in the [API Reference](api_reference.md)
- When building GUIs, throttle UI updates (e.g., 10–20 Hz) even if telemetry is 50 Hz
