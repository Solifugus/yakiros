/*
 * capability.h - YakirOS capability registry
 *
 * Manages system capabilities that components provide and require.
 * Tracks which capabilities are active and which components provide them.
 */

#ifndef CAPABILITY_H
#define CAPABILITY_H

#define MAX_CAPABILITIES 512
#define MAX_NAME 128

/* Capability structure */
typedef struct {
    char name[MAX_NAME];
    int  active;         /* 1 if this capability is currently provided */
    int  provider_idx;   /* index into components array */
} capability_t;

/* Find a capability by name, return index (-1 if not found) */
int capability_index(const char *name);

/* Check if a capability is currently active */
int capability_active(const char *name);

/* Register a capability as provided by a component */
void capability_register(const char *name, int provider_idx);

/* Withdraw a capability (component stopped providing it) */
void capability_withdraw(const char *name);

/* Get total number of registered capabilities */
int capability_count(void);

/* Get capability name by index (for iteration) */
const char *capability_name(int idx);

/* Check if capability is active by index */
int capability_active_by_idx(int idx);

/* Get provider component index for a capability */
int capability_provider(int idx);

/* Initialize/reset the capability registry */
void capability_init(void);

#endif /* CAPABILITY_H */