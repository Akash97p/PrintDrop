#pragma once

// Software escape hatch into the ROM download mode.
//
// This DevKitC-1 has only half of the auto-reset circuit wired: the CP210x
// bridge on COM5 can pull EN low (it resets the chip) but nothing drives GPIO0,
// so esptool can never talk COM5 into download mode -- it always lands in
// SPI_FAST_FLASH_BOOT and reports "Wrong boot mode detected (0x8)".
//
// Once the USB mass storage firmware takes the native port away from the
// USB-Serial-JTAG peripheral, that leaves the physical BOOT button as the only
// way back in. To avoid that, every firmware here watches UART0 for the word
// BOOTLOADER and forces a download-mode reboot in software.
//
// Usage from a host:  echo BOOTLOADER > COM5   (then flash normally)

#include <Arduino.h>
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>

#if ARDUINO_USB_MODE == 0
#include <esp32-hal-tinyusb.h>
#endif

// Which object is the physical UART0 console depends on the USB configuration:
//   CDC_ON_BOOT=1 -> Serial is the USB console, Serial0 is UART0
//   CDC_ON_BOOT=0 -> Serial is UART0
#if ARDUINO_USB_CDC_ON_BOOT
#define UART_CONSOLE Serial0
#else
#define UART_CONSOLE Serial
#endif

static const char kReflashMagic[] = "BOOTLOADER";

inline void reflashHatchReboot() {
#if ARDUINO_USB_MODE == 0
    // Hands the USB PHY back to the USB-Serial-JTAG controller on the way down,
    // so the ROM loader reappears on the same cable.
    usb_persist_restart(RESTART_BOOTLOADER);
#else
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
#endif
}

// Call from loop(). Matches the magic word as a rolling window, so partial or
// noisy input cannot wedge it.
inline void reflashHatchPoll() {
    static uint8_t matched = 0;
    while (UART_CONSOLE.available()) {
        char c = (char)UART_CONSOLE.read();
        if (c == kReflashMagic[matched]) {
            if (++matched == sizeof(kReflashMagic) - 1) {
                UART_CONSOLE.println("[hatch] rebooting into download mode...");
                UART_CONSOLE.flush();
                delay(50);
                reflashHatchReboot();
            }
        } else {
            // Restart the match, allowing for the character to begin a new one.
            matched = (c == kReflashMagic[0]) ? 1 : 0;
        }
    }
}
