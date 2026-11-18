#include "lzo1x.hpp"

#include <algorithm>
#include <cstring>

namespace mt2lz::lzo1x
{
namespace
{
// Compressor parameters. They are part of the output: changing any of them changes the packed bytes.
constexpr std::size_t kBlockSize = 49152; // the input is compressed in blocks of this size
constexpr std::size_t kBlockTail = 20; // no match starts in the last bytes of a block
constexpr unsigned kDictBits = 14;
constexpr std::size_t kDictSize = std::size_t{1} << kDictBits;

// Instruction set.
constexpr std::size_t kM2MaxLength = 8;
constexpr std::size_t kM2MaxOffset = 0x0800;
constexpr std::size_t kM3MaxLength = 33;
constexpr std::size_t kM3MaxOffset = 0x4000;
constexpr std::size_t kM4MaxLength = 9;
constexpr std::size_t kM3Marker = 32;
constexpr std::size_t kM4Marker = 16;
constexpr std::size_t kMaxOpeningLiterals = 238; // a stream may start with 17 + n for its first n literals

std::uint32_t load_le32(const std::uint8_t * p) noexcept
{
	return std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8 | std::uint32_t{p[2]} << 16 | std::uint32_t{p[3]} << 24;
}

std::size_t hash(std::uint32_t value) noexcept
{
	return ((value * 0x1824429du) >> (32 - kDictBits)) & (kDictSize - 1);
}

// Writes instructions to a buffer sized for the worst case up front.
class Emitter
{
public:
	explicit Emitter(std::uint8_t * out) noexcept : op_{out} {}

	std::uint8_t * position() const noexcept { return op_; }

	void byte(std::size_t value) noexcept { *op_++ = static_cast<std::uint8_t>(value); }

	void bytes(const std::uint8_t * src, std::size_t count) noexcept
	{
		std::memcpy(op_, src, count);
		op_ += count;
	}

	// A long count is spelled as zero bytes worth 255 each, then the remainder.
	void long_count(std::size_t count) noexcept
	{
		while (count > 255)
		{
			count -= 255;
			byte(0);
		}
		byte(count);
	}

	void literals(const std::uint8_t * src, std::size_t count) noexcept
	{
		if (count <= 3)
			op_[-2] |= static_cast<std::uint8_t>(count); // stored in the low bits of the previous match
		else if (count <= 18)
			byte(count - 3);
		else
		{
			byte(0);
			long_count(count - 18);
		}
		bytes(src, count);
	}

	void match(std::size_t length, std::size_t offset) noexcept
	{
		if (length <= kM2MaxLength && offset <= kM2MaxOffset)
		{
			offset -= 1;
			byte(((length - 1) << 5) | ((offset & 7) << 2));
			byte(offset >> 3);
			return;
		}

		if (offset <= kM3MaxOffset)
		{
			offset -= 1;
			if (length <= kM3MaxLength)
				byte(kM3Marker | (length - 2));
			else
			{
				byte(kM3Marker);
				long_count(length - kM3MaxLength);
			}
		}
		else
		{
			offset -= kM3MaxOffset;
			const std::size_t marker = kM4Marker | ((offset >> 11) & 8);
			if (length <= kM4MaxLength)
				byte(marker | (length - 2));
			else
			{
				byte(marker);
				long_count(length - kM4MaxLength);
			}
		}
		byte(offset << 2);
		byte(offset >> 6);
	}

	void end_of_stream() noexcept
	{
		byte(kM4Marker | 1);
		byte(0);
		byte(0);
	}

private:
	std::uint8_t * op_;
};

// Matches are measured 4 bytes at a time; a partial last word is left to the literals that follow.
std::size_t match_length(const std::uint8_t * ip, const std::uint8_t * match, const std::uint8_t * ip_end) noexcept
{
	std::size_t length = 4;
	bool same = load_le32(ip + length) == load_le32(match + length);
	while (same)
	{
		length += 4;
		same = load_le32(ip + length) == load_le32(match + length);
		if (ip + length >= ip_end)
			break;
	}
	return length;
}

// Compresses one block. `pending` literals of earlier blocks sit right before `in` and are not written yet.
// Returns the literals still pending at the end of this block.
std::size_t compress_block(const std::uint8_t * in, std::size_t size, std::size_t pending, std::uint16_t * dict, Emitter & out) noexcept
{
	const std::uint8_t * const in_end = in + size;
	const std::uint8_t * const ip_end = in_end - kBlockTail;
	const std::uint8_t * ip = in + (pending < 4 ? 4 - pending : 0);
	const std::uint8_t * ii = in; // first byte not written yet

	// the longer the search goes without a match, the bigger the steps; past ip_end the block is over anyway
	const auto skip = [&]
	{
		const std::size_t step = 1 + (static_cast<std::size_t>(ip - ii) >> 5);
		ip = ip < ip_end && step < static_cast<std::size_t>(ip_end - ip) ? ip + step : ip_end;
	};

	skip();
	while (ip < ip_end)
	{
		const std::uint32_t word = load_le32(ip);
		std::uint16_t & slot = dict[hash(word)];
		const std::uint8_t * const candidate = in + slot;
		slot = static_cast<std::uint16_t>(ip - in);
		if (word != load_le32(candidate))
		{
			skip();
			continue;
		}

		ii -= pending;
		pending = 0;
		if (ip != ii)
			out.literals(ii, static_cast<std::size_t>(ip - ii));

		const std::size_t length = match_length(ip, candidate, ip_end);
		out.match(length, static_cast<std::size_t>(ip - candidate));
		ip += length;
		ii = ip;
	}
	return static_cast<std::size_t>(in_end - ii) + pending;
}

class Decoder
{
public:
	Decoder(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) noexcept : in_{input}, out_{output} {}

	DecompressResult run() noexcept
	{
		const Status status = decode();
		return {status, op_};
	}

private:
	// An instruction below 16 means something different depending on what came before it.
	enum class Context
	{
		Match, // stream start, or a match with no literals after it: a run of 4+ literals
		TrailingLiterals, // a match followed by 1-3 literals: a 2-byte match
		LiteralRun, // a run of 4+ literals: a 3-byte match
	};

	Status decode() noexcept
	{
		if (in_.empty())
			return Status::EofNotFound;

		Context context = Context::Match;
		if (in_[0] > 17)
		{
			const std::size_t count = in_[0] - 17u;
			ip_ = 1;
			if (!copy_literals(count))
				return status_;
			context = count < 4 ? Context::TrailingLiterals : Context::LiteralRun;
		}

		for (;;)
		{
			// Where the end of the input is checked, and what that returns, follows the original decoder.
			std::uint8_t instruction = 0;
			switch (context)
			{
			case Context::Match:
				if (ip_ >= in_.size())
					return Status::EofNotFound;
				instruction = in_[ip_++];
				break;
			case Context::TrailingLiterals:
				// the original read this byte before checking and stopped if it was the last one
				if (in_.size() - ip_ <= 1)
					return Status::EofNotFound;
				instruction = in_[ip_++];
				break;
			case Context::LiteralRun:
				if (!read_byte(instruction))
					return status_;
				break;
			}

			std::size_t distance = 0;
			std::size_t length = 0;
			if (instruction < 16)
			{
				if (context == Context::Match)
				{
					std::size_t count = instruction;
					if (count == 0 && !read_count(15, count))
						return status_;
					if (!copy_literals(count + 3))
						return status_;
					context = Context::LiteralRun;
					continue;
				}

				std::uint8_t high = 0;
				if (!read_byte(high))
					return status_;
				distance = 1 + (instruction >> 2) + (std::size_t{high} << 2);
				length = 2;
				if (context == Context::LiteralRun)
				{
					distance += kM2MaxOffset;
					length = 3;
				}
			}
			else if (instruction >= 64) // M2
			{
				std::uint8_t high = 0;
				if (!read_byte(high))
					return status_;
				distance = 1 + ((instruction >> 2) & 7) + (std::size_t{high} << 3);
				length = (instruction >> 5) + 1;
			}
			else if (instruction >= 32) // M3
			{
				length = instruction & 31;
				if (length == 0 && !read_count(31, length))
					return status_;
				std::size_t offset = 0;
				if (!read_offset(offset))
					return status_;
				distance = 1 + offset;
				length += 2;
			}
			else // M4, which also encodes the end of the stream
			{
				length = instruction & 7;
				if (length == 0 && !read_count(7, length))
					return status_;
				std::size_t offset = 0;
				if (!read_offset(offset))
					return status_;
				distance = (std::size_t{instruction & 8u} << 11) + offset;
				if (distance == 0)
					return ip_ == in_.size() ? Status::Ok : Status::InputNotConsumed;
				distance += kM3MaxOffset;
				length += 2;
			}

			if (!copy_match(distance, length))
				return status_;

			// the low 2 bits of the byte two back say how many literals follow the match
			const std::size_t trailing = in_[ip_ - 2] & 3u;
			if (trailing == 0)
			{
				context = Context::Match;
				continue;
			}
			if (!copy_literals(trailing))
				return status_;
			context = Context::TrailingLiterals;
		}
	}

	bool fail(Status status) noexcept
	{
		status_ = status;
		return false;
	}

	bool read_byte(std::uint8_t & value) noexcept
	{
		if (ip_ >= in_.size())
			return fail(Status::InputOverrun);
		value = in_[ip_++];
		return true;
	}

	// Counts too big for an instruction: zero bytes add 255 each, the first nonzero byte ends them. The sum wraps at
	// 32 bits like the original's unsigned long, and is not checked here: the end marker reads a count it never uses.
	bool read_count(std::uint32_t base, std::size_t & count) noexcept
	{
		std::uint32_t sum = base;
		for (;;)
		{
			std::uint8_t value = 0;
			if (!read_byte(value))
				return false;
			if (value != 0)
			{
				count = static_cast<std::uint32_t>(sum + value);
				return true;
			}
			sum += 255;
		}
	}

	bool read_offset(std::size_t & offset) noexcept
	{
		if (in_.size() - ip_ < 2)
			return fail(Status::InputOverrun);
		offset = (in_[ip_] | std::size_t{in_[ip_ + 1]} << 8) >> 2;
		ip_ += 2;
		return true;
	}

	bool copy_literals(std::size_t count) noexcept
	{
		if (count > in_.size() - ip_)
			return fail(Status::InputOverrun);
		if (count > out_.size() - op_)
			return fail(Status::OutputOverrun);
		std::memcpy(out_.data() + op_, in_.data() + ip_, count);
		ip_ += count;
		op_ += count;
		return true;
	}

	bool copy_match(std::size_t distance, std::size_t length) noexcept
	{
		if (distance > op_)
			return fail(Status::LookbehindOverrun);
		if (length > out_.size() - op_)
			return fail(Status::OutputOverrun);
		// source and destination overlap when distance < length, so the copy goes byte by byte, front to back
		std::uint8_t * const dst = out_.data() + op_;
		const std::uint8_t * const src = dst - distance;
		for (std::size_t i = 0; i < length; ++i)
			dst[i] = src[i];
		op_ += length;
		return true;
	}

	std::span<const std::uint8_t> in_;
	std::span<std::uint8_t> out_;
	std::size_t ip_ = 0;
	std::size_t op_ = 0;
	Status status_ = Status::Ok;
};
} // namespace

std::vector<std::uint8_t> compress(std::span<const std::uint8_t> input)
{
	std::vector<std::uint8_t> out(max_compressed_size(input.size()));
	std::vector<std::uint16_t> dict(kDictSize);
	Emitter emit{out.data()};

	const std::uint8_t * ip = input.data();
	std::size_t left = input.size();
	std::size_t pending = 0;
	while (left > kBlockTail)
	{
		const std::size_t block = std::min(left, kBlockSize);
		if ((pending + block) >> 5 == 0)
			break; // under 32 bytes to go: not worth a search, they all become literals
		std::ranges::fill(dict, std::uint16_t{0});
		pending = compress_block(ip, block, pending, dict.data(), emit);
		ip += block;
		left -= block;
	}
	pending += left;

	if (pending > 0)
	{
		const std::uint8_t * const literals = input.data() + (input.size() - pending);
		if (emit.position() == out.data() && pending <= kMaxOpeningLiterals)
		{
			emit.byte(17 + pending);
			emit.bytes(literals, pending);
		}
		else
			emit.literals(literals, pending);
	}
	emit.end_of_stream();

	out.resize(static_cast<std::size_t>(emit.position() - out.data()));
	return out;
}

DecompressResult decompress(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) noexcept
{
	return Decoder{input, output}.run();
}
} // namespace mt2lz::lzo1x
