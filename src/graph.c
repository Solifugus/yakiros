/*
 * graph.c - YakirOS graph resolution engine implementation
 */

#define _GNU_SOURCE
#include "graph.h"
#include "component.h"
#include "capability.h"
#include "log.h"
#include <time.h>
#include <signal.h>
#include <sys/types.h>

int graph_resolve(void) {
    int changes = 0;

    for (int i = 0; i < n_components; i++) {
        component_t *comp = &components[i];

        switch (comp->state) {
        case COMP_INACTIVE:
            if (requirements_met(comp)) {
                if (component_start(i) == 0) {
                    changes++;
                }
            }
            break;

        case COMP_READY_WAIT:
            /* Check if dependencies were lost while waiting for readiness */
            if (!requirements_met(comp)) {
                LOG_WARN("component '%s' dependencies lost while waiting for readiness", comp->name);
                comp->state = COMP_FAILED;
                if (comp->pid > 0) {
                    kill(comp->pid, SIGTERM);
                }
                changes++;
            }
            break;

        case COMP_ACTIVE:
            /* Check if dependencies were lost */
            if (!requirements_met(comp)) {
                comp->state = COMP_FAILED;
                for (int j = 0; j < comp->n_provides; j++) {
                    capability_withdraw(comp->provides[j]);
                }
                changes++;
            }
            break;

        case COMP_FAILED:
            /* Try to restart failed components if their dependencies are now met */
            if (requirements_met(comp)) {
                /* Only restart if enough time has passed since last restart */
                time_t now = time(NULL);
                if (now - comp->last_restart >= 5) { /* 5 second minimum between restarts */
                    LOG_INFO("attempting to restart failed component '%s'", comp->name);
                    comp->state = COMP_INACTIVE; /* Will be started on next iteration */
                    changes++;
                }
            }
            break;

        default:
            break;
        }
    }

    return changes;
}

void graph_resolve_full(void) {
    int iterations = 0;
    int changes;

    do {
        changes = graph_resolve();
        iterations++;
        if (iterations > n_components * 2) {
            LOG_ERR("graph resolution exceeded max iterations — possible cycle");
            break;
        }
    } while (changes > 0);

    LOG_INFO("graph stable after %d iterations (%d components, %d capabilities)",
             iterations, n_components, capability_count());
}