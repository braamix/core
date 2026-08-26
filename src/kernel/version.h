// One version string, shared by the boot banner and the `version` program. The
// tail is the commit count and the short hash, in a header tools/version.py
// generates: 0.7.200-b57a3dd.
#pragma once

#include "kernel/revision.h"
#include "str.h"

#define BRAAM_VERSION_BASE "0.7"

constexpr Str BRAAM_VERSION = BRAAM_VERSION_BASE "." BRAAM_REVISION;
