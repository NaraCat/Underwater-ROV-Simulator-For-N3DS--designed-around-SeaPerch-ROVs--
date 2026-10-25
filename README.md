# Underwater-ROV-Simulator-For-N3DS-(designed around SeaPerch ROVs)
A homebrew app for the N3DS systems designed for practicing controlling ROVs similar to SeaPerch ROVs.

THIS IS NOT AN OFFICIAL SeaPerch PRODUCT

## Features
- **A default bot and course to practice with:** It has a to-scale model of my team's bot and the official SeaPerch obstical course, all with physics to closely match what my team recorded in real life. 
- **A timer on the touch screen:** You can time your runs down to the nearest tenth of a second using the timer on the bottom screen.

## Installations

**Before installation, you will need:**
- The 3DS/DS package from devkitpro


Clone the repository from github onto your local device. Then, navigate to the 'Underwater ROV simulator' directory. Lastly make clean && make in the directory with the make file.
```bash
# Clone the ropository
git clone https://github.com/NaraCat/Underwater-ROV-Simulator-For-N3DS--designed-around-SeaPerch-ROVs--.git

# Navigate to the directory with the Makefile
cd Underwater-ROV-Simulator-For-N3DS--designed-around-SeaPerch-ROVs--/'Underwater ROV simulator'/

# Build project
make clean && make
```

Now, you should see a Game-Engine.elf and a Game-Engine.3dsx (or similar name, the .3dsx is the important one) in the project directory with the makefile. To run the game, you need to either run it in an emulator or put it on a modded 3DS in the directory where you launch homebrew apps.

## Important Details
**This will not automatically download the dependencies for you. You will need to download them (the 3DS/DS package from devkitpro) yourself.**

**Audio will not work on most emulators or on a 3DS without NDSP:** This project uses NDSP for audio, which most emulators do not have by default. Also, you will need to have NDSP download on your modded 3DS for the audio to work properly. Most mod guides include this automatically, so you probably already have it installed.

**This has only been tested on a New 3DS xl** The resource usage was low enough that it will probably run perfectly fine on a old 3DS, especially with the 3D slider off/all the way down. I'm also pretty certain that I should be able to make some changes in the future to make it more optimized and not function poorly at less than 60 fps.
