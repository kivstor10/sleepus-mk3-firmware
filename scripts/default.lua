local r6_enabled = R6_ENABLED ~= false
local bf6_enabled = BF6_ENABLED ~= false

local attackers = {
	"SLEDGE", "THATCHER", "ASH", "THERMITE", "TWITCH", "MONTAGNE",
	"GLAZ", "FUZE", "BLITZ", "IQ", "BUCK", "BLACKBEARD", "CAPITAO",
	"HIBANA", "JACKAL", "YING", "ZOFIA", "DOKKAEBI", "LION", "FINKA",
	"MAVERICK", "NOMAD", "GRIDLOCK", "NOKK", "AMARU", "KALI", "IANA",
	"ACE", "ZERO", "FLORES", "OSA", "SENS", "GRIM", "STRIKER",
	"BRAVA", "RAM", "DEIMOS", "RAUORA", "SNAKE"
}

local defenders = {
	"SMOKE", "MUTE", "CASTLE", "PULSE", "DOC", "ROOK", "KAPKAN",
	"TACHANKA", "JAGER", "BANDIT", "FROST", "VALKYRIE", "CAVEIRA",
	"ECHO", "MIRA", "LESION", "ELA", "VIGIL", "MAESTRO", "ALIBI",
	"CLASH", "KAID", "MOZZIE", "WARDEN", "GOYO", "WAMAI", "ORYX",
	"MELUSI", "ARUNI", "THUNDERBIRD", "THORN", "AZAMI", "SOLIS",
	"FENRIR", "TUBARAO", "SENTRY", "SKOPOS", "DENARI", "NOOR"
}

local fields = {
	"SIDE", "OPERATOR", "WEAPON", "VERTICAL", "HORIZONTAL", "MOVEMENT",
	"DEAD ZONE", "RAPID FIRE", "FIRST BULLET", "FIRST BULLET %",
	"TEA BAG", "TEA MS", "SHAIKO LEAN", "READY"
}

local bf6_fields = {
	"WEAPON", "VERTICAL", "HORIZONTAL", "ACTIVE", "WIDTH", "HEIGHT",
	"SPEED", "RAPID FIRE", "HIP RAPID", "SAVE & START"
}

local bf6_rt_threshold = 9

local bf6_loadout_names = {
	"ASSAULT", "ENGINEER", "SUPPORT", "RECON", "WILDCARD"
}

local game_options = {}
if r6_enabled then game_options[#game_options + 1] = {level = 1, label = "R6"} end
if bf6_enabled then game_options[#game_options + 1] = {level = 2, label = "BF6"} end
game_options[#game_options + 1] = {level = 3, label = "CONFIG"}

local quick_select_buttons = {
	{controller.A, "qs_a", "A"}, {controller.B, "qs_b", "B"},
	{controller.X, "qs_x", "X"}, {controller.Y, "qs_y", "Y"},
	{controller.LB, "qs_lb", "LB"}, {controller.RB, "qs_rb", "RB"},
	{controller.DPAD_UP, "qs_du", "DPAD UP"},
	{controller.DPAD_DOWN, "qs_dd", "DPAD DOWN"},
	{controller.DPAD_LEFT, "qs_dl", "DPAD LEFT"},
	{controller.DPAD_RIGHT, "qs_dr", "DPAD RIGHT"},
	{controller.LS, "qs_ls", "LS"}, {controller.RS, "qs_rs", "RS"},
	{controller.LT, "qs_lt", "LT"}, {controller.RT, "qs_rt", "RT"}
}

local function quick_binding_key(side, button)
	return "qs" .. side .. "_" .. button[2]:sub(4)
end

local quick_storage_migrated = false
for _, button in ipairs(quick_select_buttons) do
	local binding = storage.read(button[2])
	if type(binding) == "number" then
		local side = math.floor(binding / 100)
		local operator = binding % 100
		local operators = side == 1 and attackers or defenders
		if (side == 1 or side == 2) and operator >= 1 and operator <= #operators then
			storage.write(quick_binding_key(side, button), operator)
		end
		storage.write(button[2], nil)
		quick_storage_migrated = true
	end
end
if quick_storage_migrated then storage.commit() end

local function quick_binding_name(side, operator)
	for _, button in ipairs(quick_select_buttons) do
		if storage.read(quick_binding_key(side, button)) == operator then
			return button[3]
		end
	end
	return "NONE"
end

local state = {
	configuring = true,
	menu_level = 0,
	game = 1,
	config_action = 1,
	config_status = 0,
	quick_select_enabled = storage.read("quick_select") == true,
	sab_enabled = storage.read("sab_enabled") ~= false,
	quick_previous = {},
	quick_view_previous = false,
	quick_navigation = false,
	quick_chord_active = false,
	quick_consumed = false,
	quick_open_side = 0,
	quick_open_operator = 0,
	last_operator = {1, 1},
	field = 1,
	field_editing = false,
	side = 1,
	operator = 1,
	weapon = 1,
	vertical = 0,
	horizontal = 0,
	movement = 80,
	deadzone = 12,
	rapid_fire = false,
	first_bullet = false,
	first_bullet_percent = 100,
	tea_bag = false,
	tea_bag_ms = 30,
	shaiko_lean = false,
	shaiko_dir = 0,
	previous_device = {false, false, false, false},
	device_repeat_at = {0, 0, 0, 0},
	previous_y = false,
	y_pressed_at = 0,
	previous_down = false,
	down_pressed_at = 0,
	down_tap_release = false,
	previous_rt = false,
	first_bullet_until = 0,
	bf6_configuring = true,
	bf6_selecting_loadout = true,
	bf6_loadout = 1,
	bf6_weapon = 1,
	bf6_field = 1,
	bf6_field_editing = false,
	bf6_angle = 0,
	bf6_last_aim_ms = 0,
	bf6_previous_y = false,
	bf6_settings_dirty = false,
	display_ready = false
}

local loadout_profiles = {{}, {}}

local function loadout_profile()
	local side_profiles = loadout_profiles[state.side]
	local operator_profiles = side_profiles[state.operator]
	if not operator_profiles then
		operator_profiles = {}
		side_profiles[state.operator] = operator_profiles
	end
	local profile = operator_profiles[state.weapon]
	if not profile then
		local vertical, horizontal, movement, deadzone, rapid_fire,
			first_bullet, first_bullet_percent =
			storage.read_loadout(state.side, state.operator, state.weapon)
		profile = vertical and {
			vertical = vertical, horizontal = horizontal,
			movement = movement, deadzone = deadzone,
			rapid_fire = rapid_fire, first_bullet = first_bullet,
			first_bullet_percent = first_bullet_percent
		} or {
			vertical = 0, horizontal = 0, movement = 80, deadzone = 12,
			rapid_fire = false, first_bullet = false,
			first_bullet_percent = 100
		}
		operator_profiles[state.weapon] = profile
	end
	return profile
end

local function save_loadout_profile()
	local profile = loadout_profile()
	profile.vertical = state.vertical
	profile.horizontal = state.horizontal
	profile.movement = state.movement
	profile.deadzone = state.deadzone
	profile.rapid_fire = state.rapid_fire
	profile.first_bullet = state.first_bullet
	profile.first_bullet_percent = state.first_bullet_percent
	storage.write_loadout(state.side, state.operator, state.weapon,
		state.vertical, state.horizontal, state.movement, state.deadzone,
		state.rapid_fire, state.first_bullet, state.first_bullet_percent)
end

local function load_loadout_profile()
	local profile = loadout_profile()
	state.vertical = profile.vertical
	state.horizontal = profile.horizontal
	state.movement = profile.movement
	state.deadzone = profile.deadzone
	state.rapid_fire = profile.rapid_fire
	state.first_bullet = profile.first_bullet
	state.first_bullet_percent = profile.first_bullet_percent
end

local rapid_fire_macro = {
	{btn = controller.RT, val = 100, wait = 20},
	{btn = controller.RT, val = 0,   wait = 20}
}

-- Tea bag: B press/release while d-pad DOWN held >=250ms (Veritas QT_TEA_BAG_LOL)
-- [1].wait and [2].wait updated from state.tea_bag_ms before each start
local tea_bag_steps = {
	{btn = controller.B, val = 100, wait = 30},
	{btn = controller.B, val = 0,   wait = 30},
}

-- Shaiko lean left (d-pad LEFT + ADS): RS first, then RS+LS simultaneously
-- (Veritas ShaikoLeanLeft combo: 90ms RS only, 100ms RS+LS, 300ms RS+LS)
local shaiko_lean_left_steps = {
	{duration_ms = 90,  values = {[controller.RS] = 100}},
	{duration_ms = 100, values = {[controller.RS] = 100, [controller.LS] = 100}},
	{duration_ms = 300, values = {[controller.RS] = 100, [controller.LS] = 100}},
}

-- Shaiko lean right (d-pad RIGHT + ADS): LS first, then LS+RS simultaneously
-- (Veritas ShaikoLeanRight combo: 90ms LS only, 100ms LS+RS, 300ms LS+RS)
local shaiko_lean_right_steps = {
	{duration_ms = 90,  values = {[controller.LS] = 100}},
	{duration_ms = 100, values = {[controller.LS] = 100, [controller.RS] = 100}},
	{duration_ms = 300, values = {[controller.LS] = 100, [controller.RS] = 100}},
}

local function clamp(value, minimum, maximum)
	if value < minimum then return minimum end
	if value > maximum then return maximum end
	return value
end

local function round(value)
	if value < 0 then return math.ceil(value - 0.5) end
	return math.floor(value + 0.5)
end

local function operator_list()
	if state.side == 1 then return attackers end
	return defenders
end

local function on_off(value)
	if value then return "ON" end
	return "OFF"
end

local bf6_profiles = {}

local function bf6_default_profile()
	return {
		active = false, width = 10, height = 10, speed = 120,
		vertical = 0, horizontal = 0, rapid_fire = false, hip_rapid = false
	}
end

local function bf6_storage_key(loadout)
	return "bf6_loadout_" .. loadout
end

local function decode_bf6_profile(record)
	local values = {}
	for value in string.gmatch(record or "", "[^,]+") do
		values[#values + 1] = tonumber(value)
	end
	if #values ~= 7 and #values ~= 8 then return nil end
	for index = 1, #values do
		if values[index] == nil then return nil end
	end
	return {
		active = values[1] ~= 0,
		width = clamp(values[2], 1, 50),
		height = clamp(values[3], 1, 50),
		speed = clamp(values[4], 10, 720),
		vertical = clamp(values[5], 0, 99),
		horizontal = clamp(values[6], -30, 30),
		rapid_fire = values[7] ~= 0,
		hip_rapid = values[8] == 1
	}
end

local function encode_bf6_profile(profile)
	return (profile.active and "1" or "0") .. "," .. profile.width .. "," ..
		profile.height .. "," .. profile.speed .. "," .. profile.vertical .. "," ..
		profile.horizontal .. "," .. (profile.rapid_fire and "1" or "0") .. "," ..
		(profile.hip_rapid and "1" or "0")
end

local function load_bf6_loadout(loadout)
	if bf6_profiles[loadout] then return bf6_profiles[loadout] end
	local profiles = {bf6_default_profile(), bf6_default_profile()}
	local saved = storage.read(bf6_storage_key(loadout))
	if type(saved) == "string" then
		local primary, secondary = saved:match("^([^;]+);([^;]+)$")
		profiles[1] = decode_bf6_profile(primary) or profiles[1]
		profiles[2] = decode_bf6_profile(secondary) or profiles[2]
	end
	bf6_profiles[loadout] = profiles
	return profiles
end

local function bf6_profile()
	return load_bf6_loadout(state.bf6_loadout)[state.bf6_weapon]
end

local function save_bf6_settings()
	local changed = false
	for loadout = 1, #bf6_loadout_names do
		if bf6_profiles[loadout] then
			local profiles = bf6_profiles[loadout]
			local record = encode_bf6_profile(profiles[1]) .. ";" ..
				encode_bf6_profile(profiles[2])
			if storage.read(bf6_storage_key(loadout)) ~= record then
				storage.write(bf6_storage_key(loadout), record)
				changed = true
			end
		end
	end
	if changed then storage.commit() end
end

local function device_action(id, index, now)
	local down = device.get_val(id) == 100
	local previous = state.previous_device[index]
	state.previous_device[index] = down
	if not down then
		state.device_repeat_at[index] = 0
		return false, false
	end
	if not previous then
		state.device_repeat_at[index] = now + 350
		return true, false
	end
	if now >= state.device_repeat_at[index] then
		state.device_repeat_at[index] = now + 80
		return true, true
	end
	return false, false
end

local function y_tapped()
	local down = controller.get_val(controller.Y) > 0
	local now = controller.get_millis()
	if down and not state.previous_y then
		state.y_pressed_at = now
	end
	local tapped = not down and state.previous_y and
		(now - state.y_pressed_at) < 500
	state.previous_y = down
	return tapped
end

local function run_tea_bag()
	local down = controller.get_val(controller.DPAD_DOWN) > 0
	local now = controller.get_millis()
	if state.down_tap_release then
		controller.set_val(controller.DPAD_DOWN, 0)
		state.down_tap_release = false
	end
	if down then
		if not state.previous_down then
			state.down_pressed_at = now
		end
		controller.set_val(controller.DPAD_DOWN, 0)
		if now - state.down_pressed_at >= 250 and
			not controller.macro_running("tea_bag") then
			tea_bag_steps[1].wait = state.tea_bag_ms
			tea_bag_steps[2].wait = state.tea_bag_ms
			controller.start_macro("tea_bag", tea_bag_steps)
		end
	elseif state.previous_down then
		if now - state.down_pressed_at < 250 then
			controller.set_val(controller.DPAD_DOWN, 100)
			state.down_tap_release = true
		end
		if controller.macro_running("tea_bag") then
			controller.stop_macro("tea_bag")
		end
	end
	state.previous_down = down
end

local function field_value(field)
	field = field or state.field
	if field == 1  then return state.side == 1 and "ATTACKERS" or "DEFENDERS" end
	if field == 2  then return operator_list()[state.operator] end
	if field == 3  then return state.weapon == 1 and "PRIMARY" or "SECONDARY" end
	if field == 4  then return tostring(state.vertical) end
	if field == 5  then return tostring(state.horizontal) end
	if field == 6  then return tostring(state.movement) .. "%" end
	if field == 7  then return tostring(state.deadzone) end
	if field == 8  then return on_off(state.rapid_fire) end
	if field == 9  then return on_off(state.first_bullet) end
	if field == 10 then return tostring(state.first_bullet_percent) .. "%" end
	if field == 11 then return on_off(state.tea_bag) end
	if field == 12 then return tostring(state.tea_bag_ms) .. "MS" end
	if field == 13 then return on_off(state.shaiko_lean) end
	return "SAVE AND START"
end

local function draw_operator_selector()
	local operators = operator_list()
	local first = math.floor((state.operator - 1) / 4) * 4 + 1
	for index = first, math.min(first + 3, #operators) do
		local text = index == state.operator and
			">" .. operators[index] .. "<" or operators[index]
		local x = math.floor((128 - #text * 6) / 2)
		display.draw_text(math.max(0, x), (index - first) * 8, text)
	end
end

local function draw_simple_list(title, labels, selected, reserve_marker)
	display.draw_text(0, 0, title)
	for index, label in ipairs(labels) do
		local selected_label = index == selected
		if reserve_marker and selected_label then
			display.draw_text(0, index * 8, ">")
		end
		local text = selected_label and
			(reserve_marker and label .. "<" or ">" .. label .. "<") or label
		display.draw_text(reserve_marker and 6 or 0, index * 8, text)
	end
end

local function draw_config_menu()
	local labels = {
		"EXPORT", "IMPORT", "QUICK SELECT " .. on_off(state.quick_select_enabled),
		"SAB " .. on_off(state.sab_enabled), "RESET ALL PROFILES"
	}
	local first = math.floor((state.config_action - 1) / 3) * 3 + 1
	display.draw_text(0, 0, "CONFIG")
	for row = 0, 2 do
		local index = first + row
		local label = labels[index]
		if label then
			local selected = index == state.config_action
			if selected then display.draw_text(0, (row + 1) * 8, ">") end
			display.draw_text(6, (row + 1) * 8,
				selected and label .. "<" or label)
		end
	end
end

local function field_visible(field)
	if field == 10 then return state.first_bullet end
	if field == 12 then return state.tea_bag end
	return true
end

local function visible_config_fields()
	local visible = {}
	for field = 3, #fields do
		if field_visible(field) then visible[#visible + 1] = field end
	end
	return visible
end

local function draw_operator_config()
	local visible = visible_config_fields()
	local selected = 1
	for index, field in ipairs(visible) do
		if field == state.field then selected = index break end
	end
	local first = math.floor((selected - 1) / 3) * 3 + 1
	local header = (state.side == 1 and "ATT > " or "DEF > ") ..
		operator_list()[state.operator]
	local binding = quick_binding_name(state.side, state.operator)
	if binding ~= "NONE" then
		local binding_x = math.max(0, 128 - #binding * 6)
		local max_header_length = math.floor((binding_x - 6) / 6)
		if #header > max_header_length then
			header = header:sub(1, math.max(1, max_header_length - 1)) .. "."
		end
		display.draw_text(binding_x, 0, binding)
	end

	display.draw_text(0, 0, header)
	for row = 0, 2 do
		local field = visible[first + row]
		if field then
			local selected_field = field == state.field
			local value = field_value(field)
			local label = fields[field]
			local label_text = selected_field and not state.field_editing and
				label .. "<" or label
			local value_text = value
			if selected_field then
				if state.field_editing then
					value_text = ">" .. value .. "<"
				end
			end
			if selected_field and not state.field_editing then
				display.draw_text(0, (row + 1) * 8, ">")
			end
			display.draw_text(6, (row + 1) * 8, label_text)
			display.draw_text(math.max(0, 128 - #value_text * 6),
				(row + 1) * 8, value_text)
		end
	end
end

local draw_ui

local function draw_bf6_loadout_selector()
	local first = math.floor((state.bf6_loadout - 1) / 4) * 4 + 1
	for index = first, math.min(first + 3, #bf6_loadout_names) do
		local text = index == state.bf6_loadout and
			">" .. bf6_loadout_names[index] .. "<" or bf6_loadout_names[index]
		local x = math.floor((128 - #text * 6) / 2)
		display.draw_text(math.max(0, x), (index - first) * 8, text)
	end
end

local function bf6_field_visible(field)
	return field < 5 or bf6_profile().active or field > 7
end

local function draw_bf6_config()
	local profile = bf6_profile()
	local visible = {}
	for index = 1, #bf6_fields do
		if bf6_field_visible(index) then visible[#visible + 1] = index end
	end
	local selected = 1
	for index, field in ipairs(visible) do
		if field == state.bf6_field then selected = index break end
	end
	local first = math.floor((selected - 1) / 3) * 3 + 1
	display.draw_text(0, 0, bf6_loadout_names[state.bf6_loadout])
	for row = 0, 2 do
		local field = visible[first + row]
		if field then
			local label = bf6_fields[field]
			local value
			if field == 1 then
				value = state.bf6_weapon == 1 and "PRIMARY" or "SECONDARY"
			elseif field == 2 then
				value = tostring(profile.vertical)
			elseif field == 3 then
				value = tostring(profile.horizontal)
			elseif field == 4 then
				value = on_off(profile.active)
			elseif field == 5 then
				value = tostring(profile.width)
			elseif field == 6 then
				value = tostring(profile.height)
			elseif field == 7 then
				value = tostring(profile.speed) .. "D/S"
			elseif field == 8 then
				value = on_off(profile.rapid_fire)
			elseif field == 9 then
				value = on_off(profile.hip_rapid)
			else
				value = ""
			end
			if field == state.bf6_field then
				if state.bf6_field_editing then
					value = ">" .. value .. "<"
				else
					label = label .. "<"
					display.draw_text(0, (row + 1) * 8, ">")
				end
			end
			display.draw_text(6, (row + 1) * 8, label)
			if value ~= "" then
				display.draw_text(math.max(0, 128 - #value * 6),
					(row + 1) * 8, value)
			end
		end
	end
end

local function draw_bf6_gameplay()
	local profile = bf6_profile()
	display.draw_text(0, 0, "BF6 ACTIVE")
	display.draw_text(0, 8, bf6_loadout_names[state.bf6_loadout] .. " " ..
		(state.bf6_weapon == 1 and "PRIMARY" or "SECONDARY"))
	display.draw_text(0, 16, "V" .. profile.vertical .. " H" ..
		profile.horizontal .. " AIM " .. on_off(profile.active))
	display.draw_text(0, 24, "UP: MENU")
end

local function edit_bf6_loadout(direction)
	state.bf6_loadout = state.bf6_loadout + direction
	if state.bf6_loadout < 1 then state.bf6_loadout = #bf6_loadout_names end
	if state.bf6_loadout > #bf6_loadout_names then state.bf6_loadout = 1 end
	state.bf6_weapon = 1
	state.bf6_field = 1
	state.bf6_field_editing = false
end

local function bf6_field_repeats()
	return state.bf6_field == 2 or state.bf6_field == 3 or
		(state.bf6_field >= 5 and state.bf6_field <= 7)
end

local function move_bf6_field(direction)
	local field = state.bf6_field
	repeat
		field = field + direction
	until field < 1 or field > #bf6_fields or bf6_field_visible(field)
	state.bf6_field = clamp(field, 1, #bf6_fields)
end

local function edit_bf6_field(direction)
	local profile = bf6_profile()
	if state.bf6_field == 1 then
		state.bf6_weapon = state.bf6_weapon == 1 and 2 or 1
	elseif state.bf6_field == 2 then
		profile.vertical = clamp(profile.vertical + direction, 0, 99)
	elseif state.bf6_field == 3 then
		profile.horizontal = clamp(profile.horizontal + direction, -30, 30)
	elseif state.bf6_field == 4 then
		profile.active = not profile.active
	elseif state.bf6_field == 5 then
		profile.width = clamp(profile.width + direction, 1, 50)
	elseif state.bf6_field == 6 then
		profile.height = clamp(profile.height + direction, 1, 50)
	elseif state.bf6_field == 7 then
		profile.speed = clamp(profile.speed + direction * 10, 10, 720)
	elseif state.bf6_field == 8 then
		profile.rapid_fire = not profile.rapid_fire
	elseif state.bf6_field == 9 then
		profile.hip_rapid = not profile.hip_rapid
	end
	state.bf6_settings_dirty = true
end

local function start_bf6()
	if state.bf6_settings_dirty then
		save_bf6_settings()
		state.bf6_settings_dirty = false
	end
	state.bf6_configuring = false
	state.bf6_selecting_loadout = false
	state.bf6_field_editing = false
	controller.cancel_macros()
	draw_ui()
end

draw_ui = function()
	led.set(state.menu_level == 1 and state.weapon == 1)
	display.clear()
	if state.menu_level == 0 then
		local labels = {}
		for index, option in ipairs(game_options) do labels[index] = option.label end
		draw_simple_list("SELECT GAME", labels, state.game, true)
	elseif state.menu_level == 2 then
		if state.bf6_configuring then
			if state.bf6_selecting_loadout then
				draw_bf6_loadout_selector()
			else
				draw_bf6_config()
			end
		else
			draw_bf6_gameplay()
		end
	elseif state.menu_level == 3 then
		if state.config_status == 0 then
			draw_config_menu()
		elseif state.config_status == 1 then
			display.draw_text(0, 0, "REMOVE CONTROLLER")
			display.draw_text(0, 8, "INSERT USB STICK")
			display.draw_text(0, 16,
				state.config_action == 1 and "EXPORT WAITING" or "IMPORT WAITING")
		elseif state.config_status == 2 then
			display.draw_text(0, 0, "USB CONFIG")
			display.draw_text(0, 8, "WORKING")
			display.draw_text(0, 16, "DO NOT REMOVE")
		elseif state.config_status == 3 then
			display.draw_text(0, 0, "CONFIG COMPLETE")
			display.draw_text(0, 8, "REMOVE USB STICK")
			display.draw_text(0, 16, "CONNECT CONTROLLER")
			display.draw_text(0, 24, "LEFT: BACK")
		elseif state.config_status == 5 then
			display.draw_text(0, 0, "RESET ALL PROFILES?")
			display.draw_text(0, 8, "UP: CONFIRM")
			display.draw_text(0, 16, "DOWN: CANCEL")
		else
			local config_errors = {
				"UNKNOWN ERROR", "CONFIG NOT READY", "USB NOT READY",
				"USB READ ERROR", "FORMAT FAT32", "MOUNT ERROR",
				"FILE OPEN ERROR", "FILE WRITE ERROR", "FILE SYNC ERROR",
				"FILE CLOSE ERROR", "FILE RENAME ERROR", "FILE READ ERROR",
				"INVALID CONFIG"
			}
			display.draw_text(0, 0, "CONFIG FAILED")
			display.draw_text(0, 8,
				config_errors[device.config_error() + 1] or "UNKNOWN ERROR")
			display.draw_text(0, 16, "CONNECT CONTROLLER")
			display.draw_text(0, 24, "LEFT: BACK")
		end
	elseif state.configuring then
		if state.field == 1 then
			draw_simple_list("R6 > SIDE", {"ATTACKERS", "DEFENDERS"}, state.side, true)
		elseif state.field == 2 then
			draw_operator_selector()
			return
		else
			draw_operator_config()
			return
		end
	else
		local operators = operator_list()
		display.draw_text(0, 0, operators[state.operator] .. " " ..
			(state.weapon == 1 and "PRIMARY" or "SECONDARY"))
		display.draw_text(0, 8, "V" .. state.vertical ..
			" H" .. state.horizontal .. " M" .. state.movement ..
			" DZ" .. state.deadzone)
		display.draw_text(0, 16,
			"RAPID-" .. on_off(state.rapid_fire))
		display.draw_text(0, 24,
			"BOUND: " .. quick_binding_name(state.side, state.operator))
	end
end

local function field_repeats()
	local field = state.field
	return field == 2 or (field >= 4 and field <= 7) or
		field == 10 or field == 12
end

local function field_is_toggle(field)
	return field == 8 or field == 9 or field == 11 or field == 13
end

local function move_field(direction)
	local field = state.field
	repeat
		field = field + direction
	until field < 1 or field > #fields or field_visible(field)
	state.field = clamp(field, 1, #fields)
end

local function edit_field(direction)
	local field = state.field
	if field == 1 then
		save_loadout_profile()
		state.last_operator[state.side] = state.operator
		state.side = state.side == 1 and 2 or 1
		state.operator = state.last_operator[state.side]
		state.weapon = 1
		load_loadout_profile()
	elseif field == 2 then
		save_loadout_profile()
		state.operator = clamp(state.operator + direction, 1, #operator_list())
		state.last_operator[state.side] = state.operator
		load_loadout_profile()
	elseif field == 3 then
		save_loadout_profile()
		state.weapon = state.weapon == 1 and 2 or 1
		load_loadout_profile()
	elseif field == 4 then
		state.vertical = clamp(state.vertical + direction, 0, 99)
		save_loadout_profile()
	elseif field == 5 then
		state.horizontal = clamp(state.horizontal + direction, -30, 30)
		save_loadout_profile()
	elseif field == 6 then
		state.movement = clamp(state.movement + direction, 0, 99)
		save_loadout_profile()
	elseif field == 7 then
		state.deadzone = clamp(state.deadzone + direction, 1, 30)
		save_loadout_profile()
	elseif field == 8 then
		state.rapid_fire = not state.rapid_fire
		save_loadout_profile()
	elseif field == 9 then
		state.first_bullet = not state.first_bullet
		save_loadout_profile()
	elseif field == 10 then
		state.first_bullet_percent = clamp(state.first_bullet_percent + direction, 0, 200)
	elseif field == 11 then
		state.tea_bag = not state.tea_bag
	elseif field == 12 then
		state.tea_bag_ms = clamp(state.tea_bag_ms + direction, 1, 50)
	elseif field == 13 then
		state.shaiko_lean = not state.shaiko_lean
	end
end

local function save_and_start()
	save_loadout_profile()
	storage.commit()
	state.configuring = false
	state.field_editing = false
	controller.cancel_macros()
	draw_ui()
end

local function quick_button_edge(id)
	local down = controller.get_val(id) > 0
	local pressed = down and not state.quick_previous[id]
	state.quick_previous[id] = down
	return pressed
end

local function quick_open_side(side)
	if state.menu_level == 1 then
		save_loadout_profile()
		state.last_operator[state.side] = state.operator
	end
	state.menu_level = 1
	state.configuring = true
	state.field = 2
	state.field_editing = false
	state.side = side
	state.operator = 1
	state.weapon = 1
	load_loadout_profile()
	draw_ui()
end

local function quick_open_binding(side, operator)
	local operators = side == 1 and attackers or defenders
	if (side ~= 1 and side ~= 2) or operator < 1 or operator > #operators then
		return false
	end
	state.quick_open_side = side
	state.quick_open_operator = operator
	return true
end

local function apply_quick_open_binding()
	local side = state.quick_open_side
	local operator = state.quick_open_operator
	if side == 0 or operator == 0 then return end
	state.quick_open_side = 0
	state.quick_open_operator = 0
	if state.menu_level == 1 then save_loadout_profile() end
	state.menu_level = 1
	state.configuring = false
	state.field_editing = false
	state.field = 3
	state.side = side
	state.operator = operator
	state.last_operator[side] = operator
	state.weapon = 1
	load_loadout_profile()
	controller.cancel_macros()
	draw_ui()
end

local function update_quick_select()
	local view_down = controller.get_val(controller.VIEW) > 0
	local view_pressed = view_down and not state.quick_view_previous
	local pressed_button
	state.quick_consumed = false

	for _, button in ipairs(quick_select_buttons) do
		if quick_button_edge(button[1]) and not pressed_button then
			pressed_button = button
		end
	end
	state.quick_view_previous = view_down

	if not state.quick_select_enabled or
		(state.menu_level ~= 0 and state.menu_level ~= 1) then
		state.quick_navigation = false
		state.quick_chord_active = false
		return
	end
	if view_pressed then
		state.quick_navigation = false
		state.quick_chord_active = false
	end
	if not view_down then
		state.quick_navigation = false
		state.quick_chord_active = false
		return
	end

	local on_operator_selector = state.menu_level == 1 and
		state.configuring and state.field == 2
	local on_operator_config = state.menu_level == 1 and
		state.configuring and state.field > 2
	if state.quick_chord_active then
		controller.set_val(controller.VIEW, 0)
		state.quick_consumed = true
		if not pressed_button then return end
		controller.set_val(pressed_button[1], 0)
		if on_operator_selector and state.quick_navigation then
			local operator = storage.read(
				quick_binding_key(state.side, pressed_button))
			if type(operator) == "number" then
				quick_open_binding(state.side, operator)
			end
		end
		return
	end

	if pressed_button and not state.quick_navigation and
		not on_operator_config and
		(pressed_button[1] == controller.X or
		 pressed_button[1] == controller.B) then
		controller.set_val(controller.VIEW, 0)
		controller.set_val(pressed_button[1], 0)
		state.quick_consumed = true
		state.quick_chord_active = true
		quick_open_side(pressed_button[1] == controller.X and 1 or 2)
		state.quick_navigation = true
		return
	end
	if not pressed_button then return end

	if on_operator_config and not state.quick_navigation then
		controller.set_val(controller.VIEW, 0)
		controller.set_val(pressed_button[1], 0)
		state.quick_consumed = true
		state.quick_chord_active = true
		for _, button in ipairs(quick_select_buttons) do
			if button[2] ~= pressed_button[2] and
				storage.read(quick_binding_key(state.side, button)) ==
					state.operator then
				storage.write(quick_binding_key(state.side, button), nil)
			end
		end
		storage.write(quick_binding_key(state.side, pressed_button),
			state.operator)
		storage.commit()
		draw_ui()
		return
	end
end

local function update_menu()
	local now = controller.get_millis()
	local previous = device_action(device.BTN_DOWN, 1, now)
	local next = device_action(device.BTN_UP, 2, now)
	local decrease, decrease_repeat =
		device_action(device.BTN_SELECT, 3, now)
	local increase, increase_repeat =
		device_action(device.BTN_BACK, 4, now)

	if state.menu_level == 0 then
		if decrease or increase then
			state.game = state.game + (increase and 1 or -1)
			if state.game < 1 then state.game = #game_options end
			if state.game > #game_options then state.game = 1 end
			draw_ui()
		elseif next then
			state.menu_level = game_options[state.game].level
			state.config_status = 0
			draw_ui()
		end
		return
	end

	if state.menu_level == 2 then
		local start_chord =
			(device.get_val(device.BTN_UP) == 100 and
			 device.get_val(device.BTN_DOWN) == 100) or
			(device.get_val(device.BTN_SELECT) == 100 and
			 device.get_val(device.BTN_BACK) == 100)
		if not state.bf6_configuring then
			if previous or next then
				state.bf6_configuring = true
				state.bf6_selecting_loadout = true
				state.bf6_field = 1
				state.bf6_field_editing = false
				draw_ui()
			end
			return
		end
		if state.bf6_selecting_loadout then
			if increase then
				edit_bf6_loadout(1)
				draw_ui()
			elseif decrease then
				edit_bf6_loadout(-1)
				draw_ui()
			elseif next then
				state.bf6_selecting_loadout = false
				state.bf6_field = 1
				state.bf6_field_editing = false
				draw_ui()
			elseif previous then
				state.menu_level = 0
				draw_ui()
			end
			return
		end
		if start_chord then
			start_bf6()
		elseif state.bf6_field_editing then
			if decrease and (not decrease_repeat or bf6_field_repeats()) then
				edit_bf6_field(1)
				draw_ui()
			elseif increase and (not increase_repeat or bf6_field_repeats()) then
				edit_bf6_field(-1)
				draw_ui()
			elseif next or previous then
				state.bf6_field_editing = false
				draw_ui()
			end
		elseif decrease or increase then
			move_bf6_field(increase and 1 or -1)
			draw_ui()
		elseif next then
			if state.bf6_field == #bf6_fields then
				start_bf6()
			elseif state.bf6_field == 1 or state.bf6_field == 4 or
				state.bf6_field == 8 or state.bf6_field == 9 then
				edit_bf6_field(1)
				draw_ui()
			else
				state.bf6_field_editing = true
				draw_ui()
			end
		elseif previous then
			state.bf6_selecting_loadout = true
			state.bf6_field = 1
			state.bf6_field_editing = false
			draw_ui()
		end
		return
	end

	if state.menu_level == 3 then
		local status = device.config_status()
		if state.config_status ~= 5 and status ~= state.config_status then
			state.config_status = status
			if status == 3 and state.config_action == 2 then
				loadout_profiles = {{}, {}}
				state.quick_select_enabled = storage.read("quick_select") == true
				state.sab_enabled = storage.read("sab_enabled") ~= false
				load_loadout_profile()
			end
			draw_ui()
		end
		if state.config_status == 0 then
			if decrease or increase then
				state.config_action = state.config_action + (increase and 1 or -1)
				if state.config_action < 1 then state.config_action = 5 end
				if state.config_action > 5 then state.config_action = 1 end
				draw_ui()
			elseif next then
				if state.config_action == 5 then
					state.config_status = 5
					draw_ui()
				elseif state.config_action == 4 then
					state.sab_enabled = not state.sab_enabled
					storage.write("sab_enabled", state.sab_enabled)
					storage.commit()
					draw_ui()
				elseif state.config_action == 3 then
					state.quick_select_enabled = not state.quick_select_enabled
					storage.write("quick_select", state.quick_select_enabled)
					storage.commit()
					draw_ui()
				elseif device.config_request(state.config_action) then
					state.config_status = 1
					draw_ui()
				end
			elseif previous then
				state.menu_level = 0
				draw_ui()
			end
		elseif state.config_status == 5 then
			if decrease then
				storage.reset_loadouts()
				for _, button in ipairs(quick_select_buttons) do
					storage.write(quick_binding_key(1, button), nil)
					storage.write(quick_binding_key(2, button), nil)
				end
				state.quick_select_enabled = false
				storage.write("quick_select", false)
				storage.commit()
				loadout_profiles = {{}, {}}
				load_loadout_profile()
				state.config_status = 0
				draw_ui()
			elseif increase then
				state.config_status = 0
				draw_ui()
			end
		elseif state.config_status == 1 and previous then
			device.config_reset()
			state.menu_level = 0
			state.config_status = 0
			draw_ui()
		elseif (state.config_status == 3 or state.config_status == 4) and previous then
			device.config_reset()
			state.menu_level = 0
			state.config_status = 0
			draw_ui()
		end
		return
	end

	if not state.configuring then
		if previous then
			controller.cancel_macros()
			state.configuring = true
			state.field = state.field >= 3 and state.field or 3
			state.field_editing = false
			state.shaiko_dir = 0
			draw_ui()
		elseif next then
			controller.cancel_macros()
			state.configuring = true
			state.field = 1
			state.field_editing = false
			state.shaiko_dir = 0
			draw_ui()
		end
		return
	end

	if device.get_val(device.BTN_UP) == 100 and
		device.get_val(device.BTN_DOWN) == 100 then
		save_and_start()
		return
	end

	if state.field == 1 then
		if decrease or increase then
			edit_field(increase and 1 or -1)
			draw_ui()
		elseif next then
			state.field = 2
			draw_ui()
		elseif previous then
			controller.cancel_macros()
			state.menu_level = 0
			draw_ui()
		end
		return
	end

	if state.field == 2 then
		if increase and (not increase_repeat or field_repeats()) then
			edit_field(1)
			draw_ui()
		elseif decrease and (not decrease_repeat or field_repeats()) then
			edit_field(-1)
			draw_ui()
		elseif next then
			state.field = 3
			state.field_editing = false
			draw_ui()
		elseif previous then
			state.field = 1
			draw_ui()
		end
		return
	end

	if state.field_editing then
		if decrease and (not decrease_repeat or field_repeats()) then
			edit_field(1)
			draw_ui()
		elseif increase and (not increase_repeat or field_repeats()) then
			edit_field(-1)
			draw_ui()
		elseif next or previous then
			state.field_editing = false
			draw_ui()
		end
	elseif decrease or increase then
		move_field(increase and 1 or -1)
		draw_ui()
	elseif next then
		if state.field == #fields then
			save_and_start()
		elseif field_is_toggle(state.field) then
			edit_field(1)
			draw_ui()
		else
			state.field_editing = true
			draw_ui()
		end
	elseif previous then
		state.field = 2
		state.field_editing = false
		draw_ui()
	end
end

local function offset_axis(id, amount)
	local current = controller.get_val(id)
	if state.sab_enabled and amount ~= 0 then
		amount = amount + math.random(-1, 1)
	end
	local value = amount * (100 - math.abs(current)) / 100 + current
	controller.set_val(id, round(clamp(value, -100, 100)))
end

local function apply_recoil()
	if controller.get_val(controller.LT) == 0 or
		controller.get_val(controller.RT) == 0 then return end
	local vertical_recoil = state.vertical
	if controller.get_millis() < state.first_bullet_until and
		state.first_bullet and state.vertical ~= 0 then
		local multiplier = 1 + state.first_bullet_percent / 100
		vertical_recoil = state.vertical * multiplier
	end

	local x = controller.get_val(controller.RX)
	local y = controller.get_val(controller.RY)
	local magnitude = math.sqrt(x * x + y * y)
	if magnitude <= state.deadzone then
		if state.horizontal ~= 0 then
			offset_axis(controller.RX, state.horizontal)
		end
		if vertical_recoil ~= 0 then
			offset_axis(controller.RY, vertical_recoil)
		end
		return
	end

	local fade = clamp((100 - magnitude) /
		(100 - state.deadzone), 0, 1)
	local scale = state.movement / 100 * fade * fade
	local vertical_scale = scale
	if y * vertical_recoil > 0 then
		vertical_scale = 1 - math.abs(y) / 100
	elseif y == 0 then
		vertical_scale = 1
	end
	vertical_scale = vertical_scale *
		(1 - (math.abs(x) / 100) ^ 4)
	if state.horizontal ~= 0 then
		offset_axis(controller.RX, state.horizontal * scale)
	end
	if vertical_recoil ~= 0 then
		offset_axis(controller.RY, vertical_recoil * vertical_scale)
	end
end

local function run_macro(name, enabled, held, steps)
	if enabled and held then
		if not controller.macro_running(name) then
			controller.start_macro(name, steps)
		end
	elseif controller.macro_running(name) then
		controller.stop_macro(name)
	end
end

local function bf6_y_tapped()
	local down = controller.get_val(controller.Y) > 0
	local tapped = not down and state.bf6_previous_y
	state.bf6_previous_y = down
	return tapped
end

local function apply_bf6_sticky_aim()
	local profile = bf6_profile()
	local lt_held = controller.get_val(controller.LT) > 0
	local rt_held = controller.get_val(controller.RT) >= bf6_rt_threshold
	local x_offset = 0
	local y_offset = 0
	if profile.active and lt_held then
		local now = controller.get_millis()
		local elapsed = state.bf6_last_aim_ms == 0 and 10 or
			clamp(now - state.bf6_last_aim_ms, 0, 100)
		state.bf6_last_aim_ms = now
		state.bf6_angle = (state.bf6_angle + profile.speed * elapsed / 1000) % 360
		local radians = state.bf6_angle * math.pi / 180
		x_offset = math.cos(radians) * profile.width
		y_offset = math.sin(radians) * profile.height
	else
		state.bf6_last_aim_ms = 0
	end
	if lt_held and rt_held then
		x_offset = x_offset + profile.horizontal
		y_offset = y_offset + profile.vertical
	end
	if x_offset ~= 0 then offset_axis(controller.RX, x_offset) end
	if y_offset ~= 0 then offset_axis(controller.RY, y_offset) end
end

local function run_bf6_gameplay()
	if bf6_y_tapped() then
		state.bf6_weapon = state.bf6_weapon == 1 and 2 or 1
		state.bf6_field = 1
		state.bf6_field_editing = false
		draw_ui()
	end
	local profile = bf6_profile()
	run_macro("bf6_rapid_fire", profile.rapid_fire,
		controller.get_val(controller.RT) >= bf6_rt_threshold and
		(profile.hip_rapid or controller.get_val(controller.LT) > 0),
		rapid_fire_macro)
	apply_bf6_sticky_aim()
end

local function run_gameplay()
	-- Weapon switch on short Y tap
	if y_tapped() then
		save_loadout_profile()
		state.weapon = state.weapon == 1 and 2 or 1
		load_loadout_profile()
		draw_ui()
	end

	local firing  = controller.get_val(controller.RT) > 0
	local lt_held = controller.get_val(controller.LT) > 0
	local now = controller.get_millis()
	if firing and not state.previous_rt then
		state.first_bullet_until = now + 60
	end
	state.previous_rt = firing

	run_macro("rapid_fire", state.rapid_fire,  firing, rapid_fire_macro)

	-- Short DOWN taps are emitted on release; holds run tea bag without forwarding DOWN.
	if state.tea_bag then
		run_tea_bag()
	end

	-- Shaiko lean: ADS plus d-pad runs an RS/LS burst and suppresses d-pad RIGHT
	-- (Veritas ShaikoLeanLeft / ShaikoLeanRight)
	if state.shaiko_lean and lt_held then
		local want_left = controller.get_val(controller.DPAD_LEFT) > 0
		local want_right = controller.get_val(controller.DPAD_RIGHT) > 0
		controller.set_val(controller.DPAD_RIGHT, 0)
		local want_dir   = want_left and 1 or (want_right and 2 or 0)
		if want_dir ~= 0 then
			if not controller.macro_running("shaiko_lean") then
				local steps = want_dir == 1 and shaiko_lean_left_steps
				                              or shaiko_lean_right_steps
				controller.start_macro("shaiko_lean", steps)
				state.shaiko_dir = want_dir
			elseif want_dir ~= state.shaiko_dir then
				controller.stop_macro("shaiko_lean")
				local steps = want_dir == 1 and shaiko_lean_left_steps
				                              or shaiko_lean_right_steps
				controller.start_macro("shaiko_lean", steps)
				state.shaiko_dir = want_dir
			end
		elseif controller.macro_running("shaiko_lean") then
			controller.stop_macro("shaiko_lean")
			state.shaiko_dir = 0
		end
	elseif controller.macro_running("shaiko_lean") then
		controller.stop_macro("shaiko_lean")
		state.shaiko_dir = 0
	end

	apply_recoil()
end

function on_input()
	apply_quick_open_binding()
	if not state.display_ready then
		state.display_ready = true
		draw_ui()
	end
	update_quick_select()
	update_menu()
	if state.menu_level == 1 and not state.quick_consumed then
		run_gameplay()
	elseif state.menu_level == 2 then
		run_bf6_gameplay()
	end
end

function on_console_connected()
	state.menu_level = 0
	state.quick_open_side = 0
	state.quick_open_operator = 0
	state.display_ready = true
	draw_ui()
end
