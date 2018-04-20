local w = Window{text='luatk'}
w.on_close = quit
local lv = ListView{parent=w, pos={8,8}, size={256,128}}
lv:insert_column(0, 'Name')
lv:insert_column(1, 'Size')
lv:insert_item(0, {'python', '1000'})
lv:insert_item(1, {'lua', '100'})
lv:insert_item(0, {'c', 10});
w:show()
