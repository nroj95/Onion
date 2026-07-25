#include "./playActivitySchema.h"

#include <stdio.h>
#include <string.h>

/* =============================================================================
 * purpose:
 * create the first version of the activity tracker identity schema.
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

bool play_activity_identity_schema_ensure(sqlite3 *database)
{
    if (database == NULL)
        return false;

    const char *schema_sql =
        "BEGIN IMMEDIATE;"

        "CREATE TABLE IF NOT EXISTS identity_metadata("
        "    key TEXT PRIMARY KEY,"
        "    value TEXT NOT NULL"
        ");"

        "CREATE TABLE IF NOT EXISTS rom_identity("
        "    rom_id INTEGER PRIMARY KEY,"
        "    identity_type TEXT NOT NULL,"
        "    identity_value TEXT NOT NULL,"
        "    content_size INTEGER,"
        "    modified_time INTEGER,"
        "    created_at INTEGER DEFAULT (strftime('%s', 'now')),"
        "    updated_at INTEGER,"
        "    UNIQUE(identity_type, identity_value)"
        ");"

        "CREATE INDEX IF NOT EXISTS "
        "rom_identity_value_index "
        "ON rom_identity(identity_type, identity_value);"

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

        "INSERT INTO identity_metadata(key, value) "
        "VALUES('schema_version', '1') "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value;"

        "COMMIT;";

    if (execute_schema_sql(database, schema_sql))
        return true;

    sqlite3_exec(
        database,
        "ROLLBACK;",
        NULL,
        NULL,
        NULL
    );

    return false;
}

bool play_activity_identity_store(
    sqlite3 *database,
    int rom_id,
    const RomContentIdentity *identity,
    int64_t modified_time
)
{
    if (database == NULL ||
        rom_id < 0 ||
        identity == NULL ||
        identity->type[0] == '\0' ||
        identity->value[0] == '\0') {
        return false;
    }

    const char *sql =
        "INSERT INTO rom_identity("
        "    rom_id,"
        "    identity_type,"
        "    identity_value,"
        "    content_size,"
        "    modified_time,"
        "    updated_at"
        ") VALUES(?1, ?2, ?3, ?4, ?5, strftime('%s', 'now')) "
        "ON CONFLICT(rom_id) DO UPDATE SET "
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
    sqlite3_bind_int64(
        statement,
        5,
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

int play_activity_identity_find_rom_id(
    sqlite3 *database,
    const RomContentIdentity *identity
)
{
    if (database == NULL ||
        identity == NULL ||
        identity->type[0] == '\0' ||
        identity->value[0] == '\0') {
        return -1;
    }

    const char *sql =
        "SELECT rom_id "
        "FROM rom_identity "
        "WHERE identity_type = ?1 "
        "  AND identity_value = ?2 "
        "LIMIT 1;";

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

    sqlite3_bind_text(
        statement,
        1,
        identity->type,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        identity->value,
        -1,
        SQLITE_TRANSIENT
    );

    int rom_id = -1;

    if (sqlite3_step(statement) == SQLITE_ROW)
        rom_id = sqlite3_column_int(statement, 0);

    sqlite3_finalize(statement);

    return rom_id;
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

    return result == SQLITE_DONE;
}
