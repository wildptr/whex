local bp = require('binparse')

return function(buf)
  bp(buf)()
  local dos_exe = require('dos_exe')()

  local ne_header = record(function()
    u16 'magic'
    u8 'linker_version'
    u8 'linker_revision'
    u16 'entry_table_offset' -- relative to NewEXE header
    u16 'entry_table_size'
    u32 'crc'
    u16 'flags'
    u16 'auto_data_segment'
    u16 'heap_size'
    u16 'stack_size'
    u32 'cs_ip'
    u32 'ss_sp'
    u16 'num_segments'
    u16 'num_module_references'
    u16 'non_resident_name_table_size'
    u16 'segment_table_offset'
    u16 'resource_table_offset'
    u16 'resident_name_table_offset'
    u16 'module_reference_table_offset'
    u16 'imported_name_table_offset'
    u32 'non_resident_name_table_offset' -- relative to beginning of file
    u16 'num_movable_entries'
    u16 'alignment'
    u16 'num_resources'
    u8 'type'
    data(9) 'reserved'
  end)

  local segment_table_entry = record(function()
    u16 'offset'
    u16 'size'
    u16 'flags'
    u16 'virtual_size'
  end)

  local resource_table = record(function(self, pos)
    local entry = record(function(self)
      u16 'type'
      u16 'count'
      u32 'reserved'
      local resource = record(function()
        u16 'offset'
        u16 'size'
        u16 'flags'
        u16 'id'
        u32 'reserved'
      end)
      array(resource, self.count) 'resources'
    end)
    local i=1
    while true do
      local ent = entry(i)
      if buf:peeku16(pos()) == 0 then
        data(2)()
        break
      end
      i=i+1
    end
  end)

  local counted_string = record(function(self)
    u8 'length'
    ascii(self.length) 'data'
  end)

  local resource_strings = record(function(self, pos)
    local i=1
    while true do
      local str = counted_string(i)
      if buf:peek(pos()) == 0 then
        data(1)()
        break
      end
      i=i+1
    end
  end)

  local newexe = record(function(self, pos)
    dos_exe 'dos_exe'
    local hdrstart = pos()
    local hdr = ne_header 'ne_header'
    data(pos() - (hdrstart + hdr.segment_table_offset))()
    array(segment_table_entry, hdr.num_segments) 'segment_table'
    data(pos() - (hdrstart + hdr.resource_table_offset))()
    u16 'resource_alignment'
    resource_table 'resource_table'
    resource_strings 'resource_strings'
  end)

  return newexe 'newexe'
end
