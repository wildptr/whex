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
  -- returns nil if not found
end

local function find_section(pe, name)
  for i,s in pairs(pe.section_headers) do
    if s.name == name then
      return i
    end
  end
end

local function section_table(buf)
  local pe = buf:tree()
  local w = Window{text='Section table', size={640,480}}
  local lv = ListView{parent=w, pos={0,0}}
  w.on_resize = function(wid, hei)
    lv:resize(wid, hei)
  end
  lv:insert_column(0, 'Name', {width=64})
  lv:insert_column(1, 'RVA', {width=80})
  lv:insert_column(2, 'Virt. size', {width=80})
  local i = 0
  local n = pe.pe_header.num_sections
  while i < n do
    local s = pe.section_headers[1+i]
    lv:insert_item(i, {s.name, hex(s.virtual_address), hex(s.virtual_size)})
    i = i+1
  end
  w:show()
end

-- import table

local function parse_idt(buf, off)
  bp(buf, off)()
  local idt_entry = record(function()
    u32 'ilt_rva'
    u32 'timestamp'
    u32 'forwarder_chain'
    u32 'name_rva'
    u32 'iat_rva'
  end)
  local idt = record(function(self)
    local i=1
    while true do
      local ent = idt_entry()
      if ent.ilt_rva == 0 then break end
      self[i] = ent
      i=i+1
    end
  end)
  return idt 'idt'
end

local function parse_ilt(buf, off, pe32plus)
  bp(buf, off)()
  local ilt
  if pe32plus then
    ilt = record(function(self)
      local i=1
      while true do
        local entlo = u32()
        local enthi = u32()
        if entlo == 0 and enthi == 0 then break end
        self[i] = enthi&0x80000000 | entlo&0x7fffffff
        i=i+1
      end
    end)
  else
    ilt = record(function(self)
      local i=1
      while true do
        local ent = u32()
        if ent == 0 then break end
        self[i] = ent
        i=i+1
      end
    end)
  end
  return ilt 'ilt'
end

local function getcstr(buf, pos)
  local start = pos
  while true do
    local b = buf:peek(pos)
    if b == 0 then break end
    pos = pos+1
  end
  return buf:read(start, pos-start)
end

local function make_ilt(buf, idt, pe32plus)
  local ilt = {}
  for i,idtent in pairs(idt) do
    local ilt_off = rva2off(buf, idtent.ilt_rva)
    if ilt_off then
      ilt[i] = parse_ilt(buf, ilt_off, pe32plus)
    else
      ilt[i] = {}
    end
  end
  return ilt
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
        hint = buf:peeku16(off)
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
  local idata_off =
    rva2off(buf, pe.pe_header.optional_header.data_directories[2].rva)
  local idt = parse_idt(buf, idata_off)
  local pe32plus = pe.pe_header.optional_header.magic == 0x20b
  local ilt = make_ilt(buf, idt, pe32plus)
  local lb = ListBox{parent=w, pos={0,0}}
  local lv = ListView{parent=w, pos={256,0}}
  w.on_resize = function(wid, hei)
    local halfwid = wid//2
    lb:resize(halfwid, hei)
    lv:configure{pos={halfwid,0}, size={wid-halfwid, hei}}
  end
  lv:insert_column(0, 'Ord.', {width=40})
  lv:insert_column(1, 'Name', {width=176})
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

-- export table

local function parse_edt(buf, off)
  bp(buf, off)()
  local edt = record(function()
    u32 'flags'
    u32 'timestamp'
    u16 'major_version'
    u16 'minor_version'
    u32 'name_rva'
    u32 'ordinal_base'
    u32 'num_addresses'
    u32 'num_names'
    u32 'address_table_rva'
    u32 'name_table_rva'
    u32 'ordinal_table_rva'
  end)
  return edt 'edt'
end

local function export_table(buf)
  local pe = buf:tree()
  local edata_off =
    rva2off(buf, pe.pe_header.optional_header.data_directories[1].rva)
  local edt = parse_edt(buf, edata_off)
  local exp_table = {}
  if edt.name_table_rva ~= 0 then
    local name_off = rva2off(buf, edt.name_table_rva)
    local ord_off = rva2off(buf, edt.ordinal_table_rva)
    local addr_table_off = rva2off(buf, edt.address_table_rva)
    for i=1,edt.num_names do
      local name_rva = buf:peeku32(name_off)
      name_off = name_off + 4
      local ord = buf:peeku16(ord_off)
      ord_off = ord_off + 2
      local addr = buf:peeku32(addr_table_off + ord*4)
      local name = getcstr(buf, rva2off(buf, name_rva))
      exp_table[i] = {name, ord, string.format('0x%x', addr)}
    end
  end
  local w = Window{text='Export table', size={640,480}}
  local lv = ListView{parent=w, pos={0,0}}
  w.on_resize = function(wid, hei)
    lv:resize(wid, hei)
  end
  lv:insert_column(0, 'Name', {width=216})
  lv:insert_column(1, 'Ord.', {width=40})
  lv:insert_column(2, 'Add.', {width=128})
  for i=1,#exp_table do
    lv:insert_item(i-1, exp_table[i])
  end
  w:show()
end

local function loadapi(dllpath, funcname)
  local func, err = package.loadlib(dllpath, funcname)
  if func == nil then
    print(err)
    return
  end
  return func
end

local dasmdll

local function disassembly(buf)
  local disasm = loadapi(dasmdll, 'api_disasm')
  local format_inst = loadapi(dasmdll, 'api_format_inst')

  local pe = buf:tree()
  local sectno = find_section(pe, '.text\0\0\0')
  if sectno == nil then return end
  local sh = pe.section_headers[sectno]
  local code = buf:read(sh.raw_data_offset, sh.raw_data_size)
  local off = 0
  local instlist = {}
  local i
  local total_lines = 256
  for i=1,total_lines do
    local inst = disasm(code, off)
    off = off + string.len(inst.bytes)
    instlist[i] = inst
  end
  local w = Window{text='Disassembly', size={640,480}}
  local me = MonoEdit{parent=w}
  me.source = function(ln)
    if ln < 0 or ln >= total_lines then return '' end
    local inst = instlist[1+ln]
    local bytes = inst.bytes
    local s = bytes:gsub('.', function(c)
      return string.format('%02x', string.byte(c))
    end)
    return string.format('%-32s%s', s, format_inst(inst))
  end
  me.total_lines = total_lines
  w.on_resize = function(wid, hei)
    me:resize(wid, hei)
  end
  w:show()
  me:update()
end

local functions = {
  {section_table, 'Section Table...'},
  {import_table, 'Import Table...'},
  {export_table, 'Export Table...'},
}

local dasm_config = require 'config/dasm'
if dasm_config then
  dasmdll = dasm_config.dllpath
  functions[#functions+1] = {disassembly, 'Disassembly...'}
end

return {
  name = 'PE',
  parser = require 'pe',
  functions = functions
}
