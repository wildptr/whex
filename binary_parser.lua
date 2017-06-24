local P = {} -- this package

local Node = {}
Node.__index = Node

function Node:new(name, start, size, value)
  o = {}
  o.children = {}
  o.dir = {} -- find child by name
  o.name = name
  o.start = start
  o.size = size
  o.value = value -- leaf node only
  setmetatable(o, self)
  return o
end

function Node:append_node(child)
  local index = #self.children+1
  self.children[index] = child
  self.dir[child.name] = index
end

function Node:__call(field)
  local child = self.children[self.dir[field]]
  return child.value or child
  --[[local node = self
  for i=1,#list do
    node = node:child(list[i])
  end
  return node.value or node]]
end

function Node:print()
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

function P.new(file)

  local pos = 0
  local current_node

  local function u8(name)
    local bytes = file:read(1)
    value = string.byte(bytes:sub(1,1))
    local t = Node:new(name, pos, 1, value)
    current_node:append_node(t)
    pos = pos+1
    return value
  end

  local function u16(name)
    local bytes = file:read(2)
    value = string.byte(bytes:sub(1,1))
          | string.byte(bytes:sub(2,2)) << 8
    local t = Node:new(name, pos, 2, value)
    current_node:append_node(t)
    pos = pos+2
    return value
  end

  local function u32(name)
    local bytes = file:read(4)
    value = string.byte(bytes:sub(1,1))
          | string.byte(bytes:sub(2,2)) << 8
          | string.byte(bytes:sub(3,3)) << 16
          | string.byte(bytes:sub(4,4)) << 24
    local t = Node:new(name, pos, 4, value)
    current_node:append_node(t)
    pos = pos+4
    return value
  end

  local function u64(name)
    local bytes = file:read(8)
    value =
    string.byte(bytes:sub(1,1))       |
    string.byte(bytes:sub(2,2)) <<  8 |
    string.byte(bytes:sub(3,3)) << 16 |
    string.byte(bytes:sub(4,4)) << 24 |
    string.byte(bytes:sub(5,5)) << 32 |
    string.byte(bytes:sub(6,6)) << 40 |
    string.byte(bytes:sub(7,7)) << 48 |
    string.byte(bytes:sub(8,8)) << 56
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
      t.size = pos - t.start
      current_node:append_node(t)
      return t
    end
  end

  local function ascii(n)
    return function(name)
      local bytes = file:read(n)
      local t = Node:new(name, pos, n, bytes)
      current_node:append_node(t)
      pos = pos+n
      return t
    end
  end

  local function map(proc, list)
    return function(name)
      local t = Node:new(name, pos)
      local saved_current_node = current_node
      current_node = t
      for i=1,#list.children do
        proc(list.children[i])(i)
      end
      current_node = saved_current_node
      t.size = pos - t.start
      current_node:append_node(t)
      return t
    end
  end

  local function data(n)
    return function(name)
      file:seek('cur', n)
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
      proc(function(field)
        if field == '.' then
          return pos-t.start
        end
        return t(field)
      end)
      current_node = saved_current_node
      t.size = pos - t.start
      if current_node then
        current_node:append_node(t)
      else
        current_node = t
      end
      return t
    end
  end

  local table =  {
    u8     = u8,
    u16    = u16,
    u32    = u32,
    u64    = u64,
    array  = array,
    ascii  = ascii,
    data   = data,
    map    = map,
    record = record,
  }

  setmetatable(table, {__call = function(self)
    for k,v in pairs(self) do
      _G[k] = v
    end
  end})

  return table
end

return P
