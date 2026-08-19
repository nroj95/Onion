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

    case ROM_IDENTITY_KIND_CUE:
        calculated =
            rom_identity_calculate_cue(rom_path, identity);
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

static const char *identity_source_type(
    RomIdentityKind kind
)
{
    switch (kind) {
    case ROM_IDENTITY_KIND_ZIP:
        return "zip-stat-v2";

    case ROM_IDENTITY_KIND_CUE:
        return "cue-stat-v2";

    case ROM_IDENTITY_KIND_M3U:
        return "m3u-stat-v3";

    default:
        return NULL;
    }
}

static bool calculate_identity_source_signature(
    RomIdentityKind kind,
    const char *rom_path,
    char *signature_out,
    size_t signature_out_size
)
{
    switch (kind) {
    case ROM_IDENTITY_KIND_ZIP:
        return rom_identity_calculate_file_source_signature(
            rom_path,
            signature_out,
            signature_out_size
        );

    case ROM_IDENTITY_KIND_CUE:
        return rom_identity_calculate_cue_source_signature(
            rom_path,
            signature_out,
            signature_out_size
        );

    case ROM_IDENTITY_KIND_M3U:
        return rom_identity_calculate_m3u_source_signature(
            rom_path,
            signature_out,
            signature_out_size
        );

    default:
        return false;
    }
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

static int resolve_rom_for_start(const char *rom_path)
{
    RomContentIdentity identity;
    RomIdentityContext context;
    char source_signature[17] = "";
    int64_t modified_time = 0;
    bool has_identity = false;
    bool identity_reused = false;
    bool has_source_signature = false;

    memset(&context, 0, sizeof(context));
    rom_identity_context_build(rom_path, &context);

    const char *source_type =
        identity_source_type(context.kind);

    play_activity_db_open();

    if (play_activity_db == NULL)
        return ROM_NOT_FOUND;

    /*
     * A rom row belongs to one particular library path. Content identity is
     * cached for that row but never decides ownership or path reconciliation.
     */
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

    if (rom_id != ROM_NOT_FOUND &&
        source_type != NULL) {
        has_source_signature =
            calculate_identity_source_signature(
                context.kind,
                rom_path,
                source_signature,
                sizeof(source_signature)
            );

        if (has_source_signature &&
            play_activity_identity_source_matches(
                play_activity_db,
                rom_id,
                source_type,
                source_signature)) {
            identity_reused =
                play_activity_identity_load(
                    play_activity_db,
                    rom_id,
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

    if (source_type != NULL &&
        !has_source_signature) {
        has_source_signature =
            calculate_identity_source_signature(
                context.kind,
                rom_path,
                source_signature,
                sizeof(source_signature)
            );
    }

    if (rom_id != ROM_NOT_FOUND)
        update_rom_for_path(rom_id, rom_path);
    else
        rom_id = create_rom_for_path(rom_path);

    if (rom_id != ROM_NOT_FOUND &&
        has_identity &&
        !identity_reused) {
        if (!play_activity_identity_store(
                play_activity_db,
                rom_id,
                context.system,
                &identity,
                modified_time)) {
            fprintf(
                stderr,
                "Warning: unable to store rom identity: %s\n",
                rom_path
            );
        }
    }

    if (rom_id != ROM_NOT_FOUND) {
        if (source_type != NULL &&
            has_identity &&
            has_source_signature) {
            if (!play_activity_identity_source_store(
                    play_activity_db,
                    rom_id,
                    source_type,
                    source_signature)) {
                fprintf(
                    stderr,
                    "Warning: unable to store rom source signature: %s\n",
                    rom_path
                );
            }
        }
        else {
            play_activity_identity_source_delete(
                play_activity_db,
                rom_id
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
                char *rom_path = argv[++i];

                play_activity_stop(rom_path);
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
