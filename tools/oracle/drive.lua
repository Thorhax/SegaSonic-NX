-- MAME oracle for the port: run `sonic` headless and record, per frame,
-- the V60 PC and selected memory. Env:
--   S32_FRAMES  frames to run (exit after)
--   S32_SNAP    snapshot every N frames (0: none)
--   S32_DUMP    directory for raw work-RAM dumps every S32_SNAP frames
--   S32_LOG     per-frame log file (frame, PC, work-RAM checksum)
--   S32_INPUTS  input script as s32run --inputs ("A-B key=value"; coin1..3,
--               start1..3, button1..3; trackballs not driven here)
local last = tonumber(os.getenv("S32_FRAMES") or "0")
local every = tonumber(os.getenv("S32_SNAP") or "0")
local dump = os.getenv("S32_DUMP")
local logpath = os.getenv("S32_LOG")
local cpu = manager.machine.devices[":mainpcb:maincpu"]
local space = cpu.spaces["program"]
local screen = manager.machine.screens:at(1)
local log = logpath and io.open(logpath, "w")

local events = {}
local script = os.getenv("S32_INPUTS")
if script then
  for line in io.lines(script) do
    local a, b, k, v = line:match("^(%d+)%-(%d+)%s+(%w+)=(%-?%d+)")
    if a then table.insert(events, {tonumber(a), tonumber(b), k, tonumber(v)}) end
  end
end
local fields = {}
for _, port in pairs(manager.machine.ioport.ports) do
  for name, f in pairs(port.fields) do fields[name] = f end
end
local names = {coin1 = "Coin 1", coin2 = "Coin 2", coin3 = "Coin 3", start1 = "1 Player Start",
  start2 = "2 Players Start", start3 = "3 Players Start", button1 = "P1 Button 1",
  button2 = "P2 Button 1", button3 = "P3 Button 1"}
local function apply_inputs(frame)
  local on = {}
  for _, e in ipairs(events) do
    if frame >= e[1] and frame <= e[2] and names[e[3]] and e[4] ~= 0 then on[names[e[3]]] = true end
  end
  for _, n in pairs(names) do
    if fields[n] then fields[n]:set_value(on[n] and 1 or 0) end
  end
end

local function workram_sum()
  local s = 0
  for a = 0x200000, 0x20fffc, 4 do s = (s * 31 + space:read_u32(a)) & 0xffffffff end
  return s
end

emu.register_frame_done(function()
  local frame = screen:frame_number()
  apply_inputs(frame + 1) -- inputs for the frame about to run (s32run sets them before each frame)
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
