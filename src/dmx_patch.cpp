#include "core/dmx_patch.h"

// Returns the DMX value for a single channel identified by its fixture-local id.
// Offset within a section is the sum of count fields of preceding channels
// that share the same section index.
uint8_t getChannelById(const patch_state_t* state, const uint8_t* dmxBuf, uint8_t id) {
    const personality_t* pers = &state->table[state->personality_idx];
    uint16_t sec_off[8] = {0};
    for (uint8_t i = 0; i < pers->n_channels; i++) {
        const dmx_channel_t* ch = &pers->channels[i];
        if (ch->id == id)
            return dmxBuf[state->section_start[ch->section] + sec_off[ch->section]];
        sec_off[ch->section] += ch->count;
    }
    return 0;
}

// Returns a pointer to the first byte of a multi-channel block and writes
// the block size (in channels) to out_count. Returns nullptr if id not found.
const uint8_t* getChannelBlockById(const patch_state_t* state, const uint8_t* dmxBuf,
                                   uint8_t id, uint8_t* out_count) {
    const personality_t* pers = &state->table[state->personality_idx];
    uint16_t sec_off[8] = {0};
    for (uint8_t i = 0; i < pers->n_channels; i++) {
        const dmx_channel_t* ch = &pers->channels[i];
        if (ch->id == id) {
            if (out_count) *out_count = ch->count;
            return &dmxBuf[state->section_start[ch->section] + sec_off[ch->section]];
        }
        sec_off[ch->section] += ch->count;
    }
    if (out_count) *out_count = 0;
    return nullptr;
}

// Returns the default_val for a channel id within a personality descriptor.
uint8_t getChannelDefault(const personality_t* pers, uint8_t id) {
    for (uint8_t i = 0; i < pers->n_channels; i++) {
        if (pers->channels[i].id == id)
            return pers->channels[i].default_val;
    }
    return 0;
}

