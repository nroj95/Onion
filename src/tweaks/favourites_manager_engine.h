#ifndef TWEAKS_FAVOURITES_MANAGER_ENGINE_H__
#define TWEAKS_FAVOURITES_MANAGER_ENGINE_H__

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "components/JsonGameEntry.h"
#include "utils/file.h"
#include "utils/json.h"
#include "utils/str.h"

#define FAVOURITES_TEMP_PATH \
    "/mnt/SDCARD/Roms/favourite.json.tmp"

#define FAVOURITES_BACKUP_PATH \
    "/mnt/SDCARD/Roms/favourite.json.bak"

typedef struct favourites_result_s {
    int entries;
    int kept;
    int duplicates;
    int missing;
    int malformed;
    int non_rom;
    int repaired_images;
    bool changed;
} FavouritesResult;

typedef struct favourites_entry_s {
    cJSON *json;
    char *resolved_rompath;
    char label[STR_MAX];
    char system[STR_MAX];
    bool rom_backed;
    bool keep;
} FavouritesEntry;

typedef struct favourites_collection_s {
    FavouritesEntry *entries;
    int count;
    int capacity;
} FavouritesCollection;

static int favourites_sort_mode_active =
    FAVOURITES_SORT_ALPHABETICAL;

static void favourites_collection_free(
    FavouritesCollection *collection)
{
    if (collection == NULL)
        return;

    for (int i = 0; i < collection->count; i++) {
        cJSON_Delete(collection->entries[i].json);
        free(collection->entries[i].resolved_rompath);
    }

    free(collection->entries);

    *collection = (FavouritesCollection){0};
}

static bool favourites_collection_add(
    FavouritesCollection *collection,
    FavouritesEntry entry)
{
    if (collection->count >= collection->capacity) {
        int new_capacity =
            collection->capacity == 0
                ? 32
                : collection->capacity * 2;

        FavouritesEntry *new_entries =
            realloc(
                collection->entries,
                new_capacity * sizeof(FavouritesEntry));

        if (new_entries == NULL)
            return false;

        collection->entries = new_entries;
        collection->capacity = new_capacity;
    }

    collection->entries[collection->count++] = entry;
    return true;
}

static void favourites_extract_system(
    const char *resolved_rompath,
    char system[STR_MAX])
{
    system[0] = '\0';

    const char prefix[] = "/mnt/SDCARD/Roms/";
    size_t prefix_length = strlen(prefix);

    if (strncmp(
            resolved_rompath,
            prefix,
            prefix_length) != 0) {
        return;
    }

    const char *system_start =
        resolved_rompath + prefix_length;

    const char *system_end =
        strchr(system_start, '/');

    size_t system_length =
        system_end == NULL
            ? strlen(system_start)
            : (size_t)(system_end - system_start);

    if (system_length >= STR_MAX)
        system_length = STR_MAX - 1;

    memcpy(system, system_start, system_length);
    system[system_length] = '\0';
}

static bool favourites_is_duplicate(
    const FavouritesCollection *collection,
    const char *resolved_rompath)
{
    for (int i = 0; i < collection->count; i++) {
        const FavouritesEntry *entry =
            &collection->entries[i];

        if (!entry->rom_backed ||
            entry->resolved_rompath == NULL) {
            continue;
        }

        if (strcmp(
                entry->resolved_rompath,
                resolved_rompath) == 0) {
            return true;
        }
    }

    return false;
}

static bool favourites_read(
    FavouritesCollection *collection,
    FavouritesResult *result)
{
    *collection = (FavouritesCollection){0};
    *result = (FavouritesResult){0};

    FILE *file = fopen(FAVORITES_PATH, "r");
    if (file == NULL)
        return false;

    char line[8192];

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t line_length = strlen(line);

        if (line_length > 0 &&
            line[line_length - 1] != '\n' &&
            !feof(file)) {
            int character;

            while ((character = fgetc(file)) != '\n' &&
                   character != EOF) {
            }

            result->malformed++;
            continue;
        }

        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0')
            continue;

        cJSON *root = cJSON_Parse(line);

        if (root == NULL || !cJSON_IsObject(root)) {
            cJSON_Delete(root);
            result->malformed++;
            continue;
        }

        FavouritesEntry entry = {
            .json = root,
            .resolved_rompath = NULL,
            .label = "",
            .system = "",
            .rom_backed = false,
            .keep = true,
        };

        cJSON *label_item =
            cJSON_GetObjectItemCaseSensitive(
                root,
                "label");

        if (cJSON_IsString(label_item) &&
            label_item->valuestring != NULL) {
            strncpy(
                entry.label,
                label_item->valuestring,
                STR_MAX - 1);
        }

        cJSON *rompath_item =
            cJSON_GetObjectItemCaseSensitive(
                root,
                "rompath");

        if (rompath_item == NULL ||
            (cJSON_IsString(rompath_item) &&
             rompath_item->valuestring != NULL &&
             rompath_item->valuestring[0] == '\0')) {
            result->non_rom++;
        }
        else if (!cJSON_IsString(rompath_item) ||
                 rompath_item->valuestring == NULL) {
            cJSON_Delete(root);
            result->malformed++;
            continue;
        }
        else {
            entry.rom_backed = true;
            entry.resolved_rompath =
                file_resolvePath(
                    rompath_item->valuestring);

            if (entry.resolved_rompath == NULL) {
                cJSON_Delete(root);
                fclose(file);
                favourites_collection_free(collection);
                return false;
            }

            favourites_extract_system(
                entry.resolved_rompath,
                entry.system);
        }

        if (!favourites_collection_add(
                collection,
                entry)) {
            cJSON_Delete(root);
            free(entry.resolved_rompath);
            fclose(file);
            favourites_collection_free(collection);
            return false;
        }

        result->entries++;
    }

    bool success = !ferror(file);
    fclose(file);

    if (!success)
        favourites_collection_free(collection);

    return success;
}

static bool favourites_repair_image_path(
    FavouritesEntry *entry)
{
    if (!entry->rom_backed)
        return false;

    cJSON *rompath_item =
        cJSON_GetObjectItemCaseSensitive(
            entry->json,
            "rompath");

    if (!cJSON_IsString(rompath_item) ||
        rompath_item->valuestring == NULL) {
        return false;
    }

    const char *rompath = rompath_item->valuestring;
    const char *filename = strrchr(rompath, '/');

    filename =
        filename == NULL
            ? rompath
            : filename + 1;

    const char *extension = strrchr(filename, '.');

    size_t directory_length =
        filename - rompath;

    size_t stem_length =
        extension == NULL
            ? strlen(filename)
            : (size_t)(extension - filename);

    char image_path[PATH_MAX];

    int written = snprintf(
        image_path,
        sizeof(image_path),
        "%.*sImgs/%.*s.png",
        (int)directory_length,
        rompath,
        (int)stem_length,
        filename);

    if (written < 0 ||
        written >= (int)sizeof(image_path)) {
        return false;
    }

    cJSON *current_image =
        cJSON_GetObjectItemCaseSensitive(
            entry->json,
            "imgpath");

    if (cJSON_IsString(current_image) &&
        current_image->valuestring != NULL &&
        strcmp(
            current_image->valuestring,
            image_path) == 0) {
        return false;
    }

    return json_forceSetString(
        entry->json,
        "imgpath",
        image_path);
}

static int favourites_compare_entries(
    const void *left_pointer,
    const void *right_pointer)
{
    const FavouritesEntry *left =
        (const FavouritesEntry *)left_pointer;

    const FavouritesEntry *right =
        (const FavouritesEntry *)right_pointer;

    if (favourites_sort_mode_active ==
        FAVOURITES_SORT_BY_SYSTEM) {
        if (left->rom_backed != right->rom_backed)
            return left->rom_backed ? -1 : 1;

        int system_compare =
            strcasecmp(left->system, right->system);

        if (system_compare != 0)
            return system_compare;
    }

    int label_compare =
        strcasecmp(left->label, right->label);

    if (label_compare != 0)
        return label_compare;

    if (left->resolved_rompath == NULL &&
        right->resolved_rompath == NULL) {
        return 0;
    }

    if (left->resolved_rompath == NULL)
        return 1;

    if (right->resolved_rompath == NULL)
        return -1;

    return strcmp(
        left->resolved_rompath,
        right->resolved_rompath);
}

static void favourites_apply_rules(
    FavouritesCollection *collection,
    FavouritesResult *result)
{
    for (int i = 0; i < collection->count; i++) {
        FavouritesEntry *entry =
            &collection->entries[i];

        if (!entry->rom_backed)
            continue;

        if (favourites_manager_settings.remove_duplicates &&
            favourites_is_duplicate(
                &(FavouritesCollection){
                    .entries = collection->entries,
                    .count = i,
                    .capacity = i,
                },
                entry->resolved_rompath)) {
            entry->keep = false;
            result->duplicates++;
            continue;
        }

        if (favourites_manager_settings.remove_missing &&
            !exists(entry->resolved_rompath)) {
            entry->keep = false;
            result->missing++;
            continue;
        }

        if (favourites_manager_settings.repair_box_art &&
            favourites_repair_image_path(entry)) {
            result->repaired_images++;
        }
    }

    favourites_sort_mode_active =
        favourites_manager_settings.sort_mode;

    qsort(
        collection->entries,
        collection->count,
        sizeof(FavouritesEntry),
        favourites_compare_entries);

    for (int i = 0; i < collection->count; i++) {
        if (collection->entries[i].keep)
            result->kept++;
    }
}

static bool favourites_write(
    FavouritesCollection *collection,
    FavouritesResult *result)
{
    FILE *file = fopen(FAVOURITES_TEMP_PATH, "w");

    if (file == NULL)
        return false;

    bool success = true;

    for (int i = 0;
         i < collection->count && success;
         i++) {
        FavouritesEntry *entry =
            &collection->entries[i];

        if (!entry->keep)
            continue;

        char *json =
            cJSON_PrintUnformatted(entry->json);

        if (json == NULL) {
            success = false;
            break;
        }

        if (fprintf(file, "%s\n", json) < 0)
            success = false;

        cJSON_free(json);
    }

    if (success && fflush(file) != 0)
        success = false;

    if (success && fsync(fileno(file)) != 0)
        success = false;

    if (fclose(file) != 0)
        success = false;

    if (!success) {
        remove(FAVOURITES_TEMP_PATH);
        return false;
    }

    char *original = file_read(FAVORITES_PATH);
    char *updated = file_read(FAVOURITES_TEMP_PATH);

    if (original == NULL || updated == NULL) {
        free(original);
        free(updated);
        remove(FAVOURITES_TEMP_PATH);
        return false;
    }

    result->changed =
        strcmp(original, updated) != 0;

    free(original);
    free(updated);

    if (!result->changed) {
        remove(FAVOURITES_TEMP_PATH);
        return true;
    }

    file_copy(
        FAVORITES_PATH,
        FAVOURITES_BACKUP_PATH);

    if (rename(
            FAVOURITES_TEMP_PATH,
            FAVORITES_PATH) != 0) {
        remove(FAVOURITES_TEMP_PATH);
        return false;
    }

    sync();
    return true;
}

static bool favourites_preview(
    FavouritesResult *result)
{
    FavouritesCollection collection;

    if (!favourites_read(
            &collection,
            result)) {
        return false;
    }

    if (result->malformed > 0) {
        favourites_collection_free(&collection);
        return true;
    }

    favourites_apply_rules(
        &collection,
        result);

    favourites_collection_free(&collection);
    return true;
}

static bool favourites_apply(
    FavouritesResult *result)
{
    FavouritesCollection collection;

    if (!favourites_read(
            &collection,
            result)) {
        return false;
    }

    if (result->malformed > 0) {
        favourites_collection_free(&collection);
        return true;
    }

    favourites_apply_rules(
        &collection,
        result);

    bool success =
        favourites_write(
            &collection,
            result);

    favourites_collection_free(&collection);
    return success;
}

static void favourites_show_result(
    const char *title,
    const FavouritesResult *result,
    bool applied)
{
    char message[STR_MAX];

    if (result->malformed > 0) {
        snprintf(
            message,
            sizeof(message),
            "Malformed entries: %d\n"
            "No changes were written.",
            result->malformed);

        __showInfoDialog(title, message);
        return;
    }

    snprintf(
        message,
        sizeof(message),
        "Entries: %d -> %d\n"
        "Duplicates removed: %d\n"
        "Missing removed: %d\n"
        "Image paths repaired: %d\n"
        "%s",
        result->entries,
        result->kept,
        result->duplicates,
        result->missing,
        result->repaired_images,
        applied
            ? (result->changed
                   ? "Favorites updated."
                   : "Already up to date.")
            : "Preview only.");

    __showInfoDialog(title, message);
}

static void action_favouritesManagerPreview(void *pointer)
{
    (void)pointer;

    FavouritesResult result;

    if (!favourites_preview(&result)) {
        __showInfoDialog(
            "Preview changes",
            "Could not process favourite.json.");
        return;
    }

    favourites_show_result(
        "Preview changes",
        &result,
        false);
}

static void action_favouritesManagerApply(void *pointer)
{
    (void)pointer;

    FavouritesResult result;

    if (!favourites_apply(&result)) {
        __showInfoDialog(
            "Apply changes",
            "Could not update favourite.json.");
        return;
    }

    favourites_show_result(
        "Apply changes",
        &result,
        true);
}

#endif // TWEAKS_FAVOURITES_MANAGER_ENGINE_H__
