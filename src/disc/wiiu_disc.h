#ifndef SIPHON_WIIU_DISC_H
#define SIPHON_WIIU_DISC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns 1 if @p path looks like a Wii U disc (.wux or .wud)
 */
int wiiu_disc_is_wiiu(const char* path);

/**
 * @brief Fills @p game_id and @p format (.wux or .wud)
 *
 * @returns 0 on success, -1 on failure.
 */
int wiiu_disc_inspect(const char* path, char* game_id, size_t game_id_sz,
                      char* format, size_t format_sz);

/**
 * @brief Extracts every file from the Wii U disc into @p outdir. For an encrypted .wux disc it uses @p keys_path.
 *
 * @returns 0 on success, -1 on failure.
 */
int wiiu_disc_extract(const char* path, const char* outdir, const char* keys_path);

#ifdef __cplusplus
}
#endif

#endif
