parser = require 'pe'
file = io.open('whex.exe', 'rb')
tree = parser.parse(file)
set_tree(tree)
