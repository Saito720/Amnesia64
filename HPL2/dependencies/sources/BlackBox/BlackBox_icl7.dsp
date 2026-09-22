# Microsoft Developer Studio Project File - Name="BlackBox" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=BlackBox - Win32 Release
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "BlackBox.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "BlackBox.mak" CFG="BlackBox - Win32 Release"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "BlackBox - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "BlackBox - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "BlackBox - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "icl7\Release\"
# PROP Intermediate_Dir "icl7\Release\"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /MD /W4 /WX /GX /O1 /I "./include" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "BLACKBOX_DLL" /D "BLACKBOX_EXPORTS" /D "STRICT" /D "WORK_AROUND_SRCLINE_BUG" /Yu"stdafx.h" /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo /o"./BlackBox_icl7.bsc"
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 user32.lib gdi32.lib advapi32.lib shell32.lib comdlg32.lib dbghelp.lib PSAPI.lib /nologo /dll /machine:I386 /out:"..\..\bin\BlackBox_icl7.dll" /libpath:".\lib"
# SUBTRACT LINK32 /pdb:none /debug

!ELSEIF  "$(CFG)" == "BlackBox - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "icl7\Debug\"
# PROP Intermediate_Dir "icl7\Debug\"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /Yu"stdafx.h" /FD /GZ /c

# ADD CPP /nologo /MDd /W4 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "BLACKBOX_DLL" /D "BLACKBOX_EXPORTS" /D "STRICT" /D "WORK_AROUND_SRCLINE_BUG" /Yu"stdafx.h" /FD /GZ /c
# SUBTRACT CPP /WX /FR

# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo /o"./BlackBox_icl7_d.bsc"
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib dbghelp.lib PSAPI.lib /nologo /dll /debug /machine:I386 /out:"..\..\bin\BlackBox_icl7_d.dll" /pdbtype:sept
# SUBTRACT LINK32 /nodefaultlib

!ENDIF 

# Begin Target

# Name "BlackBox - Win32 Release"
# Name "BlackBox - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=AllocAndFillProcessModuleList.cpp
# End Source File
# Begin Source File

SOURCE=BlackBox.cpp
# End Source File
# Begin Source File

SOURCE=BlackBoxUI.cpp
# End Source File
# Begin Source File

SOURCE=BSUFunctions.cpp
# End Source File
# Begin Source File

SOURCE=CrashHandler.cpp
# End Source File
# Begin Source File

SOURCE=DiagAssert.cpp
# End Source File
# Begin Source File

SOURCE=GetLoadedModules.cpp
# End Source File
# Begin Source File

SOURCE=HookImportedFunctionByName.cpp
# End Source File
# Begin Source File

SOURCE=HookOrdinalExport.cpp
# End Source File
# Begin Source File

SOURCE=IsNT.cpp
# End Source File
# Begin Source File

SOURCE=MemDumperValidator.cpp
# End Source File
# Begin Source File

SOURCE=MemStress.cpp
# End Source File
# Begin Source File

SOURCE=NT4ProcessInfo.cpp
# End Source File
# Begin Source File

SOURCE=StdAfx.cpp
# ADD CPP /Yc"stdafx.h"
# End Source File
# Begin Source File

SOURCE=TLHELPProcessInfo.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=BlackBox.h
# End Source File
# Begin Source File

SOURCE=BlackBoxUI.h
# End Source File
# Begin Source File

SOURCE=BugslayerUtil.h
# End Source File
# Begin Source File

SOURCE=CrashHandler.h
# End Source File
# Begin Source File

SOURCE=CriticalSection.h
# End Source File
# Begin Source File

SOURCE=CRTDBG_Internals.h
# End Source File
# Begin Source File

SOURCE=DiagAssert.h
# End Source File
# Begin Source File

SOURCE=Internal.h
# End Source File
# Begin Source File

SOURCE=MemDumperValidator.h
# End Source File
# Begin Source File

SOURCE=MemStress.h
# End Source File
# Begin Source File

SOURCE=MemStressConstants.h
# End Source File
# Begin Source File

SOURCE=MSJDBG.h
# End Source File
# Begin Source File

SOURCE=PSAPI.H
# End Source File
# Begin Source File

SOURCE=resource.h
# End Source File
# Begin Source File

SOURCE=StdAfx.h
# End Source File
# Begin Source File

SOURCE=SymbolEngine.h
# End Source File
# Begin Source File

SOURCE=WarningsOff.h
# End Source File
# Begin Source File

SOURCE=WarningsOn.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=BlackBox.rc
# End Source File
# Begin Source File

SOURCE=bug.ico
# End Source File
# End Group
# Begin Source File

SOURCE=ReadMe.txt
# End Source File
# End Target
# End Project


