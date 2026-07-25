-- Holds one input from boot so MAME/Verilator traces stay aligned without
-- frame-to-instruction synchronization. Field selected via env var.
-- Ports/fields per MAME hh_rw5000.cpp mfootb: ":IN.0" Forward/Kick (+ dpad),
-- ":IN.1" Score/Status/Difficulty.
local port = os.getenv("GOLDEN_PORT")   -- e.g. ":IN.0"
local field = os.getenv("GOLDEN_FIELD") -- e.g. "Forward"
-- -autoboot_script runs once the machine (and its ioports) already exist,
-- after the initial machine reset has already gone by -- so we set the
-- field directly here rather than waiting on a prestart/reset notifier
-- (emu.register_prestart never fires this late; the notification it hooks
-- has already happened by the time an autoboot script loads).
if port and field and port ~= "" and field ~= "" then
    local f = manager.machine.ioport.ports[port].fields[field]
    f:set_value(1)
    emu.print_info("golden: holding " .. port .. "/" .. field)
end
