



rem @echo off

@echo *****************************************************************
@echo SDK BR25
@echo *****************************************************************
@echo %date%

cd %~dp0


set OBJDUMP=C:\JL\pi32\bin\llvm-objdump.exe
set OBJCOPY=C:\JL\pi32\bin\llvm-objcopy.exe
set ELFFILE=sdk.elf

REM %OBJDUMP% -D -address-mask=0x1ffffff -print-dbg $1.elf > $1.lst
%OBJCOPY% -O binary -j .text %ELFFILE% text.bin
%OBJCOPY% -O binary -j .data %ELFFILE% data.bin
%OBJCOPY% -O binary -j .data_code %ELFFILE% data_code.bin


%OBJCOPY% -O binary -j .overlay_aec %ELFFILE% aeco.bin
%OBJCOPY% -O binary -j .overlay_wav %ELFFILE% wav.bin
%OBJCOPY% -O binary -j .overlay_ape %ELFFILE% ape.bin
%OBJCOPY% -O binary -j .overlay_flac %ELFFILE% flac.bin
%OBJCOPY% -O binary -j .overlay_m4a %ELFFILE% m4a.bin
%OBJCOPY% -O binary -j .overlay_amr %ELFFILE% amr.bin
%OBJCOPY% -O binary -j .overlay_dts %ELFFILE% dts.bin
%OBJCOPY% -O binary -j .overlay_fm %ELFFILE% fmo.bin
%OBJCOPY% -O binary -j .overlay_mp3 %ELFFILE% mp3o.bin
%OBJCOPY% -O binary -j .overlay_wma %ELFFILE% wmao.bin


remove_tailing_zeros -i aeco.bin -o aec.bin -mark ff
remove_tailing_zeros -i fmo.bin -o fm.bin -mark ff
remove_tailing_zeros -i mp3o.bin -o mp3.bin -mark ff
remove_tailing_zeros -i wmao.bin -o wma.bin -mark ff



%OBJDUMP% -section-headers -address-mask=0x1ffffff %ELFFILE%
%OBJDUMP% -t %ELFFILE% > symbol_tbl.txt

copy /b text.bin+data.bin+data_code.bin+aec.bin+wav.bin+ape.bin+flac.bin+m4a.bin+amr.bin+dts.bin+fm.bin+mp3.bin+wma.bin+bank.bin app.bin
copy app.bin soundbox\standard\app.bin
copy br25loader.bin soundbox\standard\br25loader.bin

del soundbox\standard\isd_config.ini
del soundbox\standard\tone.cfg





copy soundbox\standard\isd_config_sdk.ini soundbox\standard\isd_config.ini
copy soundbox\standard\tone_20201113.cfg soundbox\standard\tone.cfg
soundbox\standard\download.bat
