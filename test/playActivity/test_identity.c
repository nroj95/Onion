/* =============================================================================
 * purpose:
 * verify play activity content identities with small host-side fixtures.
 *
 * key behavior:
 * - creates all fixtures in a temporary directory.
 * - confirms identical raw content produces identical crc32 identities.
 * - confirms changed raw content produces a different identity.
 * - removes temporary files before exiting.
 * =============================================================================
 */

#include "playActivityIdentity.h"
#include "playActivitySchema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_failed = 0;

static void check_condition(
    bool condition,
    const char *test_name
)
{
    tests_run++;

    if (condition) {
        printf("pass: %s\n", test_name);
        return;
    }

    tests_failed++;
    fprintf(stderr, "fail: %s\n", test_name);
}

static bool run_command(const char *command)
{
    return system(command) == 0;
}

static bool write_fixture(
    const char *path,
    const char *contents
)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return false;

    size_t content_size = strlen(contents);
    bool written =
        fwrite(contents, 1, content_size, file) == content_size;

    fclose(file);

    return written;
}

static void test_raw_identity(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-identity-test-XXXXXX";

    if (mkdtemp(temporary_directory) == NULL) {
        check_condition(false, "create temporary directory");
        return;
    }

    char first_path[512];
    char second_path[512];

    snprintf(
        first_path,
        sizeof(first_path),
        "%s/first.rom",
        temporary_directory
    );

    snprintf(
        second_path,
        sizeof(second_path),
        "%s/second.rom",
        temporary_directory
    );

    bool fixtures_written =
        write_fixture(first_path, "same rom contents") &&
        write_fixture(second_path, "same rom contents");

    check_condition(fixtures_written, "write raw fixtures");

    RomContentIdentity first_identity;
    RomContentIdentity second_identity;

    bool first_calculated =
        rom_identity_calculate_raw(first_path, &first_identity);

    bool second_calculated =
        rom_identity_calculate_raw(second_path, &second_identity);

    check_condition(
        first_calculated && second_calculated,
        "calculate raw identities"
    );

    check_condition(
        strcmp(first_identity.type, "crc32") == 0,
        "raw identity uses crc32"
    );

    check_condition(
        strcmp(first_identity.value, second_identity.value) == 0 &&
        first_identity.content_size == second_identity.content_size,
        "same bytes produce same identity"
    );

    bool changed_fixture_written =
        write_fixture(second_path, "changed rom contents");

    check_condition(
        changed_fixture_written,
        "rewrite changed raw fixture"
    );

    RomContentIdentity changed_identity;

    bool changed_calculated =
        rom_identity_calculate_raw(second_path, &changed_identity);

    check_condition(
        changed_calculated,
        "calculate changed raw identity"
    );

    check_condition(
        strcmp(first_identity.value, changed_identity.value) != 0,
        "changed bytes produce different identity"
    );

    unlink(first_path);
    unlink(second_path);
    rmdir(temporary_directory);
}

static void test_single_file_zip_identity(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-zip-test-XXXXXX";

    if (mkdtemp(temporary_directory) == NULL) {
        check_condition(false, "create zip temporary directory");
        return;
    }

    char member_path[512];
    char first_zip_path[512];
    char second_zip_path[512];
    char changed_zip_path[512];

    snprintf(
        member_path,
        sizeof(member_path),
        "%s/game.rom",
        temporary_directory
    );

    snprintf(
        first_zip_path,
        sizeof(first_zip_path),
        "%s/first.zip",
        temporary_directory
    );

    snprintf(
        second_zip_path,
        sizeof(second_zip_path),
        "%s/second.zip",
        temporary_directory
    );

    snprintf(
        changed_zip_path,
        sizeof(changed_zip_path),
        "%s/changed.zip",
        temporary_directory
    );

    bool member_written =
        write_fixture(member_path, "same zipped rom contents");

    check_condition(member_written, "write zip member fixture");

    char first_command[2048];
    char second_command[2048];

    snprintf(
        first_command,
        sizeof(first_command),
        "cd '%s' && /usr/bin/7z a -bd -y -tzip -mx=0 '%s' game.rom >/dev/null",
        temporary_directory,
        first_zip_path
    );

    snprintf(
        second_command,
        sizeof(second_command),
        "cd '%s' && /usr/bin/7z a -bd -y -tzip -mx=9 '%s' game.rom >/dev/null",
        temporary_directory,
        second_zip_path
    );

    bool archives_created =
        run_command(first_command) &&
        run_command(second_command);

    check_condition(
        archives_created,
        "create differently compressed zip fixtures"
    );

    RomContentIdentity first_identity;
    RomContentIdentity second_identity;

    bool first_calculated =
        rom_identity_calculate_zip(first_zip_path, &first_identity);

    bool second_calculated =
        rom_identity_calculate_zip(second_zip_path, &second_identity);

    check_condition(
        first_calculated && second_calculated,
        "calculate single-file zip identities"
    );

    check_condition(
        strcmp(first_identity.type, "crc32") == 0,
        "single-file zip identity uses member crc32"
    );

    check_condition(
        strcmp(first_identity.value, second_identity.value) == 0 &&
        first_identity.content_size == second_identity.content_size,
        "recompressed zip preserves member identity"
    );

    bool changed_member_written =
        write_fixture(member_path, "changed zipped rom contents");

    check_condition(
        changed_member_written,
        "rewrite changed zip member fixture"
    );

    char changed_command[2048];

    snprintf(
        changed_command,
        sizeof(changed_command),
        "cd '%s' && /usr/bin/7z a -bd -y -tzip -mx=9 '%s' game.rom >/dev/null",
        temporary_directory,
        changed_zip_path
    );

    check_condition(
        run_command(changed_command),
        "create changed zip fixture"
    );

    RomContentIdentity changed_identity;

    bool changed_calculated =
        rom_identity_calculate_zip(
            changed_zip_path,
            &changed_identity
        );

    check_condition(
        changed_calculated,
        "calculate changed zip identity"
    );

    check_condition(
        strcmp(first_identity.value, changed_identity.value) != 0,
        "changed zip member produces different identity"
    );

    unlink(member_path);
    unlink(first_zip_path);
    unlink(second_zip_path);
    unlink(changed_zip_path);
    rmdir(temporary_directory);
}

static void test_cue_identity(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-cue-test-XXXXXX";

    if (mkdtemp(temporary_directory) == NULL) {
        check_condition(false, "create cue temporary directory");
        return;
    }

    char original_directory[1024];
    char renamed_directory[1024];
    char reordered_directory[1024];

    snprintf(
        original_directory,
        sizeof(original_directory),
        "%s/original",
        temporary_directory
    );

    snprintf(
        renamed_directory,
        sizeof(renamed_directory),
        "%s/renamed",
        temporary_directory
    );

    snprintf(
        reordered_directory,
        sizeof(reordered_directory),
        "%s/reordered",
        temporary_directory
    );

    bool directories_created =
        mkdir(original_directory, 0700) == 0 &&
        mkdir(renamed_directory, 0700) == 0 &&
        mkdir(reordered_directory, 0700) == 0;

    check_condition(
        directories_created,
        "create cue fixture directories"
    );

    char original_first_track[2048];
    char original_second_track[2048];
    char original_cue[2048];

    char renamed_first_track[2048];
    char renamed_second_track[2048];
    char renamed_cue[2048];

    char reordered_first_track[2048];
    char reordered_second_track[2048];
    char reordered_cue[2048];

    snprintf(
        original_first_track,
        sizeof(original_first_track),
        "%s/disc track 1.bin",
        original_directory
    );

    snprintf(
        original_second_track,
        sizeof(original_second_track),
        "%s/disc track 2.bin",
        original_directory
    );

    snprintf(
        original_cue,
        sizeof(original_cue),
        "%s/game.cue",
        original_directory
    );

    snprintf(
        renamed_first_track,
        sizeof(renamed_first_track),
        "%s/renamed first.bin",
        renamed_directory
    );

    snprintf(
        renamed_second_track,
        sizeof(renamed_second_track),
        "%s/renamed second.bin",
        renamed_directory
    );

    snprintf(
        renamed_cue,
        sizeof(renamed_cue),
        "%s/renamed game.cue",
        renamed_directory
    );

    snprintf(
        reordered_first_track,
        sizeof(reordered_first_track),
        "%s/first.bin",
        reordered_directory
    );

    snprintf(
        reordered_second_track,
        sizeof(reordered_second_track),
        "%s/second.bin",
        reordered_directory
    );

    snprintf(
        reordered_cue,
        sizeof(reordered_cue),
        "%s/reordered game.cue",
        reordered_directory
    );

    bool tracks_written =
        write_fixture(original_first_track, "first track contents") &&
        write_fixture(original_second_track, "second track contents") &&
        write_fixture(renamed_first_track, "first track contents") &&
        write_fixture(renamed_second_track, "second track contents") &&
        write_fixture(reordered_first_track, "first track contents") &&
        write_fixture(reordered_second_track, "second track contents");

    check_condition(
        tracks_written,
        "write cue track fixtures"
    );

    const char *original_cue_contents =
        "FILE \"disc track 1.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"disc track 2.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:00\n";

    const char *renamed_cue_contents =
        "FILE \"renamed first.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"renamed second.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:00\n";

    const char *reordered_cue_contents =
        "FILE \"second.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"first.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n";

    bool cues_written =
        write_fixture(original_cue, original_cue_contents) &&
        write_fixture(renamed_cue, renamed_cue_contents) &&
        write_fixture(reordered_cue, reordered_cue_contents);

    check_condition(
        cues_written,
        "write cue sheet fixtures"
    );

    RomContentIdentity original_identity;
    RomContentIdentity renamed_identity;
    RomContentIdentity reordered_identity;

    bool original_calculated =
        rom_identity_calculate_cue(
            original_cue,
            &original_identity
        );

    bool renamed_calculated =
        rom_identity_calculate_cue(
            renamed_cue,
            &renamed_identity
        );

    bool reordered_calculated =
        rom_identity_calculate_cue(
            reordered_cue,
            &reordered_identity
        );

    check_condition(
        original_calculated &&
        renamed_calculated &&
        reordered_calculated,
        "calculate cue identities"
    );

    check_condition(
        strcmp(original_identity.type, "cue-fnv1a64") == 0,
        "cue identity uses cue-fnv1a64"
    );

    check_condition(
        strcmp(
            original_identity.value,
            renamed_identity.value
        ) == 0 &&
        original_identity.content_size ==
            renamed_identity.content_size,
        "renamed cue tracks preserve identity"
    );

    check_condition(
        strcmp(
            original_identity.value,
            reordered_identity.value
        ) != 0,
        "reordered cue tracks change identity"
    );

    unlink(original_first_track);
    unlink(original_second_track);
    unlink(original_cue);

    unlink(renamed_first_track);
    unlink(renamed_second_track);
    unlink(renamed_cue);

    unlink(reordered_first_track);
    unlink(reordered_second_track);
    unlink(reordered_cue);

    rmdir(original_directory);
    rmdir(renamed_directory);
    rmdir(reordered_directory);
    rmdir(temporary_directory);
}

static void test_m3u_identity(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-m3u-test-XXXXXX";

    if (mkdtemp(temporary_directory) == NULL) {
        check_condition(false, "create m3u temporary directory");
        return;
    }

    char original_directory[1024];
    char renamed_directory[1024];
    char reordered_directory[1024];

    snprintf(
        original_directory,
        sizeof(original_directory),
        "%s/original",
        temporary_directory
    );

    snprintf(
        renamed_directory,
        sizeof(renamed_directory),
        "%s/renamed",
        temporary_directory
    );

    snprintf(
        reordered_directory,
        sizeof(reordered_directory),
        "%s/reordered",
        temporary_directory
    );

    bool directories_created =
        mkdir(original_directory, 0700) == 0 &&
        mkdir(renamed_directory, 0700) == 0 &&
        mkdir(reordered_directory, 0700) == 0;

    check_condition(
        directories_created,
        "create m3u fixture directories"
    );

    char original_first_file[2048];
    char original_second_file[2048];
    char original_playlist[2048];

    char renamed_first_file[2048];
    char renamed_second_file[2048];
    char renamed_playlist[2048];

    char reordered_first_file[2048];
    char reordered_second_file[2048];
    char reordered_playlist[2048];

    snprintf(
        original_first_file,
        sizeof(original_first_file),
        "%s/disc one.bin",
        original_directory
    );

    snprintf(
        original_second_file,
        sizeof(original_second_file),
        "%s/disc two.bin",
        original_directory
    );

    snprintf(
        original_playlist,
        sizeof(original_playlist),
        "%s/game.m3u",
        original_directory
    );

    snprintf(
        renamed_first_file,
        sizeof(renamed_first_file),
        "%s/renamed first.bin",
        renamed_directory
    );

    snprintf(
        renamed_second_file,
        sizeof(renamed_second_file),
        "%s/renamed second.bin",
        renamed_directory
    );

    snprintf(
        renamed_playlist,
        sizeof(renamed_playlist),
        "%s/renamed game.m3u",
        renamed_directory
    );

    snprintf(
        reordered_first_file,
        sizeof(reordered_first_file),
        "%s/first.bin",
        reordered_directory
    );

    snprintf(
        reordered_second_file,
        sizeof(reordered_second_file),
        "%s/second.bin",
        reordered_directory
    );

    snprintf(
        reordered_playlist,
        sizeof(reordered_playlist),
        "%s/reordered game.m3u",
        reordered_directory
    );

    bool referenced_files_written =
        write_fixture(original_first_file, "first disc contents") &&
        write_fixture(original_second_file, "second disc contents") &&
        write_fixture(renamed_first_file, "first disc contents") &&
        write_fixture(renamed_second_file, "second disc contents") &&
        write_fixture(reordered_first_file, "first disc contents") &&
        write_fixture(reordered_second_file, "second disc contents");

    check_condition(
        referenced_files_written,
        "write m3u referenced files"
    );

    const char *original_playlist_contents =
        "disc one.bin\n"
        "disc two.bin\n";

    const char *renamed_playlist_contents =
        "renamed first.bin\n"
        "renamed second.bin\n";

    const char *reordered_playlist_contents =
        "second.bin\n"
        "first.bin\n";

    bool playlists_written =
        write_fixture(
            original_playlist,
            original_playlist_contents
        ) &&
        write_fixture(
            renamed_playlist,
            renamed_playlist_contents
        ) &&
        write_fixture(
            reordered_playlist,
            reordered_playlist_contents
        );

    check_condition(
        playlists_written,
        "write m3u playlist fixtures"
    );

    RomContentIdentity original_identity;
    RomContentIdentity renamed_identity;
    RomContentIdentity reordered_identity;

    bool original_calculated =
        rom_identity_calculate_m3u(
            original_playlist,
            &original_identity
        );

    bool renamed_calculated =
        rom_identity_calculate_m3u(
            renamed_playlist,
            &renamed_identity
        );

    bool reordered_calculated =
        rom_identity_calculate_m3u(
            reordered_playlist,
            &reordered_identity
        );

    check_condition(
        original_calculated &&
        renamed_calculated &&
        reordered_calculated,
        "calculate m3u identities"
    );

    check_condition(
        strcmp(original_identity.type, "m3u-fnv1a64") == 0,
        "m3u identity uses m3u-fnv1a64"
    );

    check_condition(
        strcmp(
            original_identity.value,
            renamed_identity.value
        ) == 0 &&
        original_identity.content_size ==
            renamed_identity.content_size,
        "renamed m3u entries preserve identity"
    );

    check_condition(
        strcmp(
            original_identity.value,
            reordered_identity.value
        ) != 0,
        "reordered m3u entries change identity"
    );

    unlink(original_first_file);
    unlink(original_second_file);
    unlink(original_playlist);

    unlink(renamed_first_file);
    unlink(renamed_second_file);
    unlink(renamed_playlist);

    unlink(reordered_first_file);
    unlink(reordered_second_file);
    unlink(reordered_playlist);

    rmdir(original_directory);
    rmdir(renamed_directory);
    rmdir(reordered_directory);
    rmdir(temporary_directory);
}

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

static void test_identity_schema_storage(void)
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

static void test_identity_rom_merge(void)
{
    sqlite3 *database = NULL;

    bool database_opened =
        sqlite3_open(":memory:", &database) == SQLITE_OK;

    check_condition(
        database_opened,
        "open in-memory merge database"
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
        "create merge fixture base tables"
    );

    check_condition(
        play_activity_identity_schema_ensure(database),
        "create identity schema for merge"
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
        "create duplicate rom merge fixtures"
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
            &survivor_identity,
            1000
        ) &&
        play_activity_identity_store(
            database,
            20,
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
        play_activity_identity_merge_roms(
            database,
            10,
            20,
            &final_identity,
            3000,
            "/Roms/FC/old-name.zip",
            "/Roms/FC/current.zip"
        ),
        "merge duplicate rom rows"
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
        "merge keeps survivor and removes duplicate"
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
        "merge preserves both activity rows"
    );

    check_condition(
        sqlite_query_int(
            database,
            "SELECT SUM(play_time) "
            "FROM play_activity "
            "WHERE rom_id = 10;"
        ) == 360,
        "merge preserves total play time"
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
        "merge preserves and records path history"
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
        "merge records duplicate path transition"
    );

    check_condition(
        sqlite_query_int(
            database,
            "SELECT COUNT(*) "
            "FROM rom_identity_source "
            "WHERE rom_id IN (10, 20);"
        ) == 0,
        "merge removes stale source signatures"
    );

    RomContentIdentity loaded_identity;

    check_condition(
        play_activity_identity_load(
            database,
            10,
            &loaded_identity
        ),
        "load merged survivor identity"
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
        "merge stores final survivor identity"
    );

    check_condition(
        play_activity_identity_find_rom_id(
            database,
            &final_identity
        ) == 10,
        "merged identity resolves to survivor"
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

int main(void)
{
    printf("play activity identity tests\n\n");

    test_raw_identity();
    test_single_file_zip_identity();
    test_cue_identity();
    test_m3u_identity();
    test_identity_schema_storage();
    test_identity_rom_merge();

    printf(
        "\nsummary: %d run, %d passed, %d failed\n",
        tests_run,
        tests_run - tests_failed,
        tests_failed
    );

    return tests_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
