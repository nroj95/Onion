#ifndef TWEAKS_FAVOURITES_MANAGER_H__
#define TWEAKS_FAVOURITES_MANAGER_H__

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "components/JsonGameEntry.h"
#include "components/list.h"
#include "utils/file.h"
#include "utils/json.h"

#include "./appstate.h"
#include "./info_dialog.h"

#define FAVOURITES_MANAGER_CONFIG_DIR \
    "/mnt/SDCARD/Saves/CurrentProfile/config/favourites-manager"

#define FAVOURITES_MANAGER_CONFIG_PATH \
    FAVOURITES_MANAGER_CONFIG_DIR "/config.json"

#define FAVOURITES_MANAGER_CONFIG_TEMP_PATH \
    FAVOURITES_MANAGER_CONFIG_DIR "/config.json.tmp"

typedef enum favourites_sort_mode_e {
    FAVOURITES_SORT_ALPHABETICAL = 0,
    FAVOURITES_SORT_BY_SYSTEM = 1,
} FavouritesSortMode;

typedef enum favourites_inner_sort_e {
    FAVOURITES_INNER_SORT_ALPHABETICAL = 0,
} FavouritesInnerSort;

typedef enum favourites_system_order_e {
    FAVOURITES_SYSTEM_ORDER_KEEP_CURRENT = 0,
    FAVOURITES_SYSTEM_ORDER_ALPHABETICAL = 1,
    FAVOURITES_SYSTEM_ORDER_CUSTOM = 2,
} FavouritesSystemOrder;

#define FAVOURITES_MAX_SYSTEMS 256

static char favourites_custom_system_order
    [FAVOURITES_MAX_SYSTEMS][STR_MAX];

static int favourites_custom_system_count = 0;

typedef struct favourites_manager_settings_s {
    int sort_mode;
    int inner_sort;
    int system_order;
    bool remove_duplicates;
    bool remove_missing;
    bool repair_box_art;
    bool run_on_startup;
} FavouritesManagerSettings;

static FavouritesManagerSettings favourites_manager_settings;
static bool favourites_manager_settings_loaded = false;

static void favourites_manager_setDefaults(void)
{
    favourites_manager_settings = (FavouritesManagerSettings){
        .sort_mode = FAVOURITES_SORT_ALPHABETICAL,
        .inner_sort = FAVOURITES_INNER_SORT_ALPHABETICAL,
        .system_order = FAVOURITES_SYSTEM_ORDER_ALPHABETICAL,
        .remove_duplicates = true,
        .remove_missing = false,
        .repair_box_art = true,
        .run_on_startup = false,
    };
}

static bool favourites_manager_saveSettings(void)
{
    mkdirs(FAVOURITES_MANAGER_CONFIG_DIR);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
        return false;

    cJSON_AddNumberToObject(
        root, "sort_mode", favourites_manager_settings.sort_mode);
    cJSON_AddNumberToObject(
        root, "inner_sort", favourites_manager_settings.inner_sort);
    cJSON_AddNumberToObject(
        root, "system_order_mode", favourites_manager_settings.system_order);
    cJSON_AddBoolToObject(
        root, "remove_duplicates",
        favourites_manager_settings.remove_duplicates);
    cJSON_AddBoolToObject(
        root, "remove_missing",
        favourites_manager_settings.remove_missing);
    cJSON_AddBoolToObject(
        root, "repair_box_art",
        favourites_manager_settings.repair_box_art);
    cJSON_AddBoolToObject(
        root, "run_on_startup",
        favourites_manager_settings.run_on_startup);

    cJSON *custom_system_order =
        cJSON_AddArrayToObject(
            root,
            "custom_system_order");

    if (custom_system_order == NULL) {
        cJSON_Delete(root);
        return false;
    }

    for (int i = 0;
         i < favourites_custom_system_count;
         i++) {
        cJSON *system =
            cJSON_CreateString(
                favourites_custom_system_order[i]);

        if (system == NULL) {
            cJSON_Delete(root);
            return false;
        }

        cJSON_AddItemToArray(
            custom_system_order,
            system);
    }

    char *output = cJSON_Print(root);
    cJSON_Delete(root);

    if (output == NULL)
        return false;

    FILE *file = fopen(FAVOURITES_MANAGER_CONFIG_TEMP_PATH, "w");
    if (file == NULL) {
        cJSON_free(output);
        return false;
    }

    size_t output_length = strlen(output);
    bool success =
        fwrite(output, 1, output_length, file) == output_length &&
        fputc('\n', file) != EOF &&
        fflush(file) == 0 &&
        fsync(fileno(file)) == 0;

    if (fclose(file) != 0)
        success = false;

    cJSON_free(output);

    if (!success) {
        remove(FAVOURITES_MANAGER_CONFIG_TEMP_PATH);
        return false;
    }

    if (rename(
            FAVOURITES_MANAGER_CONFIG_TEMP_PATH,
            FAVOURITES_MANAGER_CONFIG_PATH) != 0) {
        remove(FAVOURITES_MANAGER_CONFIG_TEMP_PATH);
        return false;
    }

    return true;
}

static void favourites_manager_loadSettings(void)
{
    favourites_manager_setDefaults();

    if (!exists(FAVOURITES_MANAGER_CONFIG_PATH)) {
        favourites_manager_settings_loaded = true;
        return;
    }

    cJSON *root = json_load(FAVOURITES_MANAGER_CONFIG_PATH);
    if (root == NULL) {
        favourites_manager_settings_loaded = true;
        return;
    }

    json_getInt(
        root, "sort_mode",
        &favourites_manager_settings.sort_mode);
    json_getInt(
        root, "inner_sort",
        &favourites_manager_settings.inner_sort);
    bool migrate_system_order = false;

    cJSON *system_order_mode =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "system_order_mode");

    if (cJSON_IsNumber(system_order_mode)) {
        favourites_manager_settings.system_order =
            system_order_mode->valueint;
    }
    else {
        cJSON *legacy_system_order =
            cJSON_GetObjectItemCaseSensitive(
                root,
                "system_order");

        if (cJSON_IsNumber(legacy_system_order)) {
            favourites_manager_settings.system_order =
                legacy_system_order->valueint;
            migrate_system_order = true;
        }
    }

    favourites_custom_system_count = 0;

    cJSON *custom_system_order =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "custom_system_order");

    if (cJSON_IsArray(custom_system_order)) {
        cJSON *system = NULL;

        cJSON_ArrayForEach(
            system,
            custom_system_order) {
            if (!cJSON_IsString(system) ||
                system->valuestring == NULL ||
                system->valuestring[0] == '\0' ||
                favourites_custom_system_count >=
                    FAVOURITES_MAX_SYSTEMS) {
                continue;
            }

            strncpy(
                favourites_custom_system_order[
                    favourites_custom_system_count],
                system->valuestring,
                STR_MAX - 1);

            favourites_custom_system_order[
                favourites_custom_system_count]
                [STR_MAX - 1] = '\0';

            favourites_custom_system_count++;
        }
    }

    json_getBool(
        root, "remove_duplicates",
        &favourites_manager_settings.remove_duplicates);
    json_getBool(
        root, "remove_missing",
        &favourites_manager_settings.remove_missing);
    json_getBool(
        root, "repair_box_art",
        &favourites_manager_settings.repair_box_art);
    json_getBool(
        root, "run_on_startup",
        &favourites_manager_settings.run_on_startup);

    cJSON_Delete(root);

    // Reject unsupported values instead of exposing broken menu indexes.
    if (favourites_manager_settings.sort_mode <
            FAVOURITES_SORT_ALPHABETICAL ||
        favourites_manager_settings.sort_mode >
            FAVOURITES_SORT_BY_SYSTEM) {
        favourites_manager_settings.sort_mode =
            FAVOURITES_SORT_ALPHABETICAL;
    }

    favourites_manager_settings.inner_sort =
        FAVOURITES_INNER_SORT_ALPHABETICAL;

    if (favourites_manager_settings.system_order <
            FAVOURITES_SYSTEM_ORDER_KEEP_CURRENT ||
        favourites_manager_settings.system_order >
            FAVOURITES_SYSTEM_ORDER_CUSTOM) {
        favourites_manager_settings.system_order =
            FAVOURITES_SYSTEM_ORDER_ALPHABETICAL;
    }

    favourites_manager_settings_loaded = true;

    if (migrate_system_order)
        favourites_manager_saveSettings();
}

static void favourites_manager_ensureSettingsLoaded(void)
{
    if (!favourites_manager_settings_loaded)
        favourites_manager_loadSettings();
}

static void action_favouritesManagerSortMode(void *pointer)
{
    favourites_manager_settings.sort_mode =
        ((ListItem *)pointer)->value;
    favourites_manager_saveSettings();
}

static void action_favouritesManagerSystemOrder(void *pointer)
{
    favourites_manager_settings.system_order =
        ((ListItem *)pointer)->value;
    favourites_manager_saveSettings();
}

static void action_favouritesManagerRemoveDuplicates(void *pointer)
{
    favourites_manager_settings.remove_duplicates =
        ((ListItem *)pointer)->value == 1;
    favourites_manager_saveSettings();
}

static void action_favouritesManagerRemoveMissing(void *pointer)
{
    favourites_manager_settings.remove_missing =
        ((ListItem *)pointer)->value == 1;
    favourites_manager_saveSettings();
}

static void action_favouritesManagerRepairBoxArt(void *pointer)
{
    favourites_manager_settings.repair_box_art =
        ((ListItem *)pointer)->value == 1;
    favourites_manager_saveSettings();
}

static void action_favouritesManagerRunOnStartup(void *pointer)
{
    favourites_manager_settings.run_on_startup =
        ((ListItem *)pointer)->value == 1;
    favourites_manager_saveSettings();
}

#include "./favourites_manager_engine.h"

static void action_favouritesManagerReset(void *pointer)
{
    (void)pointer;

    favourites_manager_setDefaults();
    favourites_manager_saveSettings();

    // Recreate the menu so every visible value reflects the defaults.
    reset_menus = true;
    all_changed = true;
}

#endif // TWEAKS_FAVOURITES_MANAGER_H__
