#ifndef RFID_DRIVER_H
#define RFID_DRIVER_H

#include <Arduino.h>
#include <MFRC522.h>
#include "Config.h"

// HAL wrapper around MFRC522 so application/logic code never touches
// the library directly (Directive 3: Hardware Abstraction Layer).
class RFID_Driver {
public:
    RFID_Driver();
    void begin();

    // Non-blocking: call every loop tick. Returns true exactly once
    // per new card presented, with the UID written into outUid.
    bool pollForCard(String &outUid);

    // Call after a successful read to release the card / reset crypto.
    void releaseCard();

    // Basic self-test: reads the firmware version register.
    // Returns false if the reader isn't responding (wiring/power issue).
    bool selfTest();

private:
    MFRC522 _mfrc522;
    String _uidToHexString(MFRC522::Uid &uid);
};

#endif // RFID_DRIVER_H
