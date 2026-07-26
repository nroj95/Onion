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
    rmdir(directory_source);
    rmdir(directory_destination);
    rmdir(saves_path);
    rmdir(temporary_directory);
}
