#include "playActivityAssets.h"
#include "playActivitySchema.h"
#include "playActivityTransfer.h"
#include "test_support.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool join_fixture_path(
    char *path_out,
    size_t path_out_size,
    const char *base,
    const char *suffix
)
{
    if (path_out == NULL ||
        path_out_size == 0 ||
        base == NULL ||
        suffix == NULL) {
        return false;
    }

    size_t base_length = strlen(base);
    size_t suffix_length = strlen(suffix);

    if (base_length > path_out_size - 1 ||
        suffix_length >
            path_out_size - base_length - 1) {
        path_out[0] = '\0';
        return false;
    }

    memcpy(path_out, base, base_length);
    memcpy(
        path_out + base_length,
        suffix,
        suffix_length + 1
    );

    return true;
}

static int query_fixture_int(
    sqlite3 *database,
    const char *sql
)
{
    sqlite3_stmt *statement = NULL;
    int value = -1;

    if (sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            NULL) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW) {
        value = sqlite3_column_int(statement, 0);
    }

    sqlite3_finalize(statement);
    return value;
}

void test_identity_transfer_plan(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-transfer-XXXXXX";

    bool temporary_directory_created =
        mkdtemp(temporary_directory) != NULL;

    check_condition(
        temporary_directory_created,
        "create transfer planner temporary directory"
    );

    if (!temporary_directory_created)
        return;

    char roms_root[512];
    char system_directory[512];
    char cores_directory[512];
    char saves_root[512];
    char states_root[512];
    char saves_directory[512];
    char states_directory[512];
    char history_path[512];
    char missing_history_path[512];
    char core_path[512];
    char info_path[512];
    char source_absolute_path[512];
    char old_save_path[512];
    char old_state_path[512];
    char new_save_path[512];

    bool paths_built =
        join_fixture_path(
            roms_root,
            sizeof(roms_root),
            temporary_directory,
            "/Roms"
        ) &&
        join_fixture_path(
            system_directory,
            sizeof(system_directory),
            roms_root,
            "/GB"
        ) &&
        join_fixture_path(
            cores_directory,
            sizeof(cores_directory),
            temporary_directory,
            "/cores"
        ) &&
        join_fixture_path(
            saves_root,
            sizeof(saves_root),
            temporary_directory,
            "/saves"
        ) &&
        join_fixture_path(
            states_root,
            sizeof(states_root),
            temporary_directory,
            "/states"
        ) &&
        join_fixture_path(
            saves_directory,
            sizeof(saves_directory),
            saves_root,
            "/Gambatte"
        ) &&
        join_fixture_path(
            states_directory,
            sizeof(states_directory),
            states_root,
            "/Gambatte"
        ) &&
        join_fixture_path(
            history_path,
            sizeof(history_path),
            temporary_directory,
            "/content_history.lpl"
        ) &&
        join_fixture_path(
            missing_history_path,
            sizeof(missing_history_path),
            temporary_directory,
            "/missing-history.lpl"
        ) &&
        join_fixture_path(
            core_path,
            sizeof(core_path),
            cores_directory,
            "/gambatte_libretro.so"
        ) &&
        join_fixture_path(
            info_path,
            sizeof(info_path),
            cores_directory,
            "/gambatte_libretro.info"
        ) &&
        join_fixture_path(
            source_absolute_path,
            sizeof(source_absolute_path),
            system_directory,
            "/old-name.gb"
        ) &&
        join_fixture_path(
            old_save_path,
            sizeof(old_save_path),
            saves_directory,
            "/old-name.srm"
        ) &&
        join_fixture_path(
            old_state_path,
            sizeof(old_state_path),
            states_directory,
            "/old-name.state.auto"
        ) &&
        join_fixture_path(
            new_save_path,
            sizeof(new_save_path),
            saves_directory,
            "/new-name.srm"
        );

    check_condition(
        paths_built,
        "build transfer planner fixture paths"
    );

    bool directories_created =
        paths_built &&
        mkdir(roms_root, 0700) == 0 &&
        mkdir(system_directory, 0700) == 0 &&
        mkdir(cores_directory, 0700) == 0 &&
        mkdir(saves_root, 0700) == 0 &&
        mkdir(states_root, 0700) == 0 &&
        mkdir(saves_directory, 0700) == 0 &&
        mkdir(states_directory, 0700) == 0;

    check_condition(
        directories_created,
        "create transfer planner fixture directories"
    );

    sqlite3 *database = NULL;

    bool database_ready =
        sqlite3_open(":memory:", &database) == SQLITE_OK &&
        sqlite3_exec(
            database,
            "CREATE TABLE rom("
            "    id INTEGER PRIMARY KEY,"
            "    type TEXT,"
            "    name TEXT,"
            "    file_path TEXT,"
            "    image_path TEXT,"
            "    created_at INTEGER,"
            "    updated_at INTEGER"
            ");"
            "CREATE TABLE play_activity("
            "    rom_id INTEGER,"
            "    play_time INTEGER,"
            "    created_at INTEGER,"
            "    updated_at INTEGER"
            ");",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK &&
        play_activity_identity_schema_ensure(database);

    check_condition(
        database_ready,
        "create transfer planner database"
    );

    RomContentIdentity identity;
    memset(&identity, 0, sizeof(identity));

    snprintf(
        identity.type,
        sizeof(identity.type),
        "%s",
        "crc32"
    );
    snprintf(
        identity.value,
        sizeof(identity.value),
        "%s",
        "01234567"
    );
    identity.content_size = 1234;

    bool source_stored =
        database_ready &&
        sqlite3_exec(
            database,
            "INSERT INTO rom("
            "    id, name, file_path"
            ") VALUES("
            "    90, 'old name', 'GB/old-name.gb'"
            ");",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK &&
        play_activity_identity_store(
            database,
            90,
            "GB",
            &identity,
            123456789
        );

    check_condition(
        source_stored,
        "store transfer planner source"
    );

    char history_contents[2048];

    snprintf(
        history_contents,
        sizeof(history_contents),
        "{\n"
        "  \"version\": \"1.5\",\n"
        "  \"items\": [\n"
        "    {\n"
        "      \"path\": \"%s\",\n"
        "      \"core_path\": \"%s\"\n"
        "    }\n"
        "  ]\n"
        "}\n",
        source_absolute_path,
        core_path
    );

    bool fixture_files_written =
        write_fixture(
            info_path,
            "display_name = Nintendo - Game Boy\n"
            "corename = Gambatte\n"
        ) &&
        write_fixture(
            history_path,
            history_contents
        ) &&
        write_fixture(
            old_save_path,
            "save"
        ) &&
        write_fixture(
            old_state_path,
            "state"
        );

    check_condition(
        fixture_files_written,
        "write transfer planner fixture files"
    );

    char reconstructed_path[512] = "";

    check_condition(
        play_activity_transfer_stored_path_to_absolute(
            roms_root,
            "GB/old-name.gb",
            reconstructed_path,
            sizeof(reconstructed_path)
        ) &&
        strcmp(
            reconstructed_path,
            source_absolute_path
        ) == 0,
        "reconstruct stored relative rom path"
    );

    char legacy_path[512] = "";

    check_condition(
        play_activity_transfer_stored_path_to_absolute(
            roms_root,
            "../../Roms/GB/old-name.gb",
            legacy_path,
            sizeof(legacy_path)
        ) &&
        strcmp(
            legacy_path,
            source_absolute_path
        ) == 0,
        "reconstruct legacy rom path"
    );

    PlayActivityTransferPlan plan;

    bool ready =
        source_stored &&
        fixture_files_written &&
        play_activity_transfer_plan(
            database,
            90,
            "GB",
            &identity,
            "GB/new-name.gb",
            roms_root,
            history_path,
            saves_root,
            states_root,
            &plan
        );

    check_condition(
        ready &&
        plan.kind == PLAY_ACTIVITY_TRANSFER_PLAN_READY,
        "transfer planner reports ready"
    );

    check_condition(
        ready &&
        strcmp(
            plan.source_core_name,
            "Gambatte"
        ) == 0 &&
        strcmp(
            plan.source_file_path,
            "GB/old-name.gb"
        ) == 0 &&
        strcmp(
            plan.saves_directory,
            saves_directory
        ) == 0 &&
        strcmp(
            plan.states_directory,
            states_directory
        ) == 0 &&
        plan.blocked == 0,
        "ready plan resolves source asset locations"
    );

    check_condition(
        sqlite3_exec(
            database,
            "INSERT INTO rom("
            "    id, name, file_path"
            ") VALUES("
            "    93,"
            "    'transferred name',"
            "    'GB/transferred-name.gb'"
            ");",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK &&
        play_activity_identity_store(
            database,
            93,
            "GB",
            &identity,
            123456789
        ) &&
        play_activity_identity_record_path_change(
            database,
            93,
            "GB/old-name.gb",
            "GB/transferred-name.gb"
        ),
        "create transferred-but-unlaunched planner source"
    );

    bool history_fallback_ready =
        play_activity_transfer_plan(
            database,
            93,
            "GB",
            &identity,
            "GB/fallback-target.gb",
            roms_root,
            history_path,
            saves_root,
            states_root,
            &plan
        );

    check_condition(
        history_fallback_ready &&
        plan.kind == PLAY_ACTIVITY_TRANSFER_PLAN_READY &&
        strcmp(
            plan.source_core_name,
            "Gambatte"
        ) == 0 &&
        strcmp(
            plan.source_file_path,
            "GB/transferred-name.gb"
        ) == 0,
        "transfer planner recovers core from explicit path history"
    );

    check_condition(
        write_fixture(
            new_save_path,
            "existing destination save"
        ),
        "write transfer planner collision"
    );

    bool replacement_required =
        play_activity_transfer_plan(
            database,
            90,
            "GB",
            &identity,
            "GB/new-name.gb",
            roms_root,
            history_path,
            saves_root,
            states_root,
            &plan
        );

    check_condition(
        replacement_required &&
        plan.kind ==
            PLAY_ACTIVITY_TRANSFER_PLAN_REPLACE &&
        plan.blocked == 1 &&
        strcmp(
            plan.source_core_name,
            "Gambatte"
        ) == 0,
        "transfer planner reports replacement collision"
    );

    unlink(new_save_path);

    bool activity_only =
        play_activity_transfer_plan(
            database,
            90,
            "GB",
            &identity,
            "GB/new-name.gb",
            roms_root,
            missing_history_path,
            saves_root,
            states_root,
            &plan
        );

    check_condition(
        activity_only &&
        plan.kind ==
            PLAY_ACTIVITY_TRANSFER_PLAN_ACTIVITY_ONLY,
        "transfer planner permits activity-only fallback"
    );

    check_condition(
        !play_activity_transfer_plan(
            database,
            91,
            "GB",
            &identity,
            "GB/new-name.gb",
            roms_root,
            history_path,
            saves_root,
            states_root,
            &plan
        ),
        "transfer planner rejects unselected rom id"
    );

    check_condition(
        !play_activity_transfer_plan(
            database,
            90,
            "GBC",
            &identity,
            "GBC/new-name.gbc",
            roms_root,
            history_path,
            saves_root,
            states_root,
            &plan
        ),
        "transfer planner rejects other system"
    );

    check_condition(
        sqlite3_exec(
            database,
            "INSERT INTO play_activity("
            "    rom_id, play_time"
            ") VALUES(90, 321);"
            "CREATE TEMP TRIGGER "
            "fail_explicit_transfer "
            "BEFORE UPDATE OF rom_id ON play_activity "
            "WHEN OLD.rom_id = 90 "
            "BEGIN "
            "    SELECT RAISE("
            "        FAIL,"
            "        'forced explicit transfer failure'"
            "    );"
            "END;",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK,
        "create forced coordinator database failure"
    );

    bool outer_transaction_open =
        sqlite3_exec(
            database,
            "BEGIN IMMEDIATE;",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK;

    PlayActivityTransferPlan rollback_plan;

    bool rollback_plan_ready =
        outer_transaction_open &&
        play_activity_transfer_plan(
            database,
            90,
            "GB",
            &identity,
            "GB/new-name.gb",
            roms_root,
            history_path,
            saves_root,
            states_root,
            &rollback_plan
        ) &&
        rollback_plan.kind ==
            PLAY_ACTIVITY_TRANSFER_PLAN_READY;

    check_condition(
        rollback_plan_ready,
        "revalidate transfer under coordinator write lock"
    );

    PlayActivityAssetTransferResult asset_result;

    PlayActivityAssetTransfer *rollback_transfer = NULL;

    if (rollback_plan_ready) {
        rollback_transfer =
            play_activity_asset_transfer_prepare(
                rollback_plan.saves_directory,
                rollback_plan.states_directory,
                rollback_plan.source_file_path,
                "GB/new-name.gb",
                false,
                &asset_result
            );
    }

    bool assets_applied =
        rollback_transfer != NULL &&
        play_activity_asset_transfer_apply(
            rollback_transfer,
            &asset_result
        );

    check_condition(
        assets_applied &&
        access(old_save_path, F_OK) != 0 &&
        access(old_state_path, F_OK) != 0 &&
        access(new_save_path, F_OK) == 0,
        "apply assets before forced coordinator failure"
    );

    bool database_failed =
        assets_applied &&
        sqlite3_exec(
            database,
            "INSERT INTO rom("
            "    id, name, file_path"
            ") VALUES("
            "    91, 'new name', 'GB/new-name.gb'"
            ");",
            NULL,
            NULL,
            NULL
        ) == SQLITE_OK &&
        !play_activity_identity_transfer_roms(
            database,
            91,
            90,
            "GB",
            &identity,
            987654321,
            rollback_plan.source_file_path,
            "GB/new-name.gb"
        );

    check_condition(
        database_failed,
        "force database failure after asset rename"
    );

    if (outer_transaction_open) {
        sqlite3_exec(
            database,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL
        );
    }

    bool assets_rolled_back =
        rollback_transfer != NULL &&
        play_activity_asset_transfer_rollback(
            rollback_transfer
        );

    check_condition(
        assets_rolled_back &&
        access(old_save_path, F_OK) == 0 &&
        access(old_state_path, F_OK) == 0 &&
        access(new_save_path, F_OK) != 0,
        "coordinator failure restores save and state names"
    );

    check_condition(
        query_fixture_int(
            database,
            "SELECT COUNT(*) FROM rom WHERE id = 90;"
        ) == 1 &&
        query_fixture_int(
            database,
            "SELECT COUNT(*) FROM rom WHERE id = 91;"
        ) == 0 &&
        query_fixture_int(
            database,
            "SELECT COUNT(*) "
            "FROM play_activity "
            "WHERE rom_id = 90 AND play_time = 321;"
        ) == 1 &&
        query_fixture_int(
            database,
            "SELECT COUNT(*) "
            "FROM play_activity "
            "WHERE rom_id = 91;"
        ) == 0,
        "coordinator failure restores database state"
    );

    play_activity_asset_transfer_free(
        rollback_transfer
    );

    if (database != NULL)
        sqlite3_close(database);

    unlink(old_save_path);
    unlink(old_state_path);
    unlink(new_save_path);
    unlink(history_path);
    unlink(info_path);

    rmdir(states_directory);
    rmdir(saves_directory);
    rmdir(states_root);
    rmdir(saves_root);
    rmdir(cores_directory);
    rmdir(system_directory);
    rmdir(roms_root);
    rmdir(temporary_directory);
}
