@echo off

:: Make sure this is a decent name and not generic
set exe_name=c0.exe

:: Debug = 0, Release = 1
if "%1" == "1" (
	set release_mode=1
) else if "%1" == "release" (
	set release_mode=1
) else (
	set release_mode=0
)


set compiler_flags= -nologo -Oi -fp:precise -Gm- -MP -FC -EHsc- -GR- -GF
set compiler_flags=%compiler_flags% -TP
set compiler_defines=

if %release_mode% EQU 0 ( rem Debug
	set compiler_flags=%compiler_flags% -Od -MDd -Z7
) else ( rem Release
	set compiler_flags=%compiler_flags% -O2 -MT -Z7
)

set compiler_warnings= ^
	-W4 -WX ^
	-wd4100 -wd4101 -wd4127 -wd4146 ^
	-wd4505 -wd4200 -wd4201 ^
	-wd4456 -wd4457

set compiler_includes= ^
	/Isrc\
set libs= ^
	kernel32.lib ^
	Synchronization.lib ^
	tildebackend.lib

set linker_flags= -incremental:no -opt:ref -subsystem:console

if %release_mode% EQU 0 ( rem Debug
	set linker_flags=%linker_flags% -debug
) else ( rem Release
	set linker_flags=%linker_flags% -debug
)

set compiler_settings=%compiler_includes% %compiler_flags% %compiler_warnings% %compiler_defines%
set linker_settings=%libs% %linker_flags%

del *.pdb > NUL 2> NUL
del *.ilk > NUL 2> NUL

cl %compiler_settings% "src\main.c" /link %linker_settings% -OUT:%exe_name%
if %errorlevel% neq 0 goto end_of_build

%exe_name%