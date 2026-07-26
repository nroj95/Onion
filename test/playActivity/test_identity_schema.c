#include "playActivitySchema.h"
#include "test_support.h"

#include <sqlite3/sqlite3.h>
#include <stdio.h>
#include <string.h>

/* =============================================================================
 * purpose:
 * verify activity identity schema and cache behavior.
 *
 * key behavior:
 * - confirms schema creation and versioning.
 * - confirms stored identities load correctly.
 * - confirms metadata and source caches invalidate correctly.
 * - uses an in-memory sqlite database.
 * =============================================================================
 */

static bool sqlite_table_exists(
    sqlite3 *database,
    const char *table_name
)
{
    const char *sql =
        "SELECT 1 "
        "FROM sqlite_master "
        "WHERE type = 'table' "
        "  AND name = ?1 "
        "LIMIT 1;";

    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(
        statement,
        1,
        table_name,
        -1,
        SQLITE_TRANSIENT
    );

    bool exists = sqlite3_step(statement) == SQLITE_ROW;

    sqlite3_finalize(statement);

    return exists;
}

static bool sqlite_schema_version_matches(
    sqlite3 *database,
    const char *expected_version
)
{
    const char *sql =
        "SELECT value "
        "FROM identity_metadata "
        "WHERE key = 'schema_version' "
        "LIMIT 1;";

    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return false;
    }

    bool matches = false;

    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *value =
            sqlite3_column_text(statement, 0);

        matches =
            value != NULL &&
            strcmp(
                (const char *)value,
                expected_version
            ) == 0;
    }

    sqlite3_finalize(statement);

    return matches;
}

static int sqlite_scalar_int(
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

void test_identity_schema_storage(void)
{
    sqlite3 *database = NULL;

    bool database_opened =
        sqlite3_open(":memory:", &database) == SQLITE_OK;

    check_condition(
        database_opened,
        "open in-memory identity database"
    );

    if (!database_opened) {
        if (database != NULL)
            sqlite3_close(database);

        return;
    }

    const char *legacy_schema =
        "CREATE TABLE rom("
        "    id INTEGER PRIMARY KEY,"
        "    type TEXT,"
        "    name TEXT,"
        "    file_path TEXT,"
        "    image_path TEXT,"
        "    created_at INTEGER DEFAULT (strftime('%s', 'now')),"
        "    updated_at INTEGER"
        ");"
        "CREATE UNIQUE INDEX rom_id_index ON rom(id);"
        "CREATE TABLE play_activity("
        "    rom_id INTEGER,"
        "    play_time INTEGER,"
        "    created_at INTEGER DEFAULT (strftime('%s', 'now')),"
        "    updated_at INTEGER"
        ");"
        "CREATE INDEX play_activity_rom_id_index "
        "ON play_activity(rom_id);"
        "INSERT INTO rom("
        "    id, type, name, file_path, image_path"
        ") VALUES("
        "    1,"
        "    'GBA',"
        "    'fixture game',"
        "    'GBA/fixture game.zip',"
        "    'Imgs/GBA/fixture game.png'"
        ");"
        "INSERT INTO play_activity(rom_id, play_time) "
        "VALUES(1, 120), (1, 45);";

    check_condition(
        sqlite3_exec(
            database,
            legacy_schema,
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK,
        "create legacy activity schema fixture"
    );

    check_condition(
        play_activity_identity_schema_ensure(database),
        "upgrade legacy activity schema"
    );

    check_condition(
        play_activity_identity_schema_ensure(database),
        "identity schema creation is idempotent"
    );

    bool tables_exist =
        sqlite_table_exists(database, "identity_metadata") &&
        sqlite_table_exists(database, "rom_identity") &&
        sqlite_table_exists(database, "rom_identity_source") &&
        sqlite_table_exists(database, "rom_path_history") &&
        sqlite_table_exists(database, "rom_asset_migration");

    check_condition(
        tables_exist,
        "identity schema creates required tables"
    );

    check_condition(
        sqlite_schema_version_matches(database, "4"),
        "identity schema stores version 4"
    );

    check_condition(
        sqlite_scalar_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom "
            "WHERE id = 1 "
            "  AND type = 'GBA' "
            "  AND name = 'fixture game' "
            "  AND file_path = 'GBA/fixture game.zip' "
            "  AND image_path = 'Imgs/GBA/fixture game.png';"
        ) == 1,
        "schema upgrade preserves rom row"
    );

    check_condition(
        sqlite_scalar_int(
            database,
            "SELECT COUNT(*) "
            "FROM play_activity "
            "WHERE rom_id = 1 "
            "  AND play_time IN (120, 45);"
        ) == 2 &&
        sqlite_scalar_int(
            database,
            "SELECT SUM(play_time) "
            "FROM play_activity "
            "WHERE rom_id = 1;"
        ) == 165,
        "schema upgrade preserves activity history"
    );

    check_condition(
        sqlite3_exec(
            database,
            "INSERT INTO rom("
            "    id, type, name, file_path, image_path"
            ") VALUES("
            "    101,"
            "    'GB',"
            "    'atomic rename fixture',"
            "    'GB/atomic-old.gb',"
            "    ''"
            ");",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK,
        "create atomic path-change fixture"
    );

    check_condition(
        sqlite3_exec(
            database,
            "CREATE TRIGGER fail_asset_migration "
            "BEFORE INSERT ON rom_asset_migration "
            "BEGIN "
            "    SELECT RAISE(ABORT, 'forced migration failure');"
            "END;",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK,
        "create forced migration failure"
    );

    check_condition(
        !play_activity_identity_move_rom_path(
            database,
            101,
            "GB/atomic-old.gb",
            "GB/atomic-new.gb"
        ),
        "path move fails when migration bookkeeping fails"
    );

    check_condition(
        sqlite_scalar_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom "
            "WHERE id = 101 "
            "  AND file_path = 'GB/atomic-old.gb';"
        ) == 1 &&
        sqlite_scalar_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_path_history "
            "WHERE rom_id = 101;"
        ) == 0 &&
        sqlite_scalar_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_asset_migration "
            "WHERE rom_id = 101;"
        ) == 0,
        "failed path move rolls back all database changes"
    );

    check_condition(
        sqlite3_exec(
            database,
            "DROP TRIGGER fail_asset_migration;",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK,
        "remove forced migration failure"
    );

    check_condition(
        play_activity_identity_move_rom_path(
            database,
            101,
            "GB/atomic-old.gb",
            "GB/atomic-new.gb"
        ),
        "commit atomic rom path move"
    );

    check_condition(
        sqlite_scalar_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom "
            "WHERE id = 101 "
            "  AND file_path = 'GB/atomic-new.gb';"
        ) == 1 &&
        sqlite_scalar_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_path_history "
            "WHERE rom_id = 101 "
            "  AND old_file_path = 'GB/atomic-old.gb' "
            "  AND new_file_path = 'GB/atomic-new.gb';"
        ) == 1 &&
        sqlite_scalar_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_asset_migration "
            "WHERE rom_id = 101 "
            "  AND old_file_path = 'GB/atomic-old.gb' "
            "  AND new_file_path = 'GB/atomic-new.gb';"
        ) == 1,
        "atomic path move commits complete bookkeeping"
    );

    RomContentIdentity stored_identity;
    memset(&stored_identity, 0, sizeof(stored_identity));

    snprintf(
        stored_identity.type,
        sizeof(stored_identity.type),
        "%s",
        "crc32"
    );

    snprintf(
        stored_identity.value,
        sizeof(stored_identity.value),
        "%s",
        "a87f8344"
    );

    stored_identity.content_size = 262160;

    check_condition(
        play_activity_identity_store(
            database,
            7,
            &stored_identity,
            123456789
        ),
        "store identity row"
    );

    RomContentIdentity loaded_identity;

    check_condition(
        play_activity_identity_load(
            database,
            7,
            &loaded_identity
        ),
        "load stored identity"
    );

    check_condition(
        strcmp(
            loaded_identity.type,
            stored_identity.type
        ) == 0 &&
        strcmp(
            loaded_identity.value,
            stored_identity.value
        ) == 0 &&
        loaded_identity.content_size ==
            stored_identity.content_size,
        "loaded identity matches stored values"
    );

    RomContentIdentity first_collision_identity;
    RomContentIdentity second_collision_identity;

    memset(
        &first_collision_identity,
        0,
        sizeof(first_collision_identity)
    );
    memset(
        &second_collision_identity,
        0,
        sizeof(second_collision_identity)
    );

    snprintf(
        first_collision_identity.type,
        sizeof(first_collision_identity.type),
        "%s",
        "crc32"
    );
    snprintf(
        first_collision_identity.value,
        sizeof(first_collision_identity.value),
        "%s",
        "deadbeef"
    );
    first_collision_identity.content_size = 100;

    snprintf(
        second_collision_identity.type,
        sizeof(second_collision_identity.type),
        "%s",
        "crc32"
    );
    snprintf(
        second_collision_identity.value,
        sizeof(second_collision_identity.value),
        "%s",
        "deadbeef"
    );
    second_collision_identity.content_size = 200;

    check_condition(
        play_activity_identity_store(
            database,
            90,
            &first_collision_identity,
            1000
        ) &&
        play_activity_identity_store(
            database,
            91,
            &second_collision_identity,
            1000
        ),
        "store same hash with different content sizes"
    );

    check_condition(
        play_activity_identity_find_rom_id(
            database,
            &first_collision_identity
        ) == 90 &&
        play_activity_identity_find_rom_id(
            database,
            &second_collision_identity
        ) == 91,
        "content size distinguishes matching hashes"
    );

    RomContentIdentity unchanged_identity;

    check_condition(
        play_activity_identity_load_if_unchanged(
            database,
            7,
            stored_identity.content_size,
            123456789,
            &unchanged_identity
        ),
        "load unchanged identity from metadata cache"
    );

    check_condition(
        strcmp(
            unchanged_identity.value,
            stored_identity.value
        ) == 0 &&
        unchanged_identity.content_size ==
            stored_identity.content_size,
        "unchanged identity cache returns stored values"
    );

    check_condition(
        !play_activity_identity_load_if_unchanged(
            database,
            7,
            stored_identity.content_size + 1,
            123456789,
            &unchanged_identity
        ),
        "changed content size invalidates identity cache"
    );

    check_condition(
        !play_activity_identity_load_if_unchanged(
            database,
            7,
            stored_identity.content_size,
            123456790,
            &unchanged_identity
        ),
        "changed modified time invalidates identity cache"
    );

    check_condition(
        play_activity_identity_source_store(
            database,
            7,
            "cue-stat-v1",
            "first-signature"
        ),
        "store identity source signature"
    );

    check_condition(
        play_activity_identity_source_matches(
            database,
            7,
            "cue-stat-v1",
            "first-signature"
        ),
        "stored source signature matches"
    );

    check_condition(
        !play_activity_identity_source_matches(
            database,
            7,
            "cue-stat-v1",
            "different-signature"
        ),
        "different source signature does not match"
    );

    check_condition(
        play_activity_identity_source_store(
            database,
            7,
            "cue-stat-v1",
            "replacement-signature"
        ),
        "replace identity source signature"
    );

    check_condition(
        !play_activity_identity_source_matches(
            database,
            7,
            "cue-stat-v1",
            "first-signature"
        ),
        "replaced source signature stops matching"
    );

    check_condition(
        play_activity_identity_source_matches(
            database,
            7,
            "cue-stat-v1",
            "replacement-signature"
        ),
        "replacement source signature matches"
    );

    play_activity_identity_source_delete(database, 7);

    check_condition(
        !play_activity_identity_source_matches(
            database,
            7,
            "cue-stat-v1",
            "replacement-signature"
        ),
        "deleted source signature stops matching"
    );

    check_condition(
        play_activity_asset_migration_store(
            database,
            7,
            "/Roms/FC/original-name.zip",
            "/Roms/FC/renamed.zip"
        ),
        "store pending asset migration"
    );

    char old_migration_path[512] = "";
    char new_migration_path[512] = "";

    check_condition(
        play_activity_asset_migration_load(
            database,
            7,
            old_migration_path,
            sizeof(old_migration_path),
            new_migration_path,
            sizeof(new_migration_path)
        ),
        "load pending asset migration"
    );

    check_condition(
        strcmp(
            old_migration_path,
            "/Roms/FC/original-name.zip"
        ) == 0 &&
        strcmp(
            new_migration_path,
            "/Roms/FC/renamed.zip"
        ) == 0,
        "pending migration stores path transition"
    );

    check_condition(
        play_activity_asset_migration_store(
            database,
            7,
            "/Roms/FC/renamed.zip",
            "/Roms/FC/final-name.zip"
        ),
        "advance pending asset migration"
    );

    check_condition(
        play_activity_asset_migration_load(
            database,
            7,
            old_migration_path,
            sizeof(old_migration_path),
            new_migration_path,
            sizeof(new_migration_path
            )
        ) &&
        strcmp(
            old_migration_path,
            "/Roms/FC/original-name.zip"
        ) == 0 &&
        strcmp(
            new_migration_path,
            "/Roms/FC/final-name.zip"
        ) == 0,
        "repeated rename keeps original source"
    );

    check_condition(
        play_activity_asset_migration_delete(
            database,
            7
        ),
        "delete pending asset migration"
    );

    check_condition(
        play_activity_asset_migration_store(
            database,
            8,
            "/Roms/FC/folder/game.zip",
            "/Roms/FC/other/game.zip"
        ),
        "accept folder-only path change"
    );

    check_condition(
        !play_activity_asset_migration_load(
            database,
            8,
            old_migration_path,
            sizeof(old_migration_path),
            new_migration_path,
            sizeof(new_migration_path)
        ),
        "folder-only move creates no migration"
    );



    sqlite3_close(database);
}
