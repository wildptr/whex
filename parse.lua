parser = require 'pe'
file = io.open(whex.filepath(), 'rb')
tree = parser.parse(file)
whex.set_tree(tree)
