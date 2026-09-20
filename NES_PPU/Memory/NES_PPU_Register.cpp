///   Copyright 2016 Xma1
///
///   This file is part of NES-C#.
///
///   NES-C# is free software: you can redistribute it and/or modify
///   it under the terms of the GNU General Public License as published by
///   the Free Software Foundation, either version 3 of the License, or
///   (at your option) any later version.
///
///   NES-C# is distributed in the hope that it will be useful,
///   but WITHOUT ANY WARRANTY; without even the implied warranty of
///   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
///   See the GNU General Public License for more details.
///
///   You should have received a copy of the GNU General Public License
///   along with NES-C#. If not, see http://www.gnu.org/licenses/.
#include "NES_PPU_Register.h"
#include "NES_Memory.h"
#include "NES_Register.h"
#include "Interrupt.h"
#include "NES_PPU_Memory.h"
#include "../OAM/NES_PPU_OAM.h"
#include "../NES_PPU_Folder/NES_PPU.h"
#include <cstdlib>
#include <iostream>

namespace NES
{
    // FIXED (was a preserved C# bug, now corrected - found live via Chip 'n
    // Dale still corrupting its MMC1 shift register/scheduler state even
    // after this session's separate NES_Console::RenderFrame() fix moved
    // frame composition onto the CPU thread's own real-cycle-driven cadence
    // - see that fix's own comment): per
    // http://wiki.nesdev.com/w/index.php/NMI ("Race Condition Bug"), "If
    // the PPU is currently in vertical blank, and the PPUSTATUS ($2002)
    // vblank flag is still set (1), changing the NMI flag in bit 7 of $2000
    // from 0 to 1 will immediately generate an NMI" - that 0-to-1-transition
    // -while-still-in-vblank case is the *only* time writing PPUCTRL should
    // ever touch Interrupt::NMI() at all. Writing 0 has no interrupt side
    // effect whatsoever (it only gates whether a *future* real vblank event
    // is allowed to raise NMI - see NES_Console::RenderFrame() ->
    // NES_PPU::Display()'s own `if (PPUCTRL.V()) Interrupt::NMI(true);`
    // gate), and re-writing 1 while it was already 1 (not a real 0->1
    // transition) must not re-fire either.
    //
    // The old code called `Interrupt::NMI(v)` completely unconditionally on
    // *every* PPUCTRL write, for either value of v, regardless of the
    // previous bit-7 state and regardless of PPUSTATUS's real vblank flag.
    // Almost every real NES game (this one included) rewrites PPUCTRL once
    // per frame (e.g. to pick a nametable/pattern-table bank) while simply
    // leaving bit 7 set the whole time - each such write requested a brand
    // new, immediate NMI, with no relationship to real vblank timing at
    // all. Traced live: on this ROM this fired roughly every 800-900
    // instructions - an order of magnitude more often than a real
    // ~29780-cycle/frame NMI cadence, and (the same root-cause/symptom
    // class as the UI-driven trigger this session already fixed) far more
    // often than real hardware would ever interrupt one of the game's
    // unprotected 5-write MMC1 shift-register bank-select sequences,
    // corrupting them and its cooperative task-scheduler's own zero-page
    // bookkeeping - a permanently black screen with music running many
    // times too fast.
    void PPUCTRLFlags::V(bool v)
    {
        bool wasEnabled = V();
        adress->value(static_cast<uint8_t>(adress->value() & ~0x80));
        if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x80));

        if (v && !wasEnabled && NES_PPU_Register::PPUSTATUS.V())
            Interrupt::NMI(true);
    }

    long NES_PPU_Register::hudClearFollowupTraceRemaining = 0;
    PPUCTRLFlags NES_PPU_Register::PPUCTRL;
    PPUMASKFlags NES_PPU_Register::PPUMASK;
    PPUSTATUSFlags NES_PPU_Register::PPUSTATUS;
    std::shared_ptr<AddressSetup> NES_PPU_Register::OAMADDR;
    std::shared_ptr<AddressSetup> NES_PPU_Register::OAMDATA;
    std::shared_ptr<AddressSetup> NES_PPU_Register::PPUSCROLL;
    std::shared_ptr<AddressSetup> NES_PPU_Register::PPUADDR;
    std::shared_ptr<AddressSetup> NES_PPU_Register::PPUDATA;
    std::shared_ptr<AddressSetup> NES_PPU_Register::OAMDMA;
    uint16_t NES_PPU_Register::PPUPCADDR = 0x0000;
    uint8_t NES_PPU_Register::ppuAddrHighLatch = 0x00;

    NES_PPU_Register::NES_PPU_Register()
    {
        // The C# original set OAMADDR/OAMDATA/PPUSCROLL/PPUADDR/PPUDATA/OAMDMA
        // via static field initializers (running at type-init time, relying on
        // NES_Memory already being constructed). C++ has no equivalent
        // ordering guarantee across translation units, so those become
        // explicit INIT methods here, run from this constructor - same as
        // PPUCTRL/PPUMASK/PPUSTATUS already were in the C#.
        INITOAMADDR();
        INITOAMDATA();
        INITPPUSCROLL();
        INITPPUADDR();
        INITPPUDATA();
        INITOAMDMA();
        INITPPUCTRL();
        INITPPUMASK();
        INITPPUSTATUS();
    }

    void NES_PPU_Register::INITOAMADDR() { OAMADDR = NES_Memory::Memory[0x2003]; }
    void NES_PPU_Register::INITOAMDATA() { OAMDATA = NES_Memory::Memory[0x2004]; }

    void NES_PPU_Register::INITPPUSCROLL()
    {
        PPUSCROLL = NES_Memory::Memory[0x2005];
        PPUSCROLL->AfterSet([](uint8_t value) { NES_PPU::Scroll(value); });
    }

    void NES_PPU_Register::INITPPUMASK()
    {
        PPUMASK.adress = NES_Memory::Memory[0x2001];
    }

    // FIXED (was a preserved C# bug, now corrected - the deeper root cause
    // behind the same Lion King/AxROM segfault INITPPUDATA()'s own FIXED
    // note below describes): per http://wiki.nesdev.com/w/index.php/PPU_scrolling,
    // $2006's internal address latch is only 14 bits ($0000-$3FFF) - real
    // hardware's first write specifically clears bit 14 as part of loading
    // the new high byte. This just shifted the *entire* previous 16-bit
    // PPUPCADDR left by 8 and OR'd in the new byte with no masking at all,
    // so two consecutive $2006 writes could - and, live, did - leave
    // PPUPCADDR holding a value with bits set far above $3FFF (observed:
    // $8100), which INITPPUDATA()'s $2007 handler then indexed
    // NES_PPU_Memory::Memory with directly. INITPPUDATA()'s own fix masks
    // *after* auto-incrementing, but that can't help an address that was
    // already out of range *before* a single $2007 access ever happened -
    // this is the actual source of the value, so it needs its own mask.
    //
    // FIXED (new design, not a C# port - see INITPPUSTATUS()'s own FIXED
    // note for the bug this closes, part of the same fix): this used to
    // treat *every* $2006 write identically - "shift PPUPCADDR left 8 and
    // OR in the new byte" - with no concept of "which of the two writes is
    // this" beyond raw call count, unlike $2005's handler (NES_PPU::Scroll()),
    // which already tracked that explicitly via NES_PPU::ScrollXoY. Real
    // hardware shares *one* write-toggle `w` between $2005 and $2006 -
    // http://wiki.nesdev.com/w/index.php/PPU_scrolling ("$2005 and $2006
    // share a common write toggle w") - so this now uses that same shared
    // flag: the first write (ScrollXoY true) latches the high byte and
    // flips the toggle; the second (false) combines it with the low byte
    // into PPUPCADDR (still masked to 14 bits, per the FIXED note above)
    // and flips the toggle back. This is what makes it possible for
    // INITPPUSTATUS()'s $2002 read handler to reset *only* that shared
    // toggle and still correctly resynchronize which write ($2005's X/Y or
    // $2006's high/low byte) comes next, matching real hardware exactly,
    // instead of needing to also reset $2006's own address state as a
    // side effect.
    void NES_PPU_Register::INITPPUADDR()
    {
        PPUADDR = NES_Memory::Memory[0x2006];
        PPUADDR->AfterSet([](uint8_t value)
        {
            if (NES_PPU::ScrollXoY)
            {
                ppuAddrHighLatch = value;
            }
            else
            {
                PPUPCADDR = static_cast<uint16_t>(((static_cast<uint16_t>(ppuAddrHighLatch) << 8) | value) & 0x3FFF);
                // FIXED (real bug, found via Tiny Toon Adventures' actual
                // scanline-IRQ status-bar-split handler - not the vblank-time
                // $FD69 routine a PREVIOUS attempt at this same fix
                // (NES_PPU::SetScrollFromPPUAddr(), reverted earlier this
                // session - see git history/session notes for that
                // investigation) mistakenly chased, which really was just an
                // incidental PPUADDR reset. The REAL handler only runs when
                // the MMC3 scanline IRQ actually fires mid-frame (confirmed
                // live: traced the CPU jumping to it, $FA22, exactly when
                // NES_TRACE_MMC3_IRQLATCH shows the IRQ servicing) and does
                // the textbook nesdev split-scroll resync idiom - see
                // http://wiki.nesdev.com/w/index.php/PPU_scrolling ("Note
                // that if the game is going to change the whole scroll
                // register, ... $2006 twice will set ... only 14 of the 15
                // bits... "): write $2006 twice (a coarse/nametable-select
                // reset) immediately followed by $2005 twice (fine
                // position) - relying on real hardware sharing ONE 15-bit
                // internal register (`t`) between $2000/$2005/$2006, where
                // $2006's FIRST byte's bits 2-3 land on the SAME
                // nametable-select bits (t bits 10-11) that PPUCTRL's bits
                // 0-1 (N) also write - http://wiki.nesdev.com/w/index.php/PPU_scrolling
                // ("t: ...BA.. ........ <- d: ......BA" for $2000; "t:
                // .FEDCBA ........ <- d: ..FEDCBA" for $2006's first write,
                // which includes those same BA/nametable-select bits at
                // t's bits 10-11). This port models nametable-select only
                // via PPUCTRL.N() (read by AddxScroll()/AddyScroll(), see
                // NES_PPU.Scroll.cpp) with no unified t/v register, so a
                // real $2006 write's effect on nametable-select was
                // entirely missing - only $2000 writes could ever change
                // it. Fixed by propagating the already-latched first byte's
                // bits 2-3 into the same PPUCTRL.N() bits $2000 itself
                // writes (via its existing raw setter, which pokes the
                // stored $2000 byte directly without re-triggering that
                // register's own write hook/side effects - matches real
                // hardware's write-only PPUCTRL exactly: nothing observable
                // by the CPU changes besides N()'s only consumers,
                // AddxScroll()/AddyScroll()), applied once the address is
                // actually committed (this write, not the first), then
                // re-deriving xScroll/yScroll immediately so a same-scanline
                // IRQ split takes effect on the very row it targets -
                // consistent with RecomputeXScroll()/RecomputeYScroll()'s
                // own existing reasoning for a late PPUCTRL change.
                NES_PPU_Register::PPUCTRL.N(static_cast<uint8_t>((ppuAddrHighLatch >> 2) & 0x3));
                NES_PPU::RecomputeXScroll();
                NES_PPU::RecomputeYScroll();
            }
            NES_PPU::ScrollXoY = !NES_PPU::ScrollXoY;
        });
    }

    void NES_PPU_Register::INITOAMDMA()
    {
        OAMDMA = NES_Memory::Memory[0x4014];
        OAMDMA->AfterSet([](uint8_t value) { NES_PPU_OAM::OAMDMA(value); });
    }

    // FIXED (was a preserved C# bug, now corrected - a real segfault, found
    // live via The Lion King/AxROM once the NMI-reentrancy fix in
    // NES.Memory/Interrupt.cpp let it actually run far enough to hit this):
    // per http://wiki.nesdev.com/w/index.php/PPU_scrolling, the PPU's VRAM
    // address is a 14-bit value ($0000-$3FFF) that wraps/mirrors at $4000 -
    // but PPUCTRL.I() (the $2007 auto-increment step) can be 32, not just 1,
    // so PPUPCADDR can jump straight *past* $4000 without ever landing on
    // it exactly (e.g. $3FF0 + 32 = $4010) - a common, correct thing for a
    // game to do when writing a contiguous column of nametable/attribute
    // bytes. The old `if (PPUPCADDR == 0x4000) PPUPCADDR = 0;` only caught
    // the lucky case where a write happened to land exactly on the
    // boundary; any overshoot left PPUPCADDR pointing past
    // NES_PPU_Memory::Memory's actual size, and indexing it via operator[]
    // (no bounds check, unlike C#'s safe IndexOutOfRangeException - see
    // this port's other "vector isn't C#'s array" fixes) segfaulted on the
    // very next $2007 access. Fixed by masking to 14 bits (real hardware's
    // actual wraparound), which also degrades gracefully back to the old
    // exact-$4000 behavior for the common +1 case.
    void NES_PPU_Register::INITPPUDATA()
    {
        PPUDATA = NES_Memory::Memory[0x2007];
        PPUDATA->AfterSet([](uint8_t value)
        {
            // TEMPORARY diagnostic aid - opt-in via NES_TRACE_PPUDATA, off
            // by default. Traces every real $2007 write's target address
            // and value, to answer directly (instead of inferring from
            // snapshots) whether/where a game's nametable-streaming code is
            // actually writing.
            if (std::getenv("NES_TRACE_PPUDATA"))
                std::cerr << "[PPUDATA] $" << std::hex << PPUPCADDR << " = $" << static_cast<int>(value)
                          << " PC=0x" << NES_Register::PC << std::dec << std::endl;
            // TEMPORARY diagnostic aid - opt-in via NES_TRACE_HUDCLEAR_FOLLOWUP,
            // off by default. See hudClearFollowupTraceRemaining's own
            // comment (NES_PPU_Register.h) - arms a live instruction trace
            // the instant Tiny Toon Adventures' own code clears the first
            // byte of its status-bar nametable row to 0, so the *next* few
            // hundred real opcodes (whichever PRG bank is actually mapped
            // in at that exact moment) can be read off directly, instead of
            // a separate memory dump that risks reading a since-swapped bank.
            if (std::getenv("NES_TRACE_HUDCLEAR_FOLLOWUP") && PPUPCADDR == 0x2380 && value == 0)
                NES_PPU_Register::hudClearFollowupTraceRemaining = 400;
            NES_PPU_Memory::Memory[PPUPCADDR]->Value(value);
            PPUPCADDR = static_cast<uint16_t>((PPUPCADDR + (PPUCTRL.I() ? 32 : 1)) & 0x3FFF);
        });
        PPUDATA->BeforGet([]() { PPUDATA->value(NES_PPU_Memory::Memory[PPUPCADDR]->value()); });
    }

    void NES_PPU_Register::INITPPUCTRL()
    {
        PPUCTRL.adress = NES_Memory::Memory[0x2000];
        // FIXED - see NES_PPU::XScroll(int)'s own FIXED note for the full
        // story: a $2000 write can change PPUCTRL's nametable-select bits
        // (N, bits 0-1) *after* the $2005 write it logically belongs with
        // in the same frame (this is exactly what Chip 'n Dale's own NMI
        // handler does), so xScroll/yScroll - already computed against the
        // *previous* value of those bits - need re-deriving here too, not
        // just from a fresh $2005 write.
        PPUCTRL.adress->AfterSet([](uint8_t) {
            PPUCTRL.V(PPUCTRL.V());
            NES_PPU::RecomputeXScroll();
            NES_PPU::RecomputeYScroll();
        });
    }

    // FIXED (was a preserved C# bug, now corrected - found live via Chip 'n
    // Dale continuing to desync its background from its own (correct)
    // collision map during horizontal scrolling even after this session's
    // separate per-scanline rendering fix - see NES_PPU::RenderBackgroundScanline()'s
    // own comment): per http://wiki.nesdev.com/w/index.php/PPU_registers,
    // reading $2002 "clears the address latch used by $2005/$2006" - i.e.
    // real hardware only ever resets the shared write-*toggle*, `w`
    // (NES_PPU::ScrollXoY - see INITPPUADDR()'s own FIXED note on why
    // $2006 now shares it with $2005). It does *not* touch the actual
    // scroll position or VRAM address those writes set.
    //
    // This called PPUSCROLLRESSET() (two $2005 writes of 0 - see its own
    // definition) and `PPUADDR->Value(0)` (a $2006 write of 0) here -
    // real CPU-visible *writes*, each running through the exact same side
    // effects a game's own code would trigger, which zeroed the game's
    // live xScroll/yScroll and corrupted (or, depending on timing,
    // zeroed) an in-progress $2006 address write. Harmless as long as
    // reading $2002 stayed rare/incidental, but the whole point of a
    // scanline-accurate PPU is that real `LDA $2002 / BPL loop`
    // vblank-wait idioms - and mid-scroll polling loops - now actually run
    // at their intended cadence, making this fire far more often and far
    // more damagingly (a game polling $2002 while mid-scroll would have
    // its scroll silently zeroed on every single read). Fixed to reset
    // only the toggle, matching real hardware exactly.
    void NES_PPU_Register::INITPPUSTATUS()
    {
        PPUSTATUS.adress = NES_Memory::Memory[0x2002];
        PPUSTATUS.adress->AfterGet([]()
        {
            // TEMPORARY diagnostic aid - opt-in via NES_TRACE_2002POLL, off
            // by default. Logs every $2002 read's raw value (bit6=S,
            // bit7=V) and scanline, to see whether/when Tiny Toon
            // Adventures polls for sprite-0-hit as part of its status-bar
            // draw sequence.
            if (std::getenv("NES_TRACE_2002POLL"))
                std::cerr << "[2002poll] value=0x" << std::hex << static_cast<int>(PPUSTATUS.adress->value())
                           << std::dec << " scanline=" << NES_PPU::CurrentScanline()
                           << " PC=0x" << std::hex << NES_Register::PC << std::dec << std::endl;
            PPUSTATUS.adress->value(static_cast<uint8_t>(PPUSTATUS.adress->value() & 0x7F));
            NES_PPU::ScrollXoY = true;
        });
    }

    void NES_PPU_Register::InitialAtPower()
    {
        PPUCTRL.adress->Value(0);
        PPUMASK.adress->Value(0);
        PPUSTATUS.adress->Value(0xA0);
        OAMADDR->Value(0);
        PPUSCROLLRESSET();
        // Explicitly resynchronized to a known toggle state first (rather
        // than relying on the toggle's parity already being "true" here by
        // coincidence), then written twice - same reasoning as
        // PPUSCROLLRESSET() above, guaranteeing PPUPCADDR ends up exactly
        // 0 regardless of whatever state a previous run left the shared
        // write-toggle in. See INITPPUADDR()'s own FIXED note: a single
        // write here would, depending on that state, either not touch
        // PPUPCADDR at all (if it were consumed as the *first* of a pair)
        // or combine 0 with a stale latched high byte (if the *second*) -
        // neither reliably 0.
        NES_PPU::ScrollXoY = true;
        PPUADDR->Value(0);
        PPUADDR->Value(0);
        PPUDATA->Value(0);
    }

    void NES_PPU_Register::InitialOnReset()
    {
        PPUCTRL.adress->Value(0);
        PPUMASK.adress->Value(0);
        PPUSTATUS.adress->Value(static_cast<uint8_t>(PPUSTATUS.adress->value() & 0x80));
        PPUSCROLLRESSET();
        PPUDATA->Value(0);
    }

    void NES_PPU_Register::PPUSCROLLRESSET()
    {
        PPUSCROLL->Value(0);
        PPUSCROLL->Value(0);
    }
}
