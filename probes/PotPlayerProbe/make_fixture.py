"""Generate a small, silent, uncompressed AVI without external codecs."""
import struct
from pathlib import Path

def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data + (b'\0' if len(data) % 2 else b'')

def make(path, seconds=30):
    w, h, fps = 64, 64, 10
    size, frames = w*h*3, seconds*fps
    avih = struct.pack('<14I', 1000000//fps, size*fps, 0, 16, frames, 0, 1, size, w, h, 0, 0, 0, 0)
    strh = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'vids', b'DIB ', 0, 0, 0, 0, 1, fps, 0, frames, size, 0xffffffff, 0, 0, 0, w, h)
    strf = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, size, 0, 0, 0, 0)
    hdrl = chunk(b'LIST', b'hdrl' + chunk(b'avih', avih) + chunk(b'LIST', b'strl' + chunk(b'strh', strh) + chunk(b'strf', strf)))
    data, index, offset = [], [], 4
    for i in range(frames):
        frame = bytes((i % 256, 70, 120)) * (w*h)
        data.append(chunk(b'00db', frame))
        index.append(struct.pack('<4sIII', b'00db', 16, offset, size))
        offset += 8 + size
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(chunk(b'RIFF', b'AVI ' + hdrl + chunk(b'LIST', b'movi' + b''.join(data)) + chunk(b'idx1', b''.join(index))))

if __name__ == '__main__':
    make(Path(__file__).resolve().parents[2] / 'out' / 'fixtures' / '测试 影片.avi')
