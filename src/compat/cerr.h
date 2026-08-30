// The one bridge between Error and errno, so a port stops inventing its own.
//
// Error::Closed is the case that needs care: on a write it is EPIPE, on a read
// it is end of input and must never reach errno at all. The b_* read family
// turns it into 0 or EOF before calling this.
#pragma once

#include "kernel/result.h"

int errno_of(Error e);
Error error_of(int e);

// Sets errno from e and returns -1, which is what most of Group B returns.
int fail_with(Error e);
