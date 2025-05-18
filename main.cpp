#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

// 3rd-Party bits
#include "zstd.h" // For compression

namespace fs = std::filesystem;

// OrgEngine's .orgpack File Format V0.0.2

// This is the file format for packed RELEASES for anything ill be packing for applications.
// Will probably make it a dynamic lib for internal stuff
// Or keep it as is lol

// barebones for now, will expand as i start to get my model formats/texture formats packable for now
enum class FileType : uint8_t
{
	Image,
	Audio,
	Mesh,
	Script,
	Unknown
};

enum class CompressionType : uint8_t
{
	LZ4,
	ZSTD,
	None
};

struct alignas(128) File
{
	uint64_t offset{};
	uint64_t uncompressedSize{};
	uint64_t compressedSize{};

	char name[96]{};

	// union
	// {
	// 	std::array<uint8_t, 16> guid; // in editor;
	// 	uint32_t runtimeID; // for runtime stuff
	// };

	FileType type{};
	CompressionType compression = CompressionType::None;
};
static_assert(sizeof(File) == 128, "File size mismatch");

struct alignas(32) Header
{
	char magic[8] = "ORGPACK";
	uint8_t version = 1;
	uint32_t fileCount = 0;
	uint64_t indexOffset = 0; // a way to keep the offsets of each file
	uint8_t flags = 0; // for future use
};
static_assert(sizeof(Header) == 32, "Header size mismatch");

std::string FormatSize(uint64_t bytes)
{
	constexpr uint64_t KB = 1024;
	constexpr uint64_t MB = KB * 1024;
	constexpr uint64_t GB = MB * 1024;

	if (bytes >= GB)
	{
		return std::format("{:.2f} GiB ({}) bytes", static_cast<double>(bytes) / GB, bytes);
	}
	if (bytes >= MB)
	{
		return std::format("{:.2f} MiB ({}) bytes", static_cast<double>(bytes) / MB, bytes);
	}
	if (bytes >= KB)
	{
		return std::format("{:.2f} KiB ({}) bytes", static_cast<double>(bytes) / KB, bytes);
	}
	{
		return std::format("{} bytes", bytes);
	}
}

// needing that
FileType DetermineFileType(const File& file)
{
	const fs::path filePath(file.name);
	std::string extension = filePath.extension().string();

	// Convert extension to lowercase for case-insensitivity (pretty nice :3)
	std::ranges::transform(extension, extension.begin(), ::tolower);

	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
	{
		return FileType::Image;
	}
	if (extension == ".mp3" || extension == ".ogg" || extension == ".wav" || extension == ".flac")
	{
		return FileType::Audio;
	}
	if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb")
	{
		return FileType::Mesh;
	}
	if (extension == ".lua" || extension == ".py" || extension == ".txt" || extension == ".json" || extension == ".ini")
	{
		return FileType::Script;
	}
	return FileType::Unknown;
}

void PackFiles(const std::string& directory, const std::string& outputFile, CompressionType compression = CompressionType::None)
{
	// Sanity checks
	if (!fs::exists(directory))
	{
		std::cerr << "Error: Directory does not exist: " << directory << "\n";
		return;
	}

	if (fs::is_empty(directory))
	{
		std::cerr << "Error: Directory is empty: " << directory << "\n";
		return;
	}


	std::ofstream outFile(outputFile, std::ios::binary);
	if (!outFile.is_open())
	{
		std::cerr << "Error: Could not create output file: " << outputFile << "\n";
		return;
	}

	Header header{};
	std::vector<File> fileIndex;

	outFile.write(reinterpret_cast<const char*>(&header), sizeof(header)); // writing this first

	for (const auto& fileEntry : fs::recursive_directory_iterator(directory))
	{
		if (!fileEntry.is_regular_file())
			continue;

		const fs::path& filePath = fileEntry.path();
		std::ifstream fileStream(filePath, std::ios::binary | std::ios::ate);
		if (!fileStream.is_open())
		{
			std::cerr << "Warning: Could not open file: " << filePath << "\n";
			continue;
		}

		std::streamsize fileSize = fileStream.tellg();
		fileStream.seekg(0, std::ios::beg);

		// Set up File metadata
		File file{};
		std::memset(&file, 0, sizeof(File));
		std::string relPath = fs::relative(filePath, directory).string();
		std::ranges::replace(relPath, '\\', '/'); // normalize
		std::strncpy(file.name, relPath.c_str(), sizeof(file.name) - 1);
		file.name[sizeof(file.name) - 1] = '\0';
		file.type = DetermineFileType(file);
		file.compression = compression;
		file.offset = static_cast<uint64_t>(outFile.tellp());
		file.uncompressedSize = static_cast<uint64_t>(fileSize);

		// Read and write file data
		std::vector<char> fileBuffer(fileSize);
		fileStream.read(fileBuffer.data(), fileSize);

		if (compression == CompressionType::ZSTD)
		{
			size_t bound = ZSTD_compressBound(fileSize);

			std::vector<char> compressedBuffer(bound);

			size_t compressedSize = ZSTD_compress(compressedBuffer.data(), bound, fileBuffer.data(), fileSize, 22);
			if (ZSTD_isError(compressedSize))
			{
				std::cerr << "ZSTD compression failed for " << file.name << ": " << ZSTD_getErrorName(compressedSize) << "\n";
				continue;
			}

			file.compressedSize = compressedSize;
			outFile.write(compressedBuffer.data(), compressedSize);
		}
		else
		{
			file.compressedSize = fileSize;
			outFile.write(fileBuffer.data(), fileSize);
		}

		fileIndex.push_back(file);
	}

	// Write the file index at the end of the file
	uint64_t indexOffset = outFile.tellp();
	for (const auto& file : fileIndex)
	{
		outFile.write(reinterpret_cast<const char*>(&file), sizeof(File));
	}

	// Rewrite the header with real values
	header.fileCount = static_cast<uint32_t>(fileIndex.size());
	header.indexOffset = indexOffset;

	outFile.seekp(0, std::ios::beg);
	outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));

	outFile.close();
	std::cout << "Packed " << header.fileCount << " files into " << outputFile << "\n";
}

void UnpackFiles(const std::string& packedFile, const std::string& outputDirectory)
{
	if (!fs::exists(packedFile))
	{
		std::cerr << "Error: Packed File: " << packedFile << " does not exist.\n";
		return;
	}
	if (fs::is_empty(packedFile))
	{
		std::cerr << "Error: Directory " << packedFile << " Is empty lol.\n";
		return;
	}
	std::ifstream inFile(packedFile, std::ios::binary);

	if (!inFile.is_open())
	{
		std::cerr << "Error: Could not open packed file.\n";
		return;
	}

	if (!fs::exists(outputDirectory))
	{
		fs::create_directories(outputDirectory); // Create output directory if it doesn't exist
	}

	// Read the header
	Header header;
	inFile.read(reinterpret_cast<char*>(&header), sizeof(header));

	if (std::strncmp(header.magic, "ORGPACK", 8) != 0)
	{
		std::cerr << "Error: Invalid packed file format.\n";
		return;
	}

	std::vector<File> fileIndex(header.fileCount);

	// Read the file index
	inFile.seekg(header.indexOffset, std::ios::beg);
	for (size_t i = 0; i < header.fileCount; ++i)
	{
		inFile.read(reinterpret_cast<char*>(&fileIndex[i]), sizeof(File));
	}

	// Extract each file
	for (const auto& file : fileIndex)
	{
		fs::path outputPath = fs::path(outputDirectory) / file.name;

		inFile.seekg(file.offset, std::ios::beg);
		std::vector<char> compressedData(file.compressedSize);
		inFile.read(compressedData.data(), file.compressedSize);

		std::ofstream outFile(outputPath, std::ios::binary);
		if (!outFile.is_open())
		{
			std::cerr << "Error: Could not create file: " << outputPath << "\n";
			continue;
		}

		if (file.compression == CompressionType::ZSTD)
		{
			std::vector<char> decompressedData(file.uncompressedSize);
			size_t result = ZSTD_decompress(
				decompressedData.data(), file.uncompressedSize,
				compressedData.data(), file.compressedSize);

			if (ZSTD_isError(result))
			{
				std::cerr << "ZSTD decompression failed for " << file.name << ": "
						  << ZSTD_getErrorName(result) << "\n";
				continue;
			}

			outFile.write(decompressedData.data(), file.uncompressedSize);
		}
		else
		{
			outFile.write(compressedData.data(), file.uncompressedSize);
		}

		outFile.close();

		std::cout << "Extracted: " << file.name << " ("
			<< std::fixed << std::setprecision(2)
			<< (file.uncompressedSize / (1024.0 * 1024.0)) << " MB)\n";
	}


	inFile.close();
	std::cout << "Unpacked files to directory: " << outputDirectory << "\n";
}

void PeekFiles(const std::string& packedFile)
{
	if (!fs::exists(packedFile))
	{
		std::cerr << "Error: Packed File: " << packedFile << " does not exist.\n";
		return;
	}
	if (fs::is_empty(packedFile))
	{
		std::cerr << "Error: Directory " << packedFile << " Is empty lol.\n";
		return;
	}
	std::ifstream inFile(packedFile, std::ios::binary);
	if (!inFile.is_open())
	{
		std::cerr << "Error: Could not open packed file.\n";
		return;
	}

	// Read the header
	Header header;
	inFile.read(reinterpret_cast<char*>(&header), sizeof(header));

	if (std::strncmp(header.magic, "ORGPACK", 8) != 0)
	{
		std::cerr << "Error: Invalid packed file format.\n";
		return;
	}

	std::vector<File> fileIndex(header.fileCount);

	// Read the file index
	inFile.seekg(header.indexOffset, std::ios::beg);
	for (size_t i = 0; i < header.fileCount; ++i)
	{
		inFile.read(reinterpret_cast<char*>(&fileIndex[i]), sizeof(File));
	}

	std::cout << std::format("Packed File Structure: {}\n", packedFile);
	std::cout << "+-- Header\n";
	std::cout << std::format("|   +-- Magic: {}\n", std::string(header.magic, 8));
	std::cout << std::format("|   +-- Version: {}\n", static_cast<int>(header.version));
	std::cout << std::format("|   +-- File Count: {}\n", header.fileCount);
	std::cout << std::format("|   +-- Index Offset: {}\n", header.indexOffset);

	std::cout << "+-- Files\n";
	for (const auto& file : fileIndex)
	{
		std::cout << std::format("|   +-- {}\n", file.name);
		std::cout << std::format("|   |   +-- Type: {}\n",
								 file.type == FileType::Image ? "Image" :
								 file.type == FileType::Mesh ? "Mesh" :
								 file.type == FileType::Script ? "Script" : "Unknown");
		std::cout << std::format("|   |   +-- Compression: {}\n",
		                         file.compression == CompressionType::None ? "None" : file.compression == CompressionType::ZSTD ? "ZSTD" : "LZ4");
		std::cout << std::format("|   |   +-- Offset: {}\n", file.offset);

		if (file.compression == CompressionType::ZSTD || file.compression == CompressionType::LZ4)
			std::cout << "|   |   +-- Compressed Size: " << FormatSize(file.compressedSize) << "\n";

		std::cout << "|   |   +-- Uncompressed Size: " << FormatSize(file.uncompressedSize) << "\n";
	}
}

void PrintUsage()
{
	std::cerr << "Usage: .\\OrgEnginePacker <argument(s)> <filename/folder> \n";
	std::cout << "Arguments:\n";
	std::cout << " -p = Pack files to an output directory\n";
	std::cout << " -u = Unpack files to an output directory\n";
	std::cout << " -seek = Look through the files in a tree-like structure\n";
	std::cout << "Compression Formats (for -p):\n";
	std::cout << " none = No compression\n";
	std::cout << " lz4 = LZ4 compression\n";
	std::cout << " zstd = ZSTD compression\n";
}

void PrintArgumentUsage(const std::string& argument)
{
	if (argument == "-p")
	{
		std::cout << "Usage for -p (Pack):\n";
		std::cout << " .\\OrgEnginePacker.exe -p <folder> [compression_format]\n";
		std::cout << "Compression Formats:\n";
		std::cout << " none = No compression\n";
		std::cout << " lz4 = LZ4 compression\n";
		std::cout << " zlib = Zlib compression\n";
	}
	else if (argument == "-u")
	{
		std::cout << "Usage for -u (Unpack):\n";
		std::cout << " .\\OrgEnginePacker.exe -n <packed filename>>\n";
	}
	else if (argument == "-seek")
	{
		std::cout << "Usage for -seek (Peek):\n";
		std::cout << " .\\OrgEnginePacker.exe -seek <packed filename>\n";
	}
	else
	{
		std::cerr << "Unknown argument: " << argument << "\n";
	}
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		PrintUsage();
		return 1;
	}

	std::vector<std::string> args(argv + 1, argv + argc);
	std::string mode;
	std::string output = "output.pak";
	auto compression = CompressionType::None;
	std::vector<std::string> targets;

	// Parse arguments
	for (const auto& arg : args)
	{
		if (arg == "--help" || arg == "-help")
		{
			PrintUsage();
			return 0;
		}
		if (arg == "--pack" || arg == "-p")
		{
			mode = "pack";
		}
		else if (arg == "--unpack" || arg == "-u")
		{
			mode = "unpack";
		}
		else if (arg == "--peek" || arg == "-peek")
		{
			mode = "peek";
		}
		else if (arg.starts_with("--compress=") || arg.starts_with("-compress="))
		{
			auto val = arg.substr(11);
			if (val == "zstd") compression = CompressionType::ZSTD;
			else if (val == "lz4") compression = CompressionType::LZ4;
			else if (val == "none") compression = CompressionType::None;
			else
			{
				std::cerr << "Unknown compression type: " << val << "\n";
				return 1;
			}
		}
		else if (arg.starts_with("--out="))
		{
			output = arg.substr(6);
		}
		else
		{
			targets.push_back(arg);
		}
	}

	// Dispatch modes
	if (mode == "pack")
	{
		if (targets.empty())
		{
			std::cerr << "Error: No input folder or files provided for packing.\n";
			return 1;
		}

		if (targets.size() <= 2 && fs::is_directory(targets[0]))
		{
			PackFiles(targets[0], output, compression);
		}
		else
		{
			// You could support packing multiple files later
			std::cerr << "Packing multiple individual files not supported yet.\n";
			return 1;
		}
	}
	else if (mode == "unpack")
	{
		if (targets.size() != 1)
		{
			std::cerr << "Error: Provide exactly one .orgpack file to unpack.\n";
			return 1;
		}
		UnpackFiles(targets[0], "outputFolder");
	}
	else if (mode == "peek")
	{
		if (targets.size() != 1)
		{
			std::cerr << "Error: Provide exactly one .orgpack file to peek.\n";
			return 1;
		}
		PeekFiles(targets[0]);
	}
	else
	{
		// Fallback: drag-and-drop
		for (const auto& path : targets)
		{
			if (!fs::exists(path))
			{
				std::cerr << "Path does not exist: " << path << "\n";
				continue;
			}

			if (fs::is_directory(path))
			{
				std::cout << "Auto-packing dropped folder: " << path << "\n";
				PackFiles(path, output, compression);
			}
			else if (fs::is_regular_file(path))
			{
				std::ifstream in(path, std::ios::binary);
				char magic[8]{};
				in.read(magic, 7);
				if (std::strncmp(magic, "ORGPACK", 7) == 0)
				{
					std::cout << "Auto-unpacking detected .orgpack: " << path << "\n";
					UnpackFiles(path, "outputFolder");
				}
				else
				{
					std::cerr << "Skipped non-ORGPACK file: " << path << "\n";
				}
			}
		}
	}
	return 0;
}
