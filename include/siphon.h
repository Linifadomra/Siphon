/**
 * @file siphon.h
 *
 * The public API surface for Siphon. Extraction, inspection, and ARC handling.
 */

#ifndef SIPHON_H
#define SIPHON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result codes returned by Siphon operations
 */
typedef enum {
    SIPHON_OK              = 0, ///< The Siphon operation completed successfully.
    SIPHON_ERR_IO          = 1, ///< The Siphon operation had an error during I/O.
    SIPHON_ERR_FORMAT      = 2, ///< The Siphon operation did not recognize your disc format.
    SIPHON_ERR_NOT_FOUND   = 3, ///< The Siphon ARC operation could not find the specified file.
    SIPHON_ERR_ID_MISMATCH = 4, ///< The ID of the disc passed in was not in the list of expected IDs.
    SIPHON_ERR_CORRUPT     = 5, ///< The disc provided to Siphon was corrupted somehow.
} SiphonError;

/**
 * @brief Callback type for receiving Siphon log messages
 *
 * @param userdata The opaque userdata pointer passed in to the originating Siphon call
 * @param msg The log message text
 */
typedef void (*SiphonLogFn)(void* userdata, const char* msg);


/**
 * @brief Summary information about a disc image, filled in by siphon_disc_inspect
 */
typedef struct {
    char game_id[8];  ///< 6-char disc ID, null-terminated
    char format[16];  ///< "ISO/GCM", "CISO", "GCZ", "WIA", "RVZ", "WBFS"
    int  entry_count; ///< Number of entries
} SiphonDiscInfo;

/**
 * @brief Inspects and returns information about the provided disc image
 *
 * @param image The absolute path of the disc image to inspect
 * @param out A pointer to the output disc information
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the inspection. 0 means it completed successfully.
 */
SiphonError siphon_disc_inspect(const char* image, SiphonDiscInfo* out, SiphonLogFn log, void* userdata);

/**
 * @brief Extracts a disc image to the output directory
 *
 * @param image The absolute path of the disc image to extract
 * @param outdir The absolute path of the output folder dumped to
 * @param expect_ids An array of accepted disc IDs. If NULL, skips disc ID check.
 * @param num_ids The number of IDs passed into expect_ids. If expect_ids is NULL, this value is ignored.
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the extraction. 0 means it completed successfully.
 */
SiphonError siphon_disc_extract(
    const char* image,
    const char* outdir,
    const char* const* expect_ids,
    size_t num_ids,
    SiphonLogFn log,
    void* userdata
);

/**
 * @brief Extracts all files from a GC/RARC archive to the output directory
 *
 * @param archive The absolute path of the archive file to extract
 * @param outdir The absolute path of the output folder dumped to
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the extraction. 0 means it completed successfully.
 */
SiphonError siphon_arc_extract(
    const char* archive,
    const char* outdir,
    SiphonLogFn log,
    void* userdata
);

/**
 * @brief Lists the file entries contained in a GC/RARC archive
 *
 * Logs each file entry's name via the provided log function, skipping directory entries
 *
 * @param archive The absolute path of the archive file to list
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the listing. 0 means it completed successfully.
 */
SiphonError siphon_arc_list(
    const char* archive,
    SiphonLogFn log,
    void* userdata
);

/**
 * @brief Copies a single named file out of a GC/RARC archive to disk
 *
 * @param archive The absolute path of the archive file to read from
 * @param inner The exact name of the file entry inside the archive to extract
 * @param out_path The absolute path to write the extracted file to
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the copy. 0 means it completed successfully.
 *          Returns SIPHON_ERR_NOT_FOUND if no matching file entry exists in the archive.
 */
SiphonError siphon_arc_copy(
    const char* archive,
    const char* inner,
    const char* out_path,
    SiphonLogFn log,
    void* userdata
);

/**
 * @brief Decompresses a Yaz0-compressed file, writing the result to disk
 *
 * If the input file is not actually Yaz0-compressed, its contents are
 * copied through to the output path unchanged.
 *
 * @param in The absolute path of the input file to decompress
 * @param out The absolute path to write the decompressed data to
 * @param log A log function pointer. If NULL, logs write directly to stream.
 * @param userdata Data passed in to the log function
 * @returns A SiphonError with the result of the decompression. 0 means it completed successfully.
 */
SiphonError siphon_yaz0_decompress_file(
    const char* in,
    const char* out,
    SiphonLogFn log,
    void* userdata
);

#ifdef __cplusplus
}
#endif

#endif
