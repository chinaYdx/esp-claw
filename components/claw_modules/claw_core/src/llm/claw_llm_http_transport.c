/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "llm/claw_llm_http_transport.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#if defined(CONFIG_APP_CLAW_PETTI) && CONFIG_APP_CLAW_PETTI && \
    defined(CONFIG_PETTI_SIM_INPUT) && CONFIG_PETTI_SIM_INPUT
extern void petti_boot_memory_stage(const char *stage);
#define HTTP_MEMORY_STAGE(stage) petti_boot_memory_stage(stage)
#else
#define HTTP_MEMORY_STAGE(stage) ((void)0)
#endif

/* Request/response text is CPU-only payload. Petti keeps it out of the
 * internal DMA-capable heap used by Wi-Fi; the SDK client/driver owns its
 * transport buffers separately. Generic targets keep their original allocator. */
#if defined(CONFIG_APP_CLAW_PETTI) && CONFIG_APP_CLAW_PETTI
#include "esp_heap_caps.h"
#define HTTP_PAYLOAD_CALLOC(count, bytes) \
    heap_caps_calloc((count), (bytes), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define HTTP_PAYLOAD_REALLOC(pointer, bytes) \
    heap_caps_realloc((pointer), (bytes), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define HTTP_PAYLOAD_CALLOC(count, bytes) calloc((count), (bytes))
#define HTTP_PAYLOAD_REALLOC(pointer, bytes) realloc((pointer), (bytes))
#endif

static const char *TAG = "llm_http";

#define CLAW_LLM_HTTP_RB_INITIAL_CAP 4096

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} response_buffer_t;

typedef struct {
    response_buffer_t *buffer;
    volatile bool *abort_flag;
} http_request_context_t;

static inline bool abort_requested(const http_request_context_t *ctx)
{
    return ctx && ctx->abort_flag && *ctx->abort_flag;
}

static char *dup_printf(const char *fmt, ...)
{
    va_list args;
    va_list copy;
    int needed;
    char *buf;

    va_start(args, fmt);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return NULL;
    }

    buf = calloc(1, (size_t)needed + 1);
    if (!buf) {
        va_end(args);
        return NULL;
    }

    vsnprintf(buf, (size_t)needed + 1, fmt, args);
    va_end(args);
    return buf;
}

static char *sanitize_utf8_body_copy(const char *body)
{
    size_t src = 0;
    size_t dst = 0;
    size_t len = 0;
    char *sanitized = NULL;

    if (!body) {
        return NULL;
    }

    len = strlen(body);
    sanitized = HTTP_PAYLOAD_CALLOC(1, len + 1);
    if (!sanitized) {
        return NULL;
    }

    while (body[src]) {
        unsigned char c = (unsigned char)body[src];

        if (c < 0x80) {
            sanitized[dst++] = body[src++];
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (body[src + 1] && ((unsigned char)body[src + 1] & 0xC0) == 0x80) {
                sanitized[dst++] = body[src++];
                sanitized[dst++] = body[src++];
            } else {
                sanitized[dst++] = ' ';
                src++;
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (body[src + 1] && body[src + 2] &&
                    ((unsigned char)body[src + 1] & 0xC0) == 0x80 &&
                    ((unsigned char)body[src + 2] & 0xC0) == 0x80) {
                sanitized[dst++] = body[src++];
                sanitized[dst++] = body[src++];
                sanitized[dst++] = body[src++];
            } else {
                sanitized[dst++] = ' ';
                src++;
            }
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (body[src + 1] && body[src + 2] && body[src + 3] &&
                    ((unsigned char)body[src + 1] & 0xC0) == 0x80 &&
                    ((unsigned char)body[src + 2] & 0xC0) == 0x80 &&
                    ((unsigned char)body[src + 3] & 0xC0) == 0x80) {
                sanitized[dst++] = body[src++];
                sanitized[dst++] = body[src++];
                sanitized[dst++] = body[src++];
                sanitized[dst++] = body[src++];
            } else {
                sanitized[dst++] = ' ';
                src++;
            }
        } else {
            sanitized[dst++] = ' ';
            src++;
        }
    }

    sanitized[dst] = '\0';
    return sanitized;
}

static esp_err_t response_buffer_init(response_buffer_t *buffer)
{
    if (!buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    buffer->data = HTTP_PAYLOAD_CALLOC(1, CLAW_LLM_HTTP_RB_INITIAL_CAP);
    if (!buffer->data) {
        return ESP_ERR_NO_MEM;
    }

    buffer->cap = CLAW_LLM_HTTP_RB_INITIAL_CAP;
    buffer->len = 0;
    return ESP_OK;
}

static esp_err_t response_buffer_append(response_buffer_t *buffer, const char *data, size_t len)
{
    char *grown;
    size_t cap;

    if (!buffer || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    cap = buffer->cap;
    while (buffer->len + len + 1 > cap) {
        cap *= 2;
    }

    if (cap != buffer->cap) {
        grown = HTTP_PAYLOAD_REALLOC(buffer->data, cap);
        if (!grown) {
            return ESP_ERR_NO_MEM;
        }
        buffer->data = grown;
        buffer->cap = cap;
    }

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    buffer->data[buffer->len] = '\0';
    return ESP_OK;
}

static void response_buffer_free(response_buffer_t *buffer)
{
    if (!buffer) {
        return;
    }

    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_request_context_t *ctx = (http_request_context_t *)evt->user_data;

    if (!ctx) {
        return ESP_OK;
    }
    if (abort_requested(ctx)) {
        return ESP_FAIL;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        return response_buffer_append(ctx->buffer, (const char *)evt->data, evt->data_len);
    }

    return ESP_OK;
}

static char *build_auth_header_value(const char *auth_type, const char *api_key)
{
    const char *kind = auth_type ? auth_type : "bearer";

    if (!api_key || !api_key[0]) {
        return NULL;
    }
    if (strcmp(kind, "none") == 0) {
        return NULL;
    }
    if (strcmp(kind, "api-key") == 0) {
        return strdup(api_key);
    }

    return dup_printf("Bearer %s", api_key);
}

static const char *auth_header_name(const char *auth_type)
{
    if (auth_type && strcmp(auth_type, "api-key") == 0) {
        return "X-API-Key";
    }
    return "Authorization";
}

static char *parse_error_message_body(const char *body, int status)
{
    /* Remote errors can echo request credentials. Persist/log only status. */
    (void)body;
    return dup_printf("LLM HTTP status=%d", status);
}

esp_err_t claw_llm_http_post_json(const claw_llm_http_json_request_t *request,
                                  claw_llm_http_response_t *out_response,
                                  char **out_error_message)
{
    response_buffer_t buffer = {0};
    http_request_context_t request_ctx = {0};
    esp_http_client_config_t config = {0};
    esp_http_client_handle_t client = NULL;
    char *auth_header_value = NULL;
    char *sanitized_body = NULL;
    int status_code = 0;
    esp_err_t err;

    if (out_response) {
        memset(out_response, 0, sizeof(*out_response));
    }
    if (out_error_message) {
        *out_error_message = NULL;
    }
    if (!request || !request->url || !request->body || !out_response || !out_error_message) {
        return ESP_ERR_INVALID_ARG;
    }

    HTTP_MEMORY_STAGE("llm_http_begin");
    sanitized_body = sanitize_utf8_body_copy(request->body);
    if (!sanitized_body) {
        *out_error_message = dup_printf("Out of memory sanitizing HTTP request body");
        ESP_LOGE(TAG, "OOM sanitizing HTTP request body");
        return ESP_ERR_NO_MEM;
    }

    err = response_buffer_init(&buffer);
    if (err != ESP_OK) {
        *out_error_message = dup_printf("Out of memory allocating HTTP buffer");
        ESP_LOGE(TAG, "OOM allocating HTTP response buffer");
        goto cleanup;
    }

    HTTP_MEMORY_STAGE("llm_http_buffers");
    request_ctx.buffer = &buffer;
    request_ctx.abort_flag = request->abort_flag;
    config.url = request->url;
    config.event_handler = http_event_handler;
    config.user_data = &request_ctx;
    config.timeout_ms = request->timeout_ms;
    config.buffer_size = 4096;
    config.buffer_size_tx = 4096;
#if CONFIG_APP_CLAW_PETTI
    config.use_global_ca_store = true;
#else
    config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
#ifdef CONFIG_HTTP_REUSE_ENABLE
    config.keep_alive_enable = true;
#endif

    client = esp_http_client_init(&config);
    if (!client) {
        *out_error_message = dup_printf("Failed to create HTTP client");
        ESP_LOGE(TAG, "Failed to create HTTP client for %s", request->url);
        err = ESP_FAIL;
        goto cleanup;
    }

    HTTP_MEMORY_STAGE("llm_http_client");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    auth_header_value = build_auth_header_value(request->auth_type, request->api_key);
    if (auth_header_value) {
        esp_http_client_set_header(client, auth_header_name(request->auth_type), auth_header_value);
    }
    if (request->headers && request->header_count > 0) {
        size_t i;

        for (i = 0; i < request->header_count; i++) {
            const claw_llm_http_header_t *header = &request->headers[i];

            if (!header->name || !header->name[0] || !header->value) {
                continue;
            }
            esp_http_client_set_header(client, header->name, header->value);
        }
    }
    esp_http_client_set_post_field(client, sanitized_body, (int)strlen(sanitized_body));

    ESP_LOGD(TAG, "POST %s", request->url);
    HTTP_MEMORY_STAGE("llm_http_perform");
    err = esp_http_client_perform(client);
    HTTP_MEMORY_STAGE("llm_http_return");
    if (err != ESP_OK) {
        if (abort_requested(&request_ctx)) {
            *out_error_message = dup_printf("HTTP request aborted by caller");
            ESP_LOGW(TAG, "HTTP perform aborted: %s", esp_err_to_name(err));
            err = ESP_ERR_INVALID_STATE;
        } else {
            *out_error_message = dup_printf("HTTP request failed: %s", esp_err_to_name(err));
            ESP_LOGE(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
        }
        goto cleanup;
    }
    if (abort_requested(&request_ctx)) {
        *out_error_message = dup_printf("HTTP request aborted by caller");
        ESP_LOGW(TAG, "HTTP perform completed after caller abort");
        err = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    status_code = esp_http_client_get_status_code(client);
    ESP_LOGD(TAG, "HTTP status=%d", status_code);
    if (status_code != 200) {
        err = ESP_FAIL;
        *out_error_message = parse_error_message_body(buffer.data, status_code);
        ESP_LOGE(TAG, "LLM error: %s", *out_error_message ? *out_error_message : "(null)");
        goto cleanup;
    }

    out_response->status_code = status_code;
    out_response->body = buffer.data;
    buffer.data = NULL;
    err = ESP_OK;

cleanup:
    free(auth_header_value);
    free(sanitized_body);
    if (client) {
        /* request_ctx is on this stack frame; with http_reuse the handle outlives this call in the pool
         * and is closed later from another task, so it must not keep the handler or the pointer. */
        esp_http_client_set_event_handler(client, NULL);
        esp_http_client_set_user_data(client, NULL);
        esp_http_client_cleanup(client);
    }
    response_buffer_free(&buffer);
    return err;
}

void claw_llm_http_response_free(claw_llm_http_response_t *response)
{
    if (!response) {
        return;
    }

    free(response->body);
    memset(response, 0, sizeof(*response));
}
