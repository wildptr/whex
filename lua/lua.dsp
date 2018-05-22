# Microsoft Developer Studio Project File - Name="lua" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=lua - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "lua.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "lua.mak" CFG="lua - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "lua - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "lua - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "lua - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "../Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LUA_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LUA_EXPORTS" /D "LUA_BUILD_AS_DLL" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386

!ELSEIF  "$(CFG)" == "lua - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "../Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LUA_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LUA_EXPORTS" /D "LUA_BUILD_AS_DLL" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "lua - Win32 Release"
# Name "lua - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=".\lua-5.3.4\src\lapi.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lauxlib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lbaselib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lbitlib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lcode.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lcorolib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lctype.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ldblib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ldebug.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ldo.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ldump.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lfunc.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lgc.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\linit.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\liolib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\llex.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lmathlib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lmem.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\loadlib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lobject.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lopcodes.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\loslib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lparser.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lstate.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lstring.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lstrlib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ltable.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ltablib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ltm.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lundump.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lutf8lib.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lvm.c"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lzio.c"
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=".\lua-5.3.4\src\lapi.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lauxlib.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lcode.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lctype.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ldebug.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ldo.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lfunc.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lgc.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\llex.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\llimits.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lmem.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lobject.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lopcodes.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lparser.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lprefix.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lstate.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lstring.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ltable.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\ltm.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lua.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\luaconf.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lualib.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lundump.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lvm.h"
# End Source File
# Begin Source File

SOURCE=".\lua-5.3.4\src\lzio.h"
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
