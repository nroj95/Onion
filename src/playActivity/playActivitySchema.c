#include "./playActivitySchema.h"

#include <stdio.h>
#include <string.h>

#define STRINGIFY_VALUE_INNER(value) #value
#define STRINGIFY_VALUE(value) STRINGIFY_VALUE_INNER(value)

/* =============================================================================
 * purpose:
 * create and update the activity tracker identity schema.
 *
 * key behavior:
 * - stores one stable content identity per tracked rom row.
 * - stores file metadata used only to decide when re-fingerprinting is needed.
 * - records confirmed path transitions for debugging and future repair.
 * - keeps all changes inside the existing onion sqlite database.
 * =============================================================================
 */

static bool execute_schema_sql(
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
            "activity identity schema error: %s\n",
            error_message != NULL
                ? error_message
                : sqlite3_errmsg(database)
        );

        sqlite3_free(error_message);
        return false;
    }

    return true;
}

static bool sqlite_table_exists(
    sqlite3 *database,
    const char *table_name
)
{
    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            "SELECT 1 "
            "FROM sqlite_master "
            "WHERE type = 'table' "
            "  AND name = ?1 "
            "LIMIT 1;",
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

static bool sqlite_table_has_column(
    sqlite3 *database,
    const char *table_name,
    const char *column_name
)
{
    char *sql = sqlite3_mprintf(
        "PRAGMA table_info(%Q);",
        table_name
    );

    if (sql == NULL)
        return false;

    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        sqlite3_free(sql);
        return false;
    }

    sqlite3_free(sql);

    bool found = false;

    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *name =
            sqlite3_column_text(statement, 1);

        if (name != NULL &&
            strcmp((const char *)name, column_name) == 0) {
            found = true;
            break;
        }
    }

    sqlite3_finalize(statement);

    return found;
}

bool play_activity_identity_schema_ensure(sqlite3 *database)
{
    if (database == NULL)
        return false;

    if (sqlite3_exec(
            database,
            "BEGIN IMMEDIATE;",
            NULL,
            NULL,
            NULL
        ) != SQLITE_OK) {
        return false;
    }

    const char *support_schema_sql =
        "CREATE TABLE IF NOT EXISTS identity_metadata("
        "    key TEXT PRIMARY KEY,"
        "    value TEXT NOT NULL"
        ");"

        "CREATE TABLE IF NOT EXISTS rom_identity_source("
        "    rom_id INTEGER PRIMARY KEY,"
        "    source_type TEXT NOT NULL,"
        "    source_signature TEXT NOT NULL,"
        "    updated_at INTEGER DEFAULT (strftime('%s', 'now'))"
        ");"

        "CREATE TABLE IF NOT EXISTS rom_path_history("
        "    id INTEGER PRIMARY KEY,"
        "    rom_id INTEGER NOT NULL,"
        "    old_file_path TEXT NOT NULL,"
        "    new_file_path TEXT NOT NULL,"
        "    changed_at INTEGER DEFAULT (strftime('%s', 'now'))"
        ");"

        "CREATE INDEX IF NOT EXISTS "
        "rom_path_history_rom_id_index "
        "ON rom_path_history(rom_id);"

        "CREATE TABLE IF NOT EXISTS rom_asset_migration("
        "    rom_id INTEGER PRIMARY KEY,"
        "    old_file_path TEXT NOT NULL,"
        "    new_file_path TEXT NOT NULL,"
        "    created_at INTEGER DEFAULT (strftime('%s', 'now')),"
        "    updated_at INTEGER"
        ");";

    bool success =
        execute_schema_sql(database, support_schema_sql);

    bool identity_table_exists =
        success &&
        sqlite_table_exists(database, "rom_identity");

    bool identity_has_system =
        identity_table_exists &&
        sqlite_table_has_column(
            database,
            "rom_identity",
            "system"
        );

    bool migrated_v4_identity = false;

    if (success &&
        identity_table_exists &&
        !identity_has_system) {
        const char *migration_sql =
            "ALTER TABLE rom_identity "
            "RENAME TO rom_identity_v4;"

            "CREATE TABLE rom_identity("
            "    rom_id INTEGER PRIMARY KEY,"
            "    system TEXT NOT NULL,"
            "    identity_type TEXT NOT NULL,"
            "    identity_value TEXT NOT NULL,"
            "    content_size INTEGER NOT NULL,"
            "    modified_time INTEGER,"
            "    created_at INTEGER DEFAULT (strftime('%s', 'now')),"
            "    updated_at INTEGER"
            ");"

            "INSERT INTO rom_identity("
            "    rom_id,"
            "    system,"
            "    identity_type,"
            "    identity_value,"
            "    content_size,"
            "    modified_time,"
            "    created_at,"
            "    updated_at"
            ") "
            "SELECT "
            "    old_identity.rom_id,"
            "    COALESCE("
            "        CASE "
            "            WHEN tracked_rom.file_path "
            "                 LIKE '/mnt/SDCARD/Roms/%' THEN "
            "                substr("
            "                    substr(tracked_rom.file_path, 18),"
            "                    1,"
            "                    instr("
            "                        substr(tracked_rom.file_path, 18),"
            "                        '/'"
            "                    ) - 1"
            "                ) "
            "            WHEN tracked_rom.file_path "
            "                 LIKE '../../Roms/%' THEN "
            "                substr("
            "                    substr(tracked_rom.file_path, 12),"
            "                    1,"
            "                    instr("
            "                        substr(tracked_rom.file_path, 12),"
            "                        '/'"
            "                    ) - 1"
            "                ) "
            "            WHEN tracked_rom.file_path "
            "                 LIKE '/Roms/%' THEN "
            "                substr("
            "                    substr(tracked_rom.file_path, 7),"
            "                    1,"
            "                    instr("
            "                        substr(tracked_rom.file_path, 7),"
            "                        '/'"
            "                    ) - 1"
            "                ) "
            "            WHEN instr(tracked_rom.file_path, '/') > 0 THEN "
            "                substr("
            "                    tracked_rom.file_path,"
            "                    1,"
            "                    instr(tracked_rom.file_path, '/') - 1"
            "                ) "
            "            ELSE '' "
            "        END,"
            "        ''"
            "    ),"
            "    old_identity.identity_type,"
            "    old_identity.identity_value,"
            "    old_identity.content_size,"
            "    old_identity.modified_time,"
            "    old_identity.created_at,"
            "    old_identity.updated_at "
            "FROM rom_identity_v4 AS old_identity "
            "LEFT JOIN rom AS tracked_rom "
            "    ON tracked_rom.id = old_identity.rom_id;"

            "DROP TABLE rom_identity_v4;";

        success =
            execute_schema_sql(database, migration_sql);

        migrated_v4_identity = success;
    }
    else if (success && !identity_table_exists) {
        const char *create_identity_sql =
            "CREATE TABLE rom_identity("
            "    rom_id INTEGER PRIMARY KEY,"
            "    system TEXT NOT NULL,"
            "    identity_type TEXT NOT NULL,"
            "    identity_value TEXT NOT NULL,"
            "    content_size INTEGER NOT NULL,"
            "    modified_time INTEGER,"
            "    created_at INTEGER DEFAULT (strftime('%s', 'now')),"
            "    updated_at INTEGER"
            ");";

        success =
            execute_schema_sql(database, create_identity_sql);
    }

    if (success) {
        success = execute_schema_sql(
            database,
            "CREATE INDEX IF NOT EXISTS "
            "rom_identity_match_index "
            "ON rom_identity("
            "    system,"
            "    identity_type,"
            "    identity_value,"
            "    content_size"
            ");"
        );
    }

    /*
     * v4 migrations were created by automatic rename inference. They must
     * never execute after upgrading to the conservative v5 identity model.
     */
    if (success && migrated_v4_identity) {
        success = execute_schema_sql(
            database,
            "DELETE FROM rom_asset_migration;"
        );
    }

    if (success) {
        success = execute_schema_sql(
            database,
            "INSERT INTO identity_metadata(key, value) "
            "VALUES('schema_version', '"
                STRINGIFY_VALUE(
                    PLAY_ACTIVITY_IDENTITY_SCHEMA_VERSION
                )
            "') "
            "ON CONFLICT(key) DO UPDATE "
            "SET value = excluded.value;"
        );
    }

    if (success) {
        success =
            sqlite3_exec(
                database,
                "COMMIT;",
                NULL,
                NULL,
                NULL
            ) == SQLITE_OK;
    }

    if (!success) {
        sqlite3_exec(
            database,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    return success;
}

bool play_activity_identity_store(
    sqlite3 *database,
    int rom_id,
    const char *system,
    const RomContentIdentity *identity,
    int64_t modified_time
)
{
    if (database == NULL ||
        rom_id < 0 ||
        system == NULL ||
        system[0] == '\0' ||
        identity == NULL ||
        identity->type[0] == '\0' ||
        identity->value[0] == '\0') {
        return false;
    }

    const char *sql =
        "INSERT INTO rom_identity("
        "    rom_id,"
        "    system,"
        "    identity_type,"
        "    identity_value,"
        "    content_size,"
        "    modified_time,"
        "    updated_at"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, strftime('%s', 'now')) "
        "ON CONFLICT(rom_id) DO UPDATE SET "
        "    system = excluded.system,"
        "    identity_type = excluded.identity_type,"
        "    identity_value = excluded.identity_value,"
        "    content_size = excluded.content_size,"
        "    modified_time = excluded.modified_time,"
        "    updated_at = excluded.updated_at;";

    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        fprintf(
            stderr,
            "activity identity prepare error: %s\n",
            sqlite3_errmsg(database)
        );
        return false;
    }

    sqlite3_bind_int(statement, 1, rom_id);
    sqlite3_bind_text(
        statement,
        2,
        system,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text(
        statement,
        3,
        identity->type,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text(
        statement,
        4,
        identity->value,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_int64(
        statement,
        5,
        (sqlite3_int64)identity->content_size
    );
    sqlite3_bind_int64(
        statement,
        6,
        (sqlite3_int64)modified_time
    );

    int result = sqlite3_step(statement);

    if (result != SQLITE_DONE) {
        fprintf(
            stderr,
            "activity identity store error for rom %d: %s\n",
            rom_id,
            sqlite3_errmsg(database)
        );
    }

    sqlite3_finalize(statement);

    return result == SQLITE_DONE;
}

bool play_activity_identity_load_if_unchanged(
    sqlite3 *database,
    int rom_id,
    uint64_t content_size,
    int64_t modified_time,
    RomContentIdentity *identity_out
)
{
    if (database == NULL ||
        rom_id < 0 ||
        identity_out == NULL) {
        return false;
    }

    const char *sql =
        "SELECT identity_type, identity_value, content_size "
        "FROM rom_identity "
        "WHERE rom_id = ?1 "
        "  AND content_size = ?2 "
        "  AND modified_time = ?3 "
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

    sqlite3_bind_int(statement, 1, rom_id);
    sqlite3_bind_int64(
        statement,
        2,
        (sqlite3_int64)content_size
    );
    sqlite3_bind_int64(
        statement,
        3,
        (sqlite3_int64)modified_time
    );

    bool found = false;

    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *identity_type =
            sqlite3_column_text(statement, 0);

        const unsigned char *identity_value =
            sqlite3_column_text(statement, 1);

        if (identity_type != NULL &&
            identity_value != NULL) {
            memset(identity_out, 0, sizeof(*identity_out));

            snprintf(
                identity_out->type,
                sizeof(identity_out->type),
                "%s",
                (const char *)identity_type
            );

            snprintf(
                identity_out->value,
                sizeof(identity_out->value),
                "%s",
                (const char *)identity_value
            );

            identity_out->content_size =
                (uint64_t)sqlite3_column_int64(statement, 2);

            found = true;
        }
    }

    sqlite3_finalize(statement);

    return found;
}

bool play_activity_identity_load(
    sqlite3 *database,
    int rom_id,
    RomContentIdentity *identity_out
)
{
    if (database == NULL ||
        rom_id < 0 ||
        identity_out == NULL) {
        return false;
    }

    const char *sql =
        "SELECT identity_type, identity_value, content_size "
        "FROM rom_identity "
        "WHERE rom_id = ?1 "
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

    sqlite3_bind_int(statement, 1, rom_id);

    bool found = false;

    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *identity_type =
            sqlite3_column_text(statement, 0);

        const unsigned char *identity_value =
            sqlite3_column_text(statement, 1);

        if (identity_type != NULL &&
            identity_value != NULL) {
            memset(identity_out, 0, sizeof(*identity_out));

            snprintf(
                identity_out->type,
                sizeof(identity_out->type),
                "%s",
                (const char *)identity_type
            );

            snprintf(
                identity_out->value,
                sizeof(identity_out->value),
                "%s",
                (const char *)identity_value
            );

            identity_out->content_size =
                (uint64_t)sqlite3_column_int64(statement, 2);

            found = true;
        }
    }

    sqlite3_finalize(statement);

    return found;
}

bool play_activity_identity_source_store(
    sqlite3 *database,
    int rom_id,
    const char *source_type,
    const char *source_signature
)
{
    if (database == NULL ||
        rom_id < 0 ||
        source_type == NULL ||
        source_type[0] == '\0' ||
        source_signature == NULL ||
        source_signature[0] == '\0') {
        return false;
    }

    const char *sql =
        "INSERT INTO rom_identity_source("
        "    rom_id,"
        "    source_type,"
        "    source_signature,"
        "    updated_at"
        ") VALUES(?1, ?2, ?3, strftime('%s', 'now')) "
        "ON CONFLICT(rom_id) DO UPDATE SET "
        "    source_type = excluded.source_type,"
        "    source_signature = excluded.source_signature,"
        "    updated_at = excluded.updated_at;";

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

    sqlite3_bind_int(statement, 1, rom_id);
    sqlite3_bind_text(
        statement,
        2,
        source_type,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text(
        statement,
        3,
        source_signature,
        -1,
        SQLITE_TRANSIENT
    );

    int result = sqlite3_step(statement);
    sqlite3_finalize(statement);

    return result == SQLITE_DONE;
}

bool play_activity_identity_source_matches(
    sqlite3 *database,
    int rom_id,
    const char *source_type,
    const char *source_signature
)
{
    if (database == NULL ||
        rom_id < 0 ||
        source_type == NULL ||
        source_signature == NULL) {
        return false;
    }

    const char *sql =
        "SELECT 1 "
        "FROM rom_identity_source "
        "WHERE rom_id = ?1 "
        "  AND source_type = ?2 "
        "  AND source_signature = ?3 "
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

    sqlite3_bind_int(statement, 1, rom_id);
    sqlite3_bind_text(
        statement,
        2,
        source_type,
        -1,
        SQLITE_TRANSIENT
    );
    sqlite3_bind_text(
        statement,
        3,
        source_signature,
        -1,
        SQLITE_TRANSIENT
    );

    bool matches = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);

    return matches;
}

void play_activity_identity_source_delete(
    sqlite3 *database,
    int rom_id
)
{
    if (database == NULL || rom_id < 0)
        return;

    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            "DELETE FROM rom_identity_source WHERE rom_id = ?1;",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_int(statement, 1, rom_id);
    sqlite3_step(statement);
    sqlite3_finalize(statement);
}

sqlite3_stmt *play_activity_identity_prepare_candidates(
    sqlite3 *database,
    const char *system,
    const RomContentIdentity *identity,
    const char *excluded_file_path
)
{
    if (database == NULL ||
        system == NULL ||
        system[0] == '\0' ||
        identity == NULL ||
        identity->type[0] == '\0' ||
        identity->value[0] == '\0' ||
        excluded_file_path == NULL) {
        return NULL;
    }

    const char *sql =
        "SELECT "
        "    rom.id,"
        "    rom.file_path,"
        "    COALESCE(NULLIF(rom.name, ''), rom.file_path) "
        "FROM rom_identity "
        "JOIN rom ON rom.id = rom_identity.rom_id "
        "WHERE rom_identity.system = ?1 "
        "  AND rom_identity.identity_type = ?2 "
        "  AND rom_identity.identity_value = ?3 "
        "  AND rom_identity.content_size = ?4 "
        "  AND rom.file_path <> ?5 "
        "ORDER BY "
        "    LOWER(COALESCE(NULLIF(rom.name, ''), rom.file_path)),"
        "    rom.id;";

    sqlite3_stmt *statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return NULL;
    }

    sqlite3_bind_text(
        statement,
        1,
        system,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        identity->type,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        3,
        identity->value,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int64(
        statement,
        4,
        (sqlite3_int64)identity->content_size
    );

    sqlite3_bind_text(
        statement,
        5,
        excluded_file_path,
        -1,
        SQLITE_TRANSIENT
    );

    return statement;
}

static bool file_stems_match(
    const char *first_path,
    const char *second_path
)
{
    if (first_path == NULL || second_path == NULL)
        return false;

    const char *first_name = strrchr(first_path, '/');
    const char *second_name = strrchr(second_path, '/');

    first_name =
        first_name != NULL
            ? first_name + 1
            : first_path;

    second_name =
        second_name != NULL
            ? second_name + 1
            : second_path;

    size_t first_length = strlen(first_name);
    size_t second_length = strlen(second_name);

    const char *first_extension = strrchr(first_name, '.');
    const char *second_extension = strrchr(second_name, '.');

    if (first_extension != NULL &&
        first_extension != first_name) {
        first_length =
            (size_t)(first_extension - first_name);
    }

    if (second_extension != NULL &&
        second_extension != second_name) {
        second_length =
            (size_t)(second_extension - second_name);
    }

    return first_length == second_length &&
           strncmp(
               first_name,
               second_name,
               first_length
           ) == 0;
}

bool play_activity_asset_migration_load(
    sqlite3 *database,
    int rom_id,
    char *old_file_path_out,
    size_t old_file_path_out_size,
    char *new_file_path_out,
    size_t new_file_path_out_size
)
{
    if (old_file_path_out != NULL &&
        old_file_path_out_size > 0) {
        old_file_path_out[0] = '\0';
    }

    if (new_file_path_out != NULL &&
        new_file_path_out_size > 0) {
        new_file_path_out[0] = '\0';
    }

    if (database == NULL ||
        rom_id < 0 ||
        old_file_path_out == NULL ||
        old_file_path_out_size == 0 ||
        new_file_path_out == NULL ||
        new_file_path_out_size == 0) {
        return false;
    }

    const char *sql =
        "SELECT old_file_path, new_file_path "
        "FROM rom_asset_migration "
        "WHERE rom_id = ?1 "
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

    sqlite3_bind_int(statement, 1, rom_id);

    bool found = false;

    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *old_file_path =
            sqlite3_column_text(statement, 0);

        const unsigned char *new_file_path =
            sqlite3_column_text(statement, 1);

        if (old_file_path != NULL &&
            new_file_path != NULL) {
            snprintf(
                old_file_path_out,
                old_file_path_out_size,
                "%s",
                (const char *)old_file_path
            );

            snprintf(
                new_file_path_out,
                new_file_path_out_size,
                "%s",
                (const char *)new_file_path
            );

            found = true;
        }
    }

    sqlite3_finalize(statement);

    return found;
}

bool play_activity_asset_migration_delete(
    sqlite3 *database,
    int rom_id
)
{
    if (database == NULL || rom_id < 0)
        return false;

    const char *sql =
        "DELETE FROM rom_asset_migration "
        "WHERE rom_id = ?1;";

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

    sqlite3_bind_int(statement, 1, rom_id);

    int result = sqlite3_step(statement);

    sqlite3_finalize(statement);

    return result == SQLITE_DONE;
}

bool play_activity_asset_migration_store(
    sqlite3 *database,
    int rom_id,
    const char *old_file_path,
    const char *new_file_path
)
{
    if (database == NULL ||
        rom_id < 0 ||
        old_file_path == NULL ||
        new_file_path == NULL ||
        old_file_path[0] == '\0' ||
        new_file_path[0] == '\0') {
        return false;
    }

    char stored_old_file_path[4096] = "";
    char stored_new_file_path[4096] = "";

    bool migration_exists =
        play_activity_asset_migration_load(
            database,
            rom_id,
            stored_old_file_path,
            sizeof(stored_old_file_path),
            stored_new_file_path,
            sizeof(stored_new_file_path)
        );

    if (!migration_exists &&
        file_stems_match(
            old_file_path,
            new_file_path
        )) {
        return true;
    }

    const char *sql =
        "INSERT INTO rom_asset_migration("
        "    rom_id,"
        "    old_file_path,"
        "    new_file_path,"
        "    updated_at"
        ") VALUES("
        "    ?1,"
        "    ?2,"
        "    ?3,"
        "    strftime('%s', 'now')"
        ") "
        "ON CONFLICT(rom_id) DO UPDATE SET "
        "    new_file_path = excluded.new_file_path,"
        "    updated_at = excluded.updated_at;";

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

    sqlite3_bind_int(statement, 1, rom_id);

    sqlite3_bind_text(
        statement,
        2,
        migration_exists
            ? stored_old_file_path
            : old_file_path,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        3,
        new_file_path,
        -1,
        SQLITE_TRANSIENT
    );

    int result = sqlite3_step(statement);

    sqlite3_finalize(statement);

    return result == SQLITE_DONE;
}

bool play_activity_identity_transfer_roms(
    sqlite3 *database,
    int survivor_rom_id,
    int redundant_rom_id,
    const char *system,
    const RomContentIdentity *identity,
    int64_t modified_time,
    const char *redundant_file_path,
    const char *current_file_path
)
{
    if (database == NULL ||
        survivor_rom_id < 0 ||
        redundant_rom_id < 0 ||
        survivor_rom_id == redundant_rom_id ||
        system == NULL ||
        system[0] == '\0' ||
        identity == NULL ||
        identity->type[0] == '\0' ||
        identity->value[0] == '\0') {
        return false;
    }

    if (sqlite3_exec(
            database,
            "BEGIN IMMEDIATE;",
            NULL,
            NULL,
            NULL) != SQLITE_OK) {
        return false;
    }

    bool success = true;
    sqlite3_stmt *statement = NULL;

    const char *move_activity_sql =
        "UPDATE play_activity "
        "SET rom_id = ?1 "
        "WHERE rom_id = ?2;";

    if (sqlite3_prepare_v2(
            database,
            move_activity_sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        success = false;
    }
    else {
        sqlite3_bind_int(statement, 1, survivor_rom_id);
        sqlite3_bind_int(statement, 2, redundant_rom_id);
        success = sqlite3_step(statement) == SQLITE_DONE;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    const char *move_history_sql =
        "UPDATE rom_path_history "
        "SET rom_id = ?1 "
        "WHERE rom_id = ?2;";

    if (success &&
        sqlite3_prepare_v2(
            database,
            move_history_sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        success = false;
    }
    else if (success) {
        sqlite3_bind_int(statement, 1, survivor_rom_id);
        sqlite3_bind_int(statement, 2, redundant_rom_id);
        success = sqlite3_step(statement) == SQLITE_DONE;
    }

    sqlite3_finalize(statement);
    statement = NULL;


    const char *delete_migrations_sql =
        "DELETE FROM rom_asset_migration "
        "WHERE rom_id = ?1 OR rom_id = ?2;";

    if (success &&
        sqlite3_prepare_v2(
            database,
            delete_migrations_sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        success = false;
    }
    else if (success) {
        sqlite3_bind_int(statement, 1, survivor_rom_id);
        sqlite3_bind_int(statement, 2, redundant_rom_id);
        success = sqlite3_step(statement) == SQLITE_DONE;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    const char *delete_sources_sql =
        "DELETE FROM rom_identity_source "
        "WHERE rom_id = ?1 OR rom_id = ?2;";

    if (success &&
        sqlite3_prepare_v2(
            database,
            delete_sources_sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        success = false;
    }
    else if (success) {
        sqlite3_bind_int(statement, 1, survivor_rom_id);
        sqlite3_bind_int(statement, 2, redundant_rom_id);
        success = sqlite3_step(statement) == SQLITE_DONE;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    const char *delete_identities_sql =
        "DELETE FROM rom_identity "
        "WHERE rom_id = ?1 OR rom_id = ?2;";

    if (success &&
        sqlite3_prepare_v2(
            database,
            delete_identities_sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        success = false;
    }
    else if (success) {
        sqlite3_bind_int(statement, 1, survivor_rom_id);
        sqlite3_bind_int(statement, 2, redundant_rom_id);
        success = sqlite3_step(statement) == SQLITE_DONE;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    const char *delete_rom_sql =
        "DELETE FROM rom "
        "WHERE id = ?1;";

    if (success &&
        sqlite3_prepare_v2(
            database,
            delete_rom_sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        success = false;
    }
    else if (success) {
        sqlite3_bind_int(statement, 1, redundant_rom_id);
        success = sqlite3_step(statement) == SQLITE_DONE;
    }

    sqlite3_finalize(statement);

    if (success &&
        redundant_file_path != NULL &&
        current_file_path != NULL &&
        redundant_file_path[0] != '\0' &&
        current_file_path[0] != '\0' &&
        strcmp(redundant_file_path, current_file_path) != 0) {
        success = play_activity_identity_record_path_change(
            database,
            survivor_rom_id,
            redundant_file_path,
            current_file_path
        );
    }


    if (success) {
        success = play_activity_identity_store(
            database,
            survivor_rom_id,
            system,
            identity,
            modified_time
        );
    }

    if (success) {
        success = sqlite3_exec(
            database,
            "COMMIT;",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK;
    }

    if (!success) {
        sqlite3_exec(
            database,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    return success;
}

bool play_activity_identity_record_path_change(
    sqlite3 *database,
    int rom_id,
    const char *old_file_path,
    const char *new_file_path
)
{
    if (database == NULL ||
        rom_id < 0 ||
        old_file_path == NULL ||
        new_file_path == NULL ||
        old_file_path[0] == '\0' ||
        new_file_path[0] == '\0' ||
        strcmp(old_file_path, new_file_path) == 0) {
        return false;
    }

    const char *sql =
        "INSERT INTO rom_path_history("
        "    rom_id,"
        "    old_file_path,"
        "    new_file_path"
        ") VALUES(?1, ?2, ?3);";

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

    sqlite3_bind_int(statement, 1, rom_id);

    sqlite3_bind_text(
        statement,
        2,
        old_file_path,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        3,
        new_file_path,
        -1,
        SQLITE_TRANSIENT
    );

    int result = sqlite3_step(statement);

    sqlite3_finalize(statement);

    if (result != SQLITE_DONE)
        return false;

    return true;
}

bool play_activity_identity_move_rom_path(
    sqlite3 *database,
    int rom_id,
    const char *old_file_path,
    const char *new_file_path
)
{
    if (database == NULL ||
        rom_id < 0 ||
        old_file_path == NULL ||
        new_file_path == NULL ||
        old_file_path[0] == '\0' ||
        new_file_path[0] == '\0' ||
        strcmp(old_file_path, new_file_path) == 0) {
        return false;
    }

    if (sqlite3_exec(
            database,
            "BEGIN IMMEDIATE;",
            NULL,
            NULL,
            NULL) != SQLITE_OK) {
        return false;
    }

    bool success = true;
    sqlite3_stmt *statement = NULL;

    const char *update_path_sql =
        "UPDATE rom "
        "SET file_path = ?1 "
        "WHERE id = ?2 "
        "  AND file_path = ?3;";

    if (sqlite3_prepare_v2(
            database,
            update_path_sql,
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        success = false;
    }
    else {
        sqlite3_bind_text(
            statement,
            1,
            new_file_path,
            -1,
            SQLITE_TRANSIENT
        );
        sqlite3_bind_int(statement, 2, rom_id);
        sqlite3_bind_text(
            statement,
            3,
            old_file_path,
            -1,
            SQLITE_TRANSIENT
        );

        success =
            sqlite3_step(statement) == SQLITE_DONE &&
            sqlite3_changes(database) == 1;
    }

    sqlite3_finalize(statement);

    if (success) {
        success = play_activity_identity_record_path_change(
            database,
            rom_id,
            old_file_path,
            new_file_path
        );
    }

    if (success) {
        success = play_activity_asset_migration_store(
            database,
            rom_id,
            old_file_path,
            new_file_path
        );
    }

    if (success) {
        success = sqlite3_exec(
            database,
            "COMMIT;",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK;
    }

    if (!success) {
        sqlite3_exec(
            database,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    return success;
}
