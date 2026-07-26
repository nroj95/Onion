#ifndef PLAY_ACTIVITY_IDENTITY_TESTS_H
#define PLAY_ACTIVITY_IDENTITY_TESTS_H

/* =============================================================================
 * purpose:
 * expose split play activity test groups to the shared test runner.
 *
 * key behavior:
 * - declares only test groups already moved into separate source files.
 * - avoids changing linkage for tests still kept in test_identity.c.
 * =============================================================================
 */

void test_raw_identity(void);
void test_single_file_zip_identity(void);
void test_cue_identity(void);
void test_m3u_identity(void);
void test_identity_schema_storage(void);
void test_identity_rom_merge(void);
void test_history_core_lookup(void);

#endif
