"""Minimal synthetic loopback STUN responder, not a general STUN service."""
import socket
import struct

MAGIC = 0x2112A442
FAKE_IP = '203.0.113.17'
FAKE_PORT = 40113


def binding_response(request):
    """Return a fixed TEST-NET mapped address for a framed Binding request."""
    if not 20 <= len(request) <= 2048:
        return None
    kind, size, cookie, tid = struct.unpack('>HHI12s', request[:20])
    if kind != 1 or cookie != MAGIC or size != len(request) - 20 or size % 4:
        return None
    cursor = 20
    while cursor < len(request):
        if len(request) - cursor < 4:
            return None
        length = struct.unpack_from('>H', request, cursor + 2)[0]
        cursor += 4 + ((length + 3) & ~3)
        if cursor > len(request):
            return None
    address = struct.unpack('>I', socket.inet_aton(FAKE_IP))[0] ^ MAGIC
    attribute = struct.pack('>HHBBHI', 0x20, 8, 0, 1,
                            FAKE_PORT ^ (MAGIC >> 16), address)
    return struct.pack('>HHI12s', 0x101, len(attribute), MAGIC, tid) + attribute
