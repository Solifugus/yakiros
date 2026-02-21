/*
 * capability.c - YakirOS capability registry implementation
 *
 * Manages the registry of system capabilities and their providers.
 */

#include "capability.h"
#include "log.h"
#include <string.h>

/* Global capability registry */
static capability_t capabilities[MAX_CAPABILITIES];
static int n_capabilities = 0;

void capability_init(void) {
    memset(capabilities, 0, sizeof(capabilities));
    n_capabilities = 0;
}

int capability_index(const char *name) {
    for (int i = 0; i < n_capabilities; i++) {
        if (strcmp(capabilities[i].name, name) == 0)
            return i;
    }
    return -1; /* not found */
}

int capability_active(const char *name) {
    int idx = capability_index(name);
    return (idx >= 0 && capabilities[idx].active);
}

void capability_register(const char *name, int provider_idx) {
    int idx = capability_index(name);
    if (idx < 0) {
        /* Create new capability */
        if (n_capabilities >= MAX_CAPABILITIES) {
            LOG_ERR("capability limit reached");
            return;
        }
        idx = n_capabilities++;
        strncpy(capabilities[idx].name, name, MAX_NAME - 1);
    }
    capabilities[idx].active = 1;
    capabilities[idx].provider_idx = provider_idx;

    /* Note: We can't log the provider name here since we don't have access
     * to the components array. The caller should handle logging. */
}

void capability_withdraw(const char *name) {
    int idx = capability_index(name);
    if (idx >= 0) {
        capabilities[idx].active = 0;
        LOG_INFO("capability DOWN: %s", name);
    }
}

int capability_count(void) {
    return n_capabilities;
}

const char *capability_name(int idx) {
    if (idx >= 0 && idx < n_capabilities) {
        return capabilities[idx].name;
    }
    return NULL;
}

int capability_active_by_idx(int idx) {
    if (idx >= 0 && idx < n_capabilities) {
        return capabilities[idx].active;
    }
    return 0;
}

int capability_provider(int idx) {
    if (idx >= 0 && idx < n_capabilities) {
        return capabilities[idx].provider_idx;
    }
    return -1;
}