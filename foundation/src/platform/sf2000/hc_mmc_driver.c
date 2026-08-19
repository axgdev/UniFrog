/*
 * Source replacement for the request/probe half of libmmchosthc15.a.
 *
 * HCRTOS supplies the generic MMC core, block queue, platform bus, IRQ,
 * workqueue, timer, DMA, and device-tree APIs. This file supplies the
 * HC15xx-specific platform driver and translates the generic core's private
 * mmc_host/mmc_request/mmc_command/mmc_data objects into calls on the
 * register-level hc_mmc_host_hw_ops implemented in hc_mmc_host.c.
 *
 * None of the HCRTOS MMC structure definitions are public in this tree.
 * Their fields are therefore accessed through reverse-engineered byte offsets,
 * and all local ABI mirror structures are guarded by size/offset assertions.
 * When changing an offset or layout, verify it against the linked HCRTOS
 * objects or vendor libmmchosthc15.a rather than assuming Linux layout.
 *
 * Request lifecycle:
 *
 *   mmc core -> hc_src_request()
 *     [optional SBC] -> command IRQ -> hc_src_cmd_work()
 *     main command/data -> data IRQ -> hc_src_data_work()
 *     [optional STOP] -> command IRQ -> hc_src_cmd_work()
 *     -> hc_src_request_done() -> mmc_request_done()
 *
 * The IRQ handler only acknowledges hardware, cancels the request timeout,
 * and queues deferred work. Status/response processing and request completion
 * happen in workqueue context, matching the vendor scheduling contract. A
 * timer owns timeout recovery when no completion IRQ wins.
 */
#include <unifrog/hc_mmc_host.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hcuapi/pinmux.h>
#include <kernel/module.h>
#include <unifrog/boot_trace.h>
#include <unifrog/exception_record.h>
#include <unifrog/log.h>

#define HC_MMC_EXTRA_BYTES 288u
#define HC_MMC_DMA_BYTES 0x100000u
#define HC_MMC_DMA_ALIGN 32u
#define HC_MMC_MAX_BLOCK_SIZE 512u
#define HC_MMC_MAX_BLOCKS 2048u
#define HC_MMC_MAX_SEGS 256u

/*
 * Reverse-engineered HCRTOS struct mmc_host offsets.
 *
 * The public mmc_host pointer addresses the generic-core object. Its
 * controller-private hc_mmc_host storage starts at +0x240. These offsets are
 * fields used by the source probe/request path in that generic object.
 */
#define MMC_HOST_OPS_OFFSET 196u
#define MMC_HOST_F_MIN_OFFSET 204u
#define MMC_HOST_F_MAX_OFFSET 208u
#define MMC_HOST_OCR_AVAIL_OFFSET 216u
#define MMC_HOST_ACTUAL_CLOCK_OFFSET 532u
#define MMC_HOST_MAX_SEG_SIZE_OFFSET 268u
#define MMC_HOST_MAX_SEGS_OFFSET 272u
#define MMC_HOST_MAX_REQ_SIZE_OFFSET 276u
#define MMC_HOST_MAX_BLK_SIZE_OFFSET 280u
#define MMC_HOST_MAX_BLK_COUNT_OFFSET 284u
#define MMC_HOST_CAPS_OFFSET 256u
#define MMC_HOST_IOS_OFFSET 292u
#define MMC_HOST_PARENT_OFFSET 0u
#define MMC_HOST_CLASS_DEV_PARENT_OFFSET 588u
#define MMC_HOST_PRIVATE_BACKPTR_OFFSET 600u
#define MMC_HOST_SDIO_IRQ_THREAD_OFFSET 484u
#define MMC_HOST_SDIO_IRQ_PENDING_OFFSET 488u
#define MMC_HOST_DEFAULT_OCR_AVAIL 0x007c0000u

/* Reverse-engineered mmc_request/mmc_command/mmc_data field offsets. */
#define MMC_REQUEST_SBC_OFFSET 0u
#define MMC_REQUEST_CMD_OFFSET 4u
#define MMC_REQUEST_DATA_OFFSET 8u
#define MMC_COMMAND_OPCODE_OFFSET 0u
#define MMC_COMMAND_ARG_OFFSET 4u
#define MMC_COMMAND_RESP_OFFSET 8u
#define MMC_COMMAND_FLAGS_OFFSET 24u
#define MMC_COMMAND_ERROR_OFFSET 32u
#define MMC_DATA_BLKSZ_OFFSET 8u
#define MMC_DATA_BLOCKS_OFFSET 12u
#define MMC_DATA_ERROR_OFFSET 16u
#define MMC_DATA_FLAGS_OFFSET 20u
#define MMC_DATA_BYTES_XFERED_OFFSET 24u
#define MMC_DATA_STOP_OFFSET 28u
#define MMC_DATA_SG_LEN_OFFSET 36u
#define MMC_DATA_SG_OFFSET 44u
#define MMC_DATA_WRITE_FLAG 0x100u
#define MMC_DATA_READ_FLAG 0x200u

/*
 * HCRTOS scatterlist entries use the Linux-style page_link/offset/length
 * prefix. page_link has metadata in its low bits; the virtual and physical
 * masks intentionally differ because HC15xx DMA consumes a physical address
 * while CPU copies use the KSEG virtual page address.
 */
#define SG_PAGE_LINK_OFFSET 0u
#define SG_OFFSET_OFFSET 4u
#define SG_LENGTH_OFFSET 8u
#define SG_DMA_ADDRESS_OFFSET 12u
#define SG_PAGE_MASK 0xfffff000u
#define HC_SRC_WORK_DOING_OFFSET 24u /* NuttX work_s.doing in opaque prefix */

#define HC_SRC_IRQ_CMD_DONE 0x01u
#define HC_SRC_IRQ_DATA_DONE 0x02u
#define HC_SRC_IRQ_SDIO 0x04u
#define HC_SRC_ERR_TIMEOUT (-116)
#define HC_SRC_ERR_NOMEDIUM (-135)
#define HC_SRC_ERR_CRC (-138)
#define HC_SRC_PHASE_OWNER_IRQ 1u
#define HC_SRC_PHASE_OWNER_TIMEOUT 2u
#define HC_SRC_PHASE_GENERATION_MASK 0x3fffffffu

/*
 * Retained exception breadcrumbs survive a source-default boot that cannot
 * mount storage and therefore cannot persist normal text logs. The marker
 * family distinguishes registration/probe phases from request phases.
 */
#define HC_SRC_ACTIVITY_PROBE_BEGIN 0x4d4d0101u
#define HC_SRC_ACTIVITY_PROBE_DONE 0x4d4d0102u
#define HC_SRC_ACTIVITY_DRIVER_REGISTER_BEGIN 0x4d4d0103u
#define HC_SRC_ACTIVITY_DRIVER_REGISTER_DONE 0x4d4d0104u
#define HC_SRC_ACTIVITY_DEVICE_REGISTER_BEGIN 0x4d4d0105u
#define HC_SRC_ACTIVITY_DEVICE_REGISTER_DONE 0x4d4d0106u
#define HC_SRC_ACTIVITY_REQUEST_BEGIN 0x4d4d0201u
#define HC_SRC_ACTIVITY_PREPARE_DATA 0x4d4d0202u
#define HC_SRC_ACTIVITY_COMMAND_BEGIN 0x4d4d0203u
#define HC_SRC_ACTIVITY_COMMAND_DONE 0x4d4d0204u
#define HC_SRC_ACTIVITY_REQUEST_DONE 0x4d4d0205u
#define HC_SRC_LOG_LIMIT 256u
#define HC_SRC_CMD_LOG_LIMIT 64u
#define HC_SRC_OPS_LOG_LIMIT 48u
#define HC_SRC_WAIT_REG_LOG_LIMIT 96u

#define MMC_CAP_4_BIT_DATA (1u << 0)
#define MMC_CAP_MMC_HIGHSPEED (1u << 1)
#define MMC_CAP_SD_HIGHSPEED (1u << 2)
#define IORESOURCE_MEM 0x00000200u
#define DMA_TO_DEVICE 1
#define DMA_FROM_DEVICE 2

typedef uint32_t dma_addr_t;

struct resource;

/*
 * ABI mirrors for HCRTOS work_struct and timer_list.
 *
 * The opaque leading bytes are owned by HCRTOS. The source driver only
 * initializes callback fields whose offsets were observed in the vendor
 * object.
 */
struct hc_src_work {
    uint8_t work[40];
    void (*func)(struct hc_src_work *work);
    void *wq;
};

struct hc_src_timer {
    uint8_t work[40];
    unsigned long expires;
    void (*function2)(struct hc_src_timer *timer);
    void (*function)(unsigned long data);
    unsigned long data;
};

/*
 * Source-only diagnostics for the single HC15xx controller instance.
 *
 * Live request ownership remains in hc_mmc_host. This side structure stays
 * outside the vendor-sized host ABI and records command/IRQ ordering without
 * borrowing unknown private-host fields.
 */
struct hc_src_runtime {
    struct hc_mmc_host *host;
    volatile uint32_t command_generation;
    volatile uint32_t active_generation;
    volatile uint32_t phase_claim;
    /* Epoch rejects handlers that observed status before a timeout reset. */
    volatile uint32_t irq_epoch;
    /* Teardown waits for callbacks that free_irq() may not synchronize. */
    volatile uint32_t irq_in_flight;
    /* Covers timer/work callbacks even if HCRTOS clears work_s.doing early. */
    volatile uint32_t async_in_flight;
    /* Timeout/reset status is discarded until the next command drains it. */
    volatile uint32_t irq_quarantined;
    volatile uint32_t last_irq;
    volatile uint32_t irq_count;
    volatile uint32_t spurious_irqs;
    volatile uint32_t start_in_progress;
    volatile uint32_t shutting_down;
};

static struct hc_src_runtime hc_src_runtime;

/*
 * Static queue sentinels outlive every host allocation. Teardown queues them
 * after cancelling source callbacks; when a sentinel runs, all earlier worker
 * callbacks and their workqueue epilogues on that queue have completed.
 */
static struct hc_src_timer hc_src_timer_barrier;
static struct hc_src_work hc_src_work_barrier;
static volatile uint32_t hc_src_barrier_target;
static volatile uint32_t hc_src_timer_barrier_done;
static volatile uint32_t hc_src_work_barrier_done;

#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
static volatile uint32_t hc_src_log_seq;
static volatile uint32_t hc_src_log_count;
static volatile uint32_t hc_src_cmd_log_count;
static volatile uint32_t hc_src_ops_log_count;
static volatile uint32_t hc_src_wait_reg_log_count;

/*
 * Atomically reserve one entry from a saturating diagnostic budget.
 * Request, workqueue, timer, and probe contexts can all emit diagnostics; a
 * plain check/increment could exceed or reopen an exhausted budget.
 */
static int hc_src_log_reserve(volatile uint32_t *counter, uint32_t limit,
    uint32_t *index)
{
    uint32_t current;

    do {
        current = *counter;
        if (current >= limit)
            return 0;
    } while (!__sync_bool_compare_and_swap(counter, current, current + 1u));

    if (index)
        *index = current;
    return 1;
}

static uint32_t hc_src_log_next_seq(void)
{
    return __sync_fetch_and_add(&hc_src_log_seq, 1u);
}

/*
 * Source tracing must terminate around disk-log availability; otherwise its
 * own textual diagnostics generate more MMC traffic while servicing MMC I/O.
 */
#define HC_SRC_LOG(stage, ret, a0, a1, a2) do { \
    if (hc_src_log_reserve(&hc_src_log_count, HC_SRC_LOG_LIMIT, NULL)) { \
        unifrog_log("unifrog source_mmc seq=%lu stage=%s ret=%d a0=0x%08lx a1=0x%08lx a2=0x%08lx\n", \
            (unsigned long)hc_src_log_next_seq(), (stage), (int)(ret), \
            (unsigned long)(uintptr_t)(a0), (unsigned long)(uintptr_t)(a1), \
            (unsigned long)(uintptr_t)(a2)); \
    } \
} while (0)
#define HC_SRC_OPS_LOG(stage, ret, a0, a1, a2) do { \
    if (hc_src_log_reserve(&hc_src_ops_log_count, HC_SRC_OPS_LOG_LIMIT, \
        NULL)) { \
        HC_SRC_LOG((stage), (ret), (a0), (a1), (a2)); \
    } \
} while (0)
#else
#define HC_SRC_LOG(stage, ret, a0, a1, a2) do { \
    (void)(stage); \
    (void)(ret); \
    (void)(a0); \
    (void)(a1); \
    (void)(a2); \
} while (0)
#define HC_SRC_OPS_LOG(stage, ret, a0, a1, a2) do { \
    (void)(stage); \
    (void)(ret); \
    (void)(a0); \
    (void)(a1); \
    (void)(a2); \
} while (0)
#endif

/*
 * Local mirrors of the HCRTOS driver-core ABI. They are private to this file
 * because the SDK headers available to this build do not expose the complete
 * layouts consumed by platform_driver_register() and
 * of_platform_device_register_full().
 */
struct device {
    void *parent;
    void *p;
    void *of_node;
    uint8_t _pad[0x30];
    void *platform_data;
    void *driver_data;
};

struct device_driver {
    const char *name;
    void *bus;
    void *owner;
    const char *mod_name;
    int probe_type;
    const void *of_match_table;
    int (*probe)(struct device *dev);
    int (*remove)(struct device *dev);
    void (*shutdown)(struct device *dev);
    int (*suspend)(struct device *dev, uint32_t state);
    int (*resume)(struct device *dev);
    const void *pm;
    void *p;
};

struct of_device_id {
    char name[32];
    char type[32];
    char compatible[128];
    const void *data;
};

struct platform_device {
    uint32_t irq;
    int status;
    void *dma_buf;
    int dma_size;
    struct device dev;
};

struct platform_device_id;

struct platform_driver {
    int (*probe)(struct platform_device *pdev);
    int (*remove)(struct platform_device *pdev);
    void (*shutdown)(struct platform_device *pdev);
    struct device_driver driver;
    const struct platform_device_id *id_table;
    bool prevent_deferred_probe;
};

struct platform_device_info {
    void *parent;
    const char *name;
    int id;
    struct resource *res;
    unsigned int num_res;
    const void *data;
    size_t size_data;
    uint32_t dma_align_pad;
    uint64_t dma_mask;
};

/*
 * Compile-time ABI tripwires. A mismatch means the generic HCRTOS core would
 * read or write the wrong source-owned memory, often before the first request.
 */
#define HC_SRC_STATIC_ASSERT(name, expr) \
    typedef char name[(expr) ? 1 : -1]

HC_SRC_STATIC_ASSERT(hc_src_device_driver_size,
    sizeof(struct device_driver) == 52);
HC_SRC_STATIC_ASSERT(hc_src_device_driver_of_match_offset,
    offsetof(struct device_driver, of_match_table) == 20);
HC_SRC_STATIC_ASSERT(hc_src_platform_driver_size,
    sizeof(struct platform_driver) == 72);
HC_SRC_STATIC_ASSERT(hc_src_platform_driver_driver_offset,
    offsetof(struct platform_driver, driver) == 12);
HC_SRC_STATIC_ASSERT(hc_src_platform_driver_id_table_offset,
    offsetof(struct platform_driver, id_table) == 64);
HC_SRC_STATIC_ASSERT(hc_src_platform_device_info_size,
    sizeof(struct platform_device_info) == 40);
HC_SRC_STATIC_ASSERT(hc_src_platform_device_info_size_data_offset,
    offsetof(struct platform_device_info, size_data) == 24);
HC_SRC_STATIC_ASSERT(hc_src_platform_device_info_dma_mask_offset,
    offsetof(struct platform_device_info, dma_mask) == 32);

struct hc_mmc_host_ops {
    void (*post_req)(void *mmc, void *mrq, int err);
    void (*pre_req)(void *mmc, void *mrq, int is_first_req);
    void (*request)(void *mmc, void *mrq);
    void (*set_ios)(void *mmc, void *ios);
    int (*get_ro)(void *mmc);
    int (*get_cd)(void *mmc);
    void (*enable_sdio_irq)(void *mmc, int enable);
    void *reserved[9];
};

/*
 * Validate every mirrored callback table and private-host field used below.
 * The reserved tail in hc_mmc_host_ops is important because mmc_add_host()
 * may inspect optional callbacks beyond enable_sdio_irq.
 */
HC_SRC_STATIC_ASSERT(hc_src_mmc_host_ops_size,
    sizeof(struct hc_mmc_host_ops) == 64);
HC_SRC_STATIC_ASSERT(hc_src_mmc_host_ops_request_offset,
    offsetof(struct hc_mmc_host_ops, request) == 8);
HC_SRC_STATIC_ASSERT(hc_src_mmc_host_ops_enable_sdio_irq_offset,
    offsetof(struct hc_mmc_host_ops, enable_sdio_irq) == 24);
HC_SRC_STATIC_ASSERT(hc_src_platform_data_size,
    sizeof(struct hc_mmc_platform_data) == 44);
HC_SRC_STATIC_ASSERT(hc_src_platform_data_caps_offset,
    offsetof(struct hc_mmc_platform_data, caps) == 20);
HC_SRC_STATIC_ASSERT(hc_src_platform_data_pin_group_offset,
    offsetof(struct hc_mmc_platform_data, pin_group) == 40);
HC_SRC_STATIC_ASSERT(hc_src_hw_ops_size,
    sizeof(struct hc_mmc_host_hw_ops) == 76);
HC_SRC_STATIC_ASSERT(hc_src_hw_ops_get_response_offset,
    offsetof(struct hc_mmc_host_hw_ops, get_response) == 28);
HC_SRC_STATIC_ASSERT(hc_src_hw_ops_get_and_clear_irq_offset,
    offsetof(struct hc_mmc_host_hw_ops, get_and_clear_irq) == 64);
HC_SRC_STATIC_ASSERT(hc_src_hw_ops_clock_gate_offset,
    offsetof(struct hc_mmc_host_hw_ops, mmc_clock_gate) == 72);
HC_SRC_STATIC_ASSERT(hc_src_host_size, sizeof(struct hc_mmc_host) == 288);
HC_SRC_STATIC_ASSERT(hc_src_host_req_offset,
    offsetof(struct hc_mmc_host, req) == 28);
HC_SRC_STATIC_ASSERT(hc_src_host_cmd_offset,
    offsetof(struct hc_mmc_host, cmd) == 32);
HC_SRC_STATIC_ASSERT(hc_src_host_use_pio_offset,
    offsetof(struct hc_mmc_host, use_pio) == 36);
HC_SRC_STATIC_ASSERT(hc_src_host_sdio_irq_enabled_offset,
    offsetof(struct hc_mmc_host, sdio_irq_enabled) == 37);
HC_SRC_STATIC_ASSERT(hc_src_host_data_transferring_offset,
    offsetof(struct hc_mmc_host, data_transferring) == 38);
HC_SRC_STATIC_ASSERT(hc_src_host_virt_buf_offset,
    offsetof(struct hc_mmc_host, virt_buf) == 44);
HC_SRC_STATIC_ASSERT(hc_src_host_data_size_offset,
    offsetof(struct hc_mmc_host, data_size) == 52);
HC_SRC_STATIC_ASSERT(hc_src_host_dma_dir_offset,
    offsetof(struct hc_mmc_host, dma_dir) == 56);
HC_SRC_STATIC_ASSERT(hc_src_host_sg_len_offset,
    offsetof(struct hc_mmc_host, sg_len) == 60);
HC_SRC_STATIC_ASSERT(hc_src_host_clk_offset,
    offsetof(struct hc_mmc_host, clk) == 64);
HC_SRC_STATIC_ASSERT(hc_src_host_clock_offset,
    offsetof(struct hc_mmc_host, clock) == 68);
HC_SRC_STATIC_ASSERT(hc_src_host_bus_width_offset,
    offsetof(struct hc_mmc_host, bus_width) == 72);
HC_SRC_STATIC_ASSERT(hc_src_host_timing_offset,
    offsetof(struct hc_mmc_host, timing) == 73);
HC_SRC_STATIC_ASSERT(hc_src_host_rto_timer_offset,
    offsetof(struct hc_mmc_host, rto_timer) == 76);
HC_SRC_STATIC_ASSERT(hc_src_host_sdio_timer_offset,
    offsetof(struct hc_mmc_host, sdio_timer) == 132);
HC_SRC_STATIC_ASSERT(hc_src_host_cmdwork_offset,
    offsetof(struct hc_mmc_host, cmdwork) == 188);
HC_SRC_STATIC_ASSERT(hc_src_host_datawork_offset,
    offsetof(struct hc_mmc_host, datawork) == 236);
HC_SRC_STATIC_ASSERT(hc_src_host_pending_events_offset,
    offsetof(struct hc_mmc_host, pending_events) == 284);
HC_SRC_STATIC_ASSERT(hc_src_work_size, sizeof(struct hc_src_work) == 48);
HC_SRC_STATIC_ASSERT(hc_src_work_func_offset,
    offsetof(struct hc_src_work, func) == 40);
HC_SRC_STATIC_ASSERT(hc_src_timer_size, sizeof(struct hc_src_timer) == 56);
HC_SRC_STATIC_ASSERT(hc_src_timer_function_offset,
    offsetof(struct hc_src_timer, function) == 48);
HC_SRC_STATIC_ASSERT(hc_src_timer_data_offset,
    offsetof(struct hc_src_timer, data) == 52);

/*
 * HCRTOS APIs used by the source replacement. The declarations intentionally
 * use opaque pointers where the source driver only needs an ABI boundary.
 */
extern int platform_driver_register(struct platform_driver *drv);
extern struct resource *platform_get_resource(struct platform_device *pdev,
    unsigned int type, unsigned int num);
extern int platform_get_irq(struct platform_device *pdev, unsigned int num);
extern void *of_platform_device_register_full(struct platform_device_info *info,
    void *node);
extern void *fdt_node_probe_by_path(const char *path);
extern void *mmc_alloc_host(int extra, struct device *dev);
extern void mmc_free_host(void *mmc);
extern int mmc_add_host(void *mmc);
extern void mmc_remove_host(void *mmc);
extern void mmc_request_done(void *mmc, void *mrq);
extern int mmc_of_parse(void *mmc);
extern int mmc_gpio_get_cd(void *mmc);
extern int mmc_gpio_get_ro(void *mmc);
extern int fdt_get_property_u_32_index(int offset, const char *name, int index,
    uint32_t *outval);
extern int fdt_get_property_u_32_array(int offset, const char *name,
    uint32_t *outval, int length);
extern const void *fdt_get_property_data_by_name(int offset, const char *name,
    int *length);
extern int dma_map_sg(struct device *dev, void *sglist, int nents,
    int direction);
extern void dma_unmap_sg(struct device *dev, void *sglist, int nents,
    int direction);
extern void *sg_next(void *sg);
extern void *dma_alloc_coherent(struct device *dev, size_t size,
    dma_addr_t *dma_handle, uint32_t flags);
extern void dma_free_coherent(struct device *dev, size_t size, void *vaddr,
    dma_addr_t dma_handle);
extern void *devm_ioremap_resource(struct device *dev, struct resource *res);
extern int request_threaded_irq(unsigned int irq,
    int (*handler)(int irq, void *dev_id), void *thread_fn,
    unsigned long flags, const char *name, void *dev_id);
extern void free_irq(unsigned int irq, void *dev_id);
extern int del_timer(struct hc_src_timer *timer);
extern int mod_timer(struct hc_src_timer *timer, unsigned long expires);
extern bool queue_work_on(int cpu, void *wq, struct hc_src_work *work);
extern bool cancel_work_sync(struct hc_src_work *work);
extern void *system_wq;
extern int wake_up_process(void *task);
extern void udelay(unsigned int us);

static int hc_src_device_register_once(void);
static int hc_src_driver_register_once(void);
static void hc_src_start_command(struct hc_mmc_host *host, void *cmd,
    void *data);
static void hc_src_request_done(struct hc_mmc_host *host);
static void hc_src_abort_data(struct hc_mmc_host *host, void *data, int err);
static void hc_src_enable_sdio_irq(void *mmc, int enable);
static int hc_src_work_is_doing(const void *work);

#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
/*
 * Board-init entry point for source-default builds.
 *
 * Registering the driver first allows platform-device registration to bind and
 * probe immediately. The one-shot state is cleared on failure so a later board
 * bootstrap attempt can retry.
 */
int hc_mmc_source_bootstrap(void)
{
    static int initialized;
    int ret;

    HC_SRC_LOG("bootstrap.enter", initialized, 0, 0, 0);
    if (initialized) {
        HC_SRC_LOG("bootstrap.already", 0, 0, 0, 0);
        return 0;
    }
    initialized = 1;
    HC_SRC_LOG("bootstrap.driver.begin", 0, 0, 0, 0);
    ret = hc_src_driver_register_once();
    HC_SRC_LOG("bootstrap.driver.done", ret, 0, 0, 0);
    if (ret == 0)
        HC_SRC_LOG("bootstrap.device.begin", 0, 0, 0, 0);
    if (ret == 0)
        ret = hc_src_device_register_once();
    HC_SRC_LOG("bootstrap.device.done", ret, 0, 0, 0);
    if (ret != 0) {
        initialized = 0;
        HC_SRC_LOG("bootstrap.fail", ret, 0, 0, 0);
    }
    HC_SRC_LOG("bootstrap.exit", ret, 0, 0, 0);
    return ret;
}
#endif

/*
 * Unaligned-safe accessors for opaque generic-core objects. memcpy avoids
 * imposing alignment or strict-aliasing assumptions on reverse-engineered
 * fields while still compiling to simple loads/stores for this target.
 */
static inline uint32_t drv_read_u32(void *base, size_t offset)
{
    uint32_t value;

    memcpy(&value, (uint8_t *)base + offset, sizeof(value));
    return value;
}

static inline void drv_write_u32(void *base, size_t offset, uint32_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static inline uint8_t drv_read_u8(void *base, size_t offset)
{
    uint8_t value;

    memcpy(&value, (uint8_t *)base + offset, sizeof(value));
    return value;
}

static inline void drv_write_u8(void *base, size_t offset, uint8_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static inline void drv_write_u16(void *base, size_t offset, uint16_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

/* HCRTOS follows the Linux error-pointer convention for pointer-return APIs. */
static int hc_src_ptr_is_err(const void *ptr)
{
    return (uintptr_t)ptr >= (uintptr_t)-4095;
}

static int hc_src_ptr_err(const void *ptr)
{
    return (int)(intptr_t)ptr;
}

static inline struct hc_mmc_host *drv_host(void *mmc)
{
    return (struct hc_mmc_host *)((uint8_t *)mmc + 0x240u);
}

/* Typed views over the opaque vendor-sized timer/work storage in host. */
static inline struct hc_src_timer *hc_src_rto_timer(struct hc_mmc_host *host)
{
    return (struct hc_src_timer *)host->rto_timer;
}

static inline struct hc_src_timer *hc_src_sdio_timer(struct hc_mmc_host *host)
{
    return (struct hc_src_timer *)host->sdio_timer;
}

static inline struct hc_src_work *hc_src_cmdwork(struct hc_mmc_host *host)
{
    return (struct hc_src_work *)host->cmdwork;
}

static inline struct hc_src_work *hc_src_datawork(struct hc_mmc_host *host)
{
    return (struct hc_src_work *)host->datawork;
}

static inline struct hc_mmc_host *hc_src_host_from_cmdwork(
    struct hc_src_work *work)
{
    return (struct hc_mmc_host *)((uint8_t *)work -
        offsetof(struct hc_mmc_host, cmdwork));
}

static inline struct hc_mmc_host *hc_src_host_from_datawork(
    struct hc_src_work *work)
{
    return (struct hc_mmc_host *)((uint8_t *)work -
        offsetof(struct hc_mmc_host, datawork));
}

/*
 * Reproduce the vendor spinlock critical sections with the available HCRTOS
 * task critical API. Hard-IRQ context is already non-preemptible, so the
 * helpers intentionally become no-ops there.
 */
static void hc_src_lock(void)
{
    if (!xPortIsInISR())
        vTaskEnterCritical();
}

static void hc_src_unlock(void)
{
    if (!xPortIsInISR())
        vTaskExitCritical();
}

static unsigned long hc_src_tick_now(void)
{
    return xPortIsInISR() ? (unsigned long)xTaskGetTickCountFromISR() :
        (unsigned long)xTaskGetTickCount();
}

/*
 * Exactly one completion path may own a command phase. The generation and
 * owner are packed into one atomic word so a late timeout from an older phase
 * cannot claim a newer command after its worker starts the next phase.
 */
static uint32_t hc_src_phase_base(uint32_t generation)
{
    return generation << 2;
}

static int hc_src_claim_phase(uint32_t generation, uint32_t owner)
{
    uint32_t base = hc_src_phase_base(generation);

    /* Generation zero means a request exists but no hardware phase is live. */
    if (generation == 0u)
        return 0;
    return __sync_bool_compare_and_swap(&hc_src_runtime.phase_claim, base,
        base | owner);
}

/* Atomic read used for IRQ epoch/callback synchronization state. */
static uint32_t hc_src_atomic_read(volatile uint32_t *value)
{
    return __sync_fetch_and_add(value, 0u);
}

static void hc_src_wait_irq_idle(void)
{
    while (hc_src_atomic_read(&hc_src_runtime.irq_in_flight) != 0u)
        udelay(1);
}

static void hc_src_async_enter(void)
{
    (void)__sync_add_and_fetch(&hc_src_runtime.async_in_flight, 1u);
}

static void hc_src_async_exit(void)
{
    (void)__sync_sub_and_fetch(&hc_src_runtime.async_in_flight, 1u);
}

static void hc_src_wait_async_idle(void)
{
    while (hc_src_atomic_read(&hc_src_runtime.async_in_flight) != 0u)
        udelay(1);
}

static void hc_src_timer_barrier_callback(unsigned long token)
{
    (void)__sync_lock_test_and_set(&hc_src_timer_barrier_done,
        (uint32_t)token);
}

static void hc_src_work_barrier_callback(struct hc_src_work *work)
{
    (void)work;
    (void)__sync_lock_test_and_set(&hc_src_work_barrier_done,
        hc_src_atomic_read(&hc_src_barrier_target));
}

/*
 * HCRTOS work_process() accesses work_s after invoking its callback, which no
 * in-callback counter can cover. A sentinel on HPWORK orders after timers; a
 * sentinel on system_wq orders after command/data work. Their static storage
 * remains valid while their own non-requeued doing bytes drain.
 */
static void hc_src_wait_callback_epilogues(void)
{
    uint32_t token = __sync_add_and_fetch(&hc_src_barrier_target, 1u);

    if (token == 0u)
        token = __sync_add_and_fetch(&hc_src_barrier_target, 1u);
    hc_src_timer_barrier.function = hc_src_timer_barrier_callback;
    hc_src_timer_barrier.data = token;
    hc_src_work_barrier.func = hc_src_work_barrier_callback;

    (void)mod_timer(&hc_src_timer_barrier, hc_src_tick_now());
    while (!queue_work_on(1, system_wq, &hc_src_work_barrier))
        udelay(1);
    while (hc_src_atomic_read(&hc_src_timer_barrier_done) != token ||
        hc_src_atomic_read(&hc_src_work_barrier_done) != token)
        udelay(1);
    /*
     * Sentinels never requeue themselves, so their doing bytes reliably cover
     * their own worker epilogues and make the static objects reusable.
     */
    while (hc_src_work_is_doing(&hc_src_timer_barrier) ||
        hc_src_work_is_doing(&hc_src_work_barrier))
        udelay(1);
}

/*
 * Controller reset can leave a completion pending after request_done starts a
 * new request. Advance the IRQ epoch before reset and reject all completions
 * until reset status is acknowledged and any already-running handler exits.
 */
static void hc_src_quarantine_irq(struct hc_mmc_host *host)
{
    (void)__sync_lock_test_and_set(&hc_src_runtime.irq_quarantined, 1u);
    __sync_synchronize();
    (void)__sync_add_and_fetch(&hc_src_runtime.irq_epoch, 1u);
    if (host->ops->enable_irq)
        host->ops->enable_irq(host, NULL, 0);
}

static void hc_src_drain_quarantined_irq(struct hc_mmc_host *host)
{
    if (host->ops->get_and_clear_irq)
        (void)host->ops->get_and_clear_irq(host);
    hc_src_wait_irq_idle();
}

/*
 * Early source-default diagnostics. Text logging is globally bounded because
 * persisting a log itself uses this MMC driver; unbounded request logging would
 * recursively generate more storage traffic. Retained activity breadcrumbs
 * below remain available after the text budget is exhausted.
 */
static void hc_src_log_command(const char *stage, struct hc_mmc_host *host,
    void *cmd, void *data, int ret)
{
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
    uint32_t index;
    uint32_t opcode;
    uint32_t arg;
    uint32_t cmd_flags;
    uint32_t cmd_error;
    uint32_t resp0;
    uint32_t resp1;
    uint32_t blksz;
    uint32_t blocks;
    uint32_t flags;
    uint32_t sg_len;
    uint32_t status;
    uint32_t r00 = 0;
    uint32_t r30 = 0;

    if (!hc_src_log_reserve(&hc_src_cmd_log_count, HC_SRC_CMD_LOG_LIMIT,
        &index) ||
        !hc_src_log_reserve(&hc_src_log_count, HC_SRC_LOG_LIMIT, NULL))
        return;

    opcode = cmd ? drv_read_u32(cmd, MMC_COMMAND_OPCODE_OFFSET) : 0;
    arg = cmd ? drv_read_u32(cmd, MMC_COMMAND_ARG_OFFSET) : 0;
    cmd_flags = cmd ? drv_read_u32(cmd, MMC_COMMAND_FLAGS_OFFSET) : 0;
    cmd_error = cmd ? drv_read_u32(cmd, MMC_COMMAND_ERROR_OFFSET) : 0;
    resp0 = cmd ? drv_read_u32(cmd, MMC_COMMAND_RESP_OFFSET) : 0;
    resp1 = cmd ? drv_read_u32(cmd, MMC_COMMAND_RESP_OFFSET + 4u) : 0;
    blksz = data ? drv_read_u32(data, MMC_DATA_BLKSZ_OFFSET) : 0;
    blocks = data ? drv_read_u32(data, MMC_DATA_BLOCKS_OFFSET) : 0;
    flags = data ? drv_read_u32(data, MMC_DATA_FLAGS_OFFSET) : 0;
    sg_len = data ? drv_read_u32(data, MMC_DATA_SG_LEN_OFFSET) : 0;
    status = (host && host->ops && host->ops->get_cmd_status) ?
        host->ops->get_cmd_status(host) : 0;
    if (host && host->iobase) {
        r00 = *(volatile uint32_t *)(host->iobase + 0x00);
        r30 = *(volatile uint32_t *)(host->iobase + 0x30);
    }

    unifrog_log("unifrog source_mmc cmd seq=%lu stage=%s ret=%d host=0x%08lx op=%lu arg=0x%08lx cmd_flags=0x%08lx cmd_err=%ld resp0=0x%08lx resp1=0x%08lx data=0x%08lx blksz=%lu blocks=%lu flags=0x%08lx sg_len=%lu bytes=%lu irq=0x%08lx status=0x%08lx r00=0x%08lx r30=0x%08lx actual=%lu\n",
        (unsigned long)index, stage ? stage : "", ret,
        (unsigned long)(uintptr_t)host, (unsigned long)opcode,
        (unsigned long)arg, (unsigned long)cmd_flags,
        (long)(int32_t)cmd_error, (unsigned long)resp0,
        (unsigned long)resp1, (unsigned long)(uintptr_t)data,
        (unsigned long)blksz, (unsigned long)blocks,
        (unsigned long)flags, (unsigned long)sg_len,
        (unsigned long)(host ? host->data_size : 0),
        (unsigned long)hc_src_runtime.last_irq,
        (unsigned long)status, (unsigned long)r00, (unsigned long)r30,
        (unsigned long)(host && host->mmc ?
            drv_read_u32(host->mmc, MMC_HOST_ACTUAL_CLOCK_OFFSET) : 0));
#else
    (void)stage;
    (void)host;
    (void)cmd;
    (void)data;
    (void)ret;
#endif
}

static void hc_src_log_request_detail(void *mrq, void *sbc, void *cmd,
    void *data)
{
    (void)mrq;
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
    uint32_t opcode = cmd ? drv_read_u32(cmd, MMC_COMMAND_OPCODE_OFFSET) : 0;
    uint32_t blocks = data ? drv_read_u32(data, MMC_DATA_BLOCKS_OFFSET) : 0;
    uint32_t sg_len = data ? drv_read_u32(data, MMC_DATA_SG_LEN_OFFSET) : 0;
    uint32_t detail = (opcode & 0xffu) | ((blocks & 0xfffu) << 8) |
        ((sg_len & 0xfffu) << 20);

    HC_SRC_LOG("request.detail", 0, sbc, data, detail);
#else
    (void)sbc;
    (void)cmd;
    (void)data;
#endif
}

static void hc_src_log_wait_regs(const char *stage, struct hc_mmc_host *host,
    uint32_t irq, uint32_t done_mask, uint32_t poll_index)
{
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
    volatile uint8_t *io;

    if (!host || !host->iobase)
        return;
    if (!hc_src_log_reserve(&hc_src_wait_reg_log_count,
        HC_SRC_WAIT_REG_LOG_LIMIT, NULL) ||
        !hc_src_log_reserve(&hc_src_log_count, HC_SRC_LOG_LIMIT, NULL))
        return;

    io = host->iobase;
    unifrog_log("unifrog source_mmc wait_regs seq=%lu stage=%s host=0x%08lx irq=0x%08lx mask=0x%08lx poll=%lu status=0x%08lx r00=0x%08lx r04=0x%08lx r08=0x%08lx r0c=0x%08lx r10=0x%08lx r14=0x%08lx r18=0x%08lx r1c=0x%08lx r30=0x%08lx r34=0x%08lx r48=0x%08lx r50=0x%08lx\n",
        (unsigned long)hc_src_log_next_seq(), stage ? stage : "",
        (unsigned long)(uintptr_t)host, (unsigned long)irq,
        (unsigned long)done_mask, (unsigned long)poll_index,
        (unsigned long)(host->ops && host->ops->get_cmd_status ?
            host->ops->get_cmd_status(host) : 0),
        (unsigned long)*(volatile uint32_t *)(io + 0x00),
        (unsigned long)*(volatile uint32_t *)(io + 0x04),
        (unsigned long)*(volatile uint32_t *)(io + 0x08),
        (unsigned long)*(volatile uint32_t *)(io + 0x0c),
        (unsigned long)*(volatile uint32_t *)(io + 0x10),
        (unsigned long)*(volatile uint32_t *)(io + 0x14),
        (unsigned long)*(volatile uint32_t *)(io + 0x18),
        (unsigned long)*(volatile uint32_t *)(io + 0x1c),
        (unsigned long)*(volatile uint32_t *)(io + 0x30),
        (unsigned long)*(volatile uint32_t *)(io + 0x34),
        (unsigned long)*(volatile uint32_t *)(io + 0x48),
        (unsigned long)*(volatile uint32_t *)(io + 0x50));
#else
    (void)stage;
    (void)host;
    (void)irq;
    (void)done_mask;
    (void)poll_index;
#endif
}

/*
 * Card-detect policy mirrors the vendor path. A broken-CD board or a host with
 * non-removable capability is always present; otherwise the generic GPIO
 * helper result is accepted unless it is an encoded error pointer/value.
 */
static int hc_src_get_cd(void *mmc)
{
    struct hc_mmc_host *host = drv_host(mmc);
    int ret = mmc_gpio_get_cd(mmc);

    if (host->pdata && (host->pdata->flags & HC_MMC_PDATA_FLAG_BROKEN_CD))
        return 1;
    if (drv_read_u32(mmc, MMC_HOST_CAPS_OFFSET) & 0x100u)
        return 1;
    return (uint32_t)ret < (uint32_t)-4095 ? ret : 0;
}

static int hc_src_get_ro(void *mmc)
{
    struct hc_mmc_host *host = drv_host(mmc);

    if (host->pdata && host->pdata->wp_gpios < 128u)
        return mmc_gpio_get_ro(mmc);
    return 0;
}

/*
 * SDIO IRQ delivery is edge-like at the MMC-core boundary. Enabling arms both
 * the controller source and a short poll timer; delivery disables the source,
 * marks the generic-core pending byte, and wakes its SDIO IRQ thread. The
 * timer handles a level that becomes visible without a normal controller IRQ.
 */
static void hc_src_enable_sdio_irq(void *mmc, int enable)
{
    struct hc_mmc_host *host = drv_host(mmc);

    if (hc_src_runtime.shutting_down)
        enable = 0;
    host->sdio_irq_enabled = enable ? 1u : 0u;
    if (enable)
        (void)mod_timer(hc_src_sdio_timer(host), hc_src_tick_now() + 1u);
    hc_src_lock();
    if (host->ops && host->ops->enable_sdio_irq)
        host->ops->enable_sdio_irq(host, enable);
    hc_src_unlock();
    HC_SRC_OPS_LOG("ops.sdio_irq", enable, mmc, host, 0);
}

static void hc_src_signal_sdio_irq(void *mmc)
{
    void *thread;

    hc_src_enable_sdio_irq(mmc, 0);
    thread = (void *)(uintptr_t)drv_read_u32(mmc,
        MMC_HOST_SDIO_IRQ_THREAD_OFFSET);
    if (!thread)
        return;
    drv_write_u8(mmc, MMC_HOST_SDIO_IRQ_PENDING_OFFSET, 1u);
    (void)wake_up_process(thread);
}

static void hc_src_sdio_timeout_run(unsigned long data)
{
    void *mmc = (void *)(uintptr_t)data;
    struct hc_mmc_host *host = drv_host(mmc);

    if (hc_src_runtime.shutting_down || !host->sdio_irq_enabled)
        return;
    if (!host->data_transferring &&
        !(host->ops->get_cmd_status(host) & 0x20u)) {
        hc_src_signal_sdio_irq(mmc);
        return;
    }
    /*
     * Teardown may begin after the entry check while this callback polls.
     * Recheck immediately before rearm so cancellation cannot leave delayed
     * work referencing the freed host.
     */
    if (!hc_src_runtime.shutting_down)
        (void)mod_timer(hc_src_sdio_timer(host), hc_src_tick_now() + 5u);
}

static void hc_src_sdio_timeout(unsigned long data)
{
    hc_src_async_enter();
    hc_src_sdio_timeout_run(data);
    hc_src_async_exit();
}

/* Error accessors for the opaque mmc_command object. */
static void hc_src_set_cmd_error(void *cmd, int err)
{
    if (cmd)
        drv_write_u32(cmd, MMC_COMMAND_ERROR_OFFSET, (uint32_t)err);
}

static int hc_src_get_cmd_error(void *cmd)
{
    if (!cmd)
        return 0;
    return (int32_t)drv_read_u32(cmd, MMC_COMMAND_ERROR_OFFSET);
}

/*
 * Persist the last high-level source operation in the retained exception
 * record. This is deliberately independent of normal logs and disk access.
 */
static void hc_src_activity(uint32_t marker, void *cmd, uint32_t detail)
{
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
    uint32_t opcode = cmd ? drv_read_u32(cmd, MMC_COMMAND_OPCODE_OFFSET) : 0;
    uint32_t arg = cmd ? drv_read_u32(cmd, MMC_COMMAND_ARG_OFFSET) : detail;

    unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_FRONTEND_STORAGE,
        marker, (opcode & 0xffu) | ((detail & 0xffffu) << 8), arg);
#else
    (void)marker;
    (void)cmd;
    (void)detail;
#endif
}

static void hc_src_activity_raw(uint32_t marker, uint32_t detail0,
    uint32_t detail1)
{
#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
    unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_FRONTEND_STORAGE,
        marker, detail0, detail1);
#else
    (void)marker;
    (void)detail0;
    (void)detail1;
#endif
}

/* Scatterlist prefix accessors; callers traverse chains with sg_next(). */
static void *hc_src_sg_virt(void *entry)
{
    uint32_t page = drv_read_u32(entry, SG_PAGE_LINK_OFFSET) & SG_PAGE_MASK;
    uint32_t offset = drv_read_u32(entry, SG_OFFSET_OFFSET);

    return (void *)(uintptr_t)(page + offset);
}

/* dma_map_sg() publishes the bus address in scatterlist word 3. */
static uint32_t hc_src_sg_dma(void *entry)
{
    return drv_read_u32(entry, SG_DMA_ADDRESS_OFFSET);
}

static uint32_t hc_src_sg_len(void *entry)
{
    return drv_read_u32(entry, SG_LENGTH_OFFSET);
}

static void *hc_src_data_stop(void *data)
{
    return data ? (void *)(uintptr_t)drv_read_u32(data,
        MMC_DATA_STOP_OFFSET) : NULL;
}

static uint32_t hc_src_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void hc_src_clear_dma_state(struct hc_mmc_host *host)
{
    host->dma_dir = 0;
    host->sg_len = 0;
    host->data_size = 0;
}

/*
 * HCRTOS work_s clears its queued worker pointer before invoking the callback,
 * so cancel_work_sync(), flush_work(), and work_busy() all miss running work.
 * The doing byte at offset 24 normally remains set across the callback, while
 * the source async counter also covers HCRTOS requeue clearing doing early.
 */
static int hc_src_work_is_doing(const void *work)
{
    return *((const volatile uint8_t *)work + HC_SRC_WORK_DOING_OFFSET) != 0u;
}

static void hc_src_cancel_work_and_wait(struct hc_src_work *work)
{
    (void)cancel_work_sync(work);
    while (hc_src_work_is_doing(work) ||
        hc_src_atomic_read(&hc_src_runtime.async_in_flight) != 0u)
        udelay(1);
    /* Remove a self-requeue issued by a callback before it became idle. */
    (void)cancel_work_sync(work);
    hc_src_wait_async_idle();
}

/*
 * del_timer() maps to NuttX work_cancel() and likewise does not wait for an
 * already-running callback. Both source timer callbacks refuse to rearm after
 * shutting_down is set, so the doing byte and source async counter both drain.
 */
static void hc_src_cancel_timer_and_wait(struct hc_src_timer *timer)
{
    (void)del_timer(timer);
    while (hc_src_work_is_doing(timer) ||
        hc_src_atomic_read(&hc_src_runtime.async_in_flight) != 0u)
        udelay(1);
    /*
     * Remove a rearm issued in the small interval between the callback's last
     * shutdown check and work_s.doing clearing.
     */
    (void)del_timer(timer);
    hc_src_wait_async_idle();
}

/*
 * HC15xx DMA requires 32-byte alignment. Multi-entry SG requests also bounce
 * because the controller accepts one physical address, while the source
 * driver cannot assume dma_map_sg() coalesces arbitrary entries.
 */
static int hc_src_sg_needs_bounce(void *sg, uint32_t blksz)
{
    uintptr_t addr;

    addr = (uintptr_t)hc_src_sg_virt(sg);
    return ((addr | blksz) & (HC_MMC_DMA_ALIGN - 1u)) != 0u;
}

static uint32_t hc_src_copy_sg_to_bounce(void *bounce, void *sg,
    uint32_t sg_len, uint32_t total)
{
    uint32_t copied = 0;
    void *entry = sg;

    for (uint32_t i = 0; entry && i < sg_len && copied < total; i++) {
        uint32_t len = hc_src_min_u32(hc_src_sg_len(entry), total - copied);

        if (len)
            memcpy((uint8_t *)bounce + copied, hc_src_sg_virt(entry), len);
        copied += len;
        if (i + 1u < sg_len)
            entry = sg_next(entry);
    }
    return copied;
}

/* Return the copied byte count so incomplete or malformed SG chains fail. */
static uint32_t hc_src_copy_bounce_to_sg(void *sg, uint32_t sg_len,
    const void *bounce, uint32_t total)
{
    uint32_t copied = 0;
    void *entry = sg;

    for (uint32_t i = 0; entry && i < sg_len && copied < total; i++) {
        uint32_t len = hc_src_min_u32(hc_src_sg_len(entry), total - copied);

        if (len)
            memcpy(hc_src_sg_virt(entry), (const uint8_t *)bounce + copied,
                len);
        copied += len;
        if (i + 1u < sg_len)
            entry = sg_next(entry);
    }
    return copied;
}

static int hc_src_sg_covers(void *sg, uint32_t sg_len, uint32_t total)
{
    uint32_t copied = 0;
    void *entry = sg;

    for (uint32_t i = 0; entry && i < sg_len && copied < total; i++) {
        uint32_t len = hc_src_sg_len(entry);

        if (len > total - copied)
            len = total - copied;
        copied += len;
        if (i + 1u < sg_len)
            entry = sg_next(entry);
    }
    return copied == total;
}

/*
 * Translate the hardware-op status summary into the errno contract expected
 * by the HCRTOS MMC core. Card removal wins over controller status.
 */
static int hc_src_check_status(struct hc_mmc_host *host)
{
    void *cmd = host->cmd;
    uint32_t status = host->ops->get_cmd_status(host);
    int err;

    if (!hc_src_get_cd(host->mmc)) {
        hc_src_set_cmd_error(cmd, HC_SRC_ERR_NOMEDIUM);
        return HC_SRC_ERR_NOMEDIUM;
    }

    /*
     * The vendor status helper returns compact error bits. Completion is
     * reported by get_and_clear_irq(), while get_cmd_status() is checked after
     * completion for command error/timeout conditions.
     */
    if (status & 0x03u) {
        hc_src_set_cmd_error(cmd, HC_SRC_ERR_CRC);
        return HC_SRC_ERR_CRC;
    }

    err = hc_src_get_cmd_error(cmd);
    return err;
}

static void hc_src_reset_after_error(struct hc_mmc_host *host)
{
    if (host->ops->mmc_ip_reset)
        host->ops->mmc_ip_reset();
    if (host->ops->set_clock)
        host->ops->set_clock(host, host->clock);
    if (host->ops->set_bus_width)
        host->ops->set_bus_width(host, host->bus_width);
    if (host->ops->set_timing)
        host->ops->set_timing(host, host->timing);
}

/*
 * Program and launch one command phase.
 *
 * pending_events closes the IRQ-before-timer race: the IRQ handler sets bit 0
 * before deleting the timeout, and this function only arms the timer if that
 * bit is still clear after command start. Data commands disable forced clock
 * until the complete request finishes.
 */
static void hc_src_start_command(struct hc_mmc_host *host, void *cmd,
    void *data)
{
    uint32_t generation;
    uint32_t timeout_ticks = data ? 800u : 100u;

    /*
     * Teardown rejects phase transitions after the generic core has begun
     * removing the host. The active request is completed once by the common
     * abort path instead of launching hardware that remove is about to free.
     */
    if (hc_src_runtime.shutting_down) {
        void *mrq = host->req;
        void *request_cmd = mrq ? (void *)(uintptr_t)drv_read_u32(mrq,
            MMC_REQUEST_CMD_OFFSET) : cmd;

        hc_src_set_cmd_error(request_cmd, -ESHUTDOWN);
        hc_src_abort_data(host, data, -ESHUTDOWN);
        hc_src_request_done(host);
        return;
    }

    hc_src_activity(HC_SRC_ACTIVITY_COMMAND_BEGIN, cmd, data ? 1u : 0u);
    hc_src_runtime.start_in_progress = 1u;

    /*
     * This is the vendor start-command critical section.  Use the FreeRTOS
     * task critical API rather than raw IRQ save/restore so pending work
     * cannot run until the complete controller programming sequence exits.
     */
    hc_src_lock();
    (void)__sync_fetch_and_and(&host->pending_events, ~1u);
    /*
     * The low two phase-claim bits hold the owner, so retain a 30-bit
     * generation explicitly instead of silently discarding high bits on shift.
     */
    generation = (hc_src_runtime.command_generation + 1u) &
        HC_SRC_PHASE_GENERATION_MASK;
    if (generation == 0u)
        generation = 1u;
    hc_src_runtime.command_generation = generation;
    hc_src_runtime.active_generation = generation;
    hc_src_runtime.phase_claim = hc_src_phase_base(generation);
    /*
     * A timeout quarantines IRQ completions until the next phase has drained
     * old controller status. Epoch comparison also rejects a handler that read
     * the timed-out status before reset but reached phase claiming afterward.
     */
    if (hc_src_runtime.irq_quarantined) {
        hc_src_wait_irq_idle();
        if (host->ops->get_and_clear_irq)
            (void)host->ops->get_and_clear_irq(host);
        (void)__sync_lock_test_and_set(&hc_src_runtime.irq_quarantined, 0u);
    }
    hc_src_runtime.last_irq = 0;
    if (data) {
        host->data_transferring = 1u;
        if (host->ops->enable_force_clock)
            host->ops->enable_force_clock(host, 0);
    }
    host->cmd = cmd;
    if (host->ops->enable_irq)
        host->ops->enable_irq(host, data, 1);
    host->ops->set_cmd(host, cmd, data);
    host->ops->start_cmd(host);
    /*
     * Arm this phase's timeout before releasing the programming lock. If the
     * completion worker could start the next phase first, this older call
     * could otherwise replace the newer phase's timer data and expiry.
     * A completion IRQ racing on another CPU claims the phase first, then
     * takes this same lock to set pending_events and cancel the timer.
     */
    if (!(host->pending_events & 1u)) {
        hc_src_rto_timer(host)->data = generation;
        (void)mod_timer(hc_src_rto_timer(host),
            hc_src_tick_now() + timeout_ticks);
    }
    hc_src_unlock();

    if (data && host->use_pio) {
        hc_src_lock();
        if (drv_read_u32(data, MMC_DATA_FLAGS_OFFSET) & MMC_DATA_WRITE_FLAG)
            host->ops->pio_write(host, data);
        else
            host->ops->pio_read(host, data);
        hc_src_unlock();
        if (hc_src_get_cmd_error(cmd) != 0) {
            (void)del_timer(hc_src_rto_timer(host));
            hc_src_quarantine_irq(host);
            hc_src_reset_after_error(host);
            hc_src_drain_quarantined_irq(host);
            hc_src_runtime.start_in_progress = 0u;
            hc_src_abort_data(host, data, hc_src_get_cmd_error(cmd));
            hc_src_request_done(host);
            return;
        }
    }
    hc_src_runtime.start_in_progress = 0u;
}

/*
 * Prepare one mmc_data phase and program either PIO or DMA.
 *
 * host->sg_len is also the transfer-mode discriminator used at completion:
 *   > 0: dma_map_sg() was used and must be unmapped
 *   < 0: the coherent host bounce buffer was programmed
 *   = 0: no active DMA bookkeeping
 *
 * The source path owns correctness for multi-SG requests and therefore
 * collapses them into the coherent 1 MiB bounce buffer. A single aligned SG
 * entry uses the normal DMA mapping API. Writes are copied into bounce before
 * launch; reads are copied out only after successful completion.
 */
static int hc_src_prepare_dma(struct hc_mmc_host *host, void *data)
{
    void *sg;
    uint32_t sg_len;
    uint32_t flags;
    uint32_t rw_flags;
    uint32_t blksz;
    uint32_t blocks;
    int direction;
    int mapped;
    int bounce;

    hc_src_clear_dma_state(host);
    if (!data)
        return 0;

    drv_write_u32(data, MMC_DATA_BYTES_XFERED_OFFSET, 0);
    drv_write_u32(data, MMC_DATA_ERROR_OFFSET, 0);
    sg_len = drv_read_u32(data, MMC_DATA_SG_LEN_OFFSET);
    flags = drv_read_u32(data, MMC_DATA_FLAGS_OFFSET);
    rw_flags = flags & (MMC_DATA_WRITE_FLAG | MMC_DATA_READ_FLAG);
    blksz = drv_read_u32(data, MMC_DATA_BLKSZ_OFFSET);
    blocks = drv_read_u32(data, MMC_DATA_BLOCKS_OFFSET);
    sg = (void *)(uintptr_t)drv_read_u32(data, MMC_DATA_SG_OFFSET);
    /*
     * Reject malformed geometry before set_block() can underflow block count
     * or multiplication can wrap into an apparently small transfer. The
     * limits match those advertised to the generic MMC core at probe.
     */
    if (!sg || sg_len == 0 || sg_len > HC_MMC_MAX_SEGS ||
        blksz == 0 || blksz > HC_MMC_MAX_BLOCK_SIZE ||
        blocks == 0 || blocks > HC_MMC_MAX_BLOCKS ||
        (rw_flags != MMC_DATA_WRITE_FLAG &&
         rw_flags != MMC_DATA_READ_FLAG) ||
        blocks > HC_MMC_DMA_BYTES / blksz)
        return -EINVAL;
    direction = rw_flags == MMC_DATA_WRITE_FLAG ? DMA_TO_DEVICE :
        DMA_FROM_DEVICE;

    HC_SRC_LOG("prepare_dma.enter", 0, host, sg, sg_len);
    host->data_size = blksz * blocks;
    host->dma_dir = (uint32_t)direction;
    if (!hc_src_sg_covers(sg, sg_len, host->data_size)) {
        hc_src_clear_dma_state(host);
        return -EIO;
    }

    host->ops->set_block(host, blocks, blksz);
    if (host->use_pio) {
        /*
         * The inherited PIO operation only addresses the first SG entry and
         * transfers halfwords. Reject it until the source path can guarantee
         * bounded SG traversal and odd-byte handling. Clear the requested PIO
         * mode so one rejected diagnostic request cannot poison later DMA.
         */
        if (host->ops->pio_cleanup)
            host->ops->pio_cleanup(host);
        else
            host->use_pio = 0u;
        hc_src_clear_dma_state(host);
        return -EOPNOTSUPP;
    }

    /*
     * HC15xx is programmed with one DMA address.  The vendor path relies on
     * dma_map_sg() coalescing or an external wrapper; the source driver must be
     * correct on its own, so collapse every multi-SG request into the coherent
     * host bounce buffer.
     */
    bounce = sg_len > 1u || hc_src_sg_needs_bounce(sg, blksz);
    HC_SRC_LOG("prepare_dma.mode", bounce, sg_len, host->data_size,
        direction);
    if (bounce) {
        if ((flags & MMC_DATA_WRITE_FLAG) &&
            hc_src_copy_sg_to_bounce(host->virt_buf, sg, sg_len,
                host->data_size) != host->data_size) {
            hc_src_clear_dma_state(host);
            return -EIO;
        }
        host->sg_len = -1;
        host->ops->set_dma(host, host->phys_buf, host->data_size,
            (flags & MMC_DATA_WRITE_FLAG) ? 1 : 2);
        return 0;
    }

    mapped = dma_map_sg((struct device *)drv_read_u32(host->mmc,
        MMC_HOST_PARENT_OFFSET), sg, (int)sg_len, direction);
    if (mapped != 1) {
        if (mapped > 0)
            dma_unmap_sg((struct device *)drv_read_u32(host->mmc,
                MMC_HOST_PARENT_OFFSET), sg, (int)sg_len, direction);
        hc_src_clear_dma_state(host);
        return -EIO;
    }

    host->sg_len = mapped;
    HC_SRC_LOG("prepare_dma.mapped", mapped, sg, hc_src_sg_dma(sg),
        host->data_size);
    host->ops->set_dma(host, hc_src_sg_dma(sg), host->data_size,
        (flags & MMC_DATA_WRITE_FLAG) ? 1 : 2);
    return 0;
}

static int hc_src_finish_dma(struct hc_mmc_host *host, void *data, int ret)
{
    void *sg = (void *)(uintptr_t)
        drv_read_u32(data, MMC_DATA_SG_OFFSET);
    uint32_t sg_len = drv_read_u32(data, MMC_DATA_SG_LEN_OFFSET);
    uint32_t flags = drv_read_u32(data, MMC_DATA_FLAGS_OFFSET);
    int direction = (flags & MMC_DATA_WRITE_FLAG) ? DMA_TO_DEVICE :
        DMA_FROM_DEVICE;

    if (sg && host->sg_len > 0)
        dma_unmap_sg((struct device *)drv_read_u32(host->mmc,
            MMC_HOST_PARENT_OFFSET), sg, (int)sg_len,
            direction);
    if (ret == 0 && sg && host->sg_len < 0 &&
        direction == DMA_FROM_DEVICE &&
        hc_src_copy_bounce_to_sg(sg, sg_len, host->virt_buf,
            host->data_size) != host->data_size)
        ret = -EIO;
    if (ret == 0)
        drv_write_u32(data, MMC_DATA_BYTES_XFERED_OFFSET,
            host->data_size);
    HC_SRC_LOG("finish_dma", ret, host, sg, host->data_size);
    drv_write_u32(data, MMC_DATA_ERROR_OFFSET, ret);
    hc_src_clear_dma_state(host);
    return ret;
}

/*
 * Common error/teardown cleanup for a prepared data phase. Reads from a bounce
 * buffer are deliberately not copied after an error, while mapped SG state is
 * always unmapped and all source bookkeeping is cleared before another
 * request can start.
 */
static void hc_src_abort_data(struct hc_mmc_host *host, void *data, int err)
{
    host->data_transferring = 0u;
    if (!data) {
        hc_src_clear_dma_state(host);
        return;
    }
    if (host->use_pio) {
        if (host->ops->pio_cleanup)
            host->ops->pio_cleanup(host);
        else
            host->use_pio = 0u;
        drv_write_u32(data, MMC_DATA_ERROR_OFFSET, (uint32_t)err);
        hc_src_clear_dma_state(host);
        return;
    }
    if (host->sg_len != 0 || host->data_size != 0) {
        (void)hc_src_finish_dma(host, data, err);
        return;
    }
    /*
     * No prepared state means this is an SBC/STOP failure, not a failed data
     * phase. Preserve the data result that may already have completed.
     */
    hc_src_clear_dma_state(host);
}

/*
 * Release source request ownership before notifying the generic core. The
 * callback may synchronously cause more core activity, so no stale req/cmd
 * pointers may remain visible when mmc_request_done() runs.
 */
static void hc_src_request_done(struct hc_mmc_host *host)
{
    void *mrq = __sync_lock_test_and_set(&host->req, NULL);
    void *cmd;

    /* Timeout, IRQ work, and teardown can converge; notify the core once. */
    if (!mrq)
        return;
    cmd = host->cmd;

    if (host->ops->enable_force_clock)
        host->ops->enable_force_clock(host, 1);
    if (!hc_src_get_cd(host->mmc))
        hc_src_set_cmd_error(cmd, HC_SRC_ERR_NOMEDIUM);
    host->cmd = NULL;
    host->data_transferring = 0u;
    hc_src_runtime.active_generation = 0;
    hc_src_runtime.phase_claim = 0;
    hc_src_activity(HC_SRC_ACTIVITY_REQUEST_DONE, cmd,
        (uint32_t)hc_src_get_cmd_error(cmd));
    HC_SRC_LOG("request.done", hc_src_get_cmd_error(cmd), host->mmc, mrq,
        cmd);
    if (mrq)
        mmc_request_done(host->mmc, mrq);
}

/*
 * Common deferred completion for command-only and data-completion work.
 * Controller IRQ delivery is disabled before status/response registers are
 * consumed. Data is associated only with the request's primary command, not
 * its SBC or STOP phases.
 */
static int hc_src_complete_command(struct hc_mmc_host *host,
    const char *stage, uint32_t irq)
{
    void *mrq = host->req;
    void *cmd = host->cmd;
    void *data = NULL;
    int ret;

    if (mrq && cmd == (void *)(uintptr_t)drv_read_u32(mrq,
        MMC_REQUEST_CMD_OFFSET))
        data = (void *)(uintptr_t)drv_read_u32(mrq,
            MMC_REQUEST_DATA_OFFSET);
    if (host->ops->enable_irq)
        host->ops->enable_irq(host, NULL, 0);
    ret = hc_src_check_status(host);
    if (ret == 0 && cmd)
        host->ops->get_response(host, cmd);
    if (ret != 0)
        hc_src_set_cmd_error(cmd, ret);
    hc_src_log_wait_regs(stage, host, irq, 0, hc_src_runtime.irq_count);
    hc_src_log_command("done", host, cmd, data, ret);
    hc_src_activity(HC_SRC_ACTIVITY_COMMAND_DONE, cmd, (uint32_t)ret);
    return ret;
}

/*
 * Command-done state machine:
 *   SBC done  -> prepare data and start the primary command
 *   main command without data done -> complete request
 *   STOP done -> complete request
 *
 * Main commands with data are completed by hc_src_data_work() instead.
 */
static void hc_src_cmd_work_run(struct hc_src_work *work)
{
    struct hc_mmc_host *host = hc_src_host_from_cmdwork(work);
    void *mrq;
    void *cmd;
    void *sbc;
    void *data;
    uint32_t irq;
    int ret;

    if (!host || !host->req || !host->cmd)
        return;

    mrq = host->req;
    cmd = host->cmd;
    sbc = (void *)(uintptr_t)drv_read_u32(mrq, MMC_REQUEST_SBC_OFFSET);
    data = (void *)(uintptr_t)drv_read_u32(mrq, MMC_REQUEST_DATA_OFFSET);
    irq = hc_src_runtime.last_irq;
    ret = hc_src_complete_command(host, "work.cmd", irq);
    HC_SRC_LOG("work.cmd", ret, hc_src_runtime.active_generation,
        hc_src_runtime.irq_count,
        irq | (hc_src_runtime.start_in_progress << 16));
    if (ret != 0) {
        /*
         * A command failure can arrive before a prepared DMA engine reports
         * completion. Reset first so unmapping cannot release memory still
         * reachable by hardware.
         */
        hc_src_quarantine_irq(host);
        hc_src_reset_after_error(host);
        hc_src_drain_quarantined_irq(host);
        hc_src_abort_data(host, data, ret);
        hc_src_request_done(host);
        return;
    }

    if (sbc == cmd) {
        hc_src_activity(HC_SRC_ACTIVITY_PREPARE_DATA, cmd,
            data ? drv_read_u32(data, MMC_DATA_SG_LEN_OFFSET) : 0);
        ret = hc_src_prepare_dma(host, data);
        if (ret != 0) {
            hc_src_set_cmd_error((void *)(uintptr_t)drv_read_u32(mrq,
                MMC_REQUEST_CMD_OFFSET), ret);
            if (data)
                drv_write_u32(data, MMC_DATA_ERROR_OFFSET, (uint32_t)ret);
            hc_src_request_done(host);
            return;
        }
        hc_src_start_command(host, (void *)(uintptr_t)drv_read_u32(mrq,
            MMC_REQUEST_CMD_OFFSET), data);
        return;
    }

    if (!data || drv_read_u32(cmd, MMC_COMMAND_OPCODE_OFFSET) == 12u)
        hc_src_request_done(host);
}

static void hc_src_cmd_work(struct hc_src_work *work)
{
    hc_src_async_enter();
    hc_src_cmd_work_run(work);
    hc_src_async_exit();
}

/*
 * Data-done state machine. DMA completion unmaps/copies/account bytes; PIO
 * follows the vendor cleanup/reset path instead. A successful transfer starts
 * data->stop when present, otherwise it completes the request.
 */
static void hc_src_data_work_run(struct hc_src_work *work)
{
    struct hc_mmc_host *host = hc_src_host_from_datawork(work);
    void *mrq;
    void *cmd;
    void *data;
    void *stop;
    uint32_t irq;
    int reset_done = 0;
    int ret;

    if (!host || !host->req || !host->cmd)
        return;

    mrq = host->req;
    cmd = host->cmd;
    data = (void *)(uintptr_t)drv_read_u32(mrq, MMC_REQUEST_DATA_OFFSET);
    stop = hc_src_data_stop(data);
    irq = hc_src_runtime.last_irq;
    ret = hc_src_complete_command(host, "work.data", irq);
    HC_SRC_LOG("work.data", ret, hc_src_runtime.active_generation,
        hc_src_runtime.irq_count,
        irq | (hc_src_runtime.start_in_progress << 16));
    host->data_transferring = 0u;
    if (host->use_pio) {
        if (host->ops->pio_cleanup)
            host->ops->pio_cleanup(host);
        hc_src_quarantine_irq(host);
        hc_src_reset_after_error(host);
        hc_src_drain_quarantined_irq(host);
        reset_done = 1;
    } else if (data)
        ret = hc_src_finish_dma(host, data, ret);
    if (ret != 0) {
        /* Restore a known controller state before the next core request. */
        if (!reset_done) {
            hc_src_quarantine_irq(host);
            hc_src_reset_after_error(host);
            hc_src_drain_quarantined_irq(host);
        }
        hc_src_request_done(host);
        return;
    }

    if (stop && hc_src_get_cmd_error(cmd) == 0) {
        HC_SRC_LOG("work.stop", 0, stop, data, 0);
        hc_src_start_command(host, stop, NULL);
        return;
    }
    hc_src_request_done(host);
}

static void hc_src_data_work(struct hc_src_work *work)
{
    hc_src_async_enter();
    hc_src_data_work_run(work);
    hc_src_async_exit();
}

/*
 * Request-timeout recovery. Only the callback carrying the active phase
 * generation can reset hardware. The first raw reset followed by
 * hc_src_reset_after_error() deliberately matches the vendor callback.
 * Errors are attached to both the timed-out command and mrq->cmd so phase
 * diagnostics are accurate without leaving the overall request successful.
 */
static void hc_src_timeout(struct hc_mmc_host *host, uint32_t generation)
{
    void *mrq;
    void *cmd;
    void *request_cmd;
    void *data;
    uint32_t status = 0;
    uint32_t r00 = 0;
    uint32_t r10 = 0;

    if (!host || !hc_src_claim_phase(generation,
        HC_SRC_PHASE_OWNER_TIMEOUT))
        return;

    /*
     * The timeout owns this exact command generation. Reset only after winning
     * the phase claim; a completion IRQ that won first must be allowed to
     * finish the request without concurrent reset/unmap/request_done activity.
     */
    hc_src_quarantine_irq(host);
    if (host->ops->mmc_ip_reset)
        host->ops->mmc_ip_reset();
    hc_src_reset_after_error(host);
    hc_src_drain_quarantined_irq(host);
    if (!host->req)
        return;

    mrq = host->req;
    cmd = host->cmd;
    request_cmd = (void *)(uintptr_t)drv_read_u32(mrq,
        MMC_REQUEST_CMD_OFFSET);
    data = (void *)(uintptr_t)drv_read_u32(mrq, MMC_REQUEST_DATA_OFFSET);
    if (host->ops->get_cmd_status)
        status = host->ops->get_cmd_status(host);
    if (host->iobase) {
        r00 = *(volatile uint32_t *)(host->iobase + 0x00);
        r10 = *(volatile uint32_t *)(host->iobase + 0x10);
    }

    hc_src_abort_data(host, data, HC_SRC_ERR_TIMEOUT);
    hc_src_set_cmd_error(cmd, HC_SRC_ERR_TIMEOUT);
    hc_src_set_cmd_error(request_cmd, HC_SRC_ERR_TIMEOUT);
    HC_SRC_LOG("command.timeout", HC_SRC_ERR_TIMEOUT,
        hc_src_runtime.active_generation, status, r00);
    HC_SRC_LOG("command.timeout.response", HC_SRC_ERR_TIMEOUT,
        (uint32_t)(uintptr_t)cmd, r10, hc_src_runtime.last_irq);
    hc_src_request_done(host);
}

static void hc_src_rto_timeout(unsigned long generation)
{
    hc_src_async_enter();
    hc_src_timeout(hc_src_runtime.host, (uint32_t)generation);
    hc_src_async_exit();
}

/* Install callbacks into the opaque work/timer objects allocated with host. */
static int hc_src_init_request_engine(struct hc_mmc_host *host)
{
    memset(&hc_src_runtime, 0, sizeof(hc_src_runtime));
    hc_src_runtime.host = host;
    hc_src_cmdwork(host)->func = hc_src_cmd_work;
    hc_src_datawork(host)->func = hc_src_data_work;
    hc_src_rto_timer(host)->function = hc_src_rto_timeout;
    hc_src_rto_timer(host)->data = 0;
    hc_src_sdio_timer(host)->function = hc_src_sdio_timeout;
    hc_src_sdio_timer(host)->data = (unsigned long)(uintptr_t)host->mmc;
    return 0;
}

/*
 * MMC core request entry point. It takes ownership of mrq, prepares data
 * immediately unless an SBC must run first, starts the first command phase,
 * and returns while IRQ/work/timer callbacks finish the request.
 */
static void hc_src_request(void *mmc, void *mrq)
{
    struct hc_mmc_host *host = drv_host(mmc);
    void *sbc = (void *)(uintptr_t)drv_read_u32(mrq, MMC_REQUEST_SBC_OFFSET);
    void *cmd = (void *)(uintptr_t)drv_read_u32(mrq, MMC_REQUEST_CMD_OFFSET);
    void *data = (void *)(uintptr_t)drv_read_u32(mrq, MMC_REQUEST_DATA_OFFSET);
    int ret = 0;

    HC_SRC_LOG("request.start", 0, mmc, mrq, cmd);
    hc_src_log_request_detail(mrq, sbc, cmd, data);
    hc_src_activity(HC_SRC_ACTIVITY_REQUEST_BEGIN, cmd, data ? 1u : 0u);
    if (hc_src_runtime.shutting_down) {
        hc_src_set_cmd_error(cmd, -ESHUTDOWN);
        mmc_request_done(mmc, mrq);
        return;
    }
    if (!hc_src_get_cd(mmc)) {
        hc_src_set_cmd_error(cmd, HC_SRC_ERR_NOMEDIUM);
        mmc_request_done(mmc, mrq);
        return;
    }
    host->req = mrq;
    if (!sbc) {
        hc_src_activity(HC_SRC_ACTIVITY_PREPARE_DATA, cmd,
            data ? drv_read_u32(data, MMC_DATA_SG_LEN_OFFSET) : 0);
        ret = hc_src_prepare_dma(host, data);
    }
    if (ret != 0) {
        hc_src_set_cmd_error(cmd, ret);
        if (data)
            drv_write_u32(data, MMC_DATA_ERROR_OFFSET, (uint32_t)ret);
        hc_src_request_done(host);
        return;
    }
    hc_src_start_command(host, sbc ? sbc : cmd, sbc ? NULL : data);
}

/*
 * Cache the generic core's requested IOS state in the private host before
 * applying it. Timeout recovery replays these cached values after reset.
 */
static void hc_src_set_ios(void *mmc, void *ios)
{
    struct hc_mmc_host *host = drv_host(mmc);
    uint32_t clock = drv_read_u32(ios, 0);
    uint8_t bus_width = drv_read_u8(ios, 9);
    uint8_t timing = drv_read_u8(ios, 10);

    hc_src_lock();
    host->clock = clock;
    host->bus_width = bus_width;
    host->timing = timing;

    if (host->ops->set_clock)
        host->ops->set_clock(host, clock);
    if (host->ops->set_bus_width)
        host->ops->set_bus_width(host, bus_width);
    if (host->ops->set_timing)
        host->ops->set_timing(host, timing);
    hc_src_unlock();
    HC_SRC_OPS_LOG("ops.set_ios", 0, mmc, clock,
        ((uint32_t)bus_width << 8) | timing);
}

static const struct hc_mmc_host_ops hc_src_mmc_ops = {
    .request = hc_src_request,
    .set_ios = hc_src_set_ios,
    .get_ro = hc_src_get_ro,
    .get_cd = hc_src_get_cd,
    .enable_sdio_irq = hc_src_enable_sdio_irq,
};

/*
 * Hard-IRQ half of the request engine. It acknowledges the controller through
 * get_and_clear_irq(), records diagnostics, cancels the matching request
 * timeout under the vendor-equivalent critical section, and queues work on
 * CPU 1. It never reads responses or calls mmc_request_done() directly.
 */
static int hc_src_irq_handler_run(int irq, void *dev_id)
{
    struct hc_mmc_host *host = (struct hc_mmc_host *)dev_id;
    uint32_t entry_epoch;
    uint32_t entry_quarantined;
    uint32_t status;
    uint32_t generation;
    uint32_t completion;

    (void)irq;
    if (!host || !host->ops || !host->ops->get_and_clear_irq)
        return 0;

    entry_epoch = hc_src_atomic_read(&hc_src_runtime.irq_epoch);
    entry_quarantined = hc_src_atomic_read(&hc_src_runtime.irq_quarantined);
    status = host->ops->get_and_clear_irq(host);
    hc_src_runtime.last_irq = status;
    hc_src_runtime.irq_count++;
    if (entry_quarantined ||
        entry_epoch != hc_src_atomic_read(&hc_src_runtime.irq_epoch) ||
        hc_src_atomic_read(&hc_src_runtime.irq_quarantined))
        return 1;
    if (hc_src_runtime.shutting_down && !host->req)
        return 1;
    if (!(status & (HC_SRC_IRQ_CMD_DONE | HC_SRC_IRQ_DATA_DONE))) {
        if (!(status & HC_SRC_IRQ_SDIO))
            hc_src_runtime.spurious_irqs++;
    }

    /*
     * A controller status can contain more than one normalized completion bit.
     * Select the phase that matches current transfer state, then atomically
     * claim it so timeout and duplicate IRQ paths cannot both queue completion.
     */
    generation = hc_src_runtime.active_generation;
    completion = 0;
    if ((status & HC_SRC_IRQ_DATA_DONE) && host->data_transferring)
        completion = HC_SRC_IRQ_DATA_DONE;
    else if (status & HC_SRC_IRQ_CMD_DONE)
        completion = HC_SRC_IRQ_CMD_DONE;
    else if (status & HC_SRC_IRQ_DATA_DONE)
        completion = HC_SRC_IRQ_DATA_DONE;

    if (completion != 0 && hc_src_claim_phase(generation,
        HC_SRC_PHASE_OWNER_IRQ)) {
        hc_src_lock();
        (void)__sync_fetch_and_or(&host->pending_events, 1u);
        (void)del_timer(hc_src_rto_timer(host));
        hc_src_unlock();
    } else {
        completion = 0;
    }

    if (completion == HC_SRC_IRQ_DATA_DONE) {
        host->data_transferring = 0u;
        (void)queue_work_on(1, system_wq, hc_src_datawork(host));
    }
    if (completion == HC_SRC_IRQ_CMD_DONE) {
        (void)queue_work_on(1, system_wq, hc_src_cmdwork(host));
    }
    if ((status & HC_SRC_IRQ_SDIO) && !hc_src_runtime.shutting_down) {
        (void)del_timer(hc_src_sdio_timer(host));
        if (host->sdio_irq_enabled)
            hc_src_signal_sdio_irq(host->mmc);
    }
    return 1;
}

/*
 * free_irq() removes future delivery but exposes no portable running-handler
 * barrier. Count the complete hard-IRQ callback so teardown can wait before
 * cancelling work that an in-flight handler might still enqueue.
 */
static int hc_src_irq_handler(int irq, void *dev_id)
{
    int ret;

    (void)__sync_add_and_fetch(&hc_src_runtime.irq_in_flight, 1u);
    ret = hc_src_irq_handler_run(irq, dev_id);
    (void)__sync_sub_and_fetch(&hc_src_runtime.irq_in_flight, 1u);
    return ret;
}

/*
 * Build vendor-layout platform data when the platform device did not provide
 * one. Only properties observed in the vendor probe are interpreted here;
 * zero-initialized fields retain their vendor defaults.
 */
static struct hc_mmc_platform_data *hc_src_parse_pdata(int node)
{
    struct hc_mmc_platform_data *pdata = calloc(1, sizeof(*pdata));

    if (!pdata)
        return NULL;
    if (fdt_get_property_u_32_index(node, "num-slots", 0,
        &pdata->bus_width) != 0)
        pdata->bus_width = 1;
    if (fdt_get_property_data_by_name(node, "broken-cd", NULL))
        pdata->flags |= HC_MMC_PDATA_FLAG_BROKEN_CD;
    (void)fdt_get_property_u_32_index(node, "card-detect-delay", 0,
        &pdata->card_detect_delay);
    (void)fdt_get_property_u_32_index(node, "clock-frequency", 0,
        &pdata->bus_hz);
    if (fdt_get_property_data_by_name(node, "supports-highspeed", NULL))
        pdata->caps |= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;
    return pdata;
}

/*
 * Bind one /hcrtos/mmc platform device to the generic HCRTOS MMC core.
 *
 * Probe allocates the generic host plus the exact 288-byte private ABI,
 * installs source callbacks, maps the controller, allocates the coherent
 * bounce buffer, publishes conservative host limits, connects IRQ/work/timer
 * machinery, initializes pins/controller state, and finally calls
 * mmc_add_host() to start card enumeration.
 *
 * Failure labels unwind resources in strict reverse allocation order.
 */
static int hc_src_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct resource *res;
    struct hc_mmc_platform_data *pdata;
    struct hc_mmc_host *host;
    struct pinmux_setting *pins;
    dma_addr_t dma_phys = 0;
    uint32_t clock_range[2];
    void *mmc;
    int irq;
    int node = (int)(uintptr_t)dev->of_node;
    int pdata_allocated = 0;
    int engine_initialized = 0;
    int irq_registered = 0;
    int ret;

    unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_PROBE_BEGIN,
        (uint32_t)(uintptr_t)pdev, (uint32_t)(uintptr_t)dev->of_node, 0);
    hc_src_activity(HC_SRC_ACTIVITY_PROBE_BEGIN, NULL,
        (uint32_t)(uintptr_t)pdev);
    HC_SRC_LOG("probe.enter", 0, pdev, dev, node);
    mmc = mmc_alloc_host(HC_MMC_EXTRA_BYTES, dev);
    if (!mmc) {
        unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_PROBE_DONE,
            (uint32_t)-ENOMEM, 0, 0);
        hc_src_activity(HC_SRC_ACTIVITY_PROBE_DONE, NULL,
            (uint32_t)-ENOMEM);
        HC_SRC_LOG("probe.alloc.fail", -ENOMEM, pdev, dev, 0);
        return -ENOMEM;
    }
    HC_SRC_LOG("probe.alloc", 0, mmc, drv_host(mmc), dev);
    host = drv_host(mmc);
    memset(host, 0, HC_MMC_EXTRA_BYTES);
    host->dev = dev;
    host->mmc = mmc;
    host->ops = &hc_mmc_host_hw_ops;
    drv_write_u32(mmc, MMC_HOST_PARENT_OFFSET, (uint32_t)(uintptr_t)dev);
    drv_write_u32(mmc, MMC_HOST_CLASS_DEV_PARENT_OFFSET,
        (uint32_t)(uintptr_t)dev);
    drv_write_u32(mmc, MMC_HOST_PRIVATE_BACKPTR_OFFSET,
        (uint32_t)(uintptr_t)mmc);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    irq = platform_get_irq(pdev, 0);
    HC_SRC_LOG("probe.resource", irq, res, pdev, 0);
    if (!res || irq < 0) {
        ret = -EINVAL;
        goto fail_host;
    }
    host->resource = res;
    host->irq = (uint32_t)irq;
    host->iobase = devm_ioremap_resource(dev, res);
    HC_SRC_LOG("probe.ioremap", 0, host->iobase, res, 0);
    if (!host->iobase || hc_src_ptr_is_err((const void *)host->iobase)) {
        ret = host->iobase ? hc_src_ptr_err((const void *)host->iobase) :
            -ENOMEM;
        goto fail_host;
    }
    host->virt_buf = dma_alloc_coherent(dev, HC_MMC_DMA_BYTES, &dma_phys, 0);
    if (!host->virt_buf) {
        ret = -ENOMEM;
        goto fail_host;
    }
    host->phys_buf = dma_phys;
    HC_SRC_LOG("probe.dma", 0, host->virt_buf, host->phys_buf,
        HC_MMC_DMA_BYTES);

    pdata = dev->platform_data;
    if (!pdata) {
        pdata = hc_src_parse_pdata(node);
        pdata_allocated = 1;
    }
    if (!pdata) {
        ret = -ENOMEM;
        goto fail_dma;
    }
    host->pdata = pdata;
    HC_SRC_LOG("probe.pdata", pdata_allocated, pdata, pdata->flags,
        pdata->max_frequency);

    drv_write_u32(mmc, MMC_HOST_OPS_OFFSET, (uint32_t)(uintptr_t)
        &hc_src_mmc_ops);
    if (fdt_get_property_u_32_array(node, "clock-freq-min-max",
        clock_range, 2) == 0) {
        drv_write_u32(mmc, MMC_HOST_F_MIN_OFFSET, clock_range[0]);
        drv_write_u32(mmc, MMC_HOST_F_MAX_OFFSET, clock_range[1]);
    } else {
        drv_write_u32(mmc, MMC_HOST_F_MIN_OFFSET, 100000u);
        drv_write_u32(mmc, MMC_HOST_F_MAX_OFFSET, 198000000u);
    }
    ret = mmc_of_parse(mmc);
    HC_SRC_LOG("probe.of_parse", ret, mmc,
        drv_read_u32(mmc, MMC_HOST_F_MAX_OFFSET),
        drv_read_u32(mmc, MMC_HOST_CAPS_OFFSET));
    /* Do not register a partially configured host from a malformed DT node. */
    if (ret != 0)
        goto fail_pdata;
    if (pdata->caps)
        drv_write_u32(mmc, MMC_HOST_CAPS_OFFSET, pdata->caps);
    if (drv_read_u32(mmc, MMC_HOST_OCR_AVAIL_OFFSET) == 0)
        drv_write_u32(mmc, MMC_HOST_OCR_AVAIL_OFFSET,
            MMC_HOST_DEFAULT_OCR_AVAIL);
    if (drv_read_u32(mmc, MMC_HOST_F_MAX_OFFSET) == 0)
        drv_write_u32(mmc, MMC_HOST_F_MAX_OFFSET, 198000000u);
    drv_write_u32(mmc, MMC_HOST_MAX_BLK_COUNT_OFFSET, HC_MMC_MAX_BLOCKS);
    drv_write_u32(mmc, MMC_HOST_MAX_BLK_SIZE_OFFSET, HC_MMC_MAX_BLOCK_SIZE);
    drv_write_u32(mmc, MMC_HOST_MAX_REQ_SIZE_OFFSET, HC_MMC_DMA_BYTES);
    drv_write_u32(mmc, MMC_HOST_MAX_SEG_SIZE_OFFSET, 4096u);
    drv_write_u16(mmc, MMC_HOST_MAX_SEGS_OFFSET, HC_MMC_MAX_SEGS);
    HC_SRC_LOG("probe.limits", 0,
        drv_read_u32(mmc, MMC_HOST_OCR_AVAIL_OFFSET),
        drv_read_u32(mmc, MMC_HOST_MAX_SEG_SIZE_OFFSET),
        drv_read_u32(mmc, MMC_HOST_MAX_SEGS_OFFSET));

    ret = hc_src_init_request_engine(host);
    HC_SRC_LOG("probe.worker", ret, hc_src_cmdwork(host),
        hc_src_datawork(host), hc_src_rto_timer(host));
    if (ret != 0)
        goto fail_pdata;
    engine_initialized = 1;
    /*
     * Publish initialized timer/work/runtime state before the IRQ can observe
     * the host. This closes the vendor ordering window where an early IRQ
     * could queue an uninitialized work item.
     */
    ret = request_threaded_irq((unsigned int)irq, hc_src_irq_handler, NULL,
        0, "hc-mmc", host);
    HC_SRC_LOG("probe.irq", ret, irq, host, 0);
    if (ret != 0)
        goto fail_irq;
    irq_registered = 1;

    if (host->ops->mmc_clock_gate)
        host->ops->mmc_clock_gate(host, 0);
    HC_SRC_LOG("probe.clock_gate", 0, host, host->ops, 0);
    pins = fdt_get_property_pinmux(node, "active");
    if (pins) {
        pinmux_select_setting(pins);
        free(pins);
    }
    HC_SRC_LOG("probe.pinmux", 0, pins, node, 0);
    if (host->ops->mmc_ip_reset)
        host->ops->mmc_ip_reset();
    HC_SRC_LOG("probe.ip_reset", 0, host, host->ops, 0);

    pdev->dev.driver_data = mmc;
    HC_SRC_LOG("probe.add_host.begin", 0, mmc, host, pdev);
    ret = mmc_add_host(mmc);
    HC_SRC_LOG("probe.add_host.done", ret, mmc, host, pdev);
    if (ret != 0)
        goto fail_irq;

    (void)irq;
    unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_PROBE_DONE,
        0, (uint32_t)(uintptr_t)mmc, (uint32_t)(uintptr_t)host);
    hc_src_activity(HC_SRC_ACTIVITY_PROBE_DONE, NULL, 0);
    return 0;

fail_irq:
    pdev->dev.driver_data = NULL;
    if (engine_initialized) {
        hc_src_runtime.shutting_down = 1u;
        if (host->ops->enable_irq)
            host->ops->enable_irq(host, NULL, 0);
        if (host->ops->enable_sdio_irq)
            host->ops->enable_sdio_irq(host, 0);
        hc_src_cancel_timer_and_wait(hc_src_rto_timer(host));
        hc_src_cancel_timer_and_wait(hc_src_sdio_timer(host));
    }
    if (irq_registered)
        free_irq((unsigned int)irq, host);
    if (engine_initialized) {
        /*
         * Disconnect IRQ before cancelling work so an in-flight interrupt
         * cannot enqueue a completion after the cancellation barrier.
         */
        if (irq_registered)
            hc_src_wait_irq_idle();
        hc_src_cancel_work_and_wait(hc_src_cmdwork(host));
        hc_src_cancel_work_and_wait(hc_src_datawork(host));
        hc_src_wait_callback_epilogues();
        if (host->req) {
            void *failed_data = (void *)(uintptr_t)drv_read_u32(host->req,
                MMC_REQUEST_DATA_OFFSET);

            /*
             * mmc_add_host() normally fails before requests start. If it
             * leaves one owned, halt DMA and satisfy the core before freeing.
             */
            hc_src_reset_after_error(host);
            hc_src_set_cmd_error((void *)(uintptr_t)drv_read_u32(host->req,
                MMC_REQUEST_CMD_OFFSET), ret);
            hc_src_abort_data(host, failed_data, ret);
            hc_src_request_done(host);
        }
        hc_src_runtime.host = NULL;
    }
fail_pdata:
    if (pdata_allocated)
        free(pdata);
fail_dma:
    dma_free_coherent(dev, HC_MMC_DMA_BYTES, host->virt_buf, dma_phys);
fail_host:
    mmc_free_host(mmc);
    unifrog_boot_trace_mark(FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_PROBE_DONE,
        (uint32_t)ret, 0, 0);
    hc_src_activity(HC_SRC_ACTIVITY_PROBE_DONE, NULL, (uint32_t)ret);
    HC_SRC_LOG("probe.fail", ret, mmc, host, 0);
    return ret;
}

/*
 * Remove ordering prevents new generic-core requests before IRQ/DMA/private
 * storage are released. Platform-owned pdata is not freed by this driver.
 */
static int hc_src_remove(struct platform_device *pdev)
{
    void *mmc = pdev->dev.driver_data;
    struct hc_mmc_host *host;
    void *data;

    if (!mmc)
        return 0;
    host = drv_host(mmc);
    hc_src_runtime.shutting_down = 1u;
    pdev->dev.driver_data = NULL;
    mmc_remove_host(mmc);
    if (host->ops->enable_irq)
        host->ops->enable_irq(host, NULL, 0);
    if (host->ops->enable_sdio_irq)
        host->ops->enable_sdio_irq(host, 0);
    hc_src_cancel_timer_and_wait(hc_src_rto_timer(host));
    hc_src_cancel_timer_and_wait(hc_src_sdio_timer(host));
    free_irq(host->irq, host);
    /*
     * IRQ is disconnected and timers are cancelled before waiting for queued
     * workers, so no callback can be added after the cancellation barrier.
     */
    hc_src_wait_irq_idle();
    hc_src_cancel_work_and_wait(hc_src_cmdwork(host));
    hc_src_cancel_work_and_wait(hc_src_datawork(host));
    hc_src_wait_callback_epilogues();
    data = host->req ? (void *)(uintptr_t)drv_read_u32(host->req,
        MMC_REQUEST_DATA_OFFSET) : NULL;
    if (host->req) {
        /*
         * mmc_remove_host() normally drains requests. If a failed request is
         * still owned here, stop controller DMA before releasing its mapping.
         */
        hc_src_reset_after_error(host);
        hc_src_set_cmd_error((void *)(uintptr_t)drv_read_u32(host->req,
            MMC_REQUEST_CMD_OFFSET), -ESHUTDOWN);
        hc_src_abort_data(host, data, -ESHUTDOWN);
        hc_src_request_done(host);
    }
    if (host->virt_buf)
        dma_free_coherent(&pdev->dev, HC_MMC_DMA_BYTES, host->virt_buf,
            host->phys_buf);
    if (host->pdata && host->pdata != pdev->dev.platform_data)
        free(host->pdata);
    mmc_free_host(mmc);
    hc_src_runtime.host = NULL;
    return 0;
}

static const struct of_device_id hc_src_dt_ids[] = {
    { .compatible = "hichip,mmc" },
    { }
};

static struct platform_driver hc_src_driver = {
    .probe = hc_src_probe,
    .remove = hc_src_remove,
    .driver = {
        .name = "hc-mmc",
        .of_match_table = hc_src_dt_ids,
    },
};

/*
 * Source-default board bootstrap creates the DT-backed platform device
 * explicitly. HCRTOS error pointers are small negative values cast to pointer;
 * valid node/device pointers compare below that encoded error range.
 */
static int hc_src_device_register_once(void)
{
    static int registered;
    void *node = fdt_node_probe_by_path("/hcrtos/mmc");
    int ret = 0;

    if (registered)
        return 0;

    hc_src_activity_raw(HC_SRC_ACTIVITY_DEVICE_REGISTER_BEGIN,
        (uint32_t)(uintptr_t)node, 0);
    HC_SRC_LOG("device_register.enter", 0, node, 0, 0);
    if (!node) {
        ret = -ENODEV;
    } else if (hc_src_ptr_is_err(node)) {
        ret = hc_src_ptr_err(node);
    } else {
        struct platform_device_info info;
        void *pdev;

        memset(&info, 0, sizeof(info));
        info.name = "hc-mmc";
        info.id = -1;
        unifrog_boot_trace_mark(
            FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_DEVICE_REGISTER_BEGIN,
            (uint32_t)(uintptr_t)node, 0, 0);
        pdev = of_platform_device_register_full(&info, node);
        HC_SRC_LOG("device_register.full", 0, pdev, node, info.name);
        if (!pdev) {
            ret = -ENODEV;
        } else if (!hc_src_ptr_is_err(pdev)) {
            registered = 1;
            ret = 0;
        } else {
            ret = hc_src_ptr_err(pdev);
        }
        unifrog_boot_trace_mark(
            FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_DEVICE_REGISTER_DONE,
            (uint32_t)ret, (uint32_t)(uintptr_t)node,
            (uint32_t)(uintptr_t)pdev);
    }
    hc_src_activity_raw(HC_SRC_ACTIVITY_DEVICE_REGISTER_DONE,
        (uint32_t)ret, (uint32_t)(uintptr_t)node);
    HC_SRC_LOG("device_register.exit", ret, node, registered, 0);
    return ret;
}

/* Idempotently publish the source platform driver to the HCRTOS driver core. */
static int hc_src_driver_register_once(void)
{
    static int registered;
    int ret;

    if (registered)
        return 0;
    hc_src_activity_raw(HC_SRC_ACTIVITY_DRIVER_REGISTER_BEGIN, 0, 0);
    HC_SRC_LOG("driver_register.enter", 0, &hc_src_driver, 0, 0);
    ret = platform_driver_register(&hc_src_driver);
    if (ret == 0)
        registered = 1;
    unifrog_boot_trace_mark(
        FASTBOOT_TRACE_UNIFROG_SOURCE_MMC_DRIVER_REGISTER_DONE,
        (uint32_t)ret, 0, 0);
    hc_src_activity_raw(HC_SRC_ACTIVITY_DRIVER_REGISTER_DONE,
        (uint32_t)ret, 0);
    HC_SRC_LOG("driver_register.exit", ret, registered, &hc_src_driver, 0);
    return ret;
}
