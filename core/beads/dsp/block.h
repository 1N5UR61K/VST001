// Added for the desktop port: upper bound for the processing block size, used
// instead of variable-length arrays (not supported by MSVC).
#ifndef BEADS_DSP_BLOCK_H_
#define BEADS_DSP_BLOCK_H_
#include <cstddef>
namespace beads { const size_t kMaxBlock = 64; }
#endif
