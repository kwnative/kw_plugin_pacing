/* SPDX-FileCopyrightText: 2026 N.Leiten
 * SPDX-License-Identifier: LicenseRef-KW-Game-Plugin
 */
/* One process-local native wait owner for rendered and managed bot instances.
 * Only zero-duration waits are changed; simulation clocks are untouched. */
#define WIN32_LEAN_AND_MEAN
#define KWENGINE_USE_RUNTIME_RESOLVER
#include <windows.h>
#include <mmsystem.h>
#include <kwf/plugin.h>
#include <kwengine/engine_address.h>
#include <kwpacing/integration/pacing_policy.h>
#define PACING_PLUGIN_ID "pacing"
#define PACING_CONFIG_FILE L"kw_pacing.ini"
#define PACING_CONFIG_SECTION L"pacing"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <kwengine/detail/engine_client.inc>
static const struct kwf_context *pacing_context;
static uintptr_t pacing_wait_address;
static struct kwpacing_policy pacing_policy;
typedef void(WINAPI *sleep_fn)(DWORD);
static sleep_fn original_sleep;
static unsigned char original_call[6];
static uint32_t zero_sleep_delay_ms, pacing_diagnostics, timer_requested;
static BOOL timer_active;
static volatile LONG owner_thread;
static LARGE_INTEGER frequency, window_start;
static uint64_t sleep_total_ticks, maximum_ticks, cpu_start;
static uint32_t sleep_call_count, zero_sleep_request_count;
static uint64_t thread_cpu(void)
{
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        return 0;
    return ((uint64_t)kernel.dwHighDateTime << 32) + kernel.dwLowDateTime + ((uint64_t)user.dwHighDateTime << 32) + user.dwLowDateTime;
}
static void WINAPI pacing_sleep(DWORD milliseconds)
{
    DWORD forwarded = (!milliseconds && zero_sleep_delay_ms) ? 1u : milliseconds;
    LARGE_INTEGER begin, end;
    if (!pacing_diagnostics) {
        original_sleep(forwarded);
        return;
    }
    LONG thread = (LONG)GetCurrentThreadId();
    LONG first = InterlockedCompareExchange(&owner_thread, thread, 0);
    if (first && first != thread) {
        original_sleep(forwarded);
        return;
    }
    QueryPerformanceCounter(&begin);
    if (!first) {
        window_start = begin;
        cpu_start = thread_cpu();
    }
    original_sleep(forwarded);
    QueryPerformanceCounter(&end);
    uint64_t ticks = (uint64_t)(end.QuadPart - begin.QuadPart);
    ++sleep_call_count;
    if (!milliseconds)
        ++zero_sleep_request_count;
    sleep_total_ticks += ticks;
    if (ticks > maximum_ticks)
        maximum_ticks = ticks;
    if (end.QuadPart - window_start.QuadPart >= frequency.QuadPart * 2) {
        char line[320];
        uint64_t cpu = thread_cpu();
        snprintf(
            line, sizeof(line),
            "WAIT elapsed_ms=%.3f calls=%u zero_requests=%u zero_forwarded_ms=%u sleep_total_ms=%.3f max_sleep_ms=%.3f thread_cpu_ms=%.3f timer1_active=%u",
            (double)(end.QuadPart - window_start.QuadPart) * 1000.0 / (double)frequency.QuadPart, sleep_call_count, zero_sleep_request_count,
            zero_sleep_delay_ms, (double)sleep_total_ticks * 1000.0 / (double)frequency.QuadPart,
            (double)maximum_ticks * 1000.0 / (double)frequency.QuadPart,
            cpu && cpu_start && cpu >= cpu_start ? (double)(cpu - cpu_start) / 10000.0 : -1.0, (unsigned)timer_active);
        pacing_context->host->log(pacing_context->owner, line);
        window_start = end;
        cpu_start = cpu;
        sleep_total_ticks = maximum_ticks = 0;
        sleep_call_count = zero_sleep_request_count = 0;
    }
}
static uint32_t KWF_CALL prepare(const struct kwf_context *ctx)
{
    wchar_t path[MAX_PATH];
    char line[192];
    struct kwengine_resolution resolution;
    if (ctx->role != KWF_ROLE_RENDERED && ctx->role != KWF_ROLE_HEADLESS)
        return KWF_UNSUPPORTED;
    pacing_context = ctx;
    DWORD length = GetModuleFileNameW((HMODULE)ctx->plugin_module, path, MAX_PATH);
    wchar_t *separator = length && length < MAX_PATH ? wcsrchr(path, L'\\') : NULL;
    if (!separator || (size_t)(separator + 1 - path) + sizeof(PACING_CONFIG_FILE) / sizeof(wchar_t) > MAX_PATH)
        return KWF_ERROR;
    memcpy(separator + 1, PACING_CONFIG_FILE, sizeof(PACING_CONFIG_FILE));
    uint32_t enabled = GetPrivateProfileIntW(PACING_CONFIG_SECTION, L"enabled", 1, path);
    zero_sleep_delay_ms = GetPrivateProfileIntW(PACING_CONFIG_SECTION, L"zero_sleep_delay_ms", 1, path);
    pacing_diagnostics = GetPrivateProfileIntW(PACING_CONFIG_SECTION, L"pacing_diagnostics", 0, path);
    timer_requested = GetPrivateProfileIntW(PACING_CONFIG_SECTION, L"timer1", 1, path);
    if (enabled > 1 || zero_sleep_delay_ms > 1 || pacing_diagnostics > 1 || timer_requested > 1)
        return KWF_UNSUPPORTED;
    const wchar_t *role_section = ctx->role == KWF_ROLE_HEADLESS ? L"pacing.headless" : L"pacing.rendered";
    enabled = GetPrivateProfileIntW(role_section, L"enabled", enabled, path);
    zero_sleep_delay_ms = GetPrivateProfileIntW(role_section, L"zero_sleep_delay_ms", zero_sleep_delay_ms, path);
    pacing_diagnostics = GetPrivateProfileIntW(role_section, L"pacing_diagnostics", pacing_diagnostics, path);
    timer_requested = GetPrivateProfileIntW(role_section, L"timer1", timer_requested, path);
    if (enabled > 1 || zero_sleep_delay_ms > 1 || pacing_diagnostics > 1 || timer_requested > 1)
        return KWF_UNSUPPORTED;
    if (!enabled) {
        ctx->host->log(ctx->owner, "disabled for this process role; native waits unchanged");
        return KWF_OK;
    }
    /* Shared engine discovery happens before this plugin mutates anything.
     * These reference IDs are resolved selectors, never candidate offsets. */
    if (!kwengine_client_bind(ctx) || !kwengine_resolve((const void *)ctx->game_image, ctx->game_image_size, &resolution))
        return KWF_UNSUPPORTED;
    const uintptr_t site = kwengine_va(0x5844b2U), sleep_import = kwengine_va(0xa0812cU);
    static const unsigned char signature[] = {0x6a, 0x00, 0xff, 0x15, 0x2c, 0x81, 0xa0, 0x00, 0xe8, 0xee, 0xc0, 0x00, 0x00,
                                              0x8b, 0xd8, 0x2b, 0x05, 0x6c, 0x4f, 0xbe, 0x00, 0x3b, 0xc7, 0x72, 0xe7};
    if (sizeof(original_sleep) != 4 || !kwengine_code_span(site, sizeof(signature)) || !kwengine_is_data(sleep_import, 4) ||
        kwengine_memcmp((const void *)site, signature, sizeof(signature)))
        return KWF_UNSUPPORTED;
    pacing_wait_address = site + 2;
    memcpy(original_call, (const void *)pacing_wait_address, sizeof(original_call));
    memcpy(&original_sleep, (const void *)sleep_import, sizeof(original_sleep));
    if (!original_sleep)
        return KWF_UNSUPPORTED;
    uint32_t result = ctx->host->reserve(ctx->owner, pacing_wait_address, 6);
    if (result != KWF_OK)
        return result;
    pacing_policy = (struct kwpacing_policy){sizeof(pacing_policy), zero_sleep_delay_ms ? 2u : 1u, 1};
    snprintf(line, sizeof(line), "native wait RVA=%08lx; zero waits forward Sleep(%lu); timer1=%lu",
             (unsigned long)(pacing_wait_address - ctx->game_image), (unsigned long)zero_sleep_delay_ms, (unsigned long)timer_requested);
    ctx->host->log(ctx->owner, line);
    return ctx->host->publish(ctx->owner, KWPACING_POLICY_SERVICE, 1, &pacing_policy);
}
static uint32_t KWF_CALL activate(void)
{
    if (!pacing_wait_address)
        return KWF_OK; /* Disabled role: no timer request and no patch. */
    unsigned char replacement[6] = {0xE8, 0, 0, 0, 0, 0x90};
    uint32_t relative = (uint32_t)(uintptr_t)pacing_sleep - (uint32_t)(pacing_wait_address + 5u);
    memcpy(replacement + 1, &relative, 4);
    owner_thread = 0;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        pacing_diagnostics = 0;
    QueryPerformanceCounter(&window_start);
    cpu_start = thread_cpu();
    if (timer_requested) {
        timer_active = timeBeginPeriod(1) == TIMERR_NOERROR;
        pacing_context->host->log(pacing_context->owner,
                                  timer_active ? "timer1 request succeeded" : "timer1 request failed; stock timer resolution retained");
    }
    uint32_t result = pacing_context->host->write(pacing_context->owner, pacing_wait_address, original_call, replacement, sizeof(replacement));
    if (result != KWF_OK && timer_active) {
        timeEndPeriod(1);
        timer_active = FALSE;
    }
    return result;
}
KWF_EXPORT const struct kwf_plugin *KWF_CALL KwPluginQuery(void)
{
    static const struct kwf_plugin plugin = {sizeof(plugin),  KWF_ABI_MAJOR,    KWF_ABI_MINOR, KWF_ROLE_RENDERED | KWF_ROLE_HEADLESS,
                                             KWF_CAP_PATCHES, PACING_PLUGIN_ID, prepare,       activate};
    return &plugin;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)module;
    (void)reserved;
    /* No startup work under the loader lock. Balance the optional process-
     * lifetime WinMM request when this resident test module is torn down. */
    if (reason == DLL_PROCESS_DETACH) {
        if (timer_active) {
            timeEndPeriod(1);
            timer_active = FALSE;
        }
    }
    return TRUE;
}
