# SUScan - Getting Started Guide
SUScan is a graphical signal analysis tool compatible with a variety of signal sources, including some popular SDR devices, GQRX captures and even the soundcard.  It features a switchable real-time spectrum / waterfall view, a blind channel detector and a PSK channel inspector. QAM, OFDM and DSSS support is also planned.

Due to the current experimental status of SUScan, this document will be a brief summary of its most common use case: reverse-engineer PSK channels by discovering its modulation parameters.

## The main interface
This is what you should see after executing SUScan:

![](doc/main-window.png) 

For PSK analysis, we must take into account the following elements:

* **Preferences button**: In the upper-left corner of the window, with a gear-shaped icon. This button opens the source selection dialog and lets you configure the current signal source. Other global settings (most of them related to the channel detector) can also be configured from here.
* **Run/stop button**: Starts / stops a signal capture from the current signal source.
* **Spectrum view**: Frequency domain representation of the signal being analyzed. Supports two modes: spectrogram (default) and waterfall.
* **Spectrum view mode switch**:  By clicking on the button with the tool-shaped icon in the right you can switch between spectrogram and waterfall mode. 
* **Channel overlay button**: Right below the spectrum/waterfall switch button, this button toggles the display of discovered channels on top of the spectrum view.
* **Automatic level adjustment button**: Located after the channel overlay button, it toggles automatic level adjustment. If enabled, the spectrum is automatically rescaled to fit in the spectrum view.
* **Channel list**: Located below the spectrum view. When a signal capture is running, it is filled with all the potential channels found by the channel detector.

## Configuring a signal source
The first step to reverse-engineer a PSK signal is to tell SUScan how signal data is acquired. This is done by clicking on the preferences button:

![](doc/source-settings.png) 

The topmost drop-down list lets you choose the signal source type. SUScan currently supports the following source types:

* **Silent source**: Dummy null device, it produces a sequence of zeroes.
* **WAV File**: Useful when you record a narrow-band radio signal directly from your soundcard. This is what I usually do with HF signals received by my [Sangean ATS 909](http://www.sangean.eu/products/discontinued/ats-909-w.html) that fit in the audible spectrum.
* **GQRX I/Q recording**: If you are a [Gqrx](http://gqrx.dk/) user, you can use its signal capture files as signal source.
* **BladeRF**: Perform live capture from [Nuand's BladeRF SDR](https://nuand.com/) . If you have an [XB-300](https://www.nuand.com/blog/product/amplifier/)  amplifier, you can configure it from here.
* **HackRF**: Perform live capture from [Great Scott Gadget's HackRF](https://greatscottgadgets.com/hackrf/) .
* **ALSA live capture**: Capture samples directly from the soundcard. If you happen to use pulseaudio, you can plug the sound output of another program (like your browser) to SUScan's audio input. This is particularly useful if you are a user of WebSDR services like [UTwente's](http://websdr.ewi.utwente.nl:8901/).

The configuration options vary a lot depending on the source type, but you will usually find the following ones in most of them:

* **Sampling frequency**: Set the sample rate. For live sources, this defines the number of data samples per second delivered to SUScan. For pre-recorded sources, it used to compute frequencies and baudrates correctly and configure throttling. 
* **Center frequency**: For live sources, it corresponds to the tuner frequency in Hz. It is optional for pre-recorded sources as it simply allows you to configure the central frequency in the spectrum.
* **Loop**: For pre-recorded sources, it tells SUScan to keep reading samples from the beginning after reaching the end of file.

## Starting a signal capture and selecting a channel
Click on the Run/Stop button on the top-left corner of the window. If your source was properly configured, you should see a spectrum plot in the spectrum view and (hopefully) a channel list:

![](doc/inmarsat-aero.png) 

The red rectangles overlaid on the spectrum plot represent the parameters of the detected channels. If you zoom horizontally (using the mouse wheel) and center any channel (dragging with the primary mouse button), you will see that the corresponding rectangle is formed by two nested rectangles. The innermost rectangle represents the channel bandwidth as the integral of its PSD divided by its peak PSD, while the outermost one represents the difference between its highest and lowest frequencies. The channel center is always computed using the [autocorrelation technique](https://en.wikipedia.org/wiki/Autocorrelation_technique):

![](doc/bandwidth.png) 

Please note that automatic channel detection is a purely heuristic feature based continuous estimations of the sequelch level and, because of its nature, it could fail (especially when there are lots of channels along the spectrum). In that case, you can manually define a channel by pressing shift while dragging over the frequency span of the channel. You should see a cyan rectangle as you define the new channel limits:

![](doc/manual-selection.png) 

After finding / defining the channel of interest, you are ready start the actual channel analysis by right-clicking on it and choosing "Open PSK inspector":

![](doc/open-inspector.png) 

## The channel inspector tab
After opening the PSK inspector over the desired channel, you should see something like this:

![](doc/inspector.png)

This is the inspector tab. It allows you analyze, detect and configure a set of modulation parameters in a PSK signals. It is basically a configurable generic [PSK](https://en.wikipedia.org/wiki/Phase-shift_keying) demodulator. From left to right, top-down, it features:

* **Constellation plot**: Scatter plot representing the amplitude and phase of the sampler output.
* **Transition plot**: Symbol transition representation, either as a transition matrix or a transition graph. You can switch from one to another just by left-clicking on it.
* **Spectrum plot**: Power spectrum and cyclostationary spectrum analysis of the desired channel, disabled by default.
* **Demodulator properties**: Set of controls to define the channel modulation parameters. It also features a couple of automatic baudrate detectors.
* **Symbol recorder**: After properly configuring the demodulator parameters, it allows you to record, save and process demodulated symbols. 

Even though you can set a bunch of modulation parameters for the channel, for every channel you must define at least two: the **baudrate** and the **constellation type**. **If one of these two is missing, the demodulator will not know when to sample the received signal or how to track the carrier frequency in order to retrieve the variations of the signal phase, and you will not be able to demodulate anything.**

## First step: finding the baudrate of the signal
SUScan provides three ways to detect the baudrate of the signal:
* **Cyclostationary analysis + autocorrelation technique**: Also known as non-linear baudrate detection ([paper here](https://www.iasj.net/iasj?func=fulltext&aId=51991)). This method usually produces the best estimate. However, if there is not enough significate, it will fail. You can perform this detection by clicking on "Detect baudrate (cyclo)" under the "Clock control" (right side of the window).
* **Fast autocorrelation**: It computes an averaged autocorrelation function of the channel signal and returns the inverse of the delay of the first local minimum. The intuition behind this method is the following: if the symbol probability distribution is flat enough (which is usually true, as PSK signals are usually scrambler), the signal will not be correlated at all with itself delayed one symbol. However, this method provides a rough estimate as it depends on the number of coefficients of the autocorrelion function buffer. You can perform this detection by clocking on "Detect baudrate (FAC)"
* **Manual inspection**: If both methods fail, you can click on "Cyclostationary analysis" under "Spectrum source". This will display the frequency spectrum of the function `|x(n) - x(n-1)|`, used by the non-linear baudrate detection. You may use this function to detect peaks that are consistent with a baudrate.

In any case, you can always set the baudrate manually by typing it on the baudrate text box and clicking on "Set baudrate".

The typical procedure to find the baudrate of the signal is like this:
1. Click on "Detect baudrate (cyclo)" several times. If the baudrate text box ot stabilizes to a value that looks valid (1200, 2400, 8000... typical baudrates found out there are usually multiples of 100), you are done. If not (or if the baudrate is zero) move to the next step.

2. Click on "Detect baudrate (FAC)" several times. Again, if it stabilizes to something that looks like a baudrate, you are done. If not, move to the next step. Have in mind however that since this estimate is usually rather inaccurate, you may need to correct it in the baudrate box. It is usually a good idea to compare this result to the cyclostationary spectrum.

3. If both methods above have failed, the only chance you have to find the right baudrate is by manual inspection. Select "cyclostationary analysis" in the spectrum source frame and look for the rightmost (or leftmost) peak in the spectrum plot. For instance, this is what the cyclostationary spectrum of a 1200 baud QPSK signal looks like. Notice the peaks in +/-1200 baud surrounding the DC signal:

![](doc/cyclo.png)

As in the spectrum plot of the main window, you can zoom and re-center the spectrum using your mouse.

If you succeed at finding the baudrate, you can click on "Set baudrate". This enables the sampler and you should see a cloud of points in the constellation plot:

![](doc/cloud.png)

Now, click on "Automatic clock recovery (Gardner's method)". This will use the [Gardner algorithm](http://read.pudn.com/downloads163/ebook/741040/Gardner_algorithm.pdf) to recover the symbol timing in real-time. You should not be able to see a big difference here, because the signal carrier is still not recovered and there is a frequency component in the received samples that makes the whole constellation spin around its center. This is something we will fix in the next step.
