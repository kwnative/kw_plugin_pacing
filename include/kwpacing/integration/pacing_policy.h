/* SPDX-FileCopyrightText: 2026 N.Leiten
 * SPDX-License-Identifier: LicenseRef-KW-Game-Plugin
 */
#ifndef KWPACING_POLICY_H
#define KWPACING_POLICY_H
#include <stdint.h>
#define KWPACING_POLICY_SERVICE "kwpacing.policy"
/* Startup-only immutable metadata. 0=off, 1=observe Sleep(0), 2=Sleep(1).
 * Consumers still discover/exclude the owned call; this grants no write rights. */
struct kwpacing_policy {
    uint32_t size, mode, engine_prepared;
};
#endif
