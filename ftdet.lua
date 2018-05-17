return function(buf, ext)
  local size = buf:size()
  if size >= 0x40 then
    if buf:read(0, 2) == 'MZ' then
      local e_lfanew = buf:peeku32(0x3c)
      local magic = buf:read(e_lfanew, 2)
      if magic == 'PE' then
        if buf:read(e_lfanew+2, 2) == '\0\0' then
          return 'pe'
        end
      elseif magic == 'NE' then
        return 'newexe'
      end
      return 'dosexe'
    end
  end
end
