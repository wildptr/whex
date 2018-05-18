local bp = require('binparse')

local function rva2off(pe, rva)
  for i,s in pairs(pe.section_headers) do
    local voff = rva - s.virtual_address
    if voff >= 0 and voff < s.raw_data_size then
      return s.raw_data_offset + voff
    end
  end
  return -- not found
end

local function format_rva(pe, rva)
  local off = rva2off(pe, rva)
  if off then
    return string.format('%x (%x)', rva, off)
  end
  return string.format('%x (not in file)', rva)
end

return function(buf)

  bp(buf)()
  local rva = newtype(u32, {name='rva', format=format_rva})

  local dos_exe = require('dos_exe')()

  local data_directory = record(function()
    rva 'rva'
    u32 'size'
  end)

  local optional_header = record(function()
    local magic = u16 'magic'
    local ispe32plus
    if magic == 0x10b then
      ispe32plus = false
    elseif magic == 0x20b then
      ispe32plus = true
    else
      error(string.format('invalid optional header magic: 0x%04x', magic))
    end
    usize = ispe32plus and u64 or u32
    u8 'major_linker_version'
    u8 'minor_linker_version'
    u32 'code_size'
    u32 'data_size'
    u32 'bss_size'
    u32 'entry_point_address'
    u32 'code_base'
    if not ispe32plus then u32 'data_base' end
    usize 'image_base'
    u32 'section_align'
    u32 'file_align'
    u16 'major_os_version'
    u16 'minor_os_version'
    u16 'major_image_version'
    u16 'minor_image_version'
    u16 'major_subsystem_version'
    u16 'minor_subsystem_version'
    u32 'win32_version_value'
    u32 'image_size'
    u32 'headers_size'
    u32 'checksum'
    u16 'subsystem'
    u16 'dll_flags'
    usize 'stack_size_reserve'
    usize 'stack_size_commit'
    usize 'heap_size_reserve'
    usize 'heap_size_commit'
    u32 'loader_flags'
    local ndir = u32 'num_data_directories'
    array(data_directory, ndir) 'data_directories'
  end)

  local pe_header = record(function()
    local magic = u32('pe_signature')
    if magic ~= 0x4550 then
      error(string.format('invalid PE signature: 0x%08x', magic))
    end
    u16 'machine_type'
    u16 'num_sections'
    u32 'timestamp'
    u32 'symbol_table_offset'
    u32 'num_symbols'
    u16 'optional_header_size'
    u16 'flags'
    optional_header 'optional_header'
  end)

  local section_header = record(function()
    ascii(8) 'name'
    u32 'virtual_size'
    rva 'virtual_address'
    u32 'raw_data_size'
    u32 'raw_data_offset'
    u32 'reloc_offset'
    u32 'linenum_offset'
    u16 'num_reloc'
    u16 'num_linenum'
    u32 'flags'
  end)

  local section = function(offset, size)
    return record(function(self, pos)
      if (offset ~= 0) then
        skip(offset - pos())
        data(size) 'data'
      end
    end)
  end

  local pe = record(function(self)
    dos_exe 'dos_exe'
    pe_header 'pe_header'
    array(section_header, self.pe_header.num_sections) 'section_headers'
    foreach(self.section_headers, function(h)
      return section(h.raw_data_offset, h.raw_data_size)
    end) 'sections'
  end)

  return pe 'pe'
end
