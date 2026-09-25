-- FCEUX helper: replays a frame-numbered input file and dumps, after every
-- frame, the frame number + CPU RAM ($0000-$07FF). Optionally saves a
-- screenshot at one frame. Parameters come from environment variables:
--   REF_IN (input file), REF_N (frames), REF_OUT (binary log), REF_SHOT (frame)
local IN, N, OUT = os.getenv("REF_IN"), tonumber(os.getenv("REF_N")), os.getenv("REF_OUT")
local SHOT = tonumber(os.getenv("REF_SHOT") or "0")
local ev = {}
if IN and IN ~= "" then
  for line in io.lines(IN) do
    local f, b, s = line:match("(%d+) (%S+) (%d)")
    if f then f = tonumber(f); ev[f] = ev[f] or {}; table.insert(ev[f], {b, s == "1"}) end
  end
end
local map = {A="A",B="B",L="left",R="right",START="start",SELECT="select",UP="up",DOWN="down"}
local st = {}
local out = io.open(OUT, "wb")
local function u32(v) return string.char(v%256, math.floor(v/256)%256, math.floor(v/65536)%256, math.floor(v/16777216)%256) end
for f = 0, N do
  if ev[f-1] then for _, e in ipairs(ev[f-1]) do st[map[e[1]]] = e[2] or nil end end
  joypad.set(1, st)
  emu.frameadvance()
  local t = {u32(emu.framecount())}
  for a = 0, 0x7FF do t[#t+1] = string.char(memory.readbyte(a)) end
  out:write(table.concat(t))
  out:flush()
  if SHOT > 0 and emu.framecount() == SHOT then gui.savescreenshot() end
end
out:close()
emu.exit()
