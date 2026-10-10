-- MAME instruction trace of the V60 for the first S32_FRAMES frames, in the
-- format `s32run --trace` writes (r0 r1 r2 sp before each instruction).
-- Needs -debug -debugger none. Env: S32_TRACE (output file), S32_FRAMES.
local dbg = manager.machine.debugger
local out = os.getenv("S32_TRACE")
local last = tonumber(os.getenv("S32_FRAMES") or "8")
dbg:command('trace ' .. out .. ',:mainpcb:maincpu,noloop,{tracelog "%08X %08X %08X %08X ",r0,r1,r2,sp}')
dbg:command('go')
local screen = manager.machine.screens:at(1)
emu.register_frame_done(function()
  if screen:frame_number() >= last then dbg:command('trace off') manager.machine:exit() end
end)
