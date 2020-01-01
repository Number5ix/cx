#pragma once

enum HOSTID_SOURCES {
    HID_SourceMachineRegistry = 1,      // Per-machine registry key
    HID_SourceUserRegistry    = 2,      // Per-user registry key
    HID_SourceCrypto          = 3,      // Windows CryptoAPI MachineGUID
    HID_SourceComputerName    = 4,      // Computer Name
};
