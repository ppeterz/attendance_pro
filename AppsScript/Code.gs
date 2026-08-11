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

const SHARED_SECRET = "REPLACE_WITH_A_LONG_RANDOM_STRING"; // must match Config.h
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
    if (!Array.isArray(body.records) || body.records.length === 0) {
      return jsonResponse({ status: "error", message: "no records" });
    }

    const sheet = getOrCreateSheet();
    const deviceId = body.device || "unknown";
    const now = new Date();

    const rows = body.records.map(function (rec) {
      const scanDate = new Date(rec.ts * 1000); // rec.ts is unix seconds (UTC)
      return [
        Utilities.formatDate(scanDate, TIMEZONE, "yyyy-MM-dd"),
        Utilities.formatDate(scanDate, TIMEZONE, "HH:mm:ss"),
        rec.uid || "",
        rec.fn || "",
        rec.ln || "",
        rec.role || "",
        rec.sid || "",
        rec.type || "",
        deviceId,
        Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd HH:mm:ss") // when the row was written
      ];
    });

    sheet.getRange(sheet.getLastRow() + 1, 1, rows.length, rows[0].length).setValues(rows);

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
