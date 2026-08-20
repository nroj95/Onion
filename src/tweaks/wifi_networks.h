#ifndef TWEAKS_WIFI_NETWORKS_H__
#define TWEAKS_WIFI_NETWORKS_H__

#include <stdbool.h>
#include <stddef.h>

#include "utils/str.h"

#define SAVED_CONFIG_PATH "/mnt/SDCARD/.tmp_update/config/wifi_saved_networks.conf"

#define MAX_SAVED_NETWORKS 64
#define MAX_WIFI_CONFIG_SIZE (256 * 1024)
#define MIN_NETWORK_PRIORITY -999
#define MAX_NETWORK_PRIORITY 999

typedef struct WifiNetwork {
    char ssid[STR_MAX];
    size_t block_start;
    size_t block_end;
    bool has_priority;
    int priority;
} WifiNetwork;

typedef struct WifiConfig {
    char text[MAX_WIFI_CONFIG_SIZE + 1];
    size_t text_length;
    WifiNetwork networks[MAX_SAVED_NETWORKS];
    int network_count;
} WifiConfig;

typedef enum SaveCurrentNetworkResult {
    SAVE_CURRENT_NETWORK_SUCCESS,
    SAVE_CURRENT_NETWORK_ALREADY_SAVED,
    SAVE_CURRENT_NETWORK_NOT_CONNECTED,
    SAVE_CURRENT_NETWORK_AMBIGUOUS,
    SAVE_CURRENT_NETWORK_FAILED
} SaveCurrentNetworkResult;

int count_networks_with_ssid(
    const WifiConfig *config,
    const char *ssid);

bool load_wifi_config(
    const char *path,
    WifiConfig *config);

bool get_connected_network_ssid(
    char *ssid,
    size_t ssid_size);

SaveCurrentNetworkResult save_current_network(
    char *saved_ssid,
    size_t saved_ssid_size);

bool get_saved_network_priority(
    int network_index,
    int *priority);

bool update_saved_network_priority(
    int network_index,
    int priority);

bool delete_saved_network(
    int network_index);

bool apply_saved_networks(void);

#endif // TWEAKS_WIFI_NETWORKS_H__
