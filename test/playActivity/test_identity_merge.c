#include "playActivitySchema.h"
#include "test_support.h"

#include <sqlite3/sqlite3.h>
#include <stdio.h>
#include <string.h>

/* =============================================================================
 * purpose:
 * verify explicit activity transfer behavior.
 *
 * key behavior:
 * - preserves activity rows and total play time.
 * - preserves and records path history.
 * - removes stale identity cache rows.
 * - stores the final identity on the surviving rom.
 * - does not create automatic asset migration work.
 * =============================================================================
 */

static bool sqlite_execute(
    sqlite3 *database,
    const char *sql
)
{
    char *error_message = NULL;

    int result = sqlite3_exec(
        database,
        sql,
        NULL,
        NULL,
        &error_message
    );

    if (result != SQLITE_OK) {
        fprintf(
            stderr,
            "sqlite fixture error: %s\n",
            error_message != NULL
                ? error_message
                : sqlite3_errmsg(database)
        );

        sqlite3_free(error_message);
        return false;
    }

    return true;
}

static int sqlite_query_int(
    sqlite3 *database,
    const char *sql
)
{
    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return -1;
    }

    int value = -1;

    if (sqlite3_step(statement) == SQLITE_ROW)
        value = sqlite3_column_int(statement, 0);

    sqlite3_finalize(statement);

    return value;
}

void test_identity_explicit_transfer(void)
{
    sqlite3 *database = NULL;

    bool database_opened =
        sqlite3_open(":memory:", &database) == SQLITE_OK;

    check_condition(
        database_opened,
        "open in-memory transfer database"
    );

    if (!database_opened) {
        if (database != NULL)
            sqlite3_close(database);

        return;
    }

    bool base_schema_created =
        sqlite_execute(
            database,
            "CREATE TABLE rom("
            "    id INTEGER PRIMARY KEY,"
            "    file_path TEXT NOT NULL"
            ");"

            "CREATE TABLE play_activity("
            "    id INTEGER PRIMARY KEY,"
            "    rom_id INTEGER NOT NULL,"
            "    play_time INTEGER NOT NULL"
            ");"
        );

    check_condition(
        base_schema_created,
        "create transfer fixture base tables"
    );

    check_condition(
        play_activity_identity_schema_ensure(database),
        "create identity schema for transfer"
    );

    bool fixture_rows_created =
        sqlite_execute(
            database,
            "INSERT INTO rom(id, file_path) VALUES"
            "    (10, '/Roms/FC/current.zip'),"
            "    (20, '/Roms/FC/old-name.zip');"

            "INSERT INTO play_activity(id, rom_id, play_time) VALUES"
            "    (1, 10, 120),"
            "    (2, 20, 240);"

            "INSERT INTO rom_path_history("
            "    rom_id,"
            "    old_file_path,"
            "    new_file_path"
            ") VALUES("
            "    20,"
            "    '/Roms/FC/older-name.zip',"
            "    '/Roms/FC/old-name.zip'"
            ");"
        );

    check_condition(
        fixture_rows_created,
        "create explicit transfer fixtures"
    );

    RomContentIdentity survivor_identity;
    memset(
        &survivor_identity,
        0,
        sizeof(survivor_identity)
    );

    snprintf(
        survivor_identity.type,
        sizeof(survivor_identity.type),
        "%s",
        "crc32"
    );

    snprintf(
        survivor_identity.value,
        sizeof(survivor_identity.value),
        "%s",
        "11111111"
    );

    survivor_identity.content_size = 100;

    RomContentIdentity redundant_identity;
    memset(
        &redundant_identity,
        0,
        sizeof(redundant_identity)
    );

    snprintf(
        redundant_identity.type,
        sizeof(redundant_identity.type),
        "%s",
        "crc32"
    );

    snprintf(
        redundant_identity.value,
        sizeof(redundant_identity.value),
        "%s",
        "22222222"
    );

    redundant_identity.content_size = 200;

    bool stale_identity_rows_created =
        play_activity_identity_store(
            database,
            10,
            "FC",
            &survivor_identity,
            1000
        ) &&
        play_activity_identity_store(
            database,
            20,
            "FC",
            &redundant_identity,
            2000
        );

    check_condition(
        stale_identity_rows_created,
        "store pre-merge identity rows"
    );

    bool stale_source_rows_created =
        play_activity_identity_source_store(
            database,
            10,
            "file-stat-v1",
            "survivor-source"
        ) &&
        play_activity_identity_source_store(
            database,
            20,
            "file-stat-v1",
            "redundant-source"
        );

    check_condition(
        stale_source_rows_created,
        "store pre-merge source rows"
    );


    bool stale_migrations_created =
        play_activity_asset_migration_store(
            database,
            10,
            "/Roms/FC/stale-survivor.zip",
            "/Roms/FC/current.zip"
        ) &&
        play_activity_asset_migration_store(
            database,
            20,
            "/Roms/FC/older-name.zip",
            "/Roms/FC/old-name.zip"
        );

    check_condition(
        stale_migrations_created,
        "store pre-merge asset migrations"
    );

    RomContentIdentity final_identity;
    memset(&final_identity, 0, sizeof(final_identity));

    snprintf(
        final_identity.type,
        sizeof(final_identity.type),
        "%s",
        "crc32"
    );

    snprintf(
        final_identity.value,
        sizeof(final_identity.value),
        "%s",
        "a87f8344"
    );

    final_identity.content_size = 262160;

    check_condition(
        play_activity_identity_transfer_roms(
            database,
            10,
            20,
            "FC",
            &final_identity,
            3000,
            "/Roms/FC/old-name.zip",
            "/Roms/FC/current.zip"
        ),
        "transfer source rom data"
    );

    check_condition(
        sqlite_query_int(
            database,
            "SELECT COUNT(*) FROM rom WHERE id = 10;"
        ) == 1 &&
        sqlite_query_int(
            database,
            "SELECT COUNT(*) FROM rom WHERE id = 20;"
        ) == 0,
        "transfer keeps destination and removes source"
    );

    check_condition(
        sqlite_query_int(
            database,
            "SELECT COUNT(*) "
            "FROM play_activity "
            "WHERE rom_id = 10;"
        ) == 2 &&
        sqlite_query_int(
            database,
            "SELECT COUNT(*) "
            "FROM play_activity "
            "WHERE rom_id = 20;"
        ) == 0,
        "transfer preserves both activity rows"
    );

    check_condition(
        sqlite_query_int(
            database,
            "SELECT SUM(play_time) "
            "FROM play_activity "
            "WHERE rom_id = 10;"
        ) == 360,
        "transfer preserves total play time"
    );

    check_condition(
        sqlite_query_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_path_history "
            "WHERE rom_id = 10;"
        ) == 2 &&
        sqlite_query_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_path_history "
            "WHERE rom_id = 20;"
        ) == 0,
        "transfer preserves and records path history"
    );

    check_condition(
        sqlite_query_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_path_history "
            "WHERE rom_id = 10 "
            "  AND old_file_path = '/Roms/FC/old-name.zip' "
            "  AND new_file_path = '/Roms/FC/current.zip';"
        ) == 1,
        "transfer records explicit path transition"
    );

    check_condition(
        sqlite_query_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_identity_source "
            "WHERE rom_id IN (10, 20);"
        ) == 0,
        "transfer removes stale source signatures"
    );


    char migration_old_path[512] = "";
    char migration_new_path[512] = "";

    check_condition(
        !play_activity_asset_migration_load(
            database,
            10,
            migration_old_path,
            sizeof(migration_old_path),
            migration_new_path,
            sizeof(migration_new_path)
        ) &&
        !play_activity_asset_migration_load(
            database,
            20,
            migration_old_path,
            sizeof(migration_old_path),
            migration_new_path,
            sizeof(migration_new_path)
        ),
        "transfer clears stale migration bookkeeping"
    );

    RomContentIdentity loaded_identity;

    check_condition(
        play_activity_identity_load(
            database,
            10,
            &loaded_identity
        ),
        "load transferred destination identity"
    );

    check_condition(
        strcmp(
            loaded_identity.type,
            final_identity.type
        ) == 0 &&
        strcmp(
            loaded_identity.value,
            final_identity.value
        ) == 0 &&
        loaded_identity.content_size ==
            final_identity.content_size,
        "transfer stores final destination identity"
    );

    check_condition(
        sqlite_query_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_identity "
            "WHERE rom_id = 10 "
            "  AND system = 'FC' "
            "  AND identity_type = 'crc32' "
            "  AND identity_value = 'a87f8344' "
            "  AND content_size = 262160;"
        ) == 1,
        "transfer stores system-scoped destination identity"
    );

    check_condition(
        !play_activity_identity_load(
            database,
            20,
            &loaded_identity
        ),
        "duplicate identity row is removed"
    );

    sqlite3_close(database);
}
