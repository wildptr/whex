root = Frame{}
button = Button{parent=root, width=64, height=24, row=0, column=0}
button.command = function(w) print('hello') end
entry = Entry{parent=root, width=128, height=24, row=0, column=1, rowspan=2, sticky='nswe'}
button2 = Button{parent=root, width=64, height=24, row=1, column=0}
root:rowconfigure{0, 1, weight=1}
root:columnconfigure{0, weight=1, minsize=128}
root:columnconfigure{1, weight=3}
root:show()

root = Frame{width=320, height=240}
Button{parent=root, width=48, height=48, padx=4, pady=4, text='1', row=0, column=0, sticky='nw'}
Button{parent=root, width=48, height=48, padx=4, pady=4, text='2', row=0, column=1, sticky='n'}
Button{parent=root, width=48, height=48, padx=4, pady=4, text='3', row=0, column=2, sticky='ne'}
Button{parent=root, width=48, height=48, padx=4, pady=4, text='4', row=1, column=0, sticky='w'}
Button{parent=root, width=48, height=48, padx=4, pady=4, text='5', row=1, column=1}
Button{parent=root, width=48, height=48, padx=4, pady=4, text='6', row=1, column=2, sticky='e'}
Button{parent=root, width=48, height=48, padx=4, pady=4, text='7', row=2, column=0, sticky='sw'}
Button{parent=root, width=48, height=48, padx=4, pady=4, text='8', row=2, column=1, sticky='s'}
Button{parent=root, width=48, height=48, padx=4, pady=4, text='9', row=2, column=2, sticky='se'}
root:rowconfigure{0, 1, 2, weight=1}
root:columnconfigure{0, 1, 2, weight=1}
root:show()
