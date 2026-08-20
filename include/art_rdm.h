#pragma once

// Art-RDM proxy (Art-Net 4 §RDM) — RDM Phase 2.
//
// Lets an Art-Net controller (e.g. Obsidian Onyx) discover RDM devices on the
// board's physical RS-485 line, tunnelled over Art-Net:
//   ArtTodRequest / ArtTodControl -> RDM discovery on the wire -> ArtTodData
//   ArtRdm                        -> raw RDM transaction on the wire -> ArtRdm
//
// STATE, because this comment used to describe the finished feature: only the
// discovery half works. The ArtRdm transaction path does not, and would not
// survive contact with a real controller — ARTRDM_HDR_LEN counts one filler byte
// too many (copied from the ArtTodRequest layout, which really does have two),
// and the parser then requires a 0xCC start code that the spec says is not
// transmitted in the RdmPacket field at all. Both errors together mean every
// well-formed ArtRdm datagram is rejected inbound and every reply is malformed
// outbound. Beyond the header, the path is GET-only and covers two PIDs.
//
// So today a controller can obtain the UIDs on the segment and nothing else.
// See the notes at the offsets in art_rdm.cpp before changing them.
//
// The board acts as the RDM CONTROLLER on its RS-485 segment (bridge/output
// mode), so this needs a board with MCU-controlled DE/RE — currently the Axon
// (XDMX v1.4). Enabled via RAVLIGHT_MODULE_ARTRDM (implies RAVLIGHT_MODULE_
// DMX_PHYSICAL / esp_dmx); a no-op otherwise.
//
// RDM transactions block for milliseconds and drive the bus, so they must not
// run in the lwIP receive callback: artRdmHandlePacket() only copies the
// datagram into a queue and returns; a dedicated task performs the RDM work
// and sends the Art-Net reply.

#include <Arduino.h>

// Spin up the worker task + queue. Call once at init (guarded by the caller).
void artRdmInit();

// Called from the Art-Net UDP dispatcher for the RDM opcodes (0x8000 /
// 0x8200 / 0x8300). Copies the datagram + requester IP into the work queue
// and returns immediately. No-op when the module is disabled.
void artRdmHandlePacket(const uint8_t* buf, int len, const IPAddress& from);
