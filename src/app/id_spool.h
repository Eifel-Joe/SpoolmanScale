#pragma once

// ============================================================
//  A SPOOL ON SCREEN WITHOUT A TAG
//
//  Typed in by its id (ui/spool_id_pad.h) and shown like a scanned one, so
//  everything that works on the spool on screen - weight, location, AMS,
//  drying, labels - works on it too. Two things differ from a tag:
//
//  - Nothing is saved on its own. Automatic weighing keys on a tag; an id
//    typed by hand is one digit away from another spool, so the weight goes
//    out only when "Update weight" is tapped.
//  - It needs its own end, since no tag can leave. It ends ID_SPOOL_LIFTED_MS
//    after the spool that was put on the pad is taken off again (shorter than
//    the minute a lifted tag gets, see app_config.h, and never while a screen
//    covers the main one), when nothing is put on within ID_SPOOL_IDLE_MS,
//    when the clear button in the status line is tapped, and the moment a tag
//    turns up and takes over. Without a load cell only the last two apply.
//  - Its lift raises the questions a lifted tag gets (location, AMS, bay),
//    from idSpoolTakeLift() in the loop. The AMS question only follows a
//    weight sent by hand, by the first rule above.
// ============================================================

bool idSpoolActive();

// Loop task only: fetches the spool and shows it, see the header comment.
void idSpoolAdopt(int spool_id);

// From the loop: ends the state on the events above.
void idSpoolTick();

// True once when the spool shown has just been taken off the pad, with what
// the pad carried while it lay there. The loop raises the questions a lifted
// tag gets from this.
bool idSpoolTakeLift(float* load_g);
