#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

// OrgEngine's .orgpack File Format V0.0.1

// This is the file format for packed RELEASES for anything ill be packing for applications.
// Will probably make it a dynamic lib for internal stuff
// Or keep it as is lol

// barebones for now, will expand as i start to get my model formats/texture formats packable for now
enum class FileType
{
	Image,
	Audio,
	Mesh,
	Script,
	Unknown
};

enum class CompressionType
{
	LZ4,
	ZSTD,
	None
};

struct File
{
	char name[64]{};
	FileType type{};
	CompressionType compression = CompressionType::None;
	uint64_t offset{};
	uint64_t size{};
	uint8_t padding[40]{}; // for future use
};

struct Header
{
	char magic[8] = "ORGPACK";
	uint8_t version = 1;
	uint32_t fileCount = 0;
	uint64_t indexOffset = 0;
	uint8_t flags = 0; // for future use
};

std::string FormatSize(uint64_t bytes)
{
	constexpr uint64_t KB = 1024;
	constexpr uint64_t MB = KB * 1024;
	constexpr uint64_t GB = MB * 1024;

	if (bytes >= GB) {
		return std::format("{:.2f} GiB ({}) bytes", static_cast<double>(bytes) / GB, bytes);
	} if (bytes >= MB) {
		return std::format("{:.2f} MiB ({}) bytes", static_cast<double>(bytes) / MB, bytes);
	} if (bytes >= KB) {
		return std::format("{:.2f} KiB ({}) bytes", static_cast<double>(bytes) / KB, bytes);
	} {
		return std::format("{} bytes", bytes);
	}
}



// needing that
FileType DetermineFileType(const File& file) {
	const fs::path filePath(file.name);
	std::string extension = filePath.extension().string();

	// Convert extension to lowercase for case-insensitivity (pretty nice :3)
	std::ranges::transform(extension, extension.begin(), ::tolower);

	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
		return FileType::Image;
	}
	if (extension == ".mp3" || extension == ".ogg" || extension == ".wav" || extension == ".flac") {
		return FileType::Audio;
	}
	if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb") {
		return FileType::Mesh;
	}
	if (extension == ".lua" || extension == ".py" || extension == ".txt") {
		return FileType::Script;
	}
	return FileType::Unknown;
}

void PackFiles(const std::string& directory, const std::string& outputFile, CompressionType compression)
{
	std::ofstream outFile(outputFile, std::ios::binary);
	if (!outFile.is_open()) {
		std::cerr << "Error: Could not create output file.\n";
		return;
	}
	if (!fs::exists(directory))
	{
		std::cerr << "Error: Directory " << directory << " does not exist.\n";
		return;
	}
	if (fs::is_empty(directory))
	{
		std::cerr << "Error: Directory " << directory << " is empty lol.\n";
		return;
	}

	Header header;
	std::vector<File> fileIndex;

	outFile.write(reinterpret_cast<char*>(&header), sizeof(header)); // writing this first

	for (const auto& fileEntry : fs::directory_iterator(directory))
	{
		if (!fileEntry.is_regular_file()) continue;

		const fs::path& filePath = fileEntry.path();
		std::ifstream fileStream(filePath, std::ios::binary | std::ios::ate); // starting input stream

		File file;
		std::string fileName = filePath.filename().string(); // get filename as a string
		strncpy(file.name, fileName.c_str(), sizeof(file.name) - 1); // convert to char
		file.name[sizeof(file.name) - 1] = '\0'; // properly format it with escape char, no I don't think you want buffer overflow :3
		file.type = DetermineFileType(file);
		file.compression = compression;
		file.offset = outFile.tellp(); // size of the file stream for each file(start offset), otherwise -1 lol

		file.size = fileStream.tellg(); // end of file stream, get size
		fileStream.seekg(0, std::ios::beg); // back to beginning
		std::vector<char> fileBuffer(file.size); // for the file DATA in memory

		fileStream.read(fileBuffer.data(), file.size); // read to fileBuffer

		outFile.write(fileBuffer.data(), file.size); // then read to file output stream
		fileIndex.push_back(file); // add to my file Index
	}

	// Write the file index at the end of the file
	uint64_t indexOffset = outFile.tellp();
	for (const auto& file : fileIndex) {
		outFile.write(reinterpret_cast<const char*>(&file), sizeof(File));
	}

	// Update and write the header
	header.fileCount = fileIndex.size();
	header.indexOffset = indexOffset;
	outFile.seekp(0, std::ios::beg);
	outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));

	outFile.close();
	std::cout << "Packed " << header.fileCount << " files into " << outputFile << "\n";
}

void UnpackFiles(const std::string& packedFile, const std::string& outputDirectory,
	CompressionType compression = CompressionType::None) // in the Future....
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

	if (!inFile.is_open()) {
		std::cerr << "Error: Could not open packed file.\n";
		return;
	}

	if (!fs::exists(outputDirectory)) {
		fs::create_directories(outputDirectory); // Create output directory if it doesn't exist
	}

	// Read the header
	Header header;
	inFile.read(reinterpret_cast<char*>(&header), sizeof(header));

	if (strncpy(header.magic, "ORGPACK", 8) != 0) {
		std::cerr << "Error: Invalid packed file format.\n";
		return;
	}

	std::vector<File> fileIndex(header.fileCount);

	// Read the file index
	inFile.seekg(header.indexOffset, std::ios::beg);
	for (size_t i = 0; i < header.fileCount; ++i) {
		inFile.read(reinterpret_cast<char*>(&fileIndex[i]), sizeof(File));
	}

	// Extract each file
	for (const auto& file : fileIndex) {
		fs::path outputPath = fs::path(outputDirectory) / file.name;

		inFile.seekg(file.offset, std::ios::beg);
		std::vector<char> fileBuffer(file.size);

		inFile.read(fileBuffer.data(), file.size);

		std::ofstream outFile(outputPath, std::ios::binary);
		if (!outFile.is_open()) {
			std::cerr << "Error: Could not create file: " << outputPath << "\n";
			continue;
		}

		outFile.write(fileBuffer.data(), file.size);
		outFile.close();

		std::cout << "Extracted: " << file.name << " ("
			<< std::fixed << std::setprecision(2)
			<< (file.size / (1024.0 * 1024.0)) << " MB)\n";
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
	if (!inFile.is_open()) {
		std::cerr << "Error: Could not open packed file.\n";
		return;
	}

	// Read the header
	Header header;
	inFile.read(reinterpret_cast<char*>(&header), sizeof(header));

	if (std::strncmp(header.magic, "ORGPACK", 8) != 0) {
		std::cerr << "Error: Invalid packed file format.\n";
		return;
	}

	std::vector<File> fileIndex(header.fileCount);

	// Read the file index
	inFile.seekg(header.indexOffset, std::ios::beg);
	for (size_t i = 0; i < header.fileCount; ++i) {
		inFile.read(reinterpret_cast<char*>(&fileIndex[i]), sizeof(File));
	}

	std::cout << std::format("Packed File Structure: {}\n", packedFile);
	std::cout << "+-- Header\n";
	std::cout << std::format("|   +-- Magic: {}\n", std::string(header.magic, 8));
	std::cout << std::format("|   +-- Version: {}\n", static_cast<int>(header.version));
	std::cout << std::format("|   +-- File Count: {}\n", header.fileCount);
	std::cout << std::format("|   +-- Index Offset: {}\n", header.indexOffset);

	std::cout << "+-- Files\n";
	for (const auto& file : fileIndex) {
		std::cout << std::format("|   +-- {}\n", file.name);
		std::cout << std::format("|   |   +-- Type: {}\n",
								 file.type == FileType::Image ? "Image" :
								 file.type == FileType::Mesh ? "Mesh" :
								 file.type == FileType::Script ? "Script" : "Unknown");
		std::cout << std::format("|   |   +-- Compression: {}\n",
								 file.compression == CompressionType::None ? "None" :
								 file.compression == CompressionType::LZ4 ? "LZ4" : "ZSTD");
		std::cout << std::format("|   |   +-- Offset: {}\n", file.offset);
		std::cout << "|   |   +-- Size: " << FormatSize(file.size) << "\n";
	}
}

void PrintUsage()
{
	std::cerr << "Usage: .\\OrgEnginePacker.exe <argument> <filename/folder> [Compression Format]\n";
	std::cout << "Arguments:\n";
	std::cout << " -p = Pack files to an output directory\n";
	std::cout << " -n = Unpack files to an output directory\n";
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
    else if (argument == "-n")
    {
        std::cout << "Usage for -n (Unpack):\n";
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

    std::string argument = argv[1];

    if (argument == "-help" || argument == "--help")
    {
        PrintUsage();
        return 0;
    }

    if (argc < 3)
    {
        PrintArgumentUsage(argument);
        return 1;
    }

    std::string filename = argv[2];

    if (argument == "-p" || argument == "--p")
    {
        CompressionType compression = CompressionType::None; // Default compression
        std::string outputDirectory = "output.pak"; // Default output file

        if (argc >= 4)
        {
            std::string compressionFormat = argv[3];
            if (compressionFormat == "none")
            {
                compression = CompressionType::None;
            }
            else if (compressionFormat == "lz4")
            {
                compression = CompressionType::LZ4;
            }
            else if (compressionFormat == "zstd")
            {
                compression = CompressionType::ZSTD;
            }
            else
            {
                std::cerr << "Invalid compression format: " << compressionFormat << "\n";
                PrintArgumentUsage(argument);
                return 1;
            }
        }

        if (argc == 5)
        {
            outputDirectory = argv[4];
            if (outputDirectory.empty())
            {
                std::cerr << "Output directory is empty, defaulting to output.pak\n";
            }
        }

        PackFiles(filename, outputDirectory, compression);
    }
    else if (argument == "-n" || argument == "--n")
    {
        if (argc != 3)
        {
            std::cerr << "Error: The -n option requires exactly one filename argument.\n";
            PrintArgumentUsage(argument);
            return 1;
        }

        UnpackFiles(filename, "outputFolder");
    }
    else if (argument == "-seek" || argument == "--seek")
    {
        if (argc != 3)
        {
            std::cerr << "Error: The -seek option requires exactly one filename argument.\n";
            PrintArgumentUsage(argument);
            return 1;
        }

        PeekFiles(filename);
    }
    else
    {
        std::cerr << "Invalid argument: " << argument << "\n";
        PrintUsage();
        return 1;
    }

    return 0;
}