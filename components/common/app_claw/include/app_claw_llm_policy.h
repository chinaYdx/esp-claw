// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
/* URL is logged by upstream transports. Keep secrets out of URL and headers. */
static inline bool app_claw_llm_field_valid(const char *value, size_t capacity)
{
    if (!value || !memchr(value, 0, capacity)) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (*p < 32 || *p == 127) return false;
    return true;
}
static inline bool app_claw_llm_policy_valid(const char *url, size_t url_size,
                                            const char *model, size_t model_size,
                                            const char *key, size_t key_size)
{
    return app_claw_llm_field_valid(url, url_size) &&
           app_claw_llm_field_valid(model, model_size) &&
           app_claw_llm_field_valid(key, key_size) &&
           !strncmp(url, "https://", 8) && url[8] && model[0] &&
           !strpbrk(url, "@?# ");
}
