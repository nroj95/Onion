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

    char source_absolute_path[PATH_MAX] = "";

    if (!play_activity_transfer_stored_path_to_absolute(
            roms_root,
            source_file_path,
            source_absolute_path,
            sizeof(source_absolute_path))) {
        plan_out->kind =
            PLAY_ACTIVITY_TRANSFER_PLAN_ACTIVITY_ONLY;
        return true;
    }

    char source_core_path[PATH_MAX] = "";

    if (!play_activity_history_find_core_path(
            history_path,
            source_absolute_path,
            source_core_path,
            sizeof(source_core_path))) {
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

    char saves_directory[PATH_MAX];
    char states_directory[PATH_MAX];

    int saves_written = snprintf(
        saves_directory,
        sizeof(saves_directory),
        "%s/%s",
        saves_root,
        plan_out->source_core_name
    );

    int states_written = snprintf(
        states_directory,
        sizeof(states_directory),
        "%s/%s",
        states_root,
        plan_out->source_core_name
    );

    if (saves_written < 0 ||
        states_written < 0 ||
        (size_t)saves_written >= sizeof(saves_directory) ||
        (size_t)states_written >= sizeof(states_directory)) {
        return false;
    }

    PlayActivityAssetTransferResult asset_result;

    PlayActivityAssetTransfer *asset_transfer =
        play_activity_asset_transfer_prepare(
            saves_directory,
            states_directory,
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
