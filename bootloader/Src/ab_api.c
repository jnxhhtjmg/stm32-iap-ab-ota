/**
 * @file  ab_api.c
 * @brief Implementation of the confirm/state/request API.
 *
 * These functions execute from bootloader flash while the application runs.
 * Only appends are allowed here: an erase would stall the CPU for ~250 ms and
 * could trip the application's watchdog, so compaction stays in the boot path.
 */
#include "ab_api.h"
#include "ab_meta.h"
#include "stm32f4xx.h"

static uint32_t api_confirm(void)
{
    ab_meta_rec_t latest;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if (ab_meta_load(&latest) && latest.state == AB_ST_TRIAL) {
        latest.state = AB_ST_CONFIRMED;
        latest.attempts = 0u;
        latest.flags = 0u;
        if (latest.version > latest.min_version) {
            latest.min_version = latest.version;
        }
        latest.sec_flags |= AB_SEC_SIGNED;
        if (ab_meta_append(&latest, 0) != 0) {
            __set_PRIMASK(primask);
            return 1u;
        }
    }
    __set_PRIMASK(primask);
    return 0u;
}

static uint32_t api_state(void)
{
    ab_meta_rec_t latest;

    if (ab_meta_load(&latest)) {
        return latest.state;
    }
    return AB_ST_NONE;
}

static uint32_t api_active_slot(void)
{
    ab_meta_rec_t latest;

    if (ab_meta_load(&latest)) {
        return latest.slot;
    }
    return AB_SLOT_A;
}

static uint32_t api_running_version(void)
{
    ab_meta_rec_t latest;

    if (ab_meta_load(&latest)) {
        return latest.version;
    }
    return 0u;
}

static uint32_t api_request_update(void)
{
    *(volatile uint32_t *)(uintptr_t)AB_REQ_ADDR = AB_REQ_MAGIC;
    __DSB();
    NVIC_SystemReset();
    return 0u;
}

void ab_api_publish(void)
{
    ab_api_t *api = (ab_api_t *)(uintptr_t)AB_API_ADDR;

    api->magic = 0u;
    api->version = AB_API_VERSION;
    api->confirm = api_confirm;
    api->state = api_state;
    api->active_slot = api_active_slot;
    api->request_update = api_request_update;
    api->running_version = api_running_version;
    __DSB();
    __ISB();
    api->magic = AB_API_MAGIC;
    __DSB();
}
