local bp = require('binparse')

return function(buf)

  bp(buf)()

  local dos_header = record(function()
    local magic = u16 'e_magic'
    if magic ~= 0x5a4d then
      error(string.format('invalid DOS executable magic: 0x%04x', magic))
    end
    u16 'e_cblp'
    u16 'e_cp'
    u16 'e_crlc'
    u16 'e_cparhdr'
    u16 'e_minalloc'
    u16 'e_maxalloc'
    u16 'e_ss'
    u16 'e_sp'
    u16 'e_csum'
    u16 'e_ip'
    u16 'e_cs'
    u16 'e_lfarlc'
    u16 'e_ovno'
    data(8) 'e_res'
    u16 'e_oemid'
    u16 'e_oeminfo'
    data(20) 'e_res2'
    u32 'e_lfanew'
  end)

  local dos_exe = record(function(dot)
    local doshdr = dos_header 'dos_header'
    data( doshdr.e_lfanew - dot() ) 'data'
  end)

  local data_directory = record(function()
    u32 'rva'
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
    u32 'virtual_address'
    u32 'raw_data_size'
    u32 'raw_data_offset'
    u32 'reloc_offset'
    u32 'linenum_offset'
    u16 'num_reloc'
    u16 'num_linenum'
    u32 'flags'
  end)

  local section = function(pad_len, data_len)
    return record(function()
      data(pad_len) 'pad'
      data(data_len) 'data'
    end)
  end

  local pe = record(function(dot)
    dos_exe 'dos_exe'
    local pehdr = pe_header 'pe_header'
    local num_sections = pehdr.num_sections
    local section_headers = array(section_header, num_sections) 'section_headers'
    foreach(section_headers, function(h)
      return section(h.raw_data_offset - dot(), h.raw_data_size)
    end) 'sections'
  end)

  return pe 'pe'
end
