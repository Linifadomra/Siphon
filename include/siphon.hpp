/**
 * @file siphon.hpp
 *
 * A very thin idiomatic C++ wrapper around the Siphon C API 
 */

#ifndef SIPHON_HPP
#define SIPHON_HPP

#include "siphon.h"
#include <string>
#include <vector>

namespace siphon {

/// C++ alias for SiphonError
using Error = SiphonError;

/// C++ alias for SiphonLogFn
using LogFn = SiphonLogFn;

/// C++ alias for SiphonDiscInfo
using DiscInfo = SiphonDiscInfo;

/**
 * @brief Inspects and returns information about the provided disc image
 *
 * @param image The path of the disc image to inspect
 * @param out A reference to the output disc information
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the inspection. 0 means it completed successfully.
 */
inline Error discInspect(const std::string& image, DiscInfo& out,
                          LogFn log = nullptr, void* userdata = nullptr) {
    return siphon_disc_inspect(image.c_str(), &out, log, userdata);
}

/**
 * @brief Extracts a disc image to the output directory
 *
 * @param image The path of the disc image to extract
 * @param outdir The path of the output folder dumped to
 * @param expectIds A list of accepted disc IDs. If empty, skips the disc ID check.
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the extraction. 0 means it completed successfully.
 */
inline Error discExtract(const std::string& image, const std::string& outdir,
                          const std::vector<std::string>& expectIds = {},
                          LogFn log = nullptr, void* userdata = nullptr) {
    if (expectIds.empty()) {
        return siphon_disc_extract(image.c_str(), outdir.c_str(), nullptr, 0, log, userdata);
    }
    std::vector<const char*> ids;
    ids.reserve(expectIds.size());
    for (const auto& s : expectIds) ids.push_back(s.c_str());
    return siphon_disc_extract(image.c_str(), outdir.c_str(), ids.data(), ids.size(), log, userdata);
}

/**
 * @brief Reads a single file from a disc image into memory
 *
 * Extracts exactly one file by its path relative to the disc root.
 * The caller owns the returned buffer and must free() it.
 *
 * @param image The absolute path of the disc image to read from
 * @param filePath The path of the file inside the disc (e.g. "zelda.rel" or "rel/d_a_player.rel")
 * @param outData Pointer to receive the allocated buffer containing the file data
 * @param outSize Pointer to receive the size of the file in bytes
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the read. 0 means it completed successfully.
 *          Returns SIPHON_ERR_NOT_FOUND if the file does not exist in the disc.
 */
inline Error discReadFile(const std::string& image, const std::string& filePath,
                          void** outData, size_t* outSize,
                          LogFn log = nullptr, void* userdata = nullptr) {
    return siphon_disc_read_file(image.c_str(), filePath.c_str(), outData, outSize, log, userdata);
}

/**
 * @brief Extracts all files from a GC/RARC archive to the output directory
 *
 * @param archive The path of the archive file to extract
 * @param outdir The path of the output folder dumped to
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the extraction. 0 means it completed successfully.
 */
inline Error arcExtract(const std::string& archive, const std::string& outdir,
                         LogFn log = nullptr, void* userdata = nullptr) {
    return siphon_arc_extract(archive.c_str(), outdir.c_str(), log, userdata);
}

/**
 * @brief Lists the file entries contained in a GC/RARC archive
 *
 * Logs each file entry's name via the provided log function, skipping directory entries.
 *
 * @param archive The path of the archive file to list
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the listing. 0 means it completed successfully.
 */
inline Error arcList(const std::string& archive,
                      LogFn log = nullptr, void* userdata = nullptr) {
    return siphon_arc_list(archive.c_str(), log, userdata);
}

/**
 * @brief Copies a single named file out of a GC/RARC archive to disk
 *
 * @param archive The path of the archive file to read from
 * @param inner The exact name of the file entry inside the archive to extract
 * @param outPath The path to write the extracted file to
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the copy. 0 means it completed successfully.
 *          Returns SIPHON_ERR_NOT_FOUND if no matching file entry exists in the archive.
 */
inline Error arcCopy(const std::string& archive, const std::string& inner,
                      const std::string& outPath,
                      LogFn log = nullptr, void* userdata = nullptr) {
    return siphon_arc_copy(archive.c_str(), inner.c_str(), outPath.c_str(), log, userdata);
}

/**
 * @brief Decompresses a Yaz0-compressed file, writing the result to disk
 *
 * If the input file is not actually Yaz0-compressed, its contents are
 * copied through to the output path unchanged.
 *
 * @param in The path of the input file to decompress
 * @param out The path to write the decompressed data to
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the decompression. 0 means it completed successfully.
 */
inline Error yaz0DecompressFile(const std::string& in, const std::string& out,
                                 LogFn log = nullptr, void* userdata = nullptr) {
    return siphon_yaz0_decompress_file(in.c_str(), out.c_str(), log, userdata);
}

} // namespace siphon

#endif // SIPHON_HPP
