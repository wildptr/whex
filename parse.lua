parser = require 'pe'
file = io.open(whex.filepath(), 'rb')
tree = parser.parse(file)
print('parsing done')
whex.set_tree(tree)
