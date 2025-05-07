@echo on

REM 请使用ANSI编码保存此文件
REM 如不打包烧写文件，请注释掉Firmware/value配置

固件升级程序.exe --merge data --target 固件升级程序_包含固件.exe

pause
