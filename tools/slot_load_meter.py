"""Read-only 0x7f3e slot-cycle windows. Transport supplied by caller; no IO."""
import struct
from load_meter import WINDOWS, BLOCKS

COMMAND = 0x7f3e
SLOTS = 6

def request(index, token=1):
    if type(index) is not int or not 0 <= index < WINDOWS:
        raise ValueError('invalid slot load index')
    if type(token) is not int or not 1 <= token <= 0xffffffff:
        raise ValueError('invalid slot load token')
    return struct.pack('<HHIII', COMMAND, 16, 1, token, index)

def decode(data, index, token=1):
    request(index, token)
    if len(data) != 84:
        raise ValueError('invalid slot load reply length')
    cmd, size, version, actual, status, seq, blocks, profiled, fallback, mask, *slots = struct.unpack('<HH20I', data)
    if (cmd, size, version, actual) != (COMMAND, 84, 1, token):
        raise ValueError('mismatched slot load reply')
    if status == 18:
        if any((seq, blocks, profiled, fallback, mask, *slots)):
            raise ValueError('invalid unavailable reply')
        return dict(index=index, available=False)
    if status or not seq or blocks != BLOCKS or profiled + fallback > blocks or mask > 63 or (mask and not profiled):
        raise ValueError('invalid slot load observation')
    maxima, types = slots[:SLOTS], slots[SLOTS:]
    for i in range(SLOTS):
        if not mask & (1 << i) and (maxima[i] or types[i]):
            raise ValueError('unmeasured slot has data')
    return dict(index=index, available=True, seq=seq, blocks=blocks,
                profiled_blocks=profiled, fallback_blocks=fallback, slot_mask=mask,
                slot_max_cycles=maxima, slot_types=types)

def read(exchange, index, token=1):
    return decode(exchange(request(index, token)), index, token)
