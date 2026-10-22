# STM32H750B-DK Custom Bootloader

A fail-safe custom bootloader for the **STM32H750B-DK** discovery kit. It runs from the MCU's internal flash, receives application firmware from a host PC, verifies it, and runs it from the board's external QSPI flash.

## Overview

- **Target:** STM32H750B-DK (STM32H750XBH6, Cortex-M7 @ 480 MHz)
- **Bootloader location:** internal flash (`0x08000000`, 128 KB)
- **Application location:** external QSPI NOR flash, memory-mapped at `0x90000000`
- **Update interfaces:** UART and USB (CDC virtual COM port)
- **Host tools:** Python scripts for uploading firmware

## Key Features

- Dual-bank update: new firmware is downloaded and verified before replacing the running application
- CRC-32 per packet and SHA-256 over the full image
- Resume after power loss and watchdog protection during updates
- Never bricked: the bootloader only boots a verified application

## Branches

| Branch | Purpose |
|---|---|
| `main` | Stable, tested releases |
| `develop` | Active development |

## Author

Shaman Karunaratne
