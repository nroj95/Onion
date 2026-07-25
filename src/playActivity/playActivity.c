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

static void adopt_raw_identity(
    int rom_id,
    const char *rom_path
)
{
    RomIdentityContext context;

    if (!rom_identity_context_build(rom_path, &context))
        return;

    if (context.kind != ROM_IDENTITY_KIND_RAW)
        return;

    RomContentIdentity identity;

    if (!rom_identity_calculate_raw(rom_path, &identity)) {
        fprintf(
            stderr,
            "Warning: unable to fingerprint raw rom: %s\n",
            rom_path
        );
        return;
    }

    struct stat file_status;

    if (stat(rom_path, &file_status) != 0) {
        fprintf(
            stderr,
            "Warning: unable to read raw rom metadata: %s\n",
            rom_path
        );
        return;
    }

    play_activity_db_open();

    if (play_activity_db == NULL)
        return;

    bool stored = play_activity_identity_store(
        play_activity_db,
        rom_id,
        &identity,
        (int64_t)file_status.st_mtime
    );

    play_activity_db_close();

    if (!stored) {
        fprintf(
            stderr,
            "Warning: unable to store raw rom identity: %s\n",
            rom_path
        );
    }
}

static void play_activity_start_with_identity(
    char *rom_file_path
)
{
    printf_debug(
        "\n:: play_activity_start_with_identity(%s)\n",
        rom_file_path
    );

    int rom_id =
        play_activity_transaction_rom_find_by_file_path(
            rom_file_path,
            true
        );

    if (rom_id == ROM_NOT_FOUND)
        exit(EXIT_FAILURE);

    adopt_raw_identity(rom_id, rom_file_path);

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
