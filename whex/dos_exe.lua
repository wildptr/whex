return function()

  local header = record(function()
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

  return record(function(self, pos)
    header 'header'
    data(self.header.e_lfanew - pos()) 'data'
  end)

end
