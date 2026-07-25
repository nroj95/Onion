#include "./playActivitySchema.h"

#include <stdio.h>

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
