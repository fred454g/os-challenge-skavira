#include "server_helpers.h"
#include "messages.h"

#include <sys/types.h>
#include <unistd.h>

// We have to take in the func call and guess
// [0-31] Sha256 what we have to reverse hash
// [32-39] minVal of guess.   [40-47]maxVal of guess.
// [48] prio least(0->16)most


