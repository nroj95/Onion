#ifndef TWEAKS_FAVOURITES_MANAGER_ENGINE_H__
#define TWEAKS_FAVOURITES_MANAGER_ENGINE_H__

#include <stdbool.h>
#include <ctype.h>
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

static int favourites_system_order_active =
    FAVOURITES_SYSTEM_ORDER_ALPHABETICAL;

static char favourites_current_system_order
    [FAVOURITES_MAX_SYSTEMS][STR_MAX];

static int favourites_current_system_count = 0;

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

typedef struct favourites_system_label_s {
    const char *system;
    const char *label;
} FavouritesSystemLabel;

static const FavouritesSystemLabel favourites_system_labels[] = {
    {"ARCADE", "arcade"},
    {"ATARI", "a2600"},
    {"COLECO", "coleco"},
    {"CPC", "cpc"},
    {"CPS1", "cps1"},
    {"CPS2", "cps2"},
    {"CPS3", "cps3"},
    {"FAIRCHILD", "chf"},
    {"FC", "nes"},
    {"FDS", "fds"},
    {"GB", "gb"},
    {"GBA", "gba"},
    {"GBC", "gbc"},
    {"GG", "gg"},
    {"INTELLIVISION", "intv"},
    {"LYNX", "lynx"},
    {"MD", "md"},
    {"MEGADUCK", "megaduck"},
    {"MS", "sms"},
    {"MSX", "msx"},
    {"NDS", "nds"},
    {"NEOCD", "neocd"},
    {"NEOGEO", "neogeo"},
    {"NGP", "ngp"},
    {"ODYSSEY", "odyssey"},
    {"PCE", "pce"},
    {"PCECD", "pcecd"},
    {"PICO", "pico8"},
    {"POKE", "poke"},
    {"PORTS", "ports"},
    {"PS", "ps1"},
    {"SATELLAVIEW", "bsx"},
    {"SEGACD", "segacd"},
    {"SEGASGONE", "sg1000"},
    {"SEVENTYEIGHTHUNDRED", "a7800"},
    {"SFC", "snes"},
    {"SUPERVISION", "supervision"},
    {"THIRTYTWOX", "32x"},
    {"VB", "vb"},
    {"VECTREX", "vectrex"},
    {"WS", "ws"},
};

static const char *favourites_get_system_label(
    const char *system)
{
    size_t label_count =
        sizeof(favourites_system_labels) /
        sizeof(favourites_system_labels[0]);

    for (size_t i = 0; i < label_count; i++) {
        if (strcasecmp(
                system,
                favourites_system_labels[i].system) == 0) {
            return favourites_system_labels[i].label;
        }
    }

    return NULL;
}

static bool favourites_strip_one_system_prefix(
    char *label)
{
    size_t label_count =
        sizeof(favourites_system_labels) /
        sizeof(favourites_system_labels[0]);

    for (size_t i = 0; i < label_count; i++) {
        char prefix[STR_MAX];

        int prefix_length =
            snprintf(
                prefix,
                sizeof(prefix),
                "[%s]",
                favourites_system_labels[i].label);

        if (prefix_length <= 0 ||
            (size_t)prefix_length >= sizeof(prefix)) {
            continue;
        }

        if (strncasecmp(
                label,
                prefix,
                (size_t)prefix_length) != 0) {
            continue;
        }

        size_t remove_length =
            (size_t)prefix_length;

        while (label[remove_length] != '\0' &&
               isspace(
                   (unsigned char)
                       label[remove_length])) {
            remove_length++;
        }

        size_t label_length = strlen(label);

        memmove(
            label,
            label + remove_length,
            label_length - remove_length + 1);

        return true;
    }

    return false;
}

static void favourites_strip_system_prefixes(
    char *label)
{
    while (favourites_strip_one_system_prefix(label)) {
    }
}

static bool favourites_update_system_prefix(
    FavouritesEntry *entry)
{
    if (!entry->rom_backed ||
        entry->system[0] == '\0') {
        return false;
    }

    cJSON *label_item =
        cJSON_GetObjectItemCaseSensitive(
            entry->json,
            "label");

    if (!cJSON_IsString(label_item) ||
        label_item->valuestring == NULL) {
        return false;
    }

    char updated_label[STR_MAX];

    if (favourites_manager_settings.show_system_prefixes) {
        const char *system_label =
            favourites_get_system_label(
                entry->system);

        if (system_label == NULL)
            return false;

        int written =
            snprintf(
                updated_label,
                sizeof(updated_label),
                "[%s] %s",
                system_label,
                entry->label);

        if (written < 0 ||
            (size_t)written >= sizeof(updated_label)) {
            return false;
        }
    }
    else {
        strncpy(
            updated_label,
            entry->label,
            sizeof(updated_label) - 1);

        updated_label[
            sizeof(updated_label) - 1] = '\0';
    }

    if (strcmp(
            label_item->valuestring,
            updated_label) == 0) {
        return false;
    }

    return cJSON_SetValuestring(
               label_item,
               updated_label) != NULL;
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

            favourites_strip_system_prefixes(
                entry.label);
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

static int favourites_find_custom_system_order(
    const char *system)
{
    for (int i = 0;
         i < favourites_custom_system_count;
         i++) {
        if (strcasecmp(
                favourites_custom_system_order[i],
                system) == 0) {
            return i;
        }
    }

    return FAVOURITES_MAX_SYSTEMS;
}

static int favourites_find_system_order(
    const char *system)
{
    for (int i = 0;
         i < favourites_current_system_count;
         i++) {
        if (strcasecmp(
                favourites_current_system_order[i],
                system) == 0) {
            return i;
        }
    }

    return FAVOURITES_MAX_SYSTEMS;
}

static void favourites_capture_current_system_order(
    const FavouritesCollection *collection)
{
    favourites_current_system_count = 0;

    for (int i = 0;
         i < collection->count &&
         favourites_current_system_count <
             FAVOURITES_MAX_SYSTEMS;
         i++) {
        const FavouritesEntry *entry =
            &collection->entries[i];

        if (!entry->rom_backed ||
            entry->system[0] == '\0') {
            continue;
        }

        if (favourites_find_system_order(
                entry->system) <
            FAVOURITES_MAX_SYSTEMS) {
            continue;
        }

        strncpy(
            favourites_current_system_order[
                favourites_current_system_count],
            entry->system,
            STR_MAX - 1);

        favourites_current_system_order[
            favourites_current_system_count]
            [STR_MAX - 1] = '\0';

        favourites_current_system_count++;
    }
}

static bool favourites_priority_name_matches(
    const char *label,
    const char *priority_name)
{
    if (label == NULL ||
        priority_name == NULL ||
        priority_name[0] == '\0') {
        return false;
    }

    for (size_t start = 0;
         label[start] != '\0';
         start++) {
        unsigned char start_character =
            (unsigned char)label[start];

        if (!isalnum(start_character))
            continue;

        if (start > 0 &&
            isalnum(
                (unsigned char)label[start - 1])) {
            continue;
        }

        size_t label_position = start;
        size_t name_position = 0;

        while (true) {
            while (priority_name[name_position] != '\0' &&
                   !isalnum(
                       (unsigned char)
                           priority_name[name_position])) {
                name_position++;
            }

            if (priority_name[name_position] == '\0')
                break;

            while (label[label_position] != '\0' &&
                   !isalnum(
                       (unsigned char)
                           label[label_position])) {
                label_position++;
            }

            if (label[label_position] == '\0')
                break;

            if (tolower(
                    (unsigned char)
                        label[label_position]) !=
                tolower(
                    (unsigned char)
                        priority_name[name_position])) {
                break;
            }

            label_position++;
            name_position++;
        }

        while (priority_name[name_position] != '\0' &&
               !isalnum(
                   (unsigned char)
                       priority_name[name_position])) {
            name_position++;
        }

        if (priority_name[name_position] != '\0')
            continue;

        if (label[label_position] == '\0' ||
            !isalnum(
                (unsigned char)
                    label[label_position])) {
            return true;
        }
    }

    return false;
}

static int favourites_find_priority_name(
    const char *label)
{
    for (int i = 0;
         i < favourites_priority_name_count;
         i++) {
        if (favourites_priority_name_matches(
                label,
                favourites_priority_names[i])) {
            return i;
        }
    }

    return favourites_priority_name_count;
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
        FAVOURITES_SORT_PRIORITY_NAMES) {
        int left_priority =
            favourites_find_priority_name(
                left->label);

        int right_priority =
            favourites_find_priority_name(
                right->label);

        if (left_priority != right_priority)
            return left_priority - right_priority;
    }
    else if (favourites_sort_mode_active ==
             FAVOURITES_SORT_BY_SYSTEM) {
        if (left->rom_backed != right->rom_backed)
            return left->rom_backed ? -1 : 1;

        int system_compare = 0;

        if (favourites_system_order_active ==
            FAVOURITES_SYSTEM_ORDER_KEEP_CURRENT) {
            int left_order =
                favourites_find_system_order(
                    left->system);

            int right_order =
                favourites_find_system_order(
                    right->system);

            if (left_order != right_order)
                return left_order - right_order;
        }
        else if (favourites_system_order_active ==
                 FAVOURITES_SYSTEM_ORDER_CUSTOM) {
            int left_order =
                favourites_find_custom_system_order(
                    left->system);

            int right_order =
                favourites_find_custom_system_order(
                    right->system);

            if (left_order != right_order)
                return left_order - right_order;
        }

        system_compare =
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

        favourites_update_system_prefix(entry);
    }

    favourites_sort_mode_active =
        favourites_manager_settings.sort_mode;

    favourites_system_order_active =
        favourites_manager_settings.system_order;

    if (favourites_sort_mode_active ==
            FAVOURITES_SORT_BY_SYSTEM &&
        favourites_system_order_active ==
            FAVOURITES_SYSTEM_ORDER_KEEP_CURRENT) {
        favourites_capture_current_system_order(
            collection);
    }

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

static int favourites_get_systems(
    char systems[FAVOURITES_MAX_SYSTEMS][STR_MAX])
{
    FavouritesCollection collection;
    FavouritesResult result;

    if (!favourites_read(
            &collection,
            &result)) {
        return -1;
    }

    int system_count = 0;

    for (int i = 0;
         i < collection.count &&
         system_count < FAVOURITES_MAX_SYSTEMS;
         i++) {
        FavouritesEntry *entry =
            &collection.entries[i];

        if (!entry->rom_backed ||
            entry->system[0] == '\0') {
            continue;
        }

        bool exists_in_list = false;

        for (int j = 0;
             j < system_count;
             j++) {
            if (strcasecmp(
                    systems[j],
                    entry->system) == 0) {
                exists_in_list = true;
                break;
            }
        }

        if (exists_in_list)
            continue;

        strncpy(
            systems[system_count],
            entry->system,
            STR_MAX - 1);

        systems[system_count][STR_MAX - 1] =
            '\0';

        system_count++;
    }

    favourites_collection_free(&collection);

    for (int i = 0;
         i < system_count - 1;
         i++) {
        for (int j = i + 1;
             j < system_count;
             j++) {
            int left_order =
                favourites_find_custom_system_order(
                    systems[i]);

            int right_order =
                favourites_find_custom_system_order(
                    systems[j]);

            bool swap =
                left_order > right_order ||
                (left_order == right_order &&
                 strcasecmp(
                     systems[i],
                     systems[j]) > 0);

            if (!swap)
                continue;

            char temporary[STR_MAX];

            strcpy(temporary, systems[i]);
            strcpy(systems[i], systems[j]);
            strcpy(systems[j], temporary);
        }
    }

    return system_count;
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

static int favourites_manager_processStartup(void)
{
    favourites_manager_ensureSettingsLoaded();

    if (!favourites_manager_settings.run_on_startup)
        return 0;

    FavouritesResult result;

    if (!favourites_apply(&result))
        return 1;

    return result.malformed > 0 ? 1 : 0;
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

static void action_favouritesManagerRestoreBackup(void *pointer)
{
    (void)pointer;

    if (!exists(FAVOURITES_BACKUP_PATH)) {
        __showInfoDialog(
            "Restore last backup",
            "No favorites backup is available.");
        return;
    }

    char *backup_contents =
        file_read(FAVOURITES_BACKUP_PATH);

    if (backup_contents == NULL) {
        __showInfoDialog(
            "Restore last backup",
            "Could not read the favorites backup.");
        return;
    }

    FILE *file = fopen(FAVOURITES_TEMP_PATH, "w");

    if (file == NULL) {
        free(backup_contents);
        __showInfoDialog(
            "Restore last backup",
            "Could not prepare the restored file.");
        return;
    }

    size_t content_length = strlen(backup_contents);

    bool success =
        fwrite(
            backup_contents,
            1,
            content_length,
            file) == content_length &&
        fflush(file) == 0 &&
        fsync(fileno(file)) == 0;

    free(backup_contents);

    if (fclose(file) != 0)
        success = false;

    if (!success ||
        rename(
            FAVOURITES_TEMP_PATH,
            FAVORITES_PATH) != 0) {
        remove(FAVOURITES_TEMP_PATH);

        __showInfoDialog(
            "Restore last backup",
            "Could not restore the favorites backup.");
        return;
    }

    sync();

    __showInfoDialog(
        "Restore last backup",
        "The previous favorites list was restored.");
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
