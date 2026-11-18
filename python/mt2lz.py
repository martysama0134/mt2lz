#!/usr/bin/env python3
"""mt2lz: packs and unpacks single files with the LZO1X variant of the Metin2 patcher.

Python port of the C++ tool in ../mt2lz: same packed bytes, same unpacked bytes, same messages and exit codes.
A packed file is the unpacked size as a 32-bit little endian number, followed by the LZO1X stream.

Usable as a module too:

    import mt2lz
    stream = mt2lz.compress(data)
    status, data = mt2lz.decompress(stream, size)

Standard library only, Python 3.8+.
"""

import sys
from array import array

__all__ = [
    "OK", "INPUT_OVERRUN", "EOF_NOT_FOUND", "INPUT_NOT_CONSUMED", "OUTPUT_OVERRUN", "LOOKBEHIND_OVERRUN",
    "compress", "decompress", "pack", "unpack", "main",
]

# Outcome of decompress(). The values are printed by the command line tool, so they keep the numbers it always used.
OK = 1
INPUT_OVERRUN = -1
EOF_NOT_FOUND = -2
INPUT_NOT_CONSUMED = -3
OUTPUT_OVERRUN = -4
LOOKBEHIND_OVERRUN = -5

# Compressor parameters. They are part of the output: changing any of them changes the packed bytes.
_BLOCK_SIZE = 49152  # the input is compressed in blocks of this size
_BLOCK_TAIL = 20  # no match starts in the last bytes of a block
_DICT_BITS = 14
_DICT_SIZE = 1 << _DICT_BITS
_HASH_MULTIPLIER = 0x1824429D

# Instruction set.
_M2_MAX_LENGTH = 8
_M2_MAX_OFFSET = 0x0800
_M3_MAX_LENGTH = 33
_M3_MAX_OFFSET = 0x4000
_M4_MAX_LENGTH = 9
_M3_MARKER = 32
_M4_MARKER = 16
_MAX_OPENING_LITERALS = 238  # a stream may start with 17 + n for its first n literals
_END_OF_STREAM = bytes((_M4_MARKER | 1, 0, 0))

_HEADER_SIZE = 4


# --- compressor -------------------------------------------------------------------------------------------------------

def _word_tables(data):
    """Little endian 32-bit words at every position: the word at i is tables[i & 3][i >> 2]."""
    code = next(c for c in "IL" if array(c).itemsize == 4)
    tables = []
    for start in range(4):
        table = array(code)
        table.frombytes(data[start:start + (len(data) - start) // 4 * 4])
        if sys.byteorder == "big":
            table.byteswap()
        tables.append(table)
    return tables


def _mismatch(data, a, b, n):
    """Offset of the first byte where data[a:a+n] and data[b:b+n] differ, or n if they are equal."""
    done = 0
    step = 16
    while done < n:
        size = min(step, n - done)
        if data[a + done:a + done + size] != data[b + done:b + done + size]:
            low, high = done, done + size  # the first difference is in [low, high)
            while high - low > 8:
                middle = (low + high) >> 1
                if data[a + low:a + middle] == data[b + low:b + middle]:
                    low = middle
                else:
                    high = middle
            while data[a + low] == data[b + low]:
                low += 1
            return low
        done += size
        step <<= 1
    return n


def _match_length(data, words, ip, candidate, ip_end):
    """Matches are measured 4 bytes at a time; a partial last word is left to the literals that follow.

    The search stops at the first word that differs, or at the first length from 8 up that reaches ip_end.
    """
    if words[(ip + 4) & 3][(ip + 4) >> 2] != words[(candidate + 4) & 3][(candidate + 4) >> 2]:
        return 4
    bound = max(8, (ip_end - ip + 3) & ~3)
    differ = 8 + _mismatch(data, ip + 8, candidate + 8, bound - 8)
    return min(differ & ~3, bound)


def _emit_long_count(out, count):
    """A long count is spelled as zero bytes worth 255 each, then the remainder (1-255)."""
    zeros = (count - 1) // 255
    if zeros:
        out += bytes(zeros)
    out.append(count - zeros * 255)


def _emit_literals(out, data, start, count):
    if count <= 3:
        out[-2] |= count  # stored in the low bits of the previous match
    elif count <= 18:
        out.append(count - 3)
    else:
        out.append(0)
        _emit_long_count(out, count - 18)
    out += data[start:start + count]


def _emit_match(out, length, offset):
    if length <= _M2_MAX_LENGTH and offset <= _M2_MAX_OFFSET:
        offset -= 1
        out.append(((length - 1) << 5) | ((offset & 7) << 2))
        out.append(offset >> 3)
        return

    if offset <= _M3_MAX_OFFSET:
        offset -= 1
        if length <= _M3_MAX_LENGTH:
            out.append(_M3_MARKER | (length - 2))
        else:
            out.append(_M3_MARKER)
            _emit_long_count(out, length - _M3_MAX_LENGTH)
    else:
        offset -= _M3_MAX_OFFSET
        marker = _M4_MARKER | ((offset >> 11) & 8)
        if length <= _M4_MAX_LENGTH:
            out.append(marker | (length - 2))
        else:
            out.append(marker)
            _emit_long_count(out, length - _M4_MAX_LENGTH)
    out.append((offset << 2) & 0xFF)
    out.append((offset >> 6) & 0xFF)


def _compress_block(data, words, base, size, pending, out):
    """Compresses data[base:base+size]. `pending` literals of earlier blocks sit right before it and are not written
    yet. Returns the literals still pending at the end of this block."""
    w0, w1, w2, w3 = words
    by_offset = (w0, w1, w2, w3)
    table = [0] * _DICT_SIZE
    in_end = base + size
    ip_end = in_end - _BLOCK_TAIL
    ip = base + (4 - pending if pending < 4 else 0)
    ii = base  # first byte not written yet

    # the longer the search goes without a match, the bigger the steps; past ip_end the block is over anyway
    step = 1 + ((ip - ii) >> 5)
    ip = ip + step if ip < ip_end and step < ip_end - ip else ip_end

    while ip < ip_end:
        word = by_offset[ip & 3][ip >> 2]
        slot = ((word * _HASH_MULTIPLIER) & 0xFFFFFFFF) >> (32 - _DICT_BITS)
        candidate = base + table[slot]
        table[slot] = ip - base
        if word != by_offset[candidate & 3][candidate >> 2]:
            step = 1 + ((ip - ii) >> 5)
            ip = ip + step if step < ip_end - ip else ip_end
            continue

        ii -= pending
        pending = 0
        if ip != ii:
            _emit_literals(out, data, ii, ip - ii)

        length = _match_length(data, by_offset, ip, candidate, ip_end)
        _emit_match(out, length, ip - candidate)
        ip += length
        ii = ip

    return in_end - ii + pending


def compress(data):
    """Compresses `data` (any bytes-like object) into a complete LZO1X stream, end marker included."""
    data = bytes(data)
    words = _word_tables(data)
    out = bytearray()

    position = 0
    left = len(data)
    pending = 0
    while left > _BLOCK_TAIL:
        block = min(left, _BLOCK_SIZE)
        if (pending + block) >> 5 == 0:
            break  # under 32 bytes to go: not worth a search, they all become literals
        pending = _compress_block(data, words, position, block, pending, out)
        position += block
        left -= block
    pending += left

    if pending > 0:
        start = len(data) - pending
        if not out and pending <= _MAX_OPENING_LITERALS:
            out.append(17 + pending)
            out += data[start:]
        else:
            _emit_literals(out, data, start, pending)
    out += _END_OF_STREAM
    return bytes(out)


# --- decompressor -----------------------------------------------------------------------------------------------------

class _Stop(Exception):
    def __init__(self, status):
        super().__init__(status)
        self.status = status


# An instruction below 16 means something different depending on what came before it.
_AFTER_MATCH = 0  # stream start, or a match with no literals after it: a run of 4+ literals
_AFTER_TRAILING_LITERALS = 1  # a match followed by 1-3 literals: a 2-byte match
_AFTER_LITERAL_RUN = 2  # a run of 4+ literals: a 3-byte match


def _read_count(src, ip, base, capacity):
    """Counts too big for an instruction: zero bytes add 255 each, the first nonzero byte ends them."""
    end = len(src)
    count = base
    while True:
        if ip >= end:
            raise _Stop(INPUT_OVERRUN)
        value = src[ip]
        ip += 1
        if value:
            return count + value, ip
        count += 255
        if count > capacity:
            raise _Stop(OUTPUT_OVERRUN)


def decompress(src, capacity):
    """Decodes the LZO1X stream in `src` into at most `capacity` bytes.

    Returns (status, data): status is OK or one of the error codes, data what was decoded until then. Corrupt
    streams are rejected without reading or writing out of bounds.
    """
    src = bytes(src)
    end = len(src)
    out = bytearray()
    if end == 0:
        return EOF_NOT_FOUND, bytes(out)

    def copy_literals(ip, count):
        if count > end - ip:
            raise _Stop(INPUT_OVERRUN)
        if count > capacity - len(out):
            raise _Stop(OUTPUT_OVERRUN)
        out.extend(src[ip:ip + count])
        return ip + count

    try:
        ip = 0
        context = _AFTER_MATCH
        if src[0] > 17:
            count = src[0] - 17
            ip = copy_literals(1, count)
            context = _AFTER_TRAILING_LITERALS if count < 4 else _AFTER_LITERAL_RUN

        while True:
            # Where the end of the input is checked, and what that returns, follows the original decoder.
            if context == _AFTER_MATCH:
                if ip >= end:
                    return EOF_NOT_FOUND, bytes(out)
            elif context == _AFTER_TRAILING_LITERALS:
                # the original read this byte before checking and stopped if it was the last one
                if end - ip <= 1:
                    return EOF_NOT_FOUND, bytes(out)
            elif ip >= end:
                raise _Stop(INPUT_OVERRUN)
            instruction = src[ip]
            ip += 1

            if instruction < 16:
                if context == _AFTER_MATCH:
                    count = instruction
                    if count == 0:
                        count, ip = _read_count(src, ip, 15, capacity)
                    ip = copy_literals(ip, count + 3)
                    context = _AFTER_LITERAL_RUN
                    continue

                if ip >= end:
                    raise _Stop(INPUT_OVERRUN)
                distance = 1 + (instruction >> 2) + (src[ip] << 2)
                ip += 1
                length = 2
                if context == _AFTER_LITERAL_RUN:
                    distance += _M2_MAX_OFFSET
                    length = 3
            elif instruction >= 64:  # M2
                if ip >= end:
                    raise _Stop(INPUT_OVERRUN)
                distance = 1 + ((instruction >> 2) & 7) + (src[ip] << 3)
                ip += 1
                length = (instruction >> 5) + 1
            elif instruction >= 32:  # M3
                length = instruction & 31
                if length == 0:
                    length, ip = _read_count(src, ip, 31, capacity)
                if end - ip < 2:
                    raise _Stop(INPUT_OVERRUN)
                distance = 1 + ((src[ip] | src[ip + 1] << 8) >> 2)
                ip += 2
                length += 2
            else:  # M4, which also encodes the end of the stream
                length = instruction & 7
                if length == 0:
                    length, ip = _read_count(src, ip, 7, capacity)
                if end - ip < 2:
                    raise _Stop(INPUT_OVERRUN)
                distance = ((instruction & 8) << 11) + ((src[ip] | src[ip + 1] << 8) >> 2)
                ip += 2
                if distance == 0:
                    return (OK if ip == end else INPUT_NOT_CONSUMED), bytes(out)
                distance += _M3_MAX_OFFSET
                length += 2

            # copy the match; source and destination overlap when distance < length, which repeats the source
            written = len(out)
            if distance > written:
                raise _Stop(LOOKBEHIND_OVERRUN)
            if length > capacity - written:
                raise _Stop(OUTPUT_OVERRUN)
            start = written - distance
            if distance >= length:
                out += out[start:start + length]
            else:
                out += (out[start:] * (length // distance + 1))[:length]

            # the low 2 bits of the byte two back say how many literals follow the match
            trailing = src[ip - 2] & 3
            if trailing == 0:
                context = _AFTER_MATCH
                continue
            ip = copy_literals(ip, trailing)
            context = _AFTER_TRAILING_LITERALS
    except _Stop as stop:
        return stop.status, bytes(out)


# --- command line -----------------------------------------------------------------------------------------------------

def _print(text):
    sys.stdout.write(text)


def _wait_for_key():
    _print("Press a key to continue...\n")
    sys.stdout.flush()
    if sys.stdin is not None:
        sys.stdin.read(1)


def _program_name(path):
    """The part of argv[0] after the last backslash, without its extension."""
    start = path.rfind("\\") + 1
    dot = path.rfind(".")
    return path[start:dot] if dot >= start else path[start:]


def _usage(argv0):
    _print("Usage: %s <pack / unpack> <path to in file> <path to out file>\n" % _program_name(argv0))
    _wait_for_key()
    return -1


def _read_input(path):
    """Reads the whole file, or says why it could not and returns None."""
    try:
        file = open(path, "rb")
    except (OSError, ValueError):
        _print("Could not open %s file.\n" % path)
        _wait_for_key()
        return None
    with file:
        try:
            return file.read()
        except OSError:
            _print("Could not read %s file.\n" % path)
            _wait_for_key()
            return None


def _write_output(path, *parts):
    """Failing to create the output file is silent, as it always was."""
    try:
        with open(path, "wb") as file:
            for part in parts:
                file.write(part)
    except (OSError, ValueError):
        pass


def pack(in_path, out_path):
    data = _read_input(in_path)
    if data is None:
        return 0
    if len(data) > 0xFFFFFFFF:
        _print("%s is too big to be packed.\n" % in_path)
        return 0
    _write_output(out_path, len(data).to_bytes(_HEADER_SIZE, "little"), compress(data))
    return 0


def unpack(in_path, out_path):
    packed = _read_input(in_path)
    if packed is None:
        return 0
    if len(packed) < _HEADER_SIZE:
        _print("%s is too short to be a packed file.\n" % in_path)
        return 0

    size = int.from_bytes(packed[:_HEADER_SIZE], "little")
    status, data = decompress(memoryview(packed)[_HEADER_SIZE:], size)
    if status != OK or len(data) != size:
        _print("There was an error when decompressing %s file.\n" % in_path)
        _print("unpacked %d of %d bytes (lzo %d)\n" % (len(data), size, status))
        return 0
    _write_output(out_path, data)
    return 0


def main(argv=None):
    argv = sys.argv if argv is None else argv
    argv0 = argv[0] if argv else ""
    if len(argv) != 4:
        return _usage(argv0)

    mode, in_path, out_path = argv[1:]
    try:
        if mode == "unpack":
            return unpack(in_path, out_path)
        if mode == "pack":
            return pack(in_path, out_path)
    except MemoryError:
        _print("Memory error when make buffer for %s file.\n" % in_path)
        _wait_for_key()
        return 0
    return _usage(argv0)


if __name__ == "__main__":
    sys.exit(main())
