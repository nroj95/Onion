#ifndef PLAY_ACTIVITY_ASSETS_H
#define PLAY_ACTIVITY_ASSETS_H

#include <stdbool.h>
#include <stddef.h>

/* =============================================================================
 * purpose:
 * resolve retroarch core names and migrate rom-named asset families.
 *
 * key behavior:
 * - resolves a core's display name from its adjacent .info file.
 * - migrates every file beginning with the old rom basename.
 * - preserves the complete suffix after the basename.
 * - never overwrites an existing destination file.
 * - accepts explicit directories so filesystem behavior can be tested safely.
 * =============================================================================
 */

typedef struct PlayActivityAssetMigrationResult {
    int moved;
    int missing;
    int blocked;
    int failed;
} PlayActivityAssetMigrationResult;

bool play_activity_asset_core_name(
    const char *core_path,
    char *core_name_out,
    size_t core_name_out_size
);

bool play_activity_asset_migrate_directory(
    const char *directory_path,
    const char *old_rom_path,
    const char *new_rom_path,
    PlayActivityAssetMigrationResult *result_out
);

#endif
