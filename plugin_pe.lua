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

function parse_idt(buf, off)
  bp.new(buf, off)()
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
      if ent.ilt_rva == 0 then return end
      i=i+1
    end
  end)
  return idt 'idt'
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

local function import_table(buf)
  local pe = buf:tree()
  local w = Window{text='Import table'}
  local s = find_section(pe, '.idata')
  if not s then
    msgbox('.idata section not found')
    return
  end
  idata_off = pe.section_headers[s].raw_data_offset
  local idt = parse_idt(buf, idata_off)
  local lv = ListView{parent=w, pos={8,8}, size={256,128}}
  lv:insert_column(0, 'Name')
  for i,idtent in pairs(idt) do
    local name_rva = idtent.name_rva
    name_off = rva2off(buf, name_rva)
    local name = ''
    if name_off then
      name = getcstr(buf, name_off)
    end
    lv:insert_item(i-1, {name})
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
