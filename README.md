# pico-wumpus
Hunt the Wumpus for the Raspberry Pico

Building it.

First install the Raspberry Pico SDK. Then

```sh
git clone https://github.com/lurk101/pico-wumpus.git
cd pico-wumpus
```

Then edit CMakeList.txt and adjust the PICO_SDK_PATH. Then

```sh
mkdir build
cd build
cmake ..
make
```
Running it

Use your prefered documented method for loading and running the
code on the Pico.

Your terminal program must be set to ECHO mode. Unlike the Linux
shell, the Pico does not automatically send back every character
it receives.
