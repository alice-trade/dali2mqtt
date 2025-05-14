#ifndef COMBINE_U64_TO_U32_H
#define COMBINE_U64_TO_U32_H
#include <stdint.h>

static uint64_t combine_u32_to_u64(uint32_t high, uint32_t low) {
    return ((uint64_t)high << 32) | low;
}
#endif //COMBINE_U64_TO_U32_H
