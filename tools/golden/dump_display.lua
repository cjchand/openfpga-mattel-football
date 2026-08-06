-- Dumps mfootb's pwm_display brightness levels, one CSV row per emulated
-- frame, for the display-parity test (see sim/display_parity_tb.cpp and
-- tools/golden/display_diff.py).
--
-- The driver is mfootb in src/mame/handheld/hh_rw5000.cpp (Rockwell B6100):
--     PWM_DISPLAY(config, m_display).set_size(9, 11)
--     m_display->set_bri_levels(0.02, 0.2)
-- pwm_display_device publishes each cell as output "y.x" (pwm.cpp's
-- m_out_x{*this, "%u.%u"}), valued 0/1/2 for off/dim/bright given those two
-- levels. y is the strobe row 0..8, x the segment line 0..10 -- the same
-- (col, line) indexing led_capture.v uses, so cell = y*11 + x maps directly
-- onto our levels[] bit pairs with no remap.
--
-- Input holding is the same mechanism as hold_input.lua: GOLDEN_PORT /
-- GOLDEN_FIELD name an ioport field to assert from boot.
--
-- Env vars:
--   GOLDEN_PORT, GOLDEN_FIELD -- input to hold (optional, e.g. ":IN.0"/"Forward")
--   GOLDEN_DISPLAY_OUT        -- CSV path (required)
--   GOLDEN_DISPLAY_SKIP       -- frames to discard before recording (default 120)

local out_path = os.getenv("GOLDEN_DISPLAY_OUT")
if not out_path or out_path == "" then
    error("dump_display.lua: GOLDEN_DISPLAY_OUT is not set")
end
local skip = tonumber(os.getenv("GOLDEN_DISPLAY_SKIP") or "") or 120

local port = os.getenv("GOLDEN_PORT")
local field = os.getenv("GOLDEN_FIELD")
if port and field and port ~= "" and field ~= "" then
    -- -autoboot_script runs after the machine and its ioports exist, so the
    -- field can be set directly (see hold_input.lua for why a prestart
    -- notifier does not work here).
    manager.machine.ioport.ports[port].fields[field]:set_value(1)
    emu.print_info("dump_display: holding " .. port .. "/" .. field)
end

local f = assert(io.open(out_path, "w"))
f:write("frame")
for y = 0, 8 do for x = 0, 10 do f:write(string.format(",%d.%d", y, x)) end end
f:write("\n")

local out = manager.machine.output
local n = 0

-- Frame notifier fires once per screenless-video update, which is also the
-- cadence pwm_display_device reclassifies on (set_refresh(attotime::from_hz(60))
-- in pwm.cpp's constructor), so every row here is one full frame of levels --
-- the same 60 Hz boundary led_capture.v's WINDOW=1167 ce ticks approximates.
emu.register_frame_done(function()
    n = n + 1
    if n <= skip then return end
    local row = { tostring(n) }
    for y = 0, 8 do
        for x = 0, 10 do
            row[#row + 1] = tostring(out:get_value(string.format("%d.%d", y, x)))
        end
    end
    f:write(table.concat(row, ",") .. "\n")
end)

-- Held so the notifier subscription is not garbage-collected mid-run.
-- emu.register_stop does not exist in MAME 0.288; this is the current name.
stop_sub = emu.add_machine_stop_notifier(function()
    f:close()
    emu.print_info(string.format("dump_display: wrote %d frames to %s",
                                 math.max(0, n - skip), out_path))
end)
