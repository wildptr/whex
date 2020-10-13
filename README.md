# whex
This is a hexadecimal editor for all versions of desktop Windows since Windows
98. Be aware that this program is not yet stable enough for everyday use. Also,
it is still under heavy development and things may change drastically.

## Features

* Handles large (4GB+) files
* Fast insertion/replacement/deletion anywhere in file
* Highly extensible and fully scriptable with Lua (highly incomplete)
* Structured binary editing a la 010 Editor's Binary Templates, using Lua as
  the format description language
* A Tk-style Lua GUI library enabling easy GUI plugin development (highly incomplete)
* Uses nothing other than native Win32 API for GUI

## Technical Details

The Lua included here is Lua 5.3.6 extended with functions that handle
wide-string paths.
