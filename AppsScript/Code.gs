/**
 * ESP8266 Attendance System — Google Apps Script Web App
 *
 * Deploy this bound to your Google Sheet:
 *   Extensions > Apps Script > paste this in > Deploy > New deployment
 *   Type: "Web app"
 *   Execute as: "Me"
 *   Who has access: "Anyone"
 *
 * IMPORTANT: SHARED_SECRET must match SYNC_SHARED_SECRET in Config.h.
 *
 * Sheet layout: one worksheet tab per day, named "yyyy-MM-dd" (e.g. "2026-08-12").
 * Columns: Employee ID | Surname | First Name | Sign-In Time | Sign-Out Time | Status
 */

const SHARED_SECRET = "59cfe9e0f6ef676a8efe4e6c384de8a76682d78f7d48c0c7"; // must match Config.h
const TIMEZONE = "Africa/Lagos"; // WAT, UTC+1, no DST

/**
 * Google Sheets auto-converts "HH:mm:ss" strings into Date objects.
 * getValues() then returns Date objects, not the original strings.
 * This helper normalises either type back to an "HH:mm:ss" string
 * so the duplicate guard comparison works reliably.
 */
function cellToTimeStr(cellValue) {
  if (!cellValue) return "";
  if (cellValue instanceof Date) {
    return Utilities.formatDate(cellValue, TIMEZONE, "HH:mm:ss");
  }
  return String(cellValue).trim();
}

function doGet(e) {
  return ContentService
    .createTextOutput("Attendance webhook is live.")
    .setMimeType(ContentService.MimeType.TEXT);
}

function doPost(e) {
  // ── Acquire a script-wide lock so concurrent retries from the device
  //    don't race past the duplicate check and write multiple rows. ──
  const lock = LockService.getScriptLock();
  try {
    lock.waitLock(15000); // wait up to 15 s; throws if unavailable
  } catch (lockErr) {
    return jsonResponse({ status: "error", message: "server busy, retry" });
  }

  try {
    const body = JSON.parse(e.postData.contents);

    if (!body || body.secret !== SHARED_SECRET) {
      return jsonResponse({ status: "error", message: "invalid secret" });
    }

    const now = new Date();

    // ---------------- Query Action: getDailyState ----------------
    if (body.action === "getDailyState") {
      const targetDate = body.date || Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd");
      const sheet = getOrCreateDailySheet(targetDate);
      const data = sheet.getDataRange().getValues();
      const states = {};

      // Each row: [Employee ID, Surname, First Name, Sign-In Time, Sign-Out Time, Status]
      for (let i = 1; i < data.length; i++) {
        const empId  = String(data[i][0]).trim();  // Col A: Employee ID
        const signIn  = data[i][3];                // Col D: Sign-In Time
        const signOut = data[i][4];                // Col E: Sign-Out Time
        const status  = String(data[i][5]).trim(); // Col F: Status
        if (!empId) continue;

        let stateObj = null;
        if (signOut || status === "Signed Out") {
          stateObj = { t: 1, c: 2 }; // Checked Out
        } else if (signIn || status === "Present") {
          stateObj = { t: 0, c: 1 }; // Present / Checked In
        }
        if (stateObj) states[empId] = stateObj;
      }
      return jsonResponse({ status: "ok", date: targetDate, states: states });
    }

    // ---------------- Write Action: Process Scan Records ----------------
    if (!Array.isArray(body.records) || body.records.length === 0) {
      return jsonResponse({ status: "error", message: "no records" });
    }

    // Group records by date so offline-queue batches spanning midnight
    // each land on the correct day's sheet tab.
    const byDate = {};
    body.records.forEach(function (rec) {
      const scanDate = new Date(rec.ts * 1000);
      const dateStr  = Utilities.formatDate(scanDate, TIMEZONE, "yyyy-MM-dd");
      if (!byDate[dateStr]) byDate[dateStr] = [];
      byDate[dateStr].push(rec);
    });

    Object.keys(byDate).forEach(function (dateStr) {
      const daySheet = getOrCreateDailySheet(dateStr);

      byDate[dateStr].forEach(function (rec) {
        const scanDate  = new Date(rec.ts * 1000);
        const timeStr   = Utilities.formatDate(scanDate, TIMEZONE, "HH:mm:ss");
        const empId     = String(rec.sid || rec.uid || "").trim();
        const surname   = rec.ln || "";
        const firstName = rec.fn || "";
        const uid       = rec.uid || "";
        const isCheckIn = (rec.type === "IN");

        // ── Re-read data on EVERY record so we always see the latest
        //    rows, including any we just appended in the same batch. ──
        const data = daySheet.getDataRange().getValues();

        // ── Duplicate guard ─────────────────────────────────────────────
        let rowIndex      = -1;
        let alreadyRecorded = false;

        for (let i = 1; i < data.length; i++) {
          const rEmp = String(data[i][0]).trim(); // Col A: Employee ID
          if (rEmp === empId || (uid && rEmp === uid)) {
            rowIndex = i + 1; // 1-based sheet row
            // Use cellToTimeStr() to handle Date objects returned by getValues()
            const existingTime = isCheckIn ? cellToTimeStr(data[i][3])
                                           : cellToTimeStr(data[i][4]);
            if (existingTime === timeStr) alreadyRecorded = true;
            break;
          }
        }

        if (alreadyRecorded) return; // already written by a prior retry — skip

        if (rowIndex > 0) {
          // Update existing employee row on this day's sheet
          if (isCheckIn) {
            daySheet.getRange(rowIndex, 4).setNumberFormat("@").setValue(timeStr);      // Col D: Sign-In Time
            daySheet.getRange(rowIndex, 6).setValue("Present");    // Col F: Status
            applyStatusFormatting(daySheet, rowIndex, "Present");
          } else {
            daySheet.getRange(rowIndex, 5).setNumberFormat("@").setValue(timeStr);      // Col E: Sign-Out Time
            daySheet.getRange(rowIndex, 6).setValue("Signed Out"); // Col F: Status
            applyStatusFormatting(daySheet, rowIndex, "Signed Out");
          }
        } else {
          // First scan for this employee on this day
          const newRow = [
            empId,
            surname,
            firstName,
            isCheckIn ? timeStr : "",   // Col D: Sign-In Time
            isCheckIn ? "" : timeStr,   // Col E: Sign-Out Time
            isCheckIn ? "Present" : "Signed Out"
          ];
          daySheet.appendRow(newRow);
          const lastRow = daySheet.getLastRow();
          // Force time columns to plain text so Sheets doesn't auto-convert to Date
          daySheet.getRange(lastRow, 4).setNumberFormat("@");
          daySheet.getRange(lastRow, 5).setNumberFormat("@");
          applyStatusFormatting(daySheet, lastRow, isCheckIn ? "Present" : "Signed Out");
        }
      });
    });

    return jsonResponse({ status: "ok", count: body.records.length });
  } catch (err) {
    return jsonResponse({ status: "error", message: String(err) });
  } finally {
    lock.releaseLock();
  }
}

function applyStatusFormatting(sheet, row, status) {
  const statusCell = sheet.getRange(row, 6); // Col F: Status (no Date col on daily sheets)
  if (status === "Present") {
    statusCell.setBackground("#c8e6c9")  // Soft light green
              .setFontColor("#1b5e20")   // Dark green text
              .setFontWeight("bold");
  } else if (status === "Signed Out") {
    statusCell.setBackground("#ffcdd2")  // Soft light pink
              .setFontColor("#b71c1c")   // Dark red text
              .setFontWeight("bold");
  }
}

/**
 * Returns the worksheet for a given date string ("yyyy-MM-dd"),
 * creating and formatting it if it doesn't exist yet.
 * Sheet columns: Employee ID | Surname | First Name | Sign-In | Sign-Out | Status
 */
function getOrCreateDailySheet(dateStr) {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sheet = ss.getSheetByName(dateStr);
  if (!sheet) {
    sheet = ss.insertSheet(dateStr);

    // Move newest date to the leftmost tab position for easy access.
    ss.moveActiveSheet(1);

    const headerRange = sheet.getRange(1, 1, 1, 6);
    headerRange.setValues([[
      "Employee ID", "Surname", "First Name",
      "Sign-In Time", "Sign-Out Time", "Status"
    ]]);
    headerRange.setBackground("#388e3c")
               .setFontColor("#ffffff")
               .setFontWeight("bold");
    sheet.setFrozenRows(1);
    sheet.setColumnWidth(1, 120); // Employee ID
    sheet.setColumnWidth(2, 120); // Surname
    sheet.setColumnWidth(3, 130); // First Name
    sheet.setColumnWidth(4, 120); // Sign-In Time
    sheet.setColumnWidth(5, 120); // Sign-Out Time
    sheet.setColumnWidth(6, 120); // Status
    // Force time columns (D & E) to plain text format across all rows
    sheet.getRange("D:E").setNumberFormat("@");
  }
  return sheet;
}

function jsonResponse(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
