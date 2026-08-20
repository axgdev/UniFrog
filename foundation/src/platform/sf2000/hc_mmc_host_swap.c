/*
 * HC15xx MMC host controller driver — runtime swap between vendor and source.
 *
 * The default build links the vendor driver and boots through its exported
 * ops table. This file can redirect the live host to a separate source-owned
 * ops table for validation or diagnostics, then switch it back.
 */

#include <unifrog/hc_mmc_host_swap.h>
#include <unifrog/hc_mmc_host.h>

#include <stdint.h>
#include <string.h>

#include <unifrog/log.h>

/*
 * Vendor ops table from libmmchosthc15.a.
 */
extern const struct hc_mmc_host_hw_ops hc_mmc_host_hw_ops;

/*
 * HCRTOS bus iteration API.
 */
struct bus_type;
struct device;

extern struct bus_type platform_bus_type;
extern int bus_for_each_dev(struct bus_type *bus, struct device *start,
                            void *data,
                            int (*fn)(struct device *dev, void *data));

/*
 * Struct offsets matching the vendor layout.
 */
#define HC_HOST_OFFSET      0x240
#define HC_HOST_OPS_OFFSET  (HC_HOST_OFFSET + 0x14)  /* 0x254 */

/* pdev->dev is at pdev + 0x10 */
#define PDEV_DEV_OFFSET     0x10
/* pdev->driver_data is at pdev + 0x50 */
#define PDEV_DRIVER_DATA    0x50

static int swap_current_source = UNIFROG_MMC_HOST_SOURCE_DEFAULT ? 1 : 0;
static int swap_current_diag;
static int swap_diag_base_source;
static const struct hc_mmc_host_hw_ops *swap_diag_base_ops;

/*
 * swap_check_host — bus_for_each_dev callback.
 *
 * Validates that the device is an MMC host by checking that the ops
 * pointer at mmc_host+0x254 matches the expected vendor ops table.
 * Only swaps if validation passes.
 */
struct swap_context {
    const struct hc_mmc_host_hw_ops *from_ops;
    const struct hc_mmc_host_hw_ops *to_ops;
    int found;
};

static int swap_check_host(struct device *dev, void *data)
{
    struct swap_context *ctx = data;

    /* dev is at pdev + 0x10, so pdev = dev - 0x10 */
    void *pdev = (uint8_t *)dev - PDEV_DEV_OFFSET;

    /* Read driver_data which stores the mmc_host pointer */
    void *mmc = *(void **)((uint8_t *)pdev + PDEV_DRIVER_DATA);
    if (!mmc)
        return 0; /* not an MMC device, continue iteration */

    /* Read the ops pointer from the hc_host within mmc_host */
    void **ops_ptr = (void **)((uint8_t *)mmc + HC_HOST_OPS_OFFSET);
    void *ops = *ops_ptr;

    /* Validate: must match the expected from_ops table */
    if (ops != (void *)ctx->from_ops)
        return 0; /* not our target, continue */

    /* Swap the ops pointer */
    *ops_ptr = (void *)ctx->to_ops;
    ctx->found = 1;
    return 1; /* stop iteration */
}

static int swap_apply(const struct hc_mmc_host_hw_ops *from_ops,
                      const struct hc_mmc_host_hw_ops *to_ops)
{
    struct swap_context ctx;

    ctx.from_ops = from_ops;
    ctx.to_ops = to_ops;
    ctx.found = 0;
    bus_for_each_dev(&platform_bus_type, NULL, &ctx, swap_check_host);
    return ctx.found ? 0 : -1;
}

int unifrog_platform_mmc_host_swap(const char *target)
{
    const struct hc_mmc_host_hw_ops *current_ops;
    int current_is_source;

    if (!target || !*target)
        return -1;

    if (strcmp(target, "status") == 0)
        return swap_current_diag ? (swap_diag_base_source ? 3 : 2) :
            (swap_current_source ? 1 : 0);

    if (strcmp(target, "source") == 0) {
        if (swap_current_diag) {
            if (swap_apply(src_hc_mmc_host_diag_ops_ptr(),
                           swap_diag_base_ops) != 0)
                return -1;
            swap_current_source = swap_diag_base_source;
            swap_current_diag = 0;
        }
        if (swap_current_source)
            return 1;

#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
        return -1;
#else
        src_hc_mmc_host_save_vendor(&hc_mmc_host_hw_ops);
        if (swap_apply(&hc_mmc_host_hw_ops, src_hc_mmc_host_hw_ops_ptr()) == 0) {
            swap_current_source = 1;
            swap_current_diag = 0;
            unifrog_log("mmc_host_swap: switched to SOURCE driver\n");
            return 1;
        }
        unifrog_log("mmc_host_swap: host not found for source swap\n");
        return -1;
#endif
    }

    if (strcmp(target, "vendor") == 0) {
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
        unifrog_log("mmc_host_swap: vendor driver unavailable in source-default build\n");
        return -1;
#else
        if (swap_current_diag) {
            if (swap_apply(src_hc_mmc_host_diag_ops_ptr(),
                           swap_diag_base_ops) != 0) {
                unifrog_log("mmc_host_swap: host not found for diagnostic revert\n");
                return -1;
            }
            swap_current_source = swap_diag_base_source;
            swap_current_diag = 0;
        }
        if (!swap_current_source)
            return 0;

        if (swap_apply(src_hc_mmc_host_hw_ops_ptr(), &hc_mmc_host_hw_ops) == 0) {
            swap_current_source = 0;
            swap_current_diag = 0;
            unifrog_log("mmc_host_swap: reverted to VENDOR driver\n");
            return 0;
        }
        unifrog_log("mmc_host_swap: host not found for vendor revert\n");
        return -1;
#endif
    }

    if (strcmp(target, "diagnose") == 0) {
        if (swap_current_diag)
            return swap_diag_base_source ? 3 : 2;

#if !UNIFROG_MMC_HOST_SOURCE_DEFAULT
        src_hc_mmc_host_save_vendor(&hc_mmc_host_hw_ops);
#endif
        current_is_source = swap_current_source ? 1 : 0;
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
        current_ops = &hc_mmc_host_hw_ops;
#else
        current_ops = current_is_source ? src_hc_mmc_host_hw_ops_ptr() :
            &hc_mmc_host_hw_ops;
#endif
        src_hc_mmc_host_diag_init(current_ops, current_is_source);
        src_hc_mmc_host_diag_reset();
        swap_diag_base_ops = current_ops;
        swap_diag_base_source = current_is_source;

        if (swap_apply(current_ops, src_hc_mmc_host_diag_ops_ptr()) == 0) {
            swap_current_diag = 1;
            unifrog_log("mmc_host_swap: switched to DIAGNOSTIC driver over %s\n",
                current_is_source ? "SOURCE" : "VENDOR");
            return current_is_source ? 3 : 2;
        }
        unifrog_log("mmc_host_swap: host not found for diag swap\n");
        return -1;
    }

    if (strcmp(target, "dump") == 0) {
        src_hc_mmc_host_diag_dump();
        return 3;
    }

    return -1;
}

int unifrog_platform_mmc_host_source_available(void)
{
    return 1;
}
