#ifndef PLAY_ACTIVITY_HISTORY_H
#define PLAY_ACTIVITY_HISTORY_H

#include <stdbool.h>
#include <stddef.h>

/* =============================================================================
 * purpose:
 * resolve the retroarch core used for a launched rom.
 *
 * key behavior:
 * - reads retroarch's content history playlist.
 * - normalizes the requested rom path before matching it.
 * - returns the exact core path recorded by retroarch.
 * =============================================================================
 */

bool play_activity_history_find_core_path(
    const char *history_path,
    const char *rom_path,
    char *core_path_out,
    size_t core_path_out_size
);

#endif
