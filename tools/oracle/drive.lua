-- MAME oracle for the port: run `sonic` headless and record, per frame,
-- the V60 PC and selected memory. Env:
--   S32_FRAMES  frames to run (exit after)
--   S32_SNAP    snapshot every N frames (0: none)
--   S32_DUMP    directory for raw work-RAM dumps every S32_SNAP frames
--   S32_LOG     per-frame log file (frame, PC, work-RAM checksum)
local last = tonumber(os.getenv("S32_FRAMES") or "0")
local every = tonumber(os.getenv("S32_SNAP") or "0")
local dump = os.getenv("S32_DUMP")
local logpath = os.getenv("S32_LOG")
local cpu = manager.machine.devices[":mainpcb:maincpu"]
local space = cpu.spaces["program"]
local screen = manager.machine.screens:at(1)
local log = logpath and io.open(logpath, "w")

local function workram_sum()
  local s = 0
  for a = 0x200000, 0x20fffc, 4 do s = (s * 31 + space:read_u32(a)) & 0xffffffff end
  return s
end

emu.register_frame_done(function()
  local frame = screen:frame_number()
  if log then
    log:write(string.format("%d %08x %08x\n", frame, cpu.state["PC"].value, workram_sum()))
  end
  if every > 0 and frame % every == 0 then
    manager.machine.video:snapshot()
    if dump then
      local f = io.open(string.format("%s/wram_%05d.bin", dump, frame), "wb")
      for a = 0x200000, 0x20ffff do f:write(string.char(space:read_u8(a))) end
      f:close()
    end
  end
  if last > 0 and frame >= last then
    if log then log:close() end
    manager.machine:exit()
  end
end)
