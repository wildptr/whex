local T = {
  exe = 'pe',
  dll = 'pe',
}

return function(ext)
  return T[ext]
end
