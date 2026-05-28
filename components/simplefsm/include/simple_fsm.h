#pragma once

#include <stdbool.h>

typedef struct simple_fsm simple_fsm_t;
typedef void (*simple_fsm_state_fn_t)(simple_fsm_t *fsm, void *context);

struct simple_fsm {
    simple_fsm_state_fn_t current_state;
    simple_fsm_state_fn_t next_state;
    bool running;
};

void simple_fsm_init(simple_fsm_t *fsm, simple_fsm_state_fn_t initial_state);
void simple_fsm_transition(simple_fsm_t *fsm, simple_fsm_state_fn_t next_state);
void simple_fsm_stop(simple_fsm_t *fsm);
void simple_fsm_step(simple_fsm_t *fsm, void *context);
void simple_fsm_run(simple_fsm_t *fsm, void *context);
