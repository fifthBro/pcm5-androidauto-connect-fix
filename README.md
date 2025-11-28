# pcm5-androidauto-connect-fix
Fix for bug that's causing NATIVE_SELECTED phone setting in Porsche PCM5 (MH2P) Android Auto implementation.

This repository contains fix for the device-activation state machine used by the Android Auto / CarPlay integration in Porsche PCM5 (MH2P) on firmwares between 26xx and 28xx.
1) bytecode fix for DeviceManager$DeviceActivationRequestHandler.moveSelectionMarker() setting wrong NATIVE_SELECTED state tha prevents phonesf
2) Tool "fix_partition_1008" to fix all phone entries stored in the sqlite persistence database.
3) Mini MH2p_SD_ModKit Mod (can only be applied after enabling Android Auto with MH2p_SD_ModKit  ->  Android Auto Mod), but ultimately that fix is going to end up in the main MH2p_SD_ModKit Android Auto.

##  1. The reconnection bug in TerminalMode Device Java code

### Problematic sequence

1. Phone1 connects → Android Auto works ✅  
2. Phone1 disconnects → Phone2 connects → Android Auto works ✅  
3. Phone2 disconnects → Phone1 reconnects → USB charging mode ❌  


## Device State Machine

| State | Description |
|-------|-------------|
| **INITIAL** | Device just connected, not configured yet |
| **TM_SELECTED** | Selected for Terminal Mode (Android Auto / CarPlay) |
| **NATIVE_SELECTED** | Selected for native Bluetooth only (calls, music) |
| **DISCLAIMER_ACCEPTED** | Disclaimer accepted, Android Auto / CarPlay allowed |
| **NATIVE** | Legacy native-only mode |

---

### When NATIVE_SELECTED Is Set

#### User explicitly chooses it
Source: `NewDeviceHMIHandler.java:206–209`

- User sees the dialog: “Android Auto or USB/Bluetooth?”
- Choosing USB/Bluetooth sets:  
  - Terminal Mode → off  
  - Device → `NATIVE_SELECTED`
- Phone stays connected for calls/music only

#### Connection failure fallback
Source: `TMDeviceControl.java:573–574`

- If AA/CarPlay fails (e.g., error code 7)
- Device is demoted to `NATIVE_SELECTED`
- Ensures Bluetooth fallback still works

#### Device reconnects while in `NATIVE_SELECTED`
Source: `TMDeviceControl.java:562–564`

- Reconnection automatically disables Terminal Mode
- Preserves the user’s “native only” choice

#### Switching device types
Source: `NewDeviceHMIHandler.java:332`

- Example: CarPlay accepted, Android Auto device demoted
- Only one Terminal Mode device active at a time
- Others become `NATIVE_SELECTED`


### Root cause

Located in `DeviceManager$DeviceActivationRequestHandler.moveSelectionMarker()`

At line **437**, inactive devices are always demoted:

```java
private void moveSelectionMarker(TMDevice tmDevice) {
    GIterator iterator = ...getDeviceList().iterator();
    while (iterator.hasNext()) {
        TMDevice device = (TMDevice)iterator.next();
        if (device == tmDevice) continue;
        device.setSelected(false);                     // ✓ OK
        device.setUserAcceptState(NATIVE_SELECTED);    // ✗ BUG
    }
    tmDevice.setSelected(true);
}
``` 

### Why it's a bug

When Phone2 becomes active

The loop demotes every other device, including Phone1

This resets:
`DISCLAIMER_ACCEPTED → NATIVE_SELECTED`

When Phone1 reconnects, the system thinks the disclaimer was never accepted

User is forced to accept the popup again each time

### The Fix

A patched version of: `DeviceManager$DeviceActivationRequestHandler`

where the line `device.setUserAcceptState(NATIVE_SELECTED);`

is removed via bytecode editing

Other logic remains identical and device disclaimer acceptance is now preserved

Patch is loaded via bootclasspath injection in lsd.sh script.

## 2. Incorect persistence state in sqlite database
After installing the the bytecode fix,  `DeviceManager$DeviceActivationRequestHandler.moveSelectionMarker()` will behave correctly however persistence database will still keep `NATIVE_SELECTED` instead of `DISCLAIMER_ACCEPTED`.

 To fix that `userAcceptState` strings in `partition 1008` of sqlite persistence database has to be changed from `"NATIVE_SELECTED"` to `"DISCLAIMER_ACCEPTED"`.
 ``` 
 Storage Format (Java serialization):
   [8 bytes: CRC32 as long (big-endian)]
   [4 bytes: version = 3]
   [4 bytes: device count]
   [for each device:]
     - deviceUniqueId (UTF-8 with 2-byte length prefix)
     - smartphoneType (UTF-8 with 2-byte length prefix)
     - boolean: has name
     - name (UTF-8 with 2-byte length prefix, if has name)
     - userAcceptState (UTF-8 with 2-byte length prefix) <- This is what we fix
     - wasDisclaimerPreviouslyAccepted (boolean)
     - storeUserAcceptState (boolean)
     - lastmode (4-byte int)
     - lastConnectionType (UTF-8 with 2-byte length prefix)
``` 
 ### The Solution

  This repository provides repair tool for Partition 1008 
  - Replaces `NATIVE_SELECTED` → `DISCLAIMER_ACCEPTED` strings
  - Correctly recalculates and updates CRC32 checksum 
  - Ensures device list loads successfully 
  - Can be compiled  from simple C source code

### Features

  - ✅ Automatic CRC32 validation and repair
  - ✅ Database backup before modifications
  - ✅ Dry-run mode to preview changes
  - ✅ List mode to show affected devices
  - ✅ Supports unlimited devices per system
  - ✅ Works with QNX 6.6 (ARM) and Linux/Windows

###  Requirements

  - GCC compiler (ARM v7 LE toolchain)
  - libsqlite3
  - zlib

###  Installation

  ```
  gcc -o fix_partition_1008 fix_partition_1008.c -lsqlite3 -lz
```
  Copy fix_partition_1008 executable to mh2p/PCM5 with any method (or preferably just use MH2p SD ModKit)
  
###  Usage: 
    
    fix_partition_1008 [--list] [--dry-run] [--fix] [--db-path PATH] [--no-backup]
    
    Options:
    --list        List device states (no changes)
    --dry-run     Preview what would be changed (no changes)
    --fix         Actually apply the fix (modifies database)
    --db-path     Path to persistence database (default: /mnt/persist_new/persistence/persistence.sqlite)
    --no-backup   Skip backup creation

###  Tested on:
  - Porsche PCM5 (MH2P) with DeviceManager$DeviceActivationRequestHandler.moveSelectionMarker() bytecode fix
  - Multiple device scenarios (2+ phones)
  - Cold reset scenarios
  - CRC32 validation

# 3. Android Auto Fix that can be installed with MH2p ModKit Mod

###
Based on MH2p SD ModKit.
### 
Automatically checks whether the unit is Porsche and running 26xx–28xx firmware
### 
Install the bytecode fix
### 
Repairs the persistence partition in sqlite end-to-end (blob length, CRC32, and the other tricky bits), so a factory reset should no longer be required.

## To install it on mh2p/PCM5 system add it to MH2p SD ModKit
Put the AndroidAuto_Fix directory into MH2p SD ModKit Mods/ directory as per instructions https://lawpaul.github.io/MH2p_SD_ModKit_Site/

## Install using the official MH2p SD ModKit https://lawpaul.github.io/MH2p_SD_ModKit_Site/

alongside Android Auto Mod https://github.com/LawPaul/MH2p_AndroidAuto (if it wasn't installed before)

## After reboot, Android Auto won't require factory reset when connecting multiple devices

 # 4. Credits

  Part of the https://lawpaul.github.io/MH2p_SD_ModKit_Site/ project.
