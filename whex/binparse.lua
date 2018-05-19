local function Node(name, start, size, value)
  local o = {}
  o.children = {}
  o.name = name
  o.start = start
  o.size = size
  if value == nil then
    o.value = {}
    setmetatable(o.value, {
      __call = function()
        return o
      end
    })
  else
    o.value = value
  end
  o.append_node = function(self, child)
    local index = #self.children+1
    if child.name ~= nil then
      self.children[index] = child
      self.value[child.name] = child.value
    end
  end
  return o
end

local function print_node(self)
  local function print_r(self, depth)
    local indent = string.rep('  ', depth)
    if self.value then
      typ = type(self.value)
      if typ == 'number' then
        print(string.format('%s%s = %d (0x%x)', indent, self.name, self.value, self.value))
      elseif typ == 'string' then
        print(string.format('%s%s = %s', indent, self.name, self.value))
      end
    else
      io.write(string.format('%s%s: %d bytes', indent, self.name, self.size))
      if #self.children > 0 then
        print(' {')
        for i=1, #self.children do
          print_r(self.children[i], depth+1)
        end
        print(string.format('%s}', indent))
      else
        print()
      end
    end
  end
  return print_r(self, 0)
end

return function(buf, pos)

  pos = pos or 0
  local current_node

  local function u8(name)
    value = buf:peek(pos)
    local t = Node(name, pos, 1, value)
    current_node:append_node(t)
    pos = pos+1
    return value
  end

  local function u16(name)
    value = buf:peeku16(pos)
    local t = Node(name, pos, 2, value)
    current_node:append_node(t)
    pos = pos+2
    return value
  end

  local function u32(name)
    value = buf:peeku32(pos)
    local t = Node(name, pos, 4, value)
    current_node:append_node(t)
    pos = pos+4
    return value
  end

  local function u64(name)
    value = buf:peeku64(pos)
    local t = Node(name, pos, 8, value)
    current_node:append_node(t)
    pos = pos+8
    return value
  end

  local function array(proc, n)
    return function(name)
      local t = Node(name, pos)
      local saved_current_node = current_node
      current_node = t
      for i=1,n do
        proc(i)
      end
      current_node = saved_current_node
      t.size = pos - t.start
      current_node:append_node(t)
      return t.value
    end
  end

  local function ascii(n)
    return function(name)
      local bytes = buf:read(pos, n)
      local t = Node(name, pos, n, bytes)
      current_node:append_node(t)
      pos = pos+n
      return bytes
    end
  end

  local function foreach(list, proc)
    return function(name)
      local t = Node(name, pos)
      local saved_current_node = current_node
      current_node = t
      for i=1,#list do
        proc(list[i])(i)
      end
      current_node = saved_current_node
      t.size = pos - t.start
      current_node:append_node(t)
      return t.value
    end
  end

  local function data(n)
    if n<0 then error('negative data size') end
    return function(name)
      local t = Node(name, pos, n)
      current_node:append_node(t)
      pos = pos+n
      return t.value
    end
  end

  local function skip(n)
    if n<0 then error('negative skip size') end
    pos = pos+n
  end

  local function record(proc)
    return function(name)
      local t = Node(name, pos)
      local saved_current_node = current_node
      current_node = t
      proc(t.value, function() return pos end)
      current_node = saved_current_node
      t.size = pos - t.start
      if current_node then
        current_node:append_node(t)
      else
        current_node = t
      end
      return t.value
    end
  end

  local function newtype(proc, ty)
    whex.customtype[ty.name] = ty
    return function(name)
      local value = proc(name)
      local children = current_node.children
      children[#children].type = ty.name
      return value
    end
  end

  local table =  {
    u8      = u8,
    u16     = u16,
    u32     = u32,
    u64     = u64,
    array   = array,
    ascii   = ascii,
    data    = data,
    skip    = skip,
    foreach = foreach,
    record  = record,
    newtype = newtype
  }

  setmetatable(table, {__call = function(self)
    for k,v in pairs(self) do
      _G[k] = v
    end
  end})

  return table
end
