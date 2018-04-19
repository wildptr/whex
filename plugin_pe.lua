local function info(buf)
  local pe = buf:tree()
  local w = Window('PE Info')
  local oh = pe.pe_header.optional_header
  local osver = Label(string.format('OS version: %d.%d', oh.major_os_version, oh.minor_os_version), w)
  osver:configure(8, 8, 128, 16)
  w:show()
end

return {
  name = 'PE',
  parser = require 'pe',
  functions = {
    {info, 'PE Info...'},
  }
}
