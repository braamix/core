// Sys::Poll: which of several descriptors is ready, answered without reading
// or writing one (System_Calls.md §8, Concept.md §4.3).
#pragma once

#include "proctab.h"

// Waits until one of the descriptors the payload names is ready, until the
// timeout passes, or until a signal abandons the call. Appends a u32 revents
// for each pair to `reply` and reports how many of them are non-zero, which is
// the call's status: 0 is the timeout.
//
// Every descriptor it names is held for the length of the call, in the
// direction it was named — a second user is Err(Busy) and nothing is taken.
Task<Result<usize>> poll_wait(Proc &p, Str payload, String &reply);
