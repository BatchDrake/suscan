# Suscan - A realtime DSP library
_**Important note**: if you are looking for the graphical application based on this library, it has been deprecated and removed from the source tree. Suscan (GUI) was a GTK3-based application with poor performance and responsiveness that was eventually replaced by the Qt5 application [SigDigger](https://github.com/BatchDrake/SigDigger). You will still need Suscan (library) in order to compile SigDigger._

Suscan is a realtime DSP processing library. It provides a set of useful abstractions to perform dynamic digital signal analysis and demodulation. Suscan offers features like:

- Multicore-friendly signal processing, based on worker threads
- Generic ASK, FSK, PSK and audio demodulators
- An extensible codec interface
- Configuration file API (XML)
- Source API based on SoapySDR

## Building and installing Suscan
In order to build Suscan, you will need the development files for the following packages:

```
sigutils fftw3 sndfile SoapySDR libxml
```

If you are in a Debian-like operating system, you will also need `autoconf`, `automake`, `libtool` and `build-essential`. 

After installing all dependencies, Suscan can be compiled executing the following commands:

```
% ./autogen.sh
% ,/configure
% make
% sudo make install
```

You can verify your installation by running:
```
% suscan
```

If everything went fine, you should see the message:

```
suscan: suscan library loaded successfully.
```
