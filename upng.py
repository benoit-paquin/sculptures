# upng.py — Lightweight PNG decoder for MicroPython
# Returns dictionary with 'width', 'height', 'rgba'

import struct

def decode(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError("Not a PNG file")
    i = 8
    chunks = []
    while i < len(data):
        length = struct.unpack(">I", data[i:i+4])[0]
        chunk_type = data[i+4:i+8]
        chunk_data = data[i+8:i+8+length]
        chunks.append((chunk_type, chunk_data))
        i += length + 12  # length + type + data + CRC
        if chunk_type == b'IEND':
            break

    width = height = 0
    bit_depth = color_type = 0
    idat_data = b''

    for ctype, cdata in chunks:
        if ctype == b'IHDR':
            width, height, bit_depth, color_type, _, _, _ = struct.unpack(">IIBBBBB", cdata)
        elif ctype == b'IDAT':
            idat_data += cdata

    import zlib
    raw = zlib.decompress(idat_data)

    # Simple unfilter for no interlace, color_type 6 (RGBA), 8-bit
    stride = width*4
    rgba = bytearray(height*stride)
    p = 0
    i = 0
    for row in range(height):
        filter_type = raw[i]
        i += 1
        for col in range(stride):
            rgba[p] = raw[i]
            i += 1
            p += 1
    return {'width': width, 'height': height, 'rgba': rgba}
