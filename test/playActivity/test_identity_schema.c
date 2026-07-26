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

    check_condition(
        play_activity_identity_schema_ensure(database),
        "create identity schema"
    );

    check_condition(
        play_activity_identity_schema_ensure(database),
        "identity schema creation is idempotent"
    );

    bool tables_exist =
        sqlite_table_exists(database, "identity_metadata") &&
        sqlite_table_exists(database, "rom_identity") &&
        sqlite_table_exists(database, "rom_identity_source") &&
        sqlite_table_exists(database, "rom_path_history");

    check_condition(
        tables_exist,
        "identity schema creates required tables"
    );

    check_condition(
        sqlite_schema_version_matches(database, "2"),
        "identity schema stores version 2"
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

    sqlite3_close(database);
}
