local function dump_resources(buf)
  local ne = buf:tree()
  local hdr = ne.ne_header
  local hdrstart = ne.dos_exe.header.e_lfanew
  local res_table_offset = hdrstart + hdr.resource_table_offset
  local shift = ne.resource_alignment
  for i, ent in pairs(ne.resource_table) do
    for j, res in pairs(ent.resources) do
      local filename = string.format('%d_%d', i, j)
      local offset = res.offset << shift
      local size = res.size << shift
      f = io.open(filename, 'wb')
      local data = buf:peekstr(offset, size)
      f:write(data)
      f:close()
    end
  end
end

return {
  name = 'NewEXE',
  parser = require 'newexe',
  functions = {
    {dump_resources, 'Dump Resources'}
  }
}
