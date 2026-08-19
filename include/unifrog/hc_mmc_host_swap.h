/*
 * HC15xx MMC host driver — runtime swap support.
 *
 * This header declares the source-owned host ops table and the runtime
 * swap function that allows switching between the vendor binary driver
 * (libmmchosthc15.a) and the source implementation at runtime.
 *
 * Vendor builds can redirect the live host between vendor and source ops.
 * Diagnostic mode can wrap either the vendor or source path to capture the
 * exact register behavior for the currently selected implementation.
 *
 * Usage from JS:
 *   host.action("storage:swap_driver:source")  // switch to source
 *   host.action("storage:swap_driver:vendor")  // revert to vendor
 *   host.action("storage:swap_driver:status")   // query current driver
 *   host.action("storage:swap_driver:diagnose") // wrap current driver
 */
#ifndef UNIFROG_HC_MMC_HOST_SWAP_H
#define UNIFROG_HC_MMC_HOST_SWAP_H

/*
 * Swap the MMC host driver between vendor and source implementations.
 *
 * target: "source" = switch to source driver
 *         "vendor" = revert to vendor driver
 *         "status" = query only (return current state)
 *
 * Returns:
 *   3 = diagnostic wrapper over source
 *   2 = diagnostic wrapper over vendor
 *   1 = currently using source driver
 *   0 = currently using vendor driver
 *  -1 = error (host not found, swap failed)
 */
int unifrog_platform_mmc_host_swap(const char *target);

/*
 * Query whether the source host driver is compiled in.
 * Returns 1 if source driver is available, 0 otherwise.
 */
int unifrog_platform_mmc_host_source_available(void);

#endif /* UNIFROG_HC_MMC_HOST_SWAP_H */
