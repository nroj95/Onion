#ifndef PLAY_ACTIVITY_TRANSFER_H
#define PLAY_ACTIVITY_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>

#include <sqlite3/sqlite3.h>

#include "./playActivityIdentity.h"

typedef enum {
    PLAY_ACTIVITY_TRANSFER_PLAN_INVALID = 0,
    PLAY_ACTIVITY_TRANSFER_PLAN_READY,
    PLAY_ACTIVITY_TRANSFER_PLAN_REPLACE,
    PLAY_ACTIVITY_TRANSFER_PLAN_ACTIVITY_ONLY
} PlayActivityTransferPlanKind;

typedef struct {
    PlayActivityTransferPlanKind kind;
    char source_core_name[256];
    int blocked;
} PlayActivityTransferPlan;

bool play_activity_transfer_stored_path_to_absolute(
    const char *roms_root,
    const char *stored_file_path,
    char *absolute_path_out,
    size_t absolute_path_out_size
);

bool play_activity_transfer_plan(
    sqlite3 *database,
    int source_rom_id,
    const char *system,
    const RomContentIdentity *identity,
    const char *destination_file_path,
    const char *roms_root,
    const char *history_path,
    const char *saves_root,
    const char *states_root,
    PlayActivityTransferPlan *plan_out
);

#endif
