# Suscan - A realtime DSP library
_**Important note**: if you are looking for the graphical application based on this library, it has been deprecated and removed from the source tree. Suscan (GUI) was a GTK3-based application with poor performance and responsiveness that was eventually replaced by the Qt5 application [SigDigger](https://github.com/BatchDrake/SigDigger). You will still need Suscan (library) in order to compile SigDigger._

Suscan is a realtime DSP processing library. It provides a set of useful abstractions to perform dynamic digital signal analysis and demodulation. Suscan offers features like:

- Multicore-friendly signal processing, based on worker threads
- Generic ASK, FSK, PSK and audio demodulators
- An extensible codec interface
- Configuration file API (XML)
- Source API based on SoapySDR

## Getting the code
Just clone it from the GitHub repository. Make sure you pass `--recurse-submodules` to `git clone` so all required submodules are also cloned, and `-b develop` to get the latest changes from the development branch.

```
% git clone -b develop --recurse-submodules https://github.com/BatchDrake/suscan.git
```

## Building and installing Suscan
In order to build Suscan, you will need the development files for the following packages:

```
sigutils fftw3 sndfile SoapySDR libxml-2.0
```

If you are in a Debian-like operating system, you will also need `cmake` and `build-essential`. 

After installing all dependencies, enter Suscan's source directory and compile by typing:

```
% cd suscan
% mkdir build
% cd build
% cmake ..
% make
```

If the previous commands were successful, you are ready to install Suscan in your system by executing (as root):

```
# make install
```

You can verify your installation by running:
```
% suscan.status
```

If everything went fine, you should see the message:

```
suscan.status: suscan library loaded successfully.
```
