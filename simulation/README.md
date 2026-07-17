# Simulation and Validation

This directory will contain the files and evidence used to reproduce the HVAC controller in simulation.

## Supported tools

The project has been evaluated with:

- SimulIDE
- Proteus 8
- Microchip Studio / AVR-GCC firmware builds

Simulation files may depend on tool-specific component models and should be kept separate from the portable C source code.

## Validation goals

A complete simulation package should demonstrate:

1. Both ATmega328P controllers start correctly.
2. The LCD presents readable system information.
3. The keypad changes modes or control values.
4. Interior and exterior temperature inputs are acquired.
5. The master addresses the slave over TWI/I²C.
6. The slave receives commands and updates the PWM output.
7. Manual and automatic modes produce distinguishable behavior.
8. Communication failures or invalid readings do not leave the system in an unsafe undefined state.

## Recommended evidence

The final repository should include:

- A full-system schematic screenshot
- A close-up of the TWI wiring and pull-up resistors
- LCD output in manual mode
- LCD output in automatic mode
- Logic-analyzer or oscilloscope evidence for SDA and SCL, when available
- PWM waveform or fan-speed evidence
- Notes describing simulator limitations and hardware differences

## File handling

Binary or proprietary simulation projects should be accompanied by screenshots and a written setup procedure. Generated build outputs such as `.elf`, `.hex`, object files, and temporary simulator files should remain excluded through `.gitignore` unless a release artifact is intentionally published.
