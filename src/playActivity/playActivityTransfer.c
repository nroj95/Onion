#include "./playActivityTransfer.h"

#include "./playActivityAssets.h"
#include "./playActivityHistory.h"
#include "./playActivitySchema.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool find_transfer_candidate_path(
    sqlite3 *database,
    int source_rom_id,
    const char *system,
    const RomContentIdentity *identity,
    const char *excluded_file_path,
    char *source_file_path_out,
    size_t source_file_path_out_size
)
{
    if (source_file_path_out != NULL &&
        source_file_path_out_size > 0) {
        source_file_path_out[0] = '\0';
    }

    if (database == NULL ||
        source_rom_id < 0 ||
        system == NULL ||
        system[0] == '\0' ||
        identity == NULL ||
        excluded_file_path == NULL ||
        source_file_path_out == NULL ||
        source_file_path_out_size == 0) {
        return false;
    }

    sqlite3_stmt *statement =
        play_activity_identity_prepare_candidates(
            database,
            system,
            identity,
            excluded_file_path
        );

    if (statement == NULL)
        return false;

    bool found = false;
    int result;

    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        if (sqlite3_column_int(statement, 0) != source_rom_id)
            continue;

        const unsigned char *stored_path =
            sqlite3_column_text(statement, 1);

        if (stored_path == NULL)
            break;

        int written = snprintf(
            source_file_path_out,
            source_file_path_out_size,
            "%s",
            (const char *)stored_path
        );

        if (written >= 0 &&
            (size_t)written < source_file_path_out_size) {
            found = true;
        }
        else {
            source_file_path_out[0] = '\0';
        }

        break;
    }

    sqlite3_finalize(statement);
    return found;
}

static bool find_transfer_core_path(
    sqlite3 *database,
    int source_rom_id,
    const char *source_file_path,
    const char *roms_root,
    const char *history_path,
    char *core_path_out,
    size_t core_path_out_size,
    bool *lookup_ok_out
)
{
    if (core_path_out != NULL &&
        core_path_out_size > 0) {
        core_path_out[0] = '\0';
    }

    if (lookup_ok_out != NULL)
        *lookup_ok_out = false;

    if (database == NULL ||
        source_rom_id < 0 ||
        source_file_path == NULL ||
        roms_root == NULL ||
        history_path == NULL ||
        core_path_out == NULL ||
        core_path_out_size == 0 ||
        lookup_ok_out == NULL) {
        return false;
    }

    char absolute_path[PATH_MAX] = "";

    if (play_activity_transfer_stored_path_to_absolute(
            roms_root,
            source_file_path,
            absolute_path,
            sizeof(absolute_path)) &&
        play_activity_history_find_core_path(
            history_path,
            absolute_path,
            core_path_out,
            core_path_out_size)) {
        *lookup_ok_out = true;
        return true;
    }

    /*
     * A rom that was explicitly transferred but never launched has no
     * RetroArch history entry under its current path. Only explicit path
     * transitions belonging to this rom_id are accepted as fallback evidence.
     */
    const char *sql =
        "SELECT old_file_path "
        "FROM rom_path_history "
        "WHERE rom_id = ?1 "
        "ORDER BY changed_at DESC, id DESC;";

    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(statement, 1, source_rom_id);

    bool found = false;
    int result;

    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        const unsigned char *historical_file_path =
            sqlite3_column_text(statement, 0);

        if (historical_file_path == NULL)
            continue;

        const char *stored_historical_path =
            (const char *)historical_file_path;

        if (strcmp(
                stored_historical_path,
                source_file_path
            ) == 0) {
            continue;
        }

        absolute_path[0] = '\0';

        if (!play_activity_transfer_stored_path_to_absolute(
                roms_root,
                stored_historical_path,
                absolute_path,
                sizeof(absolute_path))) {
            continue;
        }

        if (play_activity_history_find_core_path(
                history_path,
                absolute_path,
                core_path_out,
                core_path_out_size)) {
            found = true;
            break;
        }
    }

    bool query_ok =
        found || result == SQLITE_DONE;

    sqlite3_finalize(statement);

    if (!query_ok)
        return false;

    *lookup_ok_out = true;
    return found;
}

bool play_activity_transfer_stored_path_to_absolute(
    const char *roms_root,
    const char *stored_file_path,
    char *absolute_path_out,
    size_t absolute_path_out_size
)
{
    if (absolute_path_out != NULL &&
        absolute_path_out_size > 0) {
        absolute_path_out[0] = '\0';
    }

    if (roms_root == NULL ||
        roms_root[0] == '\0' ||
        stored_file_path == NULL ||
        stored_file_path[0] == '\0' ||
        absolute_path_out == NULL ||
        absolute_path_out_size == 0) {
        return false;
    }

    size_t roms_root_length = strlen(roms_root);

    if (strncmp(
            stored_file_path,
            roms_root,
            roms_root_length
        ) == 0 &&
        stored_file_path[roms_root_length] == '/') {
        int written = snprintf(
            absolute_path_out,
            absolute_path_out_size,
            "%s",
            stored_file_path
        );

        return written >= 0 &&
               (size_t)written < absolute_path_out_size;
    }

    const char *relative_path = stored_file_path;

    static const char *legacy_prefixes[] = {
        "/mnt/SDCARD/Roms/",
        "../../Roms/",
        "/Roms/",
        "Roms/"
    };

    for (size_t index = 0;
         index <
             sizeof(legacy_prefixes) /
                 sizeof(legacy_prefixes[0]);
         index++) {
        size_t prefix_length =
            strlen(legacy_prefixes[index]);

        if (strncmp(
                relative_path,
                legacy_prefixes[index],
                prefix_length
            ) == 0) {
            relative_path += prefix_length;
            break;
        }
    }

    if (relative_path[0] == '\0' ||
        relative_path[0] == '/') {
        return false;
    }

    int written = snprintf(
        absolute_path_out,
        absolute_path_out_size,
        roms_root[roms_root_length - 1] == '/'
            ? "%s%s"
            : "%s/%s",
        roms_root,
        relative_path
    );

    return written >= 0 &&
           (size_t)written < absolute_path_out_size;
}

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
)
{
    if (plan_out != NULL)
        memset(plan_out, 0, sizeof(*plan_out));

    if (database == NULL ||
        source_rom_id < 0 ||
        system == NULL ||
        system[0] == '\0' ||
        identity == NULL ||
        identity->type[0] == '\0' ||
        identity->value[0] == '\0' ||
        destination_file_path == NULL ||
        destination_file_path[0] == '\0' ||
        roms_root == NULL ||
        history_path == NULL ||
        saves_root == NULL ||
        states_root == NULL ||
        plan_out == NULL) {
        return false;
    }

    char source_file_path[PATH_MAX] = "";

    if (!find_transfer_candidate_path(
            database,
            source_rom_id,
            system,
            identity,
            destination_file_path,
            source_file_path,
            sizeof(source_file_path))) {
        return false;
    }

    int source_written = snprintf(
        plan_out->source_file_path,
        sizeof(plan_out->source_file_path),
        "%s",
        source_file_path
    );

    if (source_written < 0 ||
        (size_t)source_written >=
            sizeof(plan_out->source_file_path)) {
        return false;
    }

    char source_core_path[PATH_MAX] = "";
    bool core_lookup_ok = false;

    bool source_core_found =
        find_transfer_core_path(
            database,
            source_rom_id,
            source_file_path,
            roms_root,
            history_path,
            source_core_path,
            sizeof(source_core_path),
            &core_lookup_ok
        );

    if (!core_lookup_ok)
        return false;

    if (!source_core_found) {
        plan_out->kind =
            PLAY_ACTIVITY_TRANSFER_PLAN_ACTIVITY_ONLY;
        return true;
    }

    if (!play_activity_asset_core_name(
            source_core_path,
            plan_out->source_core_name,
            sizeof(plan_out->source_core_name))) {
        plan_out->kind =
            PLAY_ACTIVITY_TRANSFER_PLAN_ACTIVITY_ONLY;
        return true;
    }

    int saves_written = snprintf(
        plan_out->saves_directory,
        sizeof(plan_out->saves_directory),
        "%s/%s",
        saves_root,
        plan_out->source_core_name
    );

    int states_written = snprintf(
        plan_out->states_directory,
        sizeof(plan_out->states_directory),
        "%s/%s",
        states_root,
        plan_out->source_core_name
    );

    if (saves_written < 0 ||
        states_written < 0 ||
        (size_t)saves_written >=
            sizeof(plan_out->saves_directory) ||
        (size_t)states_written >=
            sizeof(plan_out->states_directory)) {
        return false;
    }

    PlayActivityAssetTransferResult asset_result;

    PlayActivityAssetTransfer *asset_transfer =
        play_activity_asset_transfer_prepare(
            plan_out->saves_directory,
            plan_out->states_directory,
            source_file_path,
            destination_file_path,
            false,
            &asset_result
        );

    if (asset_transfer != NULL) {
        play_activity_asset_transfer_free(asset_transfer);

        plan_out->kind =
            PLAY_ACTIVITY_TRANSFER_PLAN_READY;
        return true;
    }

    if (asset_result.failed > 0)
        return false;

    if (asset_result.blocked > 0) {
        plan_out->kind =
            PLAY_ACTIVITY_TRANSFER_PLAN_REPLACE;
        plan_out->blocked = asset_result.blocked;
        return true;
    }

    return false;
}
