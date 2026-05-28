#include "simple_fsm.h"

void simple_fsm_init(simple_fsm_t *fsm, simple_fsm_state_fn_t initial_state)
{
    fsm->current_state = initial_state;
    fsm->next_state = initial_state;
    fsm->running = (initial_state != NULL);
}

void simple_fsm_transition(simple_fsm_t *fsm, simple_fsm_state_fn_t next_state)
{
    fsm->next_state = next_state;
}

void simple_fsm_stop(simple_fsm_t *fsm)
{
    fsm->running = false;
}

void simple_fsm_step(simple_fsm_t *fsm, void *context)
{
    if (!fsm->running || fsm->current_state == NULL) {
        fsm->running = false;
        return;
    }

    fsm->current_state(fsm, context);
    fsm->current_state = fsm->next_state;

    if (fsm->current_state == NULL) {
        fsm->running = false;
    }
}

void simple_fsm_run(simple_fsm_t *fsm, void *context)
{
    while (fsm->running) {
        simple_fsm_step(fsm, context);
    }
}
