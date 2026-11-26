#ifndef ADAOS_ADASCRIPT_EVENT_H
#define ADAOS_ADASCRIPT_EVENT_H

#include "as_common.h"

// Event listeners for AdaScript: "on <event> [get <name> ...] do ... endon"
// records a block of statements and runs it when the event happens instead of
// where the line stands. keypress and cursormove belong to screen mode; exit
// works in both modes and runs once when Ctrl+E stops the script. cursormove
// runs when an arrow key asks to move the cursor, before it moves, so the
// handler can "cancel" the move and keep the cursor where it is.

struct as_env;

/* The events a listener can wait for. */
enum as_event_kind {
    AS_EVENT_KEYPRESS = 0,
    AS_EVENT_CURSORMOVE,
    AS_EVENT_EXIT,
    AS_EVENT_COUNT,
};

/* Most variables an event hands to its handler: the cursor's row and column. */
#define AS_EVENT_PARAMS 2

/* One registered handler. Its body is a slice of the script's prepared lines. */
struct as_listener {
    int defined;
    int kind;  // enum as_event_kind
    int running;  // set while its own body runs, so it cannot trigger itself
    int param_count;
    char params[AS_EVENT_PARAMS][AS_NAME_MAX];
    int body_start;  // first line of the body
    int body_end;    // last line of the body, before body_start when empty
    int line;        // where the "on" line was, for error messages
    int col;
};

/* Is this line a keypress or cursormove listener? Returns the statement to
 * name in a mode error, or 0 for anything else. */
const char* as_event_cli_conflict(const char* stmt);

/* Record the handler an "on ..." line declares. An unknown event, a bad
 * variable name or the wrong number of variables is reported through the
 * environment. */
void as_event_register(struct as_env* env, const char* stmt, int body_start,
                       int body_end);

/* Does the script wait for an event a key or a cursor move can deliver? */
int as_event_has_waiters(const struct as_env* env);

/* Wait for events until Ctrl+E stops the script. */
int as_event_loop(struct as_env* env);

/* Run the exit handler, if the script registered one. */
void as_event_dispatch_exit(struct as_env* env);

#endif /* ADAOS_ADASCRIPT_EVENT_H */
