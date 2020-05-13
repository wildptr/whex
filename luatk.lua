luatk_Window = {
  __index = {
    show = function(w)
      luatk_show_window(w.luatk_window, 1--[[SW_SHOWNORMAL]])
    end,
    text = function(w)
      luatk_get_text(w.luatk_window)
    end,
    rowconfigure = function(w, config)
      for i=1,#config do
        luatk_rowconfigure(w.luatk_window, config[i], config)
      end
    end,
    columnconfigure = function(w, config)
      for i=1,#config do
        luatk_columnconfigure(w.luatk_window, config[i], config)
      end
    end
  }
}

function Frame(config)
  local w = {}
  setmetatable(w, luatk_Window)
  w.luatk_window = luatk_create_window('frame', w, config)
  return w
end

function Button(config)
  local w = {}
  setmetatable(w, luatk_Window)
  w.luatk_window = luatk_create_window('button', w, config)
  return w
end

function Entry(config)
  local w = {}
  setmetatable(w, luatk_Window)
  w.luatk_window = luatk_create_window('entry', w, config)
  return w
end
