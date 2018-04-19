local P = {} -- this package

local Node = {}
Node.__index = Node

function Node:new(name, start, size, value)
  o = {}
  o._children = {}
  o._name = name
  o._start = start
  o._size = size
  o._value = value -- leaf node only
  setmetatable(o, self)
  return o
end

function Node:append_node(child)
  local index = #self._children+1
  self._children[index] = child
  self[child._name] = child._value or child
end

function Node:print()
  local function print_r(self, depth)
    local indent = string.rep('  ', depth)
    if self._value then
      typ = type(self._value)
      if typ == 'number' then
        print(string.format('%s%s = %d (0x%x)', indent, self._name, self._value, self._value))
      elseif typ == 'string' then
        print(string.format('%s%s = %s', indent, self._name, self._value))
      end
    else
      io.write(string.format('%s%s: %d bytes', indent, self._name, self._size))
      if #self._children > 0 then
        print(' {')
        for i=1, #self._children do
          print_r(self._children[i], depth+1)
        end
        print(string.format('%s}', indent))
      else
        print()
      end
    end
  end
  return print_r(self, 0)
end

function P.new(file)

  local pos = 0
  local current_node

  local function u8(name)
    value = file:peek(pos)
    local t = Node:new(name, pos, 1, value)
    current_node:append_node(t)
    pos = pos+1
    return value
  end

  local function u16(name)
    value = file:peek(pos)
          | file:peek(pos+1) << 8
    local t = Node:new(name, pos, 2, value)
    current_node:append_node(t)
    pos = pos+2
    return value
  end

  local function u32(name)
    value = file:peek(pos)
          | file:peek(pos+1) << 8
          | file:peek(pos+2) << 16
          | file:peek(pos+3) << 24
    local t = Node:new(name, pos, 4, value)
    current_node:append_node(t)
    pos = pos+4
    return value
  end

  local function u64(name)
    value = string.byte(pos)
          | string.byte(pos+1) <<  8
          | string.byte(pos+2) << 16
          | string.byte(pos+3) << 24
          | string.byte(pos+4) << 32
          | string.byte(pos+5) << 40
          | string.byte(pos+6) << 48
          | string.byte(pos+7) << 56
    local t = Node:new(name, pos, 8, value)
    current_node:append_node(t)
    pos = pos+8
    return value
  end

  local function array(proc, n)
    return function(name)
      local t = Node:new(name, pos)
      local saved_current_node = current_node
      current_node = t
      for i=1,n do
        proc(i)
      end
      current_node = saved_current_node
      t._size = pos - t._start
      current_node:append_node(t)
      return t
    end
  end

  local function ascii(n)
    return function(name)
      local bytes = file:peekstr(pos, n)
      local t = Node:new(name, pos, n, bytes)
      current_node:append_node(t)
      pos = pos+n
      return t
    end
  end

  local function foreach(list, proc)
    return function(name)
      local t = Node:new(name, pos)
      local saved_current_node = current_node
      current_node = t
      for i=1,#list._children do
        proc(list._children[i])(i)
      end
      current_node = saved_current_node
      t._size = pos - t._start
      current_node:append_node(t)
      return t
    end
  end

  local function data(n)
    return function(name)
      local t = Node:new(name, pos, n)
      current_node:append_node(t)
      pos = pos+n
      return t
    end
  end

  local function record(proc)
    return function(name)
      local t = Node:new(name, pos)
      local saved_current_node = current_node
      current_node = t
      proc(function() return pos - t._start end)
      current_node = saved_current_node
      t._size = pos - t._start
      if current_node then
        current_node:append_node(t)
      else
        current_node = t
      end
      return t
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
    foreach = foreach,
    record  = record,
  }

  setmetatable(table, {__call = function(self)
    for k,v in pairs(self) do
      _G[k] = v
    end
  end})

  return table
end

return P
