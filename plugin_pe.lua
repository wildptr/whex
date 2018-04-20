local bp = require 'binparse'

local function hex(n)
  return string.format('0x%x', n)
end

local function rva2off(buf, rva)
  local pe = buf:tree()
  for i,s in pairs(pe.section_headers) do
    local voff = rva - s.virtual_address
    if voff >= 0 and voff < s.virtual_size then
      return s.raw_data_offset + voff
    end
  end
  return -- not found
end

local function find_section(pe, name)
  for i,s in pairs(pe.section_headers) do
    if s.name == name then
      return i
    end
  end
end

local function info(buf)
  local pe = buf:tree()
  local w = Window{text='PE Info'}
  local oh = pe.pe_header.optional_header
  local osver = Label{parent=w, pos={8,8}, size={128,16},
    text=string.format('OS version: %d.%d', oh.major_os_version, oh.minor_os_version)}
  w:show()
end

local function section_table(buf)
  local pe = buf:tree()
  local w = Window{text='Section table'}
  local lv = ListView{parent=w, pos={8,8}, size={256,128}}
  lv:insert_column(0, 'Name')
  lv:insert_column(1, 'RVA')
  lv:insert_column(2, 'Virt. size')
  local i = 0
  local n = pe.pe_header.num_sections
  while i < n do
    local s = pe.section_headers[1+i]
    lv:insert_item(i, {s.name, hex(s.virtual_address), hex(s.virtual_size)})
    i = i+1
  end
  w:show()
end

local function parse_idt(buf, off)
  bp(buf, off)()
  local idt_entry = record(function()
    u32 'ilt_rva'
    u32 'timestamp'
    u32 'forwarder_chain'
    u32 'name_rva'
    u32 'iat_rva'
  end)
  local idt = record(function()
    local i=1
    while true do
      local ent = idt_entry(i)
      if ent.ilt_rva == 0 then break end
      i=i+1
    end
  end)
  local ret = idt 'idt'
  ret[#ret] = nil
  return ret
end

local function parse_ilt(buf, off)
  bp(buf, off)()
  local ilt = record(function()
    local i=1
    while true do
      -- TODO: PE32+
      local ent = u32(i)
      if ent == 0 then break end
      i=i+1
    end
  end)
  local ret = ilt 'ilt'
  ret[#ret] = nil
  return ret
end

local function getcstr(buf, pos)
  local start = pos
  while true do
    local b = buf:peek(pos)
    if b == 0 then break end
    pos = pos+1
  end
  return buf:peekstr(start, pos-start)
end

local function getpascalstr(buf, pos)
  local n = buf:peek(pos)
  return buf:peekstr(pos+1, n)
end

local function make_ilt(buf, idt)
  local ilt = {}
  for i,idtent in pairs(idt) do
    local ilt_off = rva2off(buf, idtent.ilt_rva)
    if ilt_off then
      ilt[i] = parse_ilt(buf, ilt_off)
    else
      ilt[i] = {}
    end
  end
  return ilt
end

local function get_u16(buf, pos)
  return buf:peek(pos) | buf:peek(pos+1) << 8
end

local function show_ilt(buf, ilt, lv)
  lv:clear()
  local ident, hint
  for i,ent in pairs(ilt) do
    if (ent & 0x80000000) == 0 then
      -- import by name
      local rva = ent & 0x7fffffff
      local off = rva2off(buf, rva)
      if off then
        hint = get_u16(buf, off)
        ident = getcstr(buf, off+2)
      end
    else
      -- import by ordinal
      ident = ent & 0xffff
    end

    local item
    if type(ident) == 'string' then
      item = {'', ident, hint}
    elseif type(ident) == 'number' then
      item = {ident, '', ''}
    else
      item = {'', '', ''}
    end
    lv:insert_item(i-1, item)
  end
end

local function import_table(buf)
  local pe = buf:tree()
  local w = Window{text='Import table', size={640,480}}
  local s = find_section(pe, '.idata')
  if not s then
    msgbox('.idata section not found')
    return
  end
  idata_off = pe.section_headers[s].raw_data_offset
  local idt = parse_idt(buf, idata_off)
  local ilt = make_ilt(buf, idt)
  local lb = ListBox{parent=w, pos={0,0}}
  local lv = ListView{parent=w, pos={256,0}}
  w.on_resize = function(w, wid, hei)
    lb:resize(256, hei)
    lv:resize(wid-256, hei)
  end
  lv:insert_column(0, 'Ord.', {width=32})
  lv:insert_column(1, 'Name', {width=184})
  lv:insert_column(2, 'Hint', {width=40})
  lb.on_select = function(lb, i)
    show_ilt(buf, ilt[1+i], lv)
  end
  for i,idtent in pairs(idt) do
    local name_rva = idtent.name_rva
    name_off = rva2off(buf, name_rva)
    local name = ''
    if name_off then
      name = getcstr(buf, name_off)
    end
    lb:insert_item(i-1, name)
  end
  w:show()
end

return {
  name = 'PE',
  parser = require 'pe',
  functions = {
    {info, 'PE Info...'},
    {section_table, 'Section table...'},
    {import_table, 'Import table...'},
  }
}
