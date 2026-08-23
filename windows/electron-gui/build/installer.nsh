!macro customInstall
  ; Register ASIO driver after installation
  nsExec::ExecToLog 'regsvr32 /s "$INSTDIR\resources\iphone_asio_driver.dll"'
!macroend

!macro customUnInstall
  ; Unregister ASIO driver before uninstall
  nsExec::ExecToLog 'regsvr32 /u /s "$INSTDIR\resources\iphone_asio_driver.dll"'
!macroend
