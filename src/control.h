/*
 * control.h - YakirOS control socket interface
 */

#ifndef CONTROL_H
#define CONTROL_H

#define CONTROL_SOCKET "/tmp/graph-resolver.sock"

/* Set up the control socket for graphctl commands */
int setup_control_socket(void);

/* Handle a control command from client */
void handle_control_command(int client_fd);

#endif /* CONTROL_H */