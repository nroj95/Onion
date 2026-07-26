#include "./playActivityAssets.h"

#include "utils/file.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* =============================================================================
 * purpose:
 * safely migrate save and state files after a rom has been renamed.
 *
 * key behavior:
 * - derives retroarch corename values from libretro .info files.
 * - recognizes exact basenames and basename-plus-dot asset families.
 * - renames files only when the destination does not already exist.
 * - reports missing, blocked, and failed operations separately.
 * - does not create directories or remove any files.
 * =============================================================================
 */

static bool asset_family_matches(
    const char *filename,
    const char *old_basename
)
{
    if (filename == NULL ||
        old_basename == NULL ||
        old_basename[0] == '\0') {
        return false;
    }

    size_t old_length = strlen(old_basename);

    if (strncmp(filename, old_basename, old_length) != 0)
        return false;

    return filename[old_length] == '\0' ||
           filename[old_length] == '.';
}

typedef struct {
    char *source_path;
    char *destination_path;
} AssetMigrationOperation;

static void free_asset_migration_operations(
    AssetMigrationOperation *operations,
    size_t operation_count
)
{
    if (operations == NULL)
        return;

    for (size_t index = 0; index < operation_count; index++) {
        free(operations[index].source_path);
        free(operations[index].destination_path);
    }

    free(operations);
}

static bool append_asset_migration_operation(
    AssetMigrationOperation **operations,
    size_t *operation_count,
    size_t *operation_capacity,
    const char *source_path,
    const char *destination_path
)
{
    if (operations == NULL ||
        operation_count == NULL ||
        operation_capacity == NULL ||
        source_path == NULL ||
        destination_path == NULL) {
        return false;
    }

    if (*operation_count == *operation_capacity) {
        size_t new_capacity =
            *operation_capacity == 0
                ? 8
                : *operation_capacity * 2;

        if (new_capacity < *operation_capacity ||
            new_capacity > SIZE_MAX / sizeof(**operations)) {
            return false;
        }

        AssetMigrationOperation *resized = realloc(
            *operations,
            new_capacity * sizeof(**operations)
        );

        if (resized == NULL)
            return false;

        *operations = resized;
        *operation_capacity = new_capacity;
    }

    char *stored_source = strdup(source_path);
    if (stored_source == NULL)
        return false;

    char *stored_destination = strdup(destination_path);
    if (stored_destination == NULL) {
        free(stored_source);
        return false;
    }

    AssetMigrationOperation *operation =
        &(*operations)[*operation_count];

    operation->source_path = stored_source;
    operation->destination_path = stored_destination;
    (*operation_count)++;

    return true;
}

static void rollback_asset_migration(
    AssetMigrationOperation *operations,
    size_t moved_count,
    PlayActivityAssetMigrationResult *result
)
{
    while (moved_count > 0) {
        moved_count--;

        AssetMigrationOperation *operation =
            &operations[moved_count];

        if (rename(
                operation->destination_path,
                operation->source_path) == 0) {
            result->moved--;
        }
        else {
            result->failed++;
        }
    }
}

bool play_activity_asset_core_name(
    const char *core_path,
    char *core_name_out,
    size_t core_name_out_size
)
{
    if (core_name_out != NULL &&
        core_name_out_size > 0) {
        core_name_out[0] = '\0';
    }

    if (core_path == NULL ||
        core_path[0] == '\0' ||
        core_name_out == NULL ||
        core_name_out_size == 0) {
        return false;
    }

    char *core_base_path = file_removeExtension(core_path);

    if (core_base_path == NULL)
        return false;

    char info_path[PATH_MAX];

    int written = snprintf(
        info_path,
        sizeof(info_path),
        "%s.info",
        core_base_path
    );

    free(core_base_path);

    if (written < 0 ||
        (size_t)written >= sizeof(info_path)) {
        return false;
    }

    char parsed_core_name[256] = "";

    file_parseKeyValue(
        info_path,
        "corename",
        parsed_core_name,
        '=',
        0
    );

    if (parsed_core_name[0] == '\0')
        return false;

    written = snprintf(
        core_name_out,
        core_name_out_size,
        "%s",
        parsed_core_name
    );

    if (written < 0 ||
        (size_t)written >= core_name_out_size) {
        core_name_out[0] = '\0';
        return false;
    }

    return true;
}

bool play_activity_asset_migrate_directory(
    const char *directory_path,
    const char *old_rom_path,
    const char *new_rom_path,
    PlayActivityAssetMigrationResult *result_out
)
{
    if (result_out != NULL)
        memset(result_out, 0, sizeof(*result_out));

    if (directory_path == NULL ||
        old_rom_path == NULL ||
        new_rom_path == NULL ||
        result_out == NULL ||
        directory_path[0] == '\0' ||
        old_rom_path[0] == '\0' ||
        new_rom_path[0] == '\0') {
        return false;
    }

    const char *old_filename = file_basename(old_rom_path);
    const char *new_filename = file_basename(new_rom_path);

    char *old_basename = file_removeExtension(old_filename);
    char *new_basename = file_removeExtension(new_filename);

    if (old_basename == NULL ||
        new_basename == NULL ||
        old_basename[0] == '\0' ||
        new_basename[0] == '\0') {
        free(old_basename);
        free(new_basename);
        return false;
    }

    if (strcmp(old_basename, new_basename) == 0) {
        free(old_basename);
        free(new_basename);
        return true;
    }

    DIR *directory = opendir(directory_path);

    if (directory == NULL) {
        if (errno == ENOENT) {
            result_out->missing++;
            free(old_basename);
            free(new_basename);
            return true;
        }

        result_out->failed++;
        free(old_basename);
        free(new_basename);
        return false;
    }

    bool found_source = false;
    AssetMigrationOperation *operations = NULL;
    size_t operation_count = 0;
    size_t operation_capacity = 0;
    struct dirent *entry = NULL;

    /*
     * Preflight the complete family before mutating anything. A collision in
     * one save/state sibling must not leave the rest of the family renamed.
     */
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            !asset_family_matches(
                entry->d_name,
                old_basename
            )) {
            continue;
        }

        const char *suffix =
            entry->d_name + strlen(old_basename);

        char source_path[PATH_MAX];
        char destination_path[PATH_MAX];

        int source_written = snprintf(
            source_path,
            sizeof(source_path),
            "%s/%s",
            directory_path,
            entry->d_name
        );

        int destination_written = snprintf(
            destination_path,
            sizeof(destination_path),
            "%s/%s%s",
            directory_path,
            new_basename,
            suffix
        );

        if (source_written < 0 ||
            destination_written < 0 ||
            (size_t)source_written >= sizeof(source_path) ||
            (size_t)destination_written >=
                sizeof(destination_path)) {
            result_out->failed++;
            continue;
        }

        struct stat source_status;

        if (lstat(source_path, &source_status) != 0) {
            result_out->failed++;
            continue;
        }

        if (!S_ISREG(source_status.st_mode))
            continue;

        found_source = true;

        if (exists(destination_path)) {
            result_out->blocked++;
            continue;
        }

        if (!append_asset_migration_operation(
                &operations,
                &operation_count,
                &operation_capacity,
                source_path,
                destination_path)) {
            result_out->failed++;
        }
    }

    closedir(directory);

    if (!found_source)
        result_out->missing++;

    if (result_out->blocked != 0 ||
        result_out->failed != 0) {
        free_asset_migration_operations(
            operations,
            operation_count
        );
        free(old_basename);
        free(new_basename);
        return false;
    }

    size_t moved_count = 0;

    for (size_t index = 0; index < operation_count; index++) {
        AssetMigrationOperation *operation = &operations[index];

        /*
         * Recheck immediately before rename in case the destination appeared
         * after preflight. Roll back earlier siblings on any failure.
         */
        if (exists(operation->destination_path)) {
            result_out->blocked++;

            rollback_asset_migration(
                operations,
                moved_count,
                result_out
            );
            break;
        }

        if (rename(
                operation->source_path,
                operation->destination_path) != 0) {
            result_out->failed++;

            rollback_asset_migration(
                operations,
                moved_count,
                result_out
            );
            break;
        }

        result_out->moved++;
        moved_count++;
    }

    free_asset_migration_operations(
        operations,
        operation_count
    );

    free(old_basename);
    free(new_basename);

    return result_out->blocked == 0 &&
           result_out->failed == 0;
}
