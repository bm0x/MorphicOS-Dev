This is a standalone Calculator application for Morphic OS.
It is compiled as a separate MPK (Morphic Package) and loaded by the Kernel.
It runs in its own process (Ring 3) and communicates via Syscalls.
Currently, it outputs to the Debug Serial Port to demonstrate multitasking without interfering with the Desktop GUI.
