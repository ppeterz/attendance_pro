#include "RFID_Driver.h"
#include <SPI.h>

RFID_Driver::RFID_Driver() : _mfrc522(RFID_SS_PIN, RFID_RST_PIN) {}

void RFID_Driver::begin() {
    SPI.begin();
    _mfrc522.PCD_Init();
    delay(50); // MFRC522 needs a short settle time after reset
}

bool RFID_Driver::selfTest() {
    byte version = _mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
    // Known-good chips report 0x91 or 0x92; 0x00/0xFF means no response (wiring fault)
    return (version != 0x00 && version != 0xFF);
}

bool RFID_Driver::pollForCard(String &outUid) {
    if (!_mfrc522.PICC_IsNewCardPresent()) {
        return false;
    }
    if (!_mfrc522.PICC_ReadCardSerial()) {
        return false;
    }
    outUid = _uidToHexString(_mfrc522.uid);
    return true;
}

void RFID_Driver::releaseCard() {
    _mfrc522.PICC_HaltA();
    _mfrc522.PCD_StopCrypto1();
}

String RFID_Driver::_uidToHexString(MFRC522::Uid &uid) {
    String s;
    s.reserve(uid.size * 2);
    for (byte i = 0; i < uid.size; i++) {
        if (uid.uidByte[i] < 0x10) s += "0";
        s += String(uid.uidByte[i], HEX);
    }
    s.toUpperCase();
    return s;
}
