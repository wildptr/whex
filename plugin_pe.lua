local function info(file)
  w = Window.new('PE Info')
  w:show()
end

return {
  parser = require 'pe',
  functions = {
    {info, 'PE Info...'},
  }
}
