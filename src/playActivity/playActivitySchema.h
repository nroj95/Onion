#ifndef PLAY_ACTIVITY_SCHEMA_H
#define PLAY_ACTIVITY_SCHEMA_H

#include <stdbool.h>
#include <stdint.h>

#include <sqlite3/sqlite3.h>

#include "./playActivityIdentity.h"

/* =============================================================================
 * purpose:
 * initialize and update the activity tracker's identity data.
 *
 * key behavior:
 * - adds identity tables to onion's existing activity database.
 * - leaves the original rom and play_activity tables unchanged.
 * - stores one current content identity for each stable rom.id.
 * - merges conflicting rom rows while preserving their activity history.
 * =============================================================================
 */

#define PLAY_ACTIVITY_IDENTITY_SCHEMA_VERSION 1

bool play_activity_identity_schema_ensure(sqlite3 *database);

bool play_activity_identity_store(
    sqlite3 *database,
    int rom_id,
    const RomContentIdentity *identity,
    int64_t modified_time
);

int play_activity_identity_find_rom_id(
    sqlite3 *database,
    const RomContentIdentity *identity
);

bool play_activity_identity_merge_roms(
    sqlite3 *database,
    int survivor_rom_id,
    int redundant_rom_id,
    const RomContentIdentity *identity,
    int64_t modified_time,
    const char *redundant_file_path,
    const char *current_file_path
);

bool play_activity_identity_record_path_change(
    sqlite3 *database,
    int rom_id,
    const char *old_file_path,
    const char *new_file_path
);

#endif
