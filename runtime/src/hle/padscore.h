// padscore.h: the KPAD connection notification a game registers with KPADSetConnectCallback.
//
// The "Pro Controller" exists from the start here (it is the keyboard/host controllers acting as
// one, Input menu), so a registration queues the "connected" event; service_connect() delivers it
// to the guest callback on the game thread. main01 calls VPADRead every frame and the game polls
// KPADReadEx once it knows a Pro Controller is there, so both are safe delivery points.
#pragma once

struct Cpu;

namespace padscore_hle {
void service_connect(Cpu* c);
}
