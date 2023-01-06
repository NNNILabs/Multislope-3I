# Multislope 3I 
## Introduction
This repository contains hardware and software files for the third multislope prototype board, designated 3I. This is the most complete and proper version of the PCB so far, including a proper 8-resistor network, an LM399/ADR1399 reference, composite integrator (with two SOIC-8 footprints), shielding on the integrator summing node and test points to easily probe crucial nodes. 
## Project Motivation
The second prototype PCB was completed hastily with not much thought put into either layout or choice of parts, so a better-designed (and hopefully better-evaluated) board was in order. 
## Usage
The KiCAD project files can be used to generate gerber files, which can be used to manufacture the PCBs. The code is written in C/C++ in the VScode development environment. Detailed instructions on how to compile the software will be added later. A .uf2 file is present in the project outputs which can be uploaded directly to the Pi Pico. 
## Board Image
![Front side of the PCB](https://github.com/NNNILabs/Multislope-3I/blob/main/Resources/front.PNG)
## Results
The readings from the overall converter (PWM counts and residue) were noisy, and the raw readings from the MCP3202 residue ADC were noisy and contained unexplained banding. 
### Linearity
![Linearity](https://github.com/NNNILabs/Multislope-3I/blob/main/Resources/linearity.png)
### MCP3202 Raw Data (Detail)
![Noise](https://github.com/NNNILabs/Multislope-3I/blob/main/Resources/noise.PNG)
### Hypotheses
- Software bug that causes PWM timing or MCP3202 reading errors
- Layout that causes interference between analog and digital signal traces
## Notes
- Another version of the converter is in development, with improvements to speed, resolution and noise. 
## Link to YouTube Video
https://www.youtube.com/watch?v=aNtOfKR7sto
