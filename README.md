Same as kernel:
This was made with Claude Fable 5 — just for fun (and to obviously test Anthropic's new model).
I almost never use AI during any normal work!

Pixel 4a (sunfish) on a mainline 7.1 kernel (https://github.com/lopro104/linux, branch 7.1-port).

Works:
- Display, touch, haptics, Wi-Fi, Bluetooth, NFC, USB
- Speakers (secondary TDM, CS35L41 x2 with speaker protection firmware + factory calibration)
- Microphones (RT5514P DSP mode over SPI, tertiary TDM)
- Rear and front camera (libcamera simple pipeline + soft ISP, https://github.com/lopro104/libcamera-sunfish),
  rear autofocus (LC898219XI, contrast detect)
- Sensors through the ADSP sensor core: accelerometer, gyroscope, magnetometer, pressure, light
  (adaptive brightness), proximity (sensors/)
- Battery percentage (coulomb counting in qcom_qg)
- CPU boosts on touch and app launch (power/)

In progress:
- GPS: modem QMI LOC HAL (gnss/), waiting for an outdoor test
- Display: occasional DSI FIFO errors (white screen) during screen on/off

Doesn't work:
- Fingerprint, calls/SIM

Bring-up tools in tools/ (ssclist, qrtrls, locget, camcap).

If something dosen't work in postmarketOS then it probably wont work here too!
