// unifrog: mode=extension
/*
 * diagnose-mmc-ops.js — Capture MMC host ops register behavior.
 *
 * Wraps the currently selected MMC host driver, runs a quick benchmark,
 * dumps the captured data, then reverts to vendor mode.
 *
 * The diagnostic data appears in the device log (UFLOG lines with
 * "mmc_diag" prefix) and can be extracted after power-cycle.
 */

JS2300.log("=== MMC Host Ops Diagnostic ===");

function statusName(code) {
  if (code === 0) return "VENDOR";
  if (code === 1) return "SOURCE";
  if (code === 2) return "DIAG_VENDOR";
  if (code === 3) return "DIAG_SOURCE";
  return "UNKNOWN(" + code + ")";
}

var startStatus = JS2300.system.action("storage:swap_driver:status");
JS2300.log("Initial driver status: " + statusName(startStatus));

// Reset diagnostic ring buffer
var result = JS2300.system.action("storage:swap_driver:diagnose");
JS2300.log("Diag swap result: " + result + " status=" +
  statusName(JS2300.system.action("storage:swap_driver:status")));

if (result < 0) {
    JS2300.log("ERROR: Failed to switch to diagnostic driver!");
    throw new Error("Diagnostic swap failed");
}

// Wait for stabilization
JS2300.sleep(200);

// Run a minimal I/O to capture ops calls
JS2300.log("Running quick benchmark with diagnostic wrapper...");
var bench = JS2300.system.action("developer:storage_quick_benchmark");
JS2300.log("Benchmark result: " + bench);

// Dump the diagnostic ring buffer
JS2300.log("Dumping diagnostic ring buffer...");
var dump = JS2300.system.action("storage:swap_driver:dump");
JS2300.log("Dump result: " + dump);

// Revert to the vendor driver
JS2300.log("Reverting to vendor driver...");
var rev = JS2300.system.action("storage:swap_driver:vendor");
JS2300.log("Revert result: " + rev + " status=" +
  statusName(JS2300.system.action("storage:swap_driver:status")));

JS2300.log("=== Diagnostic Complete ===");
