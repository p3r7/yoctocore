
-- ------------------------------------------------------------------------
-- core

function random_number()
    return math.random(100, 150)
end


-- ------------------------------------------------------------------------
-- envs

function new_env(code)
    local env = {}
    setmetatable(env, {
        __index = function(_, key)
            return _G[key] or 0
        end,
        __newindex = function(_, key, value)
            rawset(env, key, value)
        end
    })
    local chunk = load(code, "chunk", "t", env)
    chunk()
    return env
end

envs = {}
for i = 1, 8 do
    table.insert(envs, new_env(""))
end

function update_env(i, code)
    envs[i] = new_env(code)
end

function env_main(i)
    return envs[i].main()
end


-- ------------------------------------------------------------------------

_yocto = _yocto or {}

_yocto.keys  = {0, 0, 0, 0, 0, 0, 0, 0, 0}
_yocto.knobs = {0, 0, 0, 0, 0, 0, 0, 0, 0}

-- key callback
_yocto.key = function(n,z)
  _yocto.keys[n] = z
  if envs[n].key then
    envs[n].key(z)
  end
end
-- enc knob
_yocto.knob = function(n,val)
  _yocto.knobs[n] = val
  if envs[n].knob then
    envs[n].knob(val)
  end
end

math.randomseed(os.time())
