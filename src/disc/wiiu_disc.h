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

typedef struct WiiUDisc WiiUDisc;
typedef void (*WiiUEntryFn)(void* data, const char* path, size_t size, int is_dir);

WiiUDisc* wiiu_disc_open(const char* path, const char* keys_path);
void wiiu_disc_close(WiiUDisc* disc);
int wiiu_disc_list(WiiUDisc* disc, WiiUEntryFn on_entry, void* userdata);
int wiiu_disc_read(WiiUDisc* disc, const char* path, void** out_data, size_t* out_size);

#ifdef __cplusplus
}
#endif

#endif
