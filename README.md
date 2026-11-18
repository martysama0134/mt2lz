# mt2lz

Packs and unpacks single files with the LZO1X variant used by the Metin2 patcher.

Fork of https://www.elitepvpers.com/forum/metin2-pserver-guides-strategies/1965355-release-metin2-original-patcher-file-archiver.html (by balika01)

## Usage

```
mt2lz pack   <path to in file> <path to out file>
mt2lz unpack <path to in file> <path to out file>
```

A packed file is the unpacked size (32-bit little endian) followed by the LZO1X stream.

## Build

Open `mt2lz.sln` in Visual Studio 2022 or later (C++20, toolset v143) and build `Release|x64` or `Release|x86`.

## Layout

- `mt2lz/lzo1x.hpp`, `mt2lz/lzo1x.cpp`: the codec. The compressor is not stock LZO1X-1 (its matches grow 4 bytes at a
  time), so it is kept as is to produce the same packed bytes.
- `mt2lz/main.cpp`: the command line tool.
- `python/mt2lz.py`: a pure Python port (standard library only, Python 3.8+) with the same output, usable as a script
  (`python mt2lz.py pack <in> <out>`) or as a module (`compress(data)`, `decompress(stream, size)`). With python-lzo
  installed it unpacks through liblzo (same results, much faster), and `--lzo` or `--lzo=9` packs with liblzo: smaller
  or faster files that mt2lz.exe unpacks, though not the same bytes it packs.
