// LZO1X codec used by the Metin2 patcher archives.
//
// Based on the LZO1X-1 code shipped with balika01's "Metin2 Original Patcher File Archiver"
// (https://www.elitepvpers.com/forum/metin2-pserver-guides-strategies/1965355-release-metin2-original-patcher-file-archiver.html),
// itself derived from LZO by Markus F.X.J. Oberhumer. The compressor is not stock LZO1X-1: its matches grow in steps of
// 4 bytes, so its output is reproduced here byte for byte rather than replaced by a library.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mt2lz::lzo1x
{
// Outcome of decompress(). The values are printed by the command line tool, so they keep the numbers it always used.
enum class Status : int
{
	Ok = 1,
	InputOverrun = -1,
	EofNotFound = -2,
	InputNotConsumed = -3,
	OutputOverrun = -4,
	LookbehindOverrun = -5,
};

struct DecompressResult
{
	Status status;
	std::size_t written; // bytes stored in the output buffer
};

// Largest stream compress() can produce for an input of `size` bytes.
constexpr std::size_t max_compressed_size(std::size_t size) noexcept
{
	return size + size / 16 + 64 + 3;
}

// Compresses `input` into a complete LZO1X stream, end marker included.
std::vector<std::uint8_t> compress(std::span<const std::uint8_t> input);

// Decodes the stream in `input` into `output`. Corrupt streams are rejected without reading or writing out of bounds.
DecompressResult decompress(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) noexcept;
} // namespace mt2lz::lzo1x
