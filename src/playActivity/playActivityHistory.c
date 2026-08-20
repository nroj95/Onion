#include "./playActivityHistory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "utils/file.h"

/* =============================================================================
 * purpose:
 * resolve the retroarch core used for a launched rom.
 *
 * key behavior:
 * - parses a retroarch content history playlist.
 * - compares normalized rom paths.
 * - leaves the output empty when no valid match exists.
 * =============================================================================
 */

bool play_activity_history_find_core_path(
    const char *history_path,
    const char *rom_path,
    char *core_path_out,
    size_t core_path_out_size
)
{
    if (core_path_out != NULL &&
        core_path_out_size > 0) {
        core_path_out[0] = '\0';
    }

    if (history_path == NULL ||
        rom_path == NULL ||
        core_path_out == NULL ||
        core_path_out_size == 0) {
        return false;
    }

    FILE *history_file = fopen(history_path, "rb");

    if (history_file == NULL)
        return false;

    if (fseek(history_file, 0, SEEK_END) != 0) {
        fclose(history_file);
        return false;
    }

    long history_size = ftell(history_file);

    if (history_size < 0 ||
        fseek(history_file, 0, SEEK_SET) != 0) {
        fclose(history_file);
        return false;
    }

    char *history_contents =
        malloc((size_t)history_size + 1);

    if (history_contents == NULL) {
        fclose(history_file);
        return false;
    }

    size_t bytes_read = fread(
        history_contents,
        1,
        (size_t)history_size,
        history_file
    );

    fclose(history_file);

    if (bytes_read != (size_t)history_size) {
        free(history_contents);
        return false;
    }

    history_contents[bytes_read] = '\0';

    cJSON *history = cJSON_Parse(history_contents);
    free(history_contents);

    if (history == NULL)
        return false;

    cJSON *items =
        cJSON_GetObjectItemCaseSensitive(history, "items");

    char *resolved_rom_path = file_resolvePath(rom_path);
    bool found = false;

    if (cJSON_IsArray(items) &&
        resolved_rom_path != NULL) {
        cJSON *item = NULL;

        cJSON_ArrayForEach(item, items) {
            cJSON *path =
                cJSON_GetObjectItemCaseSensitive(
                    item,
                    "path"
                );

            cJSON *core_path =
                cJSON_GetObjectItemCaseSensitive(
                    item,
                    "core_path"
                );

            if (!cJSON_IsString(path) ||
                path->valuestring == NULL ||
                strcmp(
                    path->valuestring,
                    resolved_rom_path
                ) != 0) {
                continue;
            }

            if (!cJSON_IsString(core_path) ||
                core_path->valuestring == NULL ||
                core_path->valuestring[0] == '\0') {
                break;
            }

            int written = snprintf(
                core_path_out,
                core_path_out_size,
                "%s",
                core_path->valuestring
            );

            if (written < 0 ||
                (size_t)written >= core_path_out_size) {
                core_path_out[0] = '\0';
                continue;
            }

            found = true;
            break;
        }
    }

    free(resolved_rom_path);
    cJSON_Delete(history);

    return found;
}
