#include "playActivityAssets.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* =============================================================================
 * purpose:
 * verify safe rom asset-family migration behavior.
 *
 * key behavior:
 * - resolves corename values from libretro info files.
 * - migrates exact and suffix-bearing basename family members.
 * - leaves unrelated files untouched.
 * - blocks destination collisions without overwriting either file.
 * - treats missing directories and missing source families safely.
 * =============================================================================
 */

static bool write_text_file(
    const char *path,
    const char *contents
)
{
    FILE *file = fopen(path, "w");

    if (file == NULL)
        return false;

    bool written =
        fputs(contents, file) >= 0 &&
        fclose(file) == 0;

    return written;
}

static bool text_file_matches(
    const char *path,
    const char *expected
)
{
    FILE *file = fopen(path, "r");

    if (file == NULL)
        return false;

    char contents[128] = "";

    bool read_ok =
        fgets(contents, sizeof(contents), file) != NULL;

    fclose(file);

    return read_ok &&
           strcmp(contents, expected) == 0;
}

void test_asset_migration(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-assets-XXXXXX";

    bool directory_created =
        mkdtemp(temporary_directory) != NULL;

    check_condition(
        directory_created,
        "create asset migration fixture directory"
    );

    if (!directory_created)
        return;

    char core_path[512];
    char info_path[512];
    char saves_path[512];

    snprintf(
        core_path,
        sizeof(core_path),
        "%s/gambatte_libretro.so",
        temporary_directory
    );

    snprintf(
        info_path,
        sizeof(info_path),
        "%s/gambatte_libretro.info",
        temporary_directory
    );

    snprintf(
        saves_path,
        sizeof(saves_path),
        "%s/saves",
        temporary_directory
    );

    check_condition(
        mkdir(saves_path, 0700) == 0 &&
        write_text_file(
            info_path,
            "display_name = Nintendo - Game Boy\n"
            "corename = Gambatte\n"
        ),
        "write asset migration fixtures"
    );

    char core_name[256] = "";

    check_condition(
        play_activity_asset_core_name(
            core_path,
            core_name,
            sizeof(core_name)
        ) &&
        strcmp(core_name, "Gambatte") == 0,
        "resolve corename from core info"
    );

    char old_save[4096];
    char old_state[4096];
    char unrelated[4096];

    snprintf(
        old_save,
        sizeof(old_save),
        "%s/old-name.srm",
        saves_path
    );

    snprintf(
        old_state,
        sizeof(old_state),
        "%s/old-name.state.auto",
        saves_path
    );

    snprintf(
        unrelated,
        sizeof(unrelated),
        "%s/old-name-extra.srm",
        saves_path
    );

    check_condition(
        write_text_file(old_save, "save") &&
        write_text_file(old_state, "state") &&
        write_text_file(unrelated, "unrelated"),
        "write basename family fixtures"
    );

    PlayActivityAssetMigrationResult result;

    check_condition(
        play_activity_asset_migrate_directory(
            saves_path,
            "/Roms/GB/old-name.gb",
            "/Roms/GB/new-name.gb",
            &result
        ),
        "migrate basename asset family"
    );

    char new_save[4096];
    char new_state[4096];

    snprintf(
        new_save,
        sizeof(new_save),
        "%s/new-name.srm",
        saves_path
    );

    snprintf(
        new_state,
        sizeof(new_state),
        "%s/new-name.state.auto",
        saves_path
    );

    check_condition(
        result.moved == 2 &&
        result.missing == 0 &&
        result.blocked == 0 &&
        result.failed == 0,
        "asset migration reports moved files"
    );

    check_condition(
        access(old_save, F_OK) != 0 &&
        access(old_state, F_OK) != 0 &&
        access(new_save, F_OK) == 0 &&
        access(new_state, F_OK) == 0,
        "asset migration preserves complete suffixes"
    );

    check_condition(
        access(unrelated, F_OK) == 0,
        "asset migration ignores non-family names"
    );

    char blocked_source[4096];
    char blocked_destination[4096];

    snprintf(
        blocked_source,
        sizeof(blocked_source),
        "%s/collision.srm",
        saves_path
    );

    snprintf(
        blocked_destination,
        sizeof(blocked_destination),
        "%s/existing.srm",
        saves_path
    );

    check_condition(
        write_text_file(blocked_source, "source") &&
        write_text_file(blocked_destination, "destination"),
        "write collision fixtures"
    );

    check_condition(
        !play_activity_asset_migrate_directory(
            saves_path,
            "/Roms/GB/collision.gb",
            "/Roms/GB/existing.gb",
            &result
        ) &&
        result.moved == 0 &&
        result.blocked == 1 &&
        result.failed == 0,
        "asset migration blocks destination collision"
    );

    check_condition(
        access(blocked_source, F_OK) == 0 &&
        access(blocked_destination, F_OK) == 0,
        "blocked migration preserves both files"
    );

    char partial_source_save[4096];
    char partial_source_state[4096];
    char partial_destination_save[4096];
    char partial_destination_state[4096];

    snprintf(
        partial_source_save,
        sizeof(partial_source_save),
        "%s/partial.srm",
        saves_path
    );

    snprintf(
        partial_source_state,
        sizeof(partial_source_state),
        "%s/partial.state.auto",
        saves_path
    );

    snprintf(
        partial_destination_save,
        sizeof(partial_destination_save),
        "%s/renamed-partial.srm",
        saves_path
    );

    snprintf(
        partial_destination_state,
        sizeof(partial_destination_state),
        "%s/renamed-partial.state.auto",
        saves_path
    );

    check_condition(
        write_text_file(partial_source_save, "save") &&
        write_text_file(partial_source_state, "state") &&
        write_text_file(
            partial_destination_state,
            "existing destination"
        ),
        "write partial-collision fixtures"
    );

    check_condition(
        !play_activity_asset_migrate_directory(
            saves_path,
            "/Roms/GB/partial.gb",
            "/Roms/GB/renamed-partial.gb",
            &result
        ) &&
        result.moved == 0 &&
        result.blocked == 1 &&
        result.failed == 0,
        "collision blocks entire asset family"
    );

    check_condition(
        access(partial_source_save, F_OK) == 0 &&
        access(partial_source_state, F_OK) == 0 &&
        access(partial_destination_save, F_OK) != 0 &&
        access(partial_destination_state, F_OK) == 0,
        "blocked family remains entirely unchanged"
    );

    char directory_source[4096];
    char directory_destination[4096];

    snprintf(
        directory_source,
        sizeof(directory_source),
        "%s/directory-family.state",
        saves_path
    );

    snprintf(
        directory_destination,
        sizeof(directory_destination),
        "%s/renamed-directory-family.state",
        saves_path
    );

    check_condition(
        mkdir(directory_source, 0700) == 0,
        "create matching asset directory fixture"
    );

    check_condition(
        play_activity_asset_migrate_directory(
            saves_path,
            "/Roms/GB/directory-family.gb",
            "/Roms/GB/renamed-directory-family.gb",
            &result
        ) &&
        result.moved == 0 &&
        result.blocked == 0 &&
        result.failed == 0,
        "ignore matching asset directories"
    );

    check_condition(
        access(directory_source, F_OK) == 0 &&
        access(directory_destination, F_OK) != 0,
        "matching asset directory remains unchanged"
    );

    char missing_directory[512];

    snprintf(
        missing_directory,
        sizeof(missing_directory),
        "%s/missing",
        temporary_directory
    );

    check_condition(
        play_activity_asset_migrate_directory(
            missing_directory,
            "/Roms/GB/old-name.gb",
            "/Roms/GB/new-name.gb",
            &result
        ) &&
        result.missing == 1 &&
        result.moved == 0 &&
        result.blocked == 0 &&
        result.failed == 0,
        "missing asset directory is safe"
    );

    check_condition(
        play_activity_asset_migrate_directory(
            saves_path,
            "/Roms/GB/absent.gb",
            "/Roms/GB/renamed.gb",
            &result
        ) &&
        result.missing == 1,
        "missing asset family is safe"
    );

    char states_path[512];

    snprintf(
        states_path,
        sizeof(states_path),
        "%s/states",
        temporary_directory
    );

    check_condition(
        mkdir(states_path, 0700) == 0,
        "create transactional state fixture directory"
    );

    char transaction_save_source[4096];
    char transaction_save_destination[4096];
    char transaction_state_source[4096];
    char transaction_state_destination[4096];

    snprintf(
        transaction_save_source,
        sizeof(transaction_save_source),
        "%s/source.srm",
        saves_path
    );
    snprintf(
        transaction_save_destination,
        sizeof(transaction_save_destination),
        "%s/destination.srm",
        saves_path
    );
    snprintf(
        transaction_state_source,
        sizeof(transaction_state_source),
        "%s/source.state.auto",
        states_path
    );
    snprintf(
        transaction_state_destination,
        sizeof(transaction_state_destination),
        "%s/destination.state.auto",
        states_path
    );

    check_condition(
        write_text_file(
            transaction_save_source,
            "source save"
        ) &&
        write_text_file(
            transaction_save_destination,
            "destination save"
        ) &&
        write_text_file(
            transaction_state_source,
            "source state"
        ) &&
        write_text_file(
            transaction_state_destination,
            "destination state"
        ),
        "write transactional asset fixtures"
    );

    PlayActivityAssetTransferResult transfer_result;

    PlayActivityAssetTransfer *blocked_transfer =
        play_activity_asset_transfer_prepare(
            saves_path,
            states_path,
            "/Roms/GB/source.gb",
            "/Roms/GB/destination.gb",
            false,
            &transfer_result
        );

    check_condition(
        blocked_transfer == NULL &&
        transfer_result.blocked == 2 &&
        access(transaction_save_source, F_OK) == 0 &&
        access(transaction_save_destination, F_OK) == 0 &&
        access(transaction_state_source, F_OK) == 0 &&
        access(transaction_state_destination, F_OK) == 0,
        "transaction preflight blocks replacement without mutation"
    );

    PlayActivityAssetTransfer *rollback_transfer =
        play_activity_asset_transfer_prepare(
            saves_path,
            states_path,
            "/Roms/GB/source.gb",
            "/Roms/GB/destination.gb",
            true,
            &transfer_result
        );

    check_condition(
        rollback_transfer != NULL,
        "prepare replacement asset transaction"
    );

    check_condition(
        rollback_transfer != NULL &&
        play_activity_asset_transfer_apply(
            rollback_transfer,
            &transfer_result
        ) &&
        transfer_result.moved == 2 &&
        transfer_result.replaced == 2,
        "apply save and state transaction together"
    );

    check_condition(
        rollback_transfer != NULL &&
        play_activity_asset_transfer_rollback(
            rollback_transfer
        ) &&
        access(transaction_save_source, F_OK) == 0 &&
        access(transaction_save_destination, F_OK) == 0 &&
        access(transaction_state_source, F_OK) == 0 &&
        access(transaction_state_destination, F_OK) == 0,
        "rollback restores source and destination families"
    );

    play_activity_asset_transfer_free(
        rollback_transfer
    );

    PlayActivityAssetTransfer *failure_transfer =
        play_activity_asset_transfer_prepare(
            saves_path,
            states_path,
            "/Roms/GB/source.gb",
            "/Roms/GB/destination.gb",
            true,
            &transfer_result
        );

    check_condition(
        failure_transfer != NULL,
        "prepare cross-directory rollback transaction"
    );

    char hidden_states_path[512];

    snprintf(
        hidden_states_path,
        sizeof(hidden_states_path),
        "%s/states-hidden",
        temporary_directory
    );

    bool states_hidden =
        rename(states_path, hidden_states_path) == 0;

    check_condition(
        states_hidden,
        "hide state directory after transaction preflight"
    );

    bool failure_rolled_back =
        failure_transfer != NULL &&
        states_hidden &&
        !play_activity_asset_transfer_apply(
            failure_transfer,
            &transfer_result
        );

    if (states_hidden)
        rename(hidden_states_path, states_path);

    check_condition(
        failure_rolled_back &&
        text_file_matches(
            transaction_save_source,
            "source save"
        ) &&
        text_file_matches(
            transaction_save_destination,
            "destination save"
        ) &&
        text_file_matches(
            transaction_state_source,
            "source state"
        ) &&
        text_file_matches(
            transaction_state_destination,
            "destination state"
        ),
        "second-directory failure rolls back complete transaction"
    );

    play_activity_asset_transfer_free(
        failure_transfer
    );

    PlayActivityAssetTransfer *commit_transfer =
        play_activity_asset_transfer_prepare(
            saves_path,
            states_path,
            "/Roms/GB/source.gb",
            "/Roms/GB/destination.gb",
            true,
            &transfer_result
        );

    check_condition(
        commit_transfer != NULL &&
        play_activity_asset_transfer_apply(
            commit_transfer,
            &transfer_result
        ) &&
        play_activity_asset_transfer_commit(
            commit_transfer
        ),
        "commit replacement asset transaction"
    );

    check_condition(
        access(transaction_save_source, F_OK) != 0 &&
        access(transaction_save_destination, F_OK) == 0 &&
        access(transaction_state_source, F_OK) != 0 &&
        access(transaction_state_destination, F_OK) == 0,
        "committed transaction keeps new asset names"
    );

    play_activity_asset_transfer_free(
        commit_transfer
    );

    unlink(info_path);
    unlink(new_save);
    unlink(new_state);
    unlink(unrelated);
    unlink(blocked_source);
    unlink(blocked_destination);
    unlink(partial_source_save);
    unlink(partial_source_state);
    unlink(partial_destination_save);
    unlink(partial_destination_state);
    unlink(transaction_save_source);
    unlink(transaction_save_destination);
    unlink(transaction_state_source);
    unlink(transaction_state_destination);
    rmdir(directory_source);
    rmdir(directory_destination);
    rmdir(states_path);
    rmdir(saves_path);
    rmdir(temporary_directory);
}
