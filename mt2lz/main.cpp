// mt2lz: packs and unpacks single files with the LZO1X variant of the Metin2 patcher.
//
// A packed file is the unpacked size as a 32-bit little endian number, followed by the LZO1X stream.

#include "lzo1x.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <format>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace mt2lz;

constexpr std::size_t kHeaderSize = 4;

std::array<std::uint8_t, kHeaderSize> make_header(std::uint32_t size) noexcept
{
	return {static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size >> 16),
			static_cast<std::uint8_t>(size >> 24)};
}

std::uint32_t read_header(std::span<const std::uint8_t> bytes) noexcept
{
	return bytes[0] | std::uint32_t{bytes[1]} << 8 | std::uint32_t{bytes[2]} << 16 | std::uint32_t{bytes[3]} << 24;
}

struct FileCloser
{
	void operator()(std::FILE * file) const noexcept { std::fclose(file); }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

template <typename... Args> void print(std::format_string<Args...> format, Args &&... args)
{
	const std::string text = std::format(format, std::forward<Args>(args)...);
	std::fwrite(text.data(), 1, text.size(), stdout);
}

void wait_for_key()
{
	print("Press a key to continue...\n");
	std::getchar();
}

// The part of argv[0] after the last backslash, without its extension.
std::string_view program_name(std::string_view path)
{
	const std::size_t slash = path.rfind('\\');
	const std::size_t start = slash == std::string_view::npos ? 0 : slash + 1;
	const std::size_t dot = path.rfind('.');
	const std::size_t end = dot == std::string_view::npos || dot < start ? std::string_view::npos : dot;
	return path.substr(start, end - start);
}

int usage(std::string_view argv0)
{
	print("Usage: {} <pack / unpack> <path to in file> <path to out file>\n", program_name(argv0));
	wait_for_key();
	return -1;
}

// Reads the whole file, or says why it could not and returns nothing.
std::optional<std::vector<std::uint8_t>> read_input(const char * path)
{
	const File file{std::fopen(path, "rb")};
	if (!file)
	{
		print("Could not open {} file.\n", path);
		wait_for_key();
		return std::nullopt;
	}

	std::vector<std::uint8_t> data;
	std::array<std::uint8_t, 64 * 1024> chunk;
	while (const std::size_t read = std::fread(chunk.data(), 1, chunk.size(), file.get()))
		data.insert(data.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read));
	if (std::ferror(file.get()))
	{
		print("Could not read {} file.\n", path);
		wait_for_key();
		return std::nullopt;
	}
	return data;
}

// Failing to create the output file is silent, as it always was.
void write_output(const char * path, std::initializer_list<std::span<const std::uint8_t>> parts)
{
	if (const File file{std::fopen(path, "wb")})
	{
		for (const auto part : parts)
			std::fwrite(part.data(), 1, part.size(), file.get());
	}
}

int pack(const char * in_path, const char * out_path)
{
	const auto input = read_input(in_path);
	if (!input)
		return 0;
	if (input->size() > std::numeric_limits<std::uint32_t>::max())
	{
		print("{} is too big to be packed.\n", in_path);
		return 0;
	}

	const auto header = make_header(static_cast<std::uint32_t>(input->size()));
	const std::vector<std::uint8_t> stream = lzo1x::compress(*input);
	write_output(out_path, {header, stream});
	return 0;
}

int unpack(const char * in_path, const char * out_path)
{
	const auto packed = read_input(in_path);
	if (!packed)
		return 0;
	if (packed->size() < kHeaderSize)
	{
		print("{} is too short to be a packed file.\n", in_path);
		return 0;
	}

	const std::span<const std::uint8_t> bytes{*packed};
	const std::uint32_t size = read_header(bytes);
	std::vector<std::uint8_t> output(size);

	const auto [status, written] = lzo1x::decompress(bytes.subspan(kHeaderSize), output);
	if (status != lzo1x::Status::Ok || written != size)
	{
		print("There was an error when decompressing {} file.\n", in_path);
		print("unpacked {} of {} bytes (lzo {})\n", written, size, static_cast<int>(status));
		return 0;
	}
	write_output(out_path, {output});
	return 0;
}

int memory_error(const char * in_path)
{
	print("Memory error when make buffer for {} file.\n", in_path);
	wait_for_key();
	return 0;
}

int run(std::string_view mode, const char * in_path, const char * out_path, std::string_view argv0)
{
	try
	{
		if (mode == "unpack")
			return unpack(in_path, out_path);
		if (mode == "pack")
			return pack(in_path, out_path);
	}
	catch (const std::bad_alloc &)
	{
		return memory_error(in_path);
	}
	catch (const std::length_error &) // a size a vector cannot even hold
	{
		return memory_error(in_path);
	}
	return usage(argv0);
}
} // namespace

int main(int argc, char * argv[])
{
	const std::string_view argv0 = argc > 0 && argv[0] ? argv[0] : "";
	if (argc != 4)
		return usage(argv0);
	return run(argv[1], argv[2], argv[3], argv0);
}
