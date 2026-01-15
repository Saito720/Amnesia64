!include x64.nsh
Name "libjpeg-turbo SDK for Visual C++ 64-bit"
OutFile "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}libjpeg-turbo-3.1.3-vc-x64.exe"
InstallDir "c:\libjpeg-turbo64"

SetCompressor bzip2

Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

Section "libjpeg-turbo SDK for Visual C++ 64-bit (required)"
!ifdef WIN64
	${If} ${RunningX64}
	${DisableX64FSRedirection}
	${Endif}
!endif
	SectionIn RO
!ifdef GCC
	IfFileExists $SYSDIR/libturbojpeg.dll exists 0
!else
	IfFileExists $SYSDIR/turbojpeg.dll exists 0
!endif
	goto notexists
	exists:
!ifdef GCC
	MessageBox MB_OK "An existing version of the libjpeg-turbo SDK for Visual C++ 64-bit is already installed.  Please uninstall it first."
!else
	MessageBox MB_OK "An existing version of the libjpeg-turbo SDK for Visual C++ 64-bit or the TurboJPEG SDK is already installed.  Please uninstall it first."
!endif
	quit

	notexists:
	SetOutPath $SYSDIR
!ifdef GCC
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\libturbojpeg.dll"
!else
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}turbojpeg.dll"
!endif
	SetOutPath $INSTDIR\bin
!ifdef GCC
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\libturbojpeg.dll"
!else
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}turbojpeg.dll"
!endif
!ifdef GCC
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\libjpeg-62.dll"
!else
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}jpeg62.dll"
!endif
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}cjpeg.exe"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}djpeg.exe"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}jpegtran.exe"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}tjbench.exe"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}rdjpgcom.exe"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}wrjpgcom.exe"
	SetOutPath $INSTDIR\lib
!ifdef GCC
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\libturbojpeg.dll.a"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\libturbojpeg.a"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\libjpeg.dll.a"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\libjpeg.a"
!else
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}turbojpeg.lib"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}turbojpeg-static.lib"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}jpeg.lib"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\${BUILDDIR}jpeg-static.lib"
!endif
	SetOutPath $INSTDIR\lib\pkgconfig
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\pkgscripts\libjpeg.pc"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\pkgscripts\libturbojpeg.pc"
	SetOutPath $INSTDIR\lib\cmake\libjpeg-turbo
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\pkgscripts\libjpeg-turboConfig.cmake"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\pkgscripts\libjpeg-turboConfigVersion.cmake"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\win\libjpeg-turboTargets.cmake"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\win\libjpeg-turboTargets-release.cmake"
!ifdef JAVA
	SetOutPath $INSTDIR\classes
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\java\turbojpeg.jar"
!endif
	SetOutPath $INSTDIR\include
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3/build-vs2026-x64\jconfig.h"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\src\jerror.h"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\src\jmorecfg.h"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\src\jpeglib.h"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\src\turbojpeg.h"
	SetOutPath $INSTDIR\doc
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\README.ijg"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\README.md"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\LICENSE.md"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\src\example.c"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\doc\libjpeg.txt"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\doc\structure.txt"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\doc\usage.txt"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\doc\wizard.txt"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\src\tjcomp.c"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\src\tjdecomp.c"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\src\tjtran.c"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\java\TJComp.java"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\java\TJDecomp.java"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\java\TJTran.java"
!ifdef GCC
	SetOutPath $INSTDIR\man\man1
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\doc\cjpeg.1"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\doc\djpeg.1"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\doc\jpegtran.1"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\doc\rdjpgcom.1"
	File "C:/Amnesia64/HPL2/dependencies/sources/libjpeg-turbo-3.1.3\doc\wrjpgcom.1"
!endif

	WriteRegStr HKLM "SOFTWARE\libjpeg-turbo64 3.1.3" "Install_Dir" "$INSTDIR"

	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo64 3.1.3" "DisplayName" "libjpeg-turbo SDK v3.1.3 for Visual C++ 64-bit"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo64 3.1.3" "UninstallString" '"$INSTDIR\uninstall_3.1.3.exe"'
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo64 3.1.3" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo64 3.1.3" "NoRepair" 1
	WriteUninstaller "uninstall_3.1.3.exe"
SectionEnd

Section "Uninstall"
!ifdef WIN64
	${If} ${RunningX64}
	${DisableX64FSRedirection}
	${Endif}
!endif

	SetShellVarContext all

	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo64 3.1.3"
	DeleteRegKey HKLM "SOFTWARE\libjpeg-turbo64 3.1.3"

!ifdef GCC
	Delete $INSTDIR\bin\libjpeg-62.dll
	Delete $INSTDIR\bin\libturbojpeg.dll
	Delete $SYSDIR\libturbojpeg.dll
	Delete $INSTDIR\lib\libturbojpeg.dll.a
	Delete $INSTDIR\lib\libturbojpeg.a
	Delete $INSTDIR\lib\libjpeg.dll.a
	Delete $INSTDIR\lib\libjpeg.a
!else
	Delete $INSTDIR\bin\jpeg62.dll
	Delete $INSTDIR\bin\turbojpeg.dll
	Delete $SYSDIR\turbojpeg.dll
	Delete $INSTDIR\lib\jpeg.lib
	Delete $INSTDIR\lib\jpeg-static.lib
	Delete $INSTDIR\lib\turbojpeg.lib
	Delete $INSTDIR\lib\turbojpeg-static.lib
!endif
	Delete $INSTDIR\lib\pkgconfig\libjpeg.pc
	Delete $INSTDIR\lib\pkgconfig\libturbojpeg.pc
	Delete $INSTDIR\lib\cmake\libjpeg-turbo\libjpeg-turboConfig.cmake
	Delete $INSTDIR\lib\cmake\libjpeg-turbo\libjpeg-turboConfigVersion.cmake
	Delete $INSTDIR\lib\cmake\libjpeg-turbo\libjpeg-turboTargets.cmake
	Delete $INSTDIR\lib\cmake\libjpeg-turbo\libjpeg-turboTargets-release.cmake
!ifdef JAVA
	Delete $INSTDIR\classes\turbojpeg.jar
!endif
	Delete $INSTDIR\bin\cjpeg.exe
	Delete $INSTDIR\bin\djpeg.exe
	Delete $INSTDIR\bin\jpegtran.exe
	Delete $INSTDIR\bin\tjbench.exe
	Delete $INSTDIR\bin\rdjpgcom.exe
	Delete $INSTDIR\bin\wrjpgcom.exe
	Delete $INSTDIR\include\jconfig.h
	Delete $INSTDIR\include\jerror.h
	Delete $INSTDIR\include\jmorecfg.h
	Delete $INSTDIR\include\jpeglib.h
	Delete $INSTDIR\include\turbojpeg.h
	Delete $INSTDIR\uninstall_3.1.3.exe
	Delete $INSTDIR\doc\README.ijg
	Delete $INSTDIR\doc\README.md
	Delete $INSTDIR\doc\LICENSE.md
	Delete $INSTDIR\doc\example.c
	Delete $INSTDIR\doc\libjpeg.txt
	Delete $INSTDIR\doc\structure.txt
	Delete $INSTDIR\doc\usage.txt
	Delete $INSTDIR\doc\wizard.txt
	Delete $INSTDIR\doc\tjcomp.c
	Delete $INSTDIR\doc\tjdecomp.c
	Delete $INSTDIR\doc\tjtran.c
	Delete $INSTDIR\doc\TJComp.java
	Delete $INSTDIR\doc\TJDecomp.java
	Delete $INSTDIR\doc\TJTran.java
!ifdef GCC
	Delete $INSTDIR\man\man1\cjpeg.1
	Delete $INSTDIR\man\man1\djpeg.1
	Delete $INSTDIR\man\man1\jpegtran.1
	Delete $INSTDIR\man\man1\rdjpgcom.1
	Delete $INSTDIR\man\man1\wrjpgcom.1
!endif

	RMDir "$INSTDIR\include"
	RMDir "$INSTDIR\lib\pkgconfig"
	RMDir "$INSTDIR\lib\cmake\libjpeg-turbo"
	RMDir "$INSTDIR\lib\cmake"
	RMDir "$INSTDIR\lib"
	RMDir "$INSTDIR\doc"
!ifdef GCC
	RMDir "$INSTDIR\man\man1"
	RMDir "$INSTDIR\man"
!endif
!ifdef JAVA
	RMDir "$INSTDIR\classes"
!endif
	RMDir "$INSTDIR\bin"
	RMDir "$INSTDIR"

SectionEnd
