# Multislope 3I 
## Introduction
This repository contains hardware and software files for the third multislope prototype board, designated 3I. This is the most complete and proper version of the PCB so far, including a proper 8-resistor network, an LM399/ADR1399 reference, composite integrator (with two SOIC-8 footprints), shielding on the integrator summing node and test points to easily probe crucial nodes. 
History and operation are documented extensively [here](https://hackaday.io/project/190528-multislope-adc).
## Project Motivation
The second prototype PCB was completed hastily with not much thought put into either layout or choice of parts, so a better-designed (and hopefully better-evaluated) board was in order. 
## Usage
The KiCAD project files can be used to generate gerber files, which can be used to manufacture the PCBs. The code is written in C/C++ in the VScode development environment. Detailed instructions on how to compile the software will be added later. A .uf2 file is present in the project outputs which can be uploaded directly to the Pi Pico. 
## List of Files
- Hardware: KiCAD PCB design files, BOM and schematic. 
![Front side of the PCB](https://github.com/NNNILabs/Multislope-3I/blob/main/Resources/front.PNG)
- Software: C/C++ code, uf2 file for direct upload. 
- Resources: Infographics
## Application Examples
- The readings from the overall converter (PWM counts and residue) were noisy, and the raw readings from the MCP3202 residue ADC were noisy and contained unexplained banding. Linearity shows a parabolic curve, probably because of wrong constants or small mismatches in switch resistances. 
### Linearity
![Linearity](https://github.com/NNNILabs/Multislope-3I/blob/main/Resources/linearity.png)
### MCP3202 Raw Data (Detail)
![Noise](https://github.com/NNNILabs/Multislope-3I/blob/main/Resources/noise.PNG)
### Cause
- Reading noise was [determined](https://hackaday.io/project/190528-multislope-adc/log/218489-a-path-forward) to be caused by poor layout and decoupling around the MCP3202 residue ADC. 
- Linearity bell curve could be caused by input resistor's PCR.
## Notes
- An interactive Excel document is provided in the Resources folder, with a slider to vary the residue constant to visually determine the best constant to reconcile runup and residue readings. Update: Excel file broke somehow, but you can still see the effect of moving the slider in the video linked below, timestamp [10:15](https://youtu.be/aNtOfKR7sto?t=615).
- Another version of the converter is in development, with improvements to speed, resolution and noise. 
## Links
https://www.youtube.com/watch?v=aNtOfKR7sto
