/*
 * graph.h - YakirOS graph resolution engine
 */

#ifndef GRAPH_H
#define GRAPH_H

/* Single pass graph resolution, returns number of state changes */
int graph_resolve(void);

/* Iterate graph resolution until stable */
void graph_resolve_full(void);

#endif /* GRAPH_H */