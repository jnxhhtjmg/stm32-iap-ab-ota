/**
 * @file  ab_api.h
 * @brief API the bootloader publishes for the running application.
 *
 * The table lives in CCM at AB_API_ADDR and is refreshed before every jump.
 * The application must check `magic` before calling anything.
 */
#ifndef AB_API_H
#define AB_API_H

#include "ab_config.h"

void ab_api_publish(void);

#endif /* AB_API_H */
