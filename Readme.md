# Rolanduino – An Arduino piano/synthesizer

There are quite a few old Roland digital pianos lying around, but parts are impossible to obtain. I had an old HP-237Re whose mechanics were OK but the logic board had died. This project combines the scanning of the keyboard with a synthesizer based on a fork of [Arduino-midi-sound-module](https://github.com/DLehenbauer/arduino-midi-sound-module). I wondered if a Mega2560 could both scan the keyboard and manage the sound production seamlessly…

## Scanning the keyboard

There are many models of these old keyboards but the scanning details are usually quite similar.

The keyboard is wired in groups of 8 keys (11 groups = 88 keys in total) The 8 T-lines are normally high, but each is pulled low in turn to sense each of the 8 notes in each group. Each group has a half-press and full-press line, the timing between them is used to sense key velocity.

In the service manuals they are called SM and PM lines (I don't know what this stands for) but some older keyboards have break and make lines instead.

So there are 11 groups \* 2 bits = 22 bits = 3 bytes reported back from each scan, for a total of 24 bytes from 8 scans.

The PM/SM lines are wired as follows on the Mega to the ports on the heel of the board (the double row connector) while the T lines are connected to port F. There are a lot of wires!

The old logic board is connected to the keyboard by a couple of flat flexible (FFC) cables. These are fragile and unobtainable, as are their sockets. Fortunately the logic board has holes for a couple of different types of keyboards which could be used for 0.1” pitch pins. I sawed the rest of the logic board off to retain the FFC sockets.

![](media/d71ec5a818ed4c34e2ea6b94091cdb35.jpeg)

I 3D-printed a carrier to mount the Mega on the original standoffs inside the piano case. The audio output circuit is the same as the one described in Arduino-midi-sound-module, but split into two channels for stereo because the original piano had left and right speakers. Once wired up it looked like this. Did I say a lot of wires?

![](media/dd07211440bc3176bbf10515274861e6.jpeg)![](media/5d6ae1e50402c320fb852c3072465790.jpeg)
