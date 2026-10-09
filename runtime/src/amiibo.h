// Virtual amiibo (hle/nfp.cpp): a figure is put on the GamePad's reader from Input > Tap amiibo or the settings
// overlay's Controls tab, and stays there until the game has read it. WWHD_AMIIBO=<figure>@<frame>[,...] for tests
// (TV frames; wolf_link, link, zelda, ganondorf).
#pragma once

namespace amiibo {
// the figures TP HD tells apart (by character: any Link is Link, Sheik is Zelda)
enum Figure : int { kWolfLink, kLink, kZelda, kGanondorf, kFigures };
const char* name(int figure);
void tap(int figure);
int on_reader();  // the figure waiting on the reader, or -1
void service();   // frame loop: the WWHD_AMIIBO taps
}  // namespace amiibo
