local values, outputs, device_values, saved, loadouts = {}, {}, {}, {}, {}
local screen = {}
local screen_at = {}
local now = 0

controller = {
  DPAD_UP=0, DPAD_DOWN=1, DPAD_LEFT=2, DPAD_RIGHT=3,
  A=4, B=5, X=6, Y=7, LB=8, RB=9, VIEW=10, MENU=11, LS=12, RS=13,
  LX=16, LY=17, RX=18, RY=19, LT=20, RT=21
}
function controller.get_val(id) return values[id] or 0 end
function controller.set_val(id, value) outputs[id] = value end
function controller.get_millis() return now end
function controller.cancel_macros() end
function controller.macro_running() return false end
function controller.start_macro() end
function controller.stop_macro() end

device = {BTN_UP=0, BTN_DOWN=1, BTN_SELECT=2, BTN_BACK=3}
function device.get_val(id) return device_values[id] or 0 end
function device.config_status() return 0 end
function device.config_error() return 0 end
function device.config_request() return true end
function device.config_reset() return true end

display = {}
function display.clear() screen = {}; screen_at = {} end
function display.draw_text(x, y, text)
  screen[y] = text
  screen_at[x .. ':' .. y] = text
end
led = {set=function() end}

storage = {}
function storage.read(key) return saved[key] end
function storage.write(key, value) saved[key] = value return true end
function storage.commit() return true end
local function loadout_key(side, operator, weapon)
  return side .. ':' .. operator .. ':' .. weapon
end
function storage.read_loadout(side, operator, weapon)
  local profile = loadouts[loadout_key(side, operator, weapon)]
  if not profile then return nil end
  return table.unpack(profile)
end
function storage.write_loadout(side, operator, weapon, ...)
  loadouts[loadout_key(side, operator, weapon)] = {...}
  return true
end
function storage.reset_loadouts()
  loadouts = {}
  return true
end

dofile('scripts/default.lua')
local function frame(controller_changes, device_changes)
  now = now + 25
  outputs = {}
  for id, value in pairs(controller_changes or {}) do values[id] = value end
  for id, value in pairs(device_changes or {}) do device_values[id] = value end
  on_input()
end
local function release_all()
  for id in pairs(values) do values[id] = 0 end
  for id in pairs(device_values) do device_values[id] = 0 end
  frame()
end

on_console_connected()
frame({[controller.VIEW]=100, [controller.B]=100})
assert(screen[0] == 'SELECT GAME', screen[0])
assert(screen_at['0:8'] == '>', screen_at['0:8'])
assert(screen_at['6:8'] == 'R6<', screen_at['6:8'])
assert(screen_at['6:16'] == 'RUST', screen_at['6:16'])
assert(screen_at['6:24'] == 'CONFIG', screen_at['6:24'])
release_all()
frame(nil, {[device.BTN_BACK]=100}); release_all()
frame(nil, {[device.BTN_BACK]=100}); release_all()
assert(screen[24] == 'CONFIG<', screen[24])
assert(screen_at['0:24'] == '>', screen_at['0:24'])
assert(screen_at['6:24'] == 'CONFIG<', screen_at['6:24'])
frame(nil, {[device.BTN_UP]=100}); release_all()
frame(nil, {[device.BTN_BACK]=100}); release_all()
frame(nil, {[device.BTN_BACK]=100}); release_all()
assert(screen_at['0:24'] == '>', screen_at['0:24'])
assert(screen_at['6:24'] == 'QUICK SELECT OFF<', screen_at['6:24'])
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(saved.quick_select == true)
assert(screen_at['6:24'] == 'QUICK SELECT ON<', screen_at['6:24'])
frame(nil, {[device.BTN_BACK]=100}); release_all()
assert(screen_at['0:8'] == '>', screen_at['0:8'])
assert(screen_at['6:8'] == 'SAB ON<', screen_at['6:8'])
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(saved.sab_enabled == false)
assert(screen_at['6:8'] == 'SAB OFF<', screen_at['6:8'])

-- The operator list is navigation-only, even while the side prefix is held.
on_console_connected()
frame({[controller.VIEW]=100, [controller.B]=100})
assert(screen[0] == '>SMOKE<', screen[0])
frame({[controller.Y]=100})
assert(saved.qs2_y == nil)
assert(screen[0] == '>SMOKE<', screen[0])
release_all()

-- Select Kaid, enter his configuration, and bind/rebind within that page.
for _ = 1, 21 do
  frame(nil, {[device.BTN_BACK]=100}); release_all()
end
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(screen[0] == 'DEF > KAID', screen[0])
assert(screen_at['0:8'] == '>', screen_at['0:8'])
assert(screen_at['6:8'] == 'WEAPON<', screen_at['6:8'])
assert(screen[8] == 'PRIMARY', screen[8])
assert(screen_at['116:0'] == nil, tostring(screen_at['116:0']))
frame({[controller.VIEW]=100, [controller.RT]=100})
assert(saved.qs2_rt == 22, tostring(saved.qs2_rt))
assert(screen_at['116:0'] == 'RT', screen_at['116:0'])
release_all()
frame({[controller.VIEW]=100, [controller.X]=100})
assert(saved.qs2_x == 22, tostring(saved.qs2_x))
assert(saved.qs2_rt == nil, tostring(saved.qs2_rt))
assert(screen_at['122:0'] == 'X', screen_at['122:0'])
release_all()

-- Bind the same X button independently to attacker Twitch.
on_console_connected()
frame({[controller.VIEW]=100, [controller.X]=100})
assert(screen[0] == '>SLEDGE<', screen[0])
release_all()
for _ = 1, 4 do
  frame(nil, {[device.BTN_BACK]=100}); release_all()
end
assert(screen[0] == '>TWITCH<', screen[0])
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(screen[0] == 'ATT > TWITCH', screen[0])
assert(screen_at['0:8'] == '>', screen_at['0:8'])
assert(screen_at['6:8'] == 'WEAPON<', screen_at['6:8'])
assert(screen_at['122:0'] == nil, tostring(screen_at['122:0']))
frame({[controller.VIEW]=100, [controller.X]=100})
assert(saved.qs1_x == 5, tostring(saved.qs1_x))
assert(saved.qs2_x == 22, tostring(saved.qs2_x))
assert(screen_at['122:0'] == 'X', screen_at['122:0'])
release_all()

-- Right enters edit, Select increments, Back decrements, Right returns to scroll.
frame(nil, {[device.BTN_BACK]=100}); release_all()
assert(screen_at['0:16'] == '>', screen_at['0:16'])
assert(screen_at['6:16'] == 'VERTICAL<', screen_at['6:16'])
assert(screen_at['6:8'] == 'WEAPON' and
  screen_at['6:24'] == 'HORIZONTAL')
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(screen_at['6:16'] == 'VERTICAL', screen_at['6:16'])
frame(nil, {[device.BTN_SELECT]=100}); release_all()
assert(screen[16] == '>1<', screen[16])
frame(nil, {[device.BTN_SELECT]=100}); release_all()
assert(screen[16] == '>2<', screen[16])
frame(nil, {[device.BTN_BACK]=100}); release_all()
assert(screen[16] == '>1<', screen[16])
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(screen_at['0:16'] == '>', screen_at['0:16'])
assert(screen_at['6:16'] == 'VERTICAL<', screen_at['6:16'])
frame(nil, {[device.BTN_BACK]=100}); release_all()
assert(screen_at['0:24'] == '>', screen_at['0:24'])
assert(screen_at['6:8'] == 'WEAPON' and
  screen_at['6:16'] == 'VERTICAL' and
  screen_at['6:24'] == 'HORIZONTAL<')
frame(nil, {[device.BTN_BACK]=100}); release_all()
assert(screen_at['0:8'] == '>', screen_at['0:8'])
assert(screen_at['6:8'] == 'MOVEMENT<', screen_at['6:8'])

-- Boolean fields toggle directly from the list without entering edit mode.
for _ = 1, 2 do
  frame(nil, {[device.BTN_BACK]=100}); release_all()
end
assert(screen_at['0:24'] == '>', screen_at['0:24'])
assert(screen_at['6:24'] == 'RAPID FIRE<', screen_at['6:24'])
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(screen[24] == 'ON', screen[24])
assert(screen_at['0:24'] == '>', screen_at['0:24'])

-- READY is the final field; binding status remains in the header.
for _ = 1, 4 do
  frame(nil, {[device.BTN_BACK]=100}); release_all()
end
assert(screen_at['0:8'] == '>', screen_at['0:8'])
assert(screen_at['6:8'] == 'READY<', screen_at['6:8'])
assert(screen[8] == 'SAVE AND START', screen[8])
assert(screen_at['122:0'] == 'X', screen_at['122:0'])

-- Prefix selects the side; the second X resolves that side's mapping.
dofile('scripts/default.lua')
on_console_connected()
frame({[controller.VIEW]=100, [controller.X]=100})
frame({[controller.X]=0})
frame({[controller.X]=100})
assert(screen[0] == '>SLEDGE<', screen[0])
frame()
assert(screen[0] == 'TWITCH PRIMARY', screen[0])
assert(screen[8] == 'V1 H0 M80 DZ12', screen[8])
assert(screen[24] == 'BOUND: X', screen[24])
release_all()
frame(nil, {[device.BTN_DOWN]=100}); release_all()
assert(screen[0] == 'ATT > TWITCH', screen[0])
assert(screen_at['0:8'] == '>', screen_at['0:8'])
assert(screen_at['6:8'] == 'WEAPON<', screen_at['6:8'])
frame(nil, {[device.BTN_DOWN]=100}); release_all()
assert(screen[0] == '>TWITCH<', screen[0])
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(screen[0] == 'ATT > TWITCH', screen[0])
frame(nil, {[device.BTN_UP]=100, [device.BTN_DOWN]=100}); release_all()
assert(screen[0] == 'TWITCH PRIMARY', screen[0])
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(screen[0] == 'R6 > SIDE', screen[0])
assert(screen_at['0:8'] == '>', screen_at['0:8'])
assert(screen_at['6:8'] == 'ATTACKERS<', screen_at['6:8'])
assert(screen_at['6:16'] == 'DEFENDERS', screen_at['6:16'])
release_all()
on_console_connected()
frame({[controller.VIEW]=100, [controller.B]=100})
frame({[controller.B]=0})
frame({[controller.X]=100})
assert(screen[0] == '>SMOKE<', screen[0])
frame()
assert(screen[0] == 'KAID PRIMARY', screen[0])
assert(screen[24] == 'BOUND: X', screen[24])
release_all()
frame({[controller.VIEW]=100})
assert(outputs[controller.VIEW] == nil, tostring(outputs[controller.VIEW]))
frame()
assert(outputs[controller.VIEW] == nil, tostring(outputs[controller.VIEW]))
release_all()
frame({[controller.MENU]=100})
assert(outputs[controller.MENU] == nil, tostring(outputs[controller.MENU]))
frame()
assert(outputs[controller.MENU] == nil, tostring(outputs[controller.MENU]))
-- Reset all profiles must require confirmation and clear persisted loadouts.
loadouts[loadout_key(1, 1, 1)] = {42, 0, 80, 12, false, false, 100}
on_console_connected()
frame(nil, {[device.BTN_BACK]=100}); release_all()
frame(nil, {[device.BTN_BACK]=100}); release_all()
frame(nil, {[device.BTN_UP]=100}); release_all()
for _ = 1, 4 do
  frame(nil, {[device.BTN_BACK]=100}); release_all()
end
assert(screen_at['6:16'] == 'RESET ALL PROFILES<', screen_at['6:16'])
frame(nil, {[device.BTN_UP]=100}); release_all()
assert(screen[0] == 'RESET ALL PROFILES?', screen[0])
assert(screen[8] == 'SELECT: CONFIRM', screen[8])
assert(screen[16] == 'BACK: CANCEL', screen[16])
frame(nil, {[device.BTN_SELECT]=100}); release_all()
assert(loadouts[loadout_key(1, 1, 1)] == nil)
print('PASS: quick-select menu, selection, and binding flows')
