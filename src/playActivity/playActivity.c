#include "./playActivity.h"
#include "./playActivityIdentity.h"
#include "./playActivitySchema.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

void printUsage()
{
    printf("Usage: playActivity list             -> List all play activities\n"
           "       playActivity start [rom_path] -> Launch the counter for this rom\n"
           "       playActivity resume           -> Resume the last rom as a new play activity\n"
           "       playActivity stop [rom_path]  -> Stop the counter for this rom\n"
           "       playActivity stop_all         -> Stop the counter for all roms\n"
           "       playActivity migrate          -> Migrate the old database (prior to Onion 4.2.0) to SQLite\n"
           "       playActivity fix_paths        -> Change all absolute paths to relative paths\n");
}

static bool ensure_identity_schema(void)
{
    play_activity_db_open();

    if (play_activity_db == NULL)
        return false;

    bool schema_ready =
        play_activity_identity_schema_ensure(play_activity_db);

    play_activity_db_close();

    return schema_ready;
}

static bool calculate_content_identity(
    const char *rom_path,
    RomContentIdentity *identity,
    int64_t *modified_time_out
)
{
    RomIdentityContext context;

    if (!rom_identity_context_build(rom_path, &context))
        return false;

    if (context.kind == ROM_IDENTITY_KIND_ARCADE ||
        context.kind == ROM_IDENTITY_KIND_UNSUPPORTED) {
        return false;
    }

    bool calculated = false;

    switch (context.kind) {
    case ROM_IDENTITY_KIND_RAW:
        calculated =
            rom_identity_calculate_raw(rom_path, identity);
        break;

    case ROM_IDENTITY_KIND_ZIP:
        calculated =
            rom_identity_calculate_zip(rom_path, identity);
        break;

    case ROM_IDENTITY_KIND_M3U:
        calculated =
            rom_identity_calculate_m3u(rom_path, identity);
        break;

    case ROM_IDENTITY_KIND_ARCADE:
    case ROM_IDENTITY_KIND_UNSUPPORTED:
    default:
        break;
    }

    if (!calculated) {
        fprintf(
            stderr,
            "Warning: unable to fingerprint %s rom: %s\n",
            rom_identity_kind_name(context.kind),
            rom_path
        );
        return false;
    }

    struct stat file_status;

    if (stat(rom_path, &file_status) != 0) {
        fprintf(
            stderr,
            "Warning: unable to read rom metadata: %s\n",
            rom_path
        );
        return false;
    }

    *modified_time_out = (int64_t)file_status.st_mtime;

    return true;
}

static void update_rom_for_path(
    int rom_id,
    const char *rom_path
)
{
    CacheDBItem *cache_item = cache_db_find(rom_path);

    if (cache_item != NULL) {
        __db_update_rom_from_cache(rom_id, cache_item);
        free(cache_item);
        return;
    }

    char *rom_name =
        file_removeExtension(file_basename(rom_path));

    __db_update_rom(
        rom_id,
        "",
        rom_name,
        rom_path,
        ""
    );

    free(rom_name);
}

static int create_rom_for_path(const char *rom_path)
{
    CacheDBItem *cache_item = cache_db_find(rom_path);

    if (cache_item != NULL) {
        int rom_id = __db_insert_rom_from_cache(cache_item);
        free(cache_item);
        return rom_id;
    }

    char *rom_name =
        file_removeExtension(file_basename(rom_path));

    int rom_id = __db_insert_rom(
        "",
        rom_name,
        rom_path,
        ""
    );

    free(rom_name);

    return rom_id;
}

static bool get_stored_rom_path(
    int rom_id,
    char *file_path_out,
    size_t file_path_out_size
)
{
    sqlite3_stmt *statement = play_activity_db_prepare(
        "SELECT file_path FROM rom WHERE id = ?1 LIMIT 1;"
    );

    if (statement == NULL)
        return false;

    sqlite3_bind_int(statement, 1, rom_id);

    bool found = false;

    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *stored_path =
            sqlite3_column_text(statement, 0);

        if (stored_path != NULL) {
            snprintf(
                file_path_out,
                file_path_out_size,
                "%s",
                (const char *)stored_path
            );

            found = true;
        }
    }

    sqlite3_finalize(statement);

    return found;
}

static int resolve_rom_for_start(const char *rom_path)
{
    RomContentIdentity identity;
    RomIdentityContext context;
    int64_t modified_time = 0;
    bool has_identity = false;
    bool identity_reused = false;

    rom_identity_context_build(rom_path, &context);

    play_activity_db_open();

    if (play_activity_db == NULL)
        return ROM_NOT_FOUND;

    int rom_id = __db_get_rom_id_by_path(rom_path);

    if (rom_id != ROM_NOT_FOUND &&
        context.kind == ROM_IDENTITY_KIND_RAW) {
        struct stat file_status;

        if (stat(rom_path, &file_status) == 0) {
            modified_time = (int64_t)file_status.st_mtime;

            identity_reused =
                play_activity_identity_load_if_unchanged(
                    play_activity_db,
                    rom_id,
                    (uint64_t)file_status.st_size,
                    modified_time,
                    &identity
                );

            has_identity = identity_reused;
        }
    }

    if (!has_identity) {
        has_identity = calculate_content_identity(
            rom_path,
            &identity,
            &modified_time
        );
    }

    int identity_rom_id = ROM_NOT_FOUND;
    bool identity_stored = identity_reused;

    if (has_identity) {
        identity_rom_id = play_activity_identity_find_rom_id(
            play_activity_db,
            &identity
        );
    }

    if (rom_id != ROM_NOT_FOUND) {
        update_rom_for_path(rom_id, rom_path);

        if (identity_rom_id != ROM_NOT_FOUND &&
            identity_rom_id != rom_id) {
            char redundant_file_path[PATH_MAX] = "";
            char current_file_path[PATH_MAX] = "";

            get_stored_rom_path(
                identity_rom_id,
                redundant_file_path,
                sizeof(redundant_file_path)
            );

            __ensure_rel_path(current_file_path, rom_path);

            identity_stored = play_activity_identity_merge_roms(
                play_activity_db,
                rom_id,
                identity_rom_id,
                &identity,
                modified_time,
                redundant_file_path,
                current_file_path
            );

            if (!identity_stored) {
                fprintf(
                    stderr,
                    "Warning: unable to merge duplicate activity rows: "
                    "%d and %d\n",
                    rom_id,
                    identity_rom_id
                );
            }
        }
    }
    else if (identity_rom_id != ROM_NOT_FOUND) {
        char old_file_path[PATH_MAX] = "";
        char new_file_path[PATH_MAX] = "";

        rom_id = identity_rom_id;

        get_stored_rom_path(
            rom_id,
            old_file_path,
            sizeof(old_file_path)
        );

        __ensure_rel_path(new_file_path, rom_path);
        update_rom_for_path(rom_id, rom_path);

        play_activity_identity_record_path_change(
            play_activity_db,
            rom_id,
            old_file_path,
            new_file_path
        );
    }

    if (rom_id == ROM_NOT_FOUND)
        rom_id = create_rom_for_path(rom_path);

    if (rom_id != ROM_NOT_FOUND &&
        has_identity &&
        !identity_stored) {
        if (!play_activity_identity_store(
                play_activity_db,
                rom_id,
                &identity,
                modified_time)) {
            fprintf(
                stderr,
                "Warning: unable to store rom identity: %s\n",
                rom_path
            );
        }
    }

    play_activity_db_close();

    return rom_id;
}

static void play_activity_start_with_identity(
    char *rom_file_path
)
{
    printf_debug(
        "\n:: play_activity_start_with_identity(%s)\n",
        rom_file_path
    );

    int rom_id = resolve_rom_for_start(rom_file_path);

    if (rom_id == ROM_NOT_FOUND)
        exit(EXIT_FAILURE);

    char *sql = sqlite3_mprintf(
        "INSERT INTO play_activity(rom_id) VALUES(%d);",
        rom_id
    );

    play_activity_db_execute(sql);
    sqlite3_free(sql);
}

int main(int argc, char *argv[])
{
    log_setName("play_activity");

    if (argc <= 1) {
        printUsage();
        return EXIT_SUCCESS;
    }

    if (!ensure_identity_schema()) {
        fprintf(stderr, "Error: unable to initialize identity schema\n");
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "start") == 0) {
            if (i + 1 < argc) {
                play_activity_start_with_identity(argv[++i]);
            }
            else {
                printf("Error: Missing rom_path argument\n");
                printUsage();
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "resume") == 0) {
            play_activity_resume();
        }
        else if (strcmp(argv[i], "stop") == 0) {
            if (i + 1 < argc) {
                play_activity_stop(argv[++i]);
            }
            else {
                printf("Error: Missing rom_path argument\n");
                printUsage();
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "stop_all") == 0) {
            play_activity_stop_all();
        }
        else if (strcmp(argv[i], "migrate") == 0) {
            migrateDB();
        }
        else if (strcmp(argv[i], "fix_paths") == 0) {
            play_activity_fix_paths();
        }
        else if (strcmp(argv[i], "list") == 0) {
            play_activity_list_all();
        }
        else {
            printf("Error: Invalid argument '%s'\n", argv[1]);
            printUsage();
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
