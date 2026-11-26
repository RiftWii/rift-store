// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "riftwii/plugin_api.h"

const rwp_host& StandaloneHost();
void* StoreMem2Low();
void* ReserveStoreMem2(std::size_t bytes);
void ReleaseStoreMem2(void* begin, void* end);
