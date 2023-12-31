# 1MHz-Bandwidth Mixed Signal Oscilloscope

- Created a cohesive mixed signal oscilloscope system integrating both hardware and software components.

- Designed a 50-ohm, 1MHz bandwidth analog front end with versatile signal processing capabilities
including, AC/DC coupled inputs, single-ended to differential signal conversion, SPI controlled variable gain
and attenuation, precise DC offset biasing, a 2nd order active anti-aliasing filter, and precise analog trigger
circuitry to interface with an 10 MSPS ADC.

- Developed Arduino C++ code for an ESP32 microcontroller to control an ILI9341 LCD display, enabling
real-time waveform plotting, XY cursors, frequency, period, duty cycle, and RMS measurements.
Implemented user-friendly controls via potentiometers and push buttons for vertical and horizontal scaling,
trigger level adjustment, and DC offset. Applied signal processing techniques for signal amplification,
attenuation, and dynamic upsampling and downsampling.

- A USB port outputs 5V of power so that an external microcontroller can be connected. The ADC output can also be read
  by an external microcontroller or FPGA. Header connectors can be used to read data from the analog front end and send
  control siignals to control signal conditioning.
