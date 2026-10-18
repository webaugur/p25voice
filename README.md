# p25voice

`p25voice` reads 48 kHz mono signed 16-bit little-endian audio on stdin and writes P25 Phase 1 C4FM baseband in the same format on stdout.

Each 20 ms of speech is one IMBE codeword from [balr0g/imbe_vocoder](https://github.com/balr0g/imbe_vocoder) (Pavel Yazev, GPL-3.0). Nine codewords become one logical data unit (LDU1, then LDU2). The frame layout, Golay and Hamming protection, NID, and the fixed LDU bits follow the boatbod OP25 voice encoder. A symbol is 10 samples, so one LDU is 180 ms and 8640 samples. The four levels are +3, +1, -1, and -3, which is the dibit order `dsd-fme` uses for `+P25p1`.

This program does not key a radio and does not add encryption. With no `--tg` or `--src`, LDU1 keeps the OP25 link control: talkgroup `0x5555`, source `0xAAAAAA`. The default NAC is `0x293`.

```
p25voice [--nac 0x293] [--tg 0x0001] [--src 0x000001]
```

`--tg` or `--src` replaces the LDU1 link control with a group call. The other flag keeps its default (`0x5555` or `0xAAAAAA`) when only one is set.

## Build

```
git clone --recurse-submodules git@github.com:webaugur/p25voice.git
cd p25voice
make
make test
```

`make test` checks that the first 24 symbols are the `dsd-fme` P25 Phase 1 frame sync (`111113113311333313133333`).

`examples/commander-mod-p25.txpipe` is the matching Hamspot Commander TX pipe script. It expects `p25voice` on `PATH`. Copy that file into the Commander `modems/` directory.

## Patent notice

IMBE is the 7200 bit/s Project 25 Phase 1 voice codec. The vocoder sources are a description of that algorithm. Executable objects compiled from them may be covered by one or more patents. Check patent and licensing requirements before using the binary. The same notice applies to mbelib, which the receive path already uses to synthesize this codec.
