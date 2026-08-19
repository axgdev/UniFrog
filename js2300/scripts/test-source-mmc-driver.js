// unifrog: mode=extension
/*
 * test-source-mmc-driver.js — Test the source MMC host driver.
 *
 * Run this script on the SF2000 device to:
 * 1. Confirm vendor boot path
 * 2. Capture a vendor diagnostic baseline
 * 3. Switch to source and capture the same diagnostic workload
 * 4. Run broader source storage checks
 * 5. Revert to vendor and verify recovery
 */

var report = [];
var timestamp = JS2300.now();
var result;
var failures = [];
var initialStatus;
var vendorAvailable = true;

function statusName(code) {
    if (code === 0) return "VENDOR";
    if (code === 1) return "SOURCE";
    if (code === 2) return "DIAG_VENDOR";
    if (code === 3) return "DIAG_SOURCE";
    return "UNKNOWN(" + code + ")";
}

function action(label, id) {
    var ret = JS2300.system.action(id);
    log(label + ": id=" + id + " ret=" + ret);
    return ret;
}

function logStatus(label) {
    var code = action(label, "storage:swap_driver:status");
    log(label + " driver=" + statusName(code));
    return code;
}

function log(msg) {
    report.push("[" + (JS2300.now() - timestamp) + "ms] " + msg);
    JS2300.log(msg);
}

function saveReport() {
    var text = "show=1\n";
    text += "title=Source MMC Driver Test\n";
    text += "detail=Test of reverse-engineered hc_mmc_host driver\n\n";
    for (var i = 0; i < report.length; i++) {
        text += "item|" + report[i] + "\n";
    }
    JS2300.fs.writeText("/media/mmcblk0/unifrog_data/reports/source-mmc-driver-test.txt", text);
    log("Report saved.");
}

function requirePositive(label, ret) {
    if (ret > 0) {
        log(label + " PASSED");
        return;
    }
    log(label + " FAILED (ret=" + ret + ")");
    failures.push(label + " ret=" + ret);
}

log("=== Source MMC Driver Test ===");
log("Starting vendor/source comparison...");

// Step 1: Confirm vendor boot path
initialStatus = logStatus("status_initial");
if (initialStatus === 1) {
    vendorAvailable = false;
    log("Source-default build detected; vendor driver is not expected to be available.");
} else if (initialStatus !== 0) {
    log("Forcing vendor driver before test.");
    result = action("switch_vendor", "storage:swap_driver:vendor");
    if (result < 0) {
        vendorAvailable = false;
        log("Vendor driver unavailable; continuing as source-default diagnostic run.");
    }
    JS2300.sleep(300);
    initialStatus = logStatus("status_after_vendor_force");
}

// Step 2: Capture vendor baseline behavior
if (vendorAvailable && initialStatus === 0) {
    log("Running vendor diagnostic benchmark...");
    result = action("diag_vendor", "storage:swap_driver:diagnose");
    if (result < 0) {
        log("ERROR: Failed to enter vendor diagnostic mode.");
        saveReport();
        throw new Error("Vendor diagnostic swap failed");
    }
    JS2300.sleep(200);
    result = action("vendor_bench", "developer:storage_quick_benchmark");
    requirePositive("vendor_bench", result);
    action("vendor_dump", "storage:swap_driver:dump");
    action("vendor_revert", "storage:swap_driver:vendor");
    JS2300.sleep(300);
    logStatus("status_after_vendor_diag");
} else {
    log("Skipping vendor baseline; this run is using source-default or vendor is unavailable.");
}

// Step 3: Switch to source and capture the same workload
log("Switching to SOURCE driver...");
result = action("switch_source", "storage:swap_driver:source");
if (result < 0) {
    log("ERROR: Failed to switch to source driver.");
    saveReport();
    throw new Error("Source driver swap failed");
}
JS2300.sleep(300);
logStatus("status_after_source");
result = action("source_diag", "storage:swap_driver:diagnose");
if (result < 0) {
    log("ERROR: Failed to enter source diagnostic mode.");
    saveReport();
    throw new Error("Source diagnostic swap failed");
}
JS2300.sleep(200);
result = action("source_bench_diag", "developer:storage_quick_benchmark");
requirePositive("source_bench_diag", result);
action("source_dump", "storage:swap_driver:dump");
action("source_diag_exit", "storage:swap_driver:source");
JS2300.sleep(300);
logStatus("status_after_source_diag");

// Step 4: Run broader source checks
log("Running source quick benchmark...");
result = action("source_bench", "developer:storage_quick_benchmark");
requirePositive("source_bench", result);
log("Running source quick mode sweep...");
result = action("source_modes", "developer:storage_quick_modes");
requirePositive("source_modes", result);

// Step 5: Read the latest benchmark report
var benchReport = JS2300.fs.readText("/media/mmcblk0/unifrog_data/reports/storage-quick-benchmark.txt");
if (benchReport) {
    log("Benchmark report (first 200 chars): " + benchReport.substring(0, 200));
}

// Step 6: Revert to vendor driver when present, otherwise keep source.
if (vendorAvailable) {
    log("Reverting to VENDOR driver...");
    result = action("final_vendor", "storage:swap_driver:vendor");
    if (result < 0) {
        log("ERROR: Failed to revert to vendor driver!");
        log("DEVICE NEEDS POWER CYCLE TO RECOVER.");
        failures.push("final_vendor ret=" + result);
    } else {
        log("Reverted to VENDOR driver successfully.");
    }
    JS2300.sleep(500);
    logStatus("status_after_final_vendor");

    // Step 7: Verify vendor driver works
    log("Running quick benchmark with VENDOR driver to verify recovery...");
    result = action("vendor_recovery_bench", "developer:storage_quick_benchmark");
    if (result > 0) {
        log("Quick benchmark PASSED with vendor driver (recovery OK).");
    } else {
        log("Quick benchmark FAILED with vendor driver (ret=" + result + ").");
        failures.push("vendor_recovery_bench ret=" + result);
    }
} else {
    log("Skipping vendor recovery benchmark because vendor driver is unavailable.");
    logStatus("status_final_source_default");
}

// Step 8: Save the report
saveReport();

if (failures.length > 0) {
    throw new Error("Source MMC driver test failed: " + failures.join("; "));
}

log("=== Test Complete ===");
