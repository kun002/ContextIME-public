-- ContextIME command-fragment support runs entirely inside librime-lua.
-- Command mode is derived from the current composition; no mode flag survives
-- Escape, focus changes, or an external context clear.

local function contextime_load_command_config(env)
  if env.contextime_command_roots ~= nil then
    return
  end

  local config = env.engine.schema.config
  local enabled = config:get_bool("contextime/command_fragments/enabled")
  env.contextime_command_enabled = enabled ~= false
  env.contextime_command_roots = {}

  local roots = config:get_list("contextime/command_fragments/roots")
  if roots == nil then
    return
  end

  for index = 0, roots.size - 1 do
    local value = roots:get_value_at(index)
    if value ~= nil and value.value ~= nil and value.value ~= "" then
      env.contextime_command_roots[value.value] = true
    end
  end
end

local function contextime_is_command_root(input, env)
  contextime_load_command_config(env)
  return env.contextime_command_enabled and env.contextime_command_roots[input] == true
end

local function contextime_is_command_input(input, env)
  contextime_load_command_config(env)
  if not env.contextime_command_enabled then
    return false
  end

  local separator = input:find(" ", 1, true)
  if separator == nil then
    return false
  end

  return env.contextime_command_roots[input:sub(1, separator - 1)] == true
end

function contextime_command_processor(key, env)
  if key:release() then
    return 2
  end

  local context = env.engine.context
  local input = context.input or ""
  local keycode = key.keycode

  if contextime_is_command_input(input, env) then
    if keycode == 0xff0d or keycode == 0xff8d then
      local command = input:gsub(" +$", "")
      if command ~= "" then
        env.engine:commit_text(command)
      end
      context:clear()
      return 1
    end

    if keycode == 0xff1b then
      context:clear()
      return 1
    end

    if keycode == 0xff08 then
      if #input > 0 then
        context:pop_input(1)
      end
      return 1
    end

    if key:ctrl() or key:alt() or key:super() then
      return 2
    end

    if keycode >= 0x20 and keycode <= 0x7e then
      context:push_input(string.char(keycode))
      return 1
    end

    return 2
  end

  if keycode == 0x20 and contextime_is_command_root(input, env) then
    context:push_input(" ")
    return 1
  end

  return 2
end

function contextime_command_translator(input, segment, env)
  if not segment:has_tag("contextime_command") or
      not contextime_is_command_input(input, env) then
    return
  end

  local candidate = Candidate(
    "contextime_command",
    segment.start,
    segment._end,
    input,
    "〔命令·Enter 上屏〕")
  candidate.quality = 100
  yield(candidate)
end

-- The native Weasel Server refreshes this property from a bounded immutable
-- snapshot on a background thread. Candidate translation only reads session
-- memory; it never opens files, calls IPC, scans a project, or invokes Node.
local contextime_project_type_labels = {
  [0] = "类",
  [1] = "方法",
  [2] = "属性",
  [3] = "枚举",
  [4] = "命名空间",
  [5] = "文件",
  [6] = "目录",
  [7] = "资源",
  [8] = "Shader",
  [9] = "术语",
}

local function contextime_project_input_supported(input)
  return #input >= 2 and input:match("^[0-9A-Za-z_]+$") ~= nil
end

function contextime_project_translator(input, segment, env)
  if not contextime_project_input_supported(input) then
    return
  end

  local serialized =
      env.engine.context:get_property("contextime_project_candidates") or ""
  if serialized:sub(1, 2) ~= "1\n" then
    return
  end

  local prefix = input:lower()
  local emitted = 0
  for line in serialized:gmatch("[^\n]+") do
    if line ~= "1" then
      local type_id, frequency, symbol =
          line:match("^(%d+)\t(%d+)\t(.+)$")
      local numeric_type = tonumber(type_id)
      local numeric_frequency = tonumber(frequency)
      if symbol ~= nil and numeric_type ~= nil and numeric_frequency ~= nil and
          symbol:lower():sub(1, #prefix) == prefix then
        local label = contextime_project_type_labels[numeric_type] or "符号"
        local candidate = Candidate(
          "contextime_project",
          segment.start,
          segment._end,
          symbol,
          "〔项目·" .. label .. "〕")
        candidate.quality = 10000 + math.min(numeric_frequency, 1000) - emitted
        yield(candidate)
        emitted = emitted + 1
        if emitted >= 16 then
          return
        end
      end
    end
  end
end
