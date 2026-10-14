// One version string, shared by the boot banner and the `version` program. The
// tail is the commit count and the short hash, in a header tools/version.py
// generates: 0.11.287-8c4740d.
#pragma once

#include "kernel/revision.h"
#include "str.h"

#define BRAAM_VERSION_BASE "0.11"

constexpr Str BRAAM_VERSION = BRAAM_VERSION_BASE "." BRAAM_REVISION;
