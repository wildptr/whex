local w = Window('luatk')
w.on_close = quit
local lv = ListView('', w)
lv:configure(8,8,192,96)
lv:insert_column(0, 'Name')
lv:insert_column(1, 'Size')
lv:insert_item(0, {'python', '1000'})
lv:insert_item(1, {'lua', '100'})
lv:insert_item(0, {'c', 10});
w:show()
