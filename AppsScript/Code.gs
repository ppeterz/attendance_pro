/**
 * ESP8266 Attendance System — Google Apps Script Web App
 *
 * Deploy this bound to your Google Sheet:
 *   Extensions > Apps Script > paste this in > Deploy > New deployment
 *   Type: "Web app"
 *   Execute as: "Me"
 *   Who has access: "Anyone"   (device can't do OAuth, so this must be
 *                                anonymous — SHARED_SECRET below is what
 *                                actually protects the endpoint)
 *   Then copy the /exec URL into Config.h -> SYNC_ENDPOINT_URL
 *
 * IMPORTANT: change SHARED_SECRET below to match SYNC_SHARED_SECRET in
 * Config.h — a long random string, not the placeholder value.
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
    const deviceId = body.device || "unknown";
    const now = new Date();

    // Query action: return daily state of all staff for target date
    if (body.action === "getDailyState") {
      const targetDate = body.date || Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd");
      const existingData = sheet.getDataRange().getValues();
      const states = {};

      for (let i = 1; i < existingData.length; i++) {
        const cellVal = existingData[i][0];
        if (!cellVal) continue;
        const rowDate = (cellVal instanceof Date)
          ? Utilities.formatDate(cellVal, TIMEZONE, "yyyy-MM-dd")
          : String(cellVal).trim();

        if (rowDate === targetDate) {
          const uid = String(existingData[i][2]).trim();
          const typeStr = String(existingData[i][7]).trim(); // "IN" or "OUT"
          if (uid) {
            if (!states[uid]) {
              states[uid] = { t: (typeStr === "OUT" ? 1 : 0), c: 1 };
            } else {
              states[uid].t = (typeStr === "OUT" ? 1 : 0);
              states[uid].c += 1;
            }
          }
        }
      }
      return jsonResponse({ status: "ok", date: targetDate, states: states });
    }

    if (!Array.isArray(body.records) || body.records.length === 0) {
      return jsonResponse({ status: "error", message: "no records" });
    }

    // Build a dedup set from existing rows (UID col=C, Type col=H, Time col=B).
    // Key = uid|type|date|time so the exact same scan can't appear twice.
    const existingData = sheet.getDataRange().getValues();
    const seen = new Set();
    for (let i = 1; i < existingData.length; i++) {
      const key = [existingData[i][2], existingData[i][7], existingData[i][0], existingData[i][1]].join("|");
      seen.add(key);
    }

    const rows = [];
    body.records.forEach(function (rec) {
      const scanDate = new Date(rec.ts * 1000);
      const dateStr = Utilities.formatDate(scanDate, TIMEZONE, "yyyy-MM-dd");
      const timeStr = Utilities.formatDate(scanDate, TIMEZONE, "HH:mm:ss");
      const key = [rec.uid, rec.type, dateStr, timeStr].join("|");
      if (seen.has(key)) return; // skip exact duplicate
      seen.add(key);
      rows.push([
        dateStr,
        timeStr,
        rec.uid  || "",
        rec.fn   || "",
        rec.ln   || "",
        rec.role || "",
        rec.sid  || "",
        rec.type || "",
        deviceId,
        Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd HH:mm:ss")
      ]);
    });

    if (rows.length > 0) {
      sheet.getRange(sheet.getLastRow() + 1, 1, rows.length, rows[0].length).setValues(rows);
    }

    return jsonResponse({ status: "ok", count: rows.length });
  } catch (err) {
    return jsonResponse({ status: "error", message: String(err) });
  }
}

function getOrCreateSheet() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sheet = ss.getSheetByName(SHEET_NAME);
  if (!sheet) {
    sheet = ss.insertSheet(SHEET_NAME);
    sheet.appendRow([
      "Date", "Time", "UID", "First Name", "Last Name",
      "Role", "Staff ID", "Type", "Device", "Synced At"
    ]);
    sheet.setFrozenRows(1);
  }
  return sheet;
}

function jsonResponse(obj) {
  // NOTE: Apps Script web apps always return HTTP 200 to the caller no
  // matter what — there's no way to set a custom status code (a known
  // Apps Script limitation). The device's firmware treats httpCode==200
  // as "request reached the script"; the JSON body's "status" field is
  // what actually indicates success ("ok") vs. rejection ("error").
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
