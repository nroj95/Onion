#ifndef PLAY_ACTIVITY_SCHEMA_H
#define PLAY_ACTIVITY_SCHEMA_H

#include <stdbool.h>
#include <sqlite3/sqlite3.h>

/* =============================================================================
 * purpose:
 * initialize and version the activity tracker identity schema.
 *
 * key behavior:
 * - adds identity tables to onion's existing activity database.
 * - leaves the original rom and play_activity tables unchanged.
 * - uses idempotent create statements so initialization is safe to repeat.
 * - records a schema version for future migrations.
 * =============================================================================
 */

#define PLAY_ACTIVITY_IDENTITY_SCHEMA_VERSION 1

bool play_activity_identity_schema_ensure(sqlite3 *database);

#endif
