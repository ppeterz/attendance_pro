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
 */

const SHARED_SECRET = "59cfe9e0f6ef676a8efe4e6c384de8a76682d78f7d48c0c7"; // must match Config.h
const SHEET_NAME = "Attendance";
const TIMEZONE = "Africa/Lagos"; // WAT, UTC+1, no DST

function doGet(e) {
  return ContentService
    .createTextOutput("Attendance webhook is live.")
    .setMimeType(ContentService.MimeType.TEXT);
}

function doPost(e) {
  try {
    const body = JSON.parse(e.postData.contents);

    if (!body || body.secret !== SHARED_SECRET) {
      return jsonResponse({ status: "error", message: "invalid secret" });
    }

    const sheet = getOrCreateSheet();
    const now = new Date();

    // ---------------- Query Action: getDailyState ----------------
    if (body.action === "getDailyState") {
      const targetDate = body.date || Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd");
      const data = sheet.getDataRange().getValues();
      const states = {};

      for (let i = 1; i < data.length; i++) {
        const cellVal = data[i][0];
        if (!cellVal) continue;
        const rowDate = (cellVal instanceof Date)
          ? Utilities.formatDate(cellVal, TIMEZONE, "yyyy-MM-dd")
          : String(cellVal).trim();

        if (rowDate === targetDate) {
          const empId = String(data[i][1]).trim();    // Employee ID
          const signIn = data[i][4];                  // Sign-In Time (Col E)
          const signOut = data[i][5];                 // Sign-Out Time (Col F)
          const status = String(data[i][6]).trim();   // Status (Col G)

          let stateObj = null;
          if (signOut || status === "Signed Out") {
            stateObj = { t: 1, c: 2 }; // Checked Out
          } else if (signIn || status === "Present") {
            stateObj = { t: 0, c: 1 }; // Present / Checked In
          }

          if (stateObj && empId) {
            states[empId] = stateObj;
          }
        }
      }
      return jsonResponse({ status: "ok", date: targetDate, states: states });
    }

    // ---------------- Write Action: Process Scan Records ----------------
    if (!Array.isArray(body.records) || body.records.length === 0) {
      return jsonResponse({ status: "error", message: "no records" });
    }

    body.records.forEach(function (rec) {
      const scanDate = new Date(rec.ts * 1000); // unix timestamp (UTC)
      const dateStr = Utilities.formatDate(scanDate, TIMEZONE, "yyyy-MM-dd");
      const timeStr = Utilities.formatDate(scanDate, TIMEZONE, "HH:mm:ss");
      const empId = rec.sid || rec.uid || "";
      const surname = rec.ln || "";
      const firstName = rec.fn || "";
      const uid = rec.uid || "";
      const isCheckIn = (rec.type === "IN");

      // Search for existing row for this date and employee ID / UID
      const data = sheet.getDataRange().getValues();
      let rowIndex = -1;
      for (let i = 1; i < data.length; i++) {
        const cellVal = data[i][0];
        if (!cellVal) continue;
        const rDate = (cellVal instanceof Date)
          ? Utilities.formatDate(cellVal, TIMEZONE, "yyyy-MM-dd")
          : String(cellVal).trim();
        const rEmp = String(data[i][1]).trim();

        if (rDate === dateStr && (rEmp === empId || (uid && rEmp === uid))) {
          rowIndex = i + 1; // 1-based row index in Google Sheet
          break;
        }
      }

      if (rowIndex > 0) {
        // Update existing row
        if (isCheckIn) {
          sheet.getRange(rowIndex, 5).setValue(timeStr);              // Col E: Sign-In Time
          sheet.getRange(rowIndex, 7).setValue("Present");              // Col G: Status
          applyStatusFormatting(sheet, rowIndex, "Present");
        } else {
          sheet.getRange(rowIndex, 6).setValue(timeStr);              // Col F: Sign-Out Time
          sheet.getRange(rowIndex, 7).setValue("Signed Out");          // Col G: Status
          applyStatusFormatting(sheet, rowIndex, "Signed Out");
        }
      } else {
        // Create new row for today
        const newRow = [
          dateStr,
          empId,
          surname,
          firstName,
          isCheckIn ? timeStr : "",
          isCheckIn ? "" : timeStr,
          isCheckIn ? "Present" : "Signed Out"
        ];
        sheet.appendRow(newRow);
        const lastRow = sheet.getLastRow();
        applyStatusFormatting(sheet, lastRow, isCheckIn ? "Present" : "Signed Out");
      }
    });

    return jsonResponse({ status: "ok", count: body.records.length });
  } catch (err) {
    return jsonResponse({ status: "error", message: String(err) });
  }
}

function applyStatusFormatting(sheet, row, status) {
  const statusCell = sheet.getRange(row, 7); // Col G
  if (status === "Present") {
    statusCell.setBackground("#c8e6c9")  // Soft light green
              .setFontColor("#1b5e20")   // Dark green text
              .setBold(true);
  } else if (status === "Signed Out") {
    statusCell.setBackground("#ffcdd2")  // Soft light pink
              .setFontColor("#b71c1c")   // Dark red text
              .setBold(true);
  }
}

function getOrCreateSheet() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sheet = ss.getSheetByName(SHEET_NAME);
  if (!sheet) {
    sheet = ss.insertSheet(SHEET_NAME);
    const headerRange = sheet.getRange(1, 1, 1, 7);
    headerRange.setValues([[
      "Date", "Employee ID", "Surname", "First Name",
      "Sign-In Time", "Sign-Out Time", "Status"
    ]]);
    headerRange.setBackground("#388e3c") // Bold green header matching your layout
               .setFontColor("#ffffff")
               .setBold(true);
    sheet.setFrozenRows(1);
    sheet.setColumnWidth(1, 110); // Date
    sheet.setColumnWidth(2, 120); // Employee ID
    sheet.setColumnWidth(3, 120); // Surname
    sheet.setColumnWidth(4, 120); // First Name
    sheet.setColumnWidth(5, 120); // Sign-In Time
    sheet.setColumnWidth(6, 120); // Sign-Out Time
    sheet.setColumnWidth(7, 130); // Status
  }
  return sheet;
}

function jsonResponse(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
