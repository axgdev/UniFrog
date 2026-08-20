/*
 * HC15xx MMC host controller driver support.
 *
 * This is the ABI boundary shared by the source HC15xx host driver, the
 * historical libmmchosthc15.a implementation, and the runtime diagnostic
 * wrapper.  The layouts, offsets, callback order, and register behavior below
 * were recovered from the vendor archive and linked firmware, then checked
 * against hardware.
 *
 * The source replacement owns the HC15xx platform driver, probe/remove path,
 * request engine, interrupt/timer/work handling, DMA/PIO setup, and low-level
 * controller operations.  It deliberately continues to use the separately
 * vendored libmmc.a core for card enumeration, MMC protocol policy, request
 * construction, block devices, queues, and filesystem integration.  Pointers
 * typed as void below cross that vendor-core ABI; they are not arbitrary
 * source-owned objects.
 *
 * The historical vendor archive exports hc_mmc_clock_gate, hc_mmc_ip_reset,
 * and hc_mmc_host_hw_ops. UniFrog can provide those exports from source while
 * retaining a distinct source-owned ops table for diagnostics and, in vendor
 * builds, runtime swapping.  Consequently this header must remain binary
 * compatible with the vendor library even after source-only builds become the
 * normal configuration.
 */
#ifndef UNIFROG_HC_MMC_HOST_H
#define UNIFROG_HC_MMC_HOST_H

#include <stdint.h>

/*
 * HC15xx SD/MMC controller register offsets.
 *
 * iobase is byte-addressed.  Access width is part of the hardware contract:
 * byte registers must not be widened, while the explicitly noted little-
 * endian 16- and 32-bit registers are accessed at their listed base offsets.
 * Some offsets are multiplexed by access width or controller phase.  In
 * particular, 0x0c is used as a 16-bit PIO FIFO by the observed PIO path, and
 * 0x30 is written as a 32-bit DMA-start register but read and acknowledged as
 * an 8-bit interrupt-status register.  The historical names are preserved
 * because source and vendor diagnostics share them.
 *
 * Other observed registers intentionally not exposed as macros here include
 * byte 0x00 (command/response control and command-start bit 0), byte 0x0d
 * (response control alias), and byte 0x50 (timing-mode selector).
 */
#define HC_MMC_REG_CMD_STATUS    0x01  /* R8: raw command/data status */
#define HC_MMC_REG_IRQ_CONFIG    0x02  /* R/W8: opcode, command IRQ, PIO width */
#define HC_MMC_REG_CLKDIV_LO     0x03  /* R/W8: clock-divider low byte */
#define HC_MMC_REG_CMD_ARG       0x04  /* W32 LE: command argument */
#define HC_MMC_REG_BLOCK_SIZE    0x08  /* W16 LE: bytes per block */
#define HC_MMC_REG_BLOCK_COUNT   0x0a  /* W8: encoded block count low byte */
#define HC_MMC_REG_BUS_WIDTH     0x0b  /* R/W8: bus width and force-clock bit */
#define HC_MMC_REG_CONTROL       0x0c  /* R/W16: observed PIO data FIFO alias */
#define HC_MMC_REG_PIO_CTRL      0x0e  /* R/W8 or R16: PIO ready/control/reset */
#define HC_MMC_REG_SDIO_IRQ_EN   0x0f  /* R/W8: SDIO interrupt enable */
#define HC_MMC_REG_RESPONSE      0x10  /* R32 LE x 4: response words */
#define HC_MMC_REG_DMA_RD_ADDR   0x20  /* W32 LE: device-to-memory DMA address */
#define HC_MMC_REG_DMA_WR_ADDR   0x24  /* W32 LE: memory-to-device DMA address */
#define HC_MMC_REG_DMA_RD_BCNT   0x28  /* W32 LE: device-to-memory byte count */
#define HC_MMC_REG_DMA_WR_BCNT   0x2c  /* W32 LE: memory-to-device byte count */
#define HC_MMC_REG_DMA_START     0x30  /* W32 DMA start; R/W8 IRQ status/ack */
#define HC_MMC_REG_CLKDIV_HI     0x34  /* R/W8: clock-divider high byte */
#define HC_MMC_REG_BLOCK_COUNT_HI 0x36 /* W8: encoded block count high byte */

/*
 * Raw command/status bits read from byte register 0x01.
 *
 * These values describe controller state after a command.  They are distinct
 * from HC_MMC_IRQ_* below: src_get_cmd_status() translates this raw byte into
 * the compact vendor callback result, while get_and_clear_irq() separately
 * reports which interrupt completed and acknowledges it at register 0x30.
 * Bit 0x20 is also observed and translated by the source implementation, but
 * its hardware meaning is not yet named with sufficient confidence.
 */
#define HC_MMC_STATUS_CMD_DONE     0x01 /* Command phase completed. */
#define HC_MMC_STATUS_CMD_CRC_ERR  0x02 /* Command/response CRC failed. */
#define HC_MMC_STATUS_CMD_TIMEOUT  0x04 /* Command/response timed out. */
#define HC_MMC_STATUS_DATA_CRC_ERR 0x08 /* Data-phase CRC failed. */
#define HC_MMC_STATUS_DATA_TIMEOUT 0x10 /* Data phase timed out. */
#define HC_MMC_STATUS_DATA_DONE    0x40 /* Data phase completed. */
#define HC_MMC_STATUS_ERROR        0x80 /* Raw aggregate/error indication. */

/*
 * Bus-width register bits (byte register 0x0b).
 *
 * The libmmc core passes its encoded ios.bus_width value to set_bus_width();
 * the hardware callback translates that encoding before writing these bits.
 * FORCE_CLK is independently toggled around data/request completion.
 */
#define HC_MMC_BUS_WIDTH_FORCE_CLK 0x01 /* Keep controller clock forced. */
#define HC_MMC_BUS_WIDTH_1BIT      0x04 /* Controller encoding for 1-bit bus. */
#define HC_MMC_BUS_WIDTH_4BIT      0x08 /* Controller encoding for 4-bit bus. */
#define HC_MMC_BUS_WIDTH_MASK      0x0e /* All observed bus-width selector bits. */

/*
 * IRQ/config register bits (byte register 0x02).  The low bits also carry the
 * command opcode, so writers preserve or rebuild the complete register.
 */
#define HC_MMC_IRQ_CONFIG_CMD_IRQ_EN  0x40 /* Enable command-completion IRQ. */
#define HC_MMC_IRQ_CONFIG_PIO_16BIT   0x80 /* Select observed 16-bit PIO mode. */

/*
 * PIO control bits at byte register 0x0e.  The PIO transfer loop additionally
 * reads a 16-bit value at this offset and treats bit 1 as FIFO-ready.
 */
#define HC_MMC_PIO_CTRL_RESET     0x01 /* Pulse during PIO cleanup/reset. */
#define HC_MMC_PIO_CTRL_FIFO_CLR  0x04 /* Select FIFO clear/setup state. */

/* SDIO-function interrupt gate written to byte register 0x0f. */
#define HC_MMC_SDIO_IRQ_EN 0x04 /* Entire value written when SDIO IRQ is on. */

/*
 * Values written as a 32-bit word to register 0x30 after programming the
 * corresponding address/count pair.  The names reflect controller transfer
 * direction, not the generic DMA API's DMA_TO_DEVICE/DMA_FROM_DEVICE values.
 */
#define HC_MMC_DMA_START_READ  0x01 /* Start device-to-memory path. */
#define HC_MMC_DMA_START_WRITE 0x02 /* Start memory-to-device path. */
#define HC_MMC_DMA_START_BOTH  0x03 /* Vendor write-path start value. */

/*
 * Normalized interrupt result returned by hc_mmc_host_hw_ops.get_and_clear_irq.
 *
 * These are software ABI values, not the raw bits read from register 0x30.
 * The callback derives CMD_DONE versus DATA_DONE from raw register 0x30 bit 6
 * and controller byte 0x00 bit 3, acknowledges the raw status by writing it
 * back, and maps raw bit 7 to HC_MMC_IRQ_ERROR.  Value 0x04 is also consumed
 * by the request engine as the SDIO indication; the shared value is inherited
 * from the vendor contract.
 */
#define HC_MMC_IRQ_CMD_DONE   0x01 /* Queue command-completion work. */
#define HC_MMC_IRQ_DATA_DONE  0x02 /* Queue data-completion work. */
#define HC_MMC_IRQ_ERROR      0x04 /* Raw error mapping; also consumed as SDIO. */

/*
 * Historical command-control start bit.  The observed implementation starts
 * a command by setting bit 0 in byte register 0x00; offset 0x0c is used by
 * PIO.  Keep this definition only for ABI/source-history compatibility until
 * the remaining register aliases are conclusively named.
 */
#define HC_MMC_CONTROL_START 0x01 /* Observed command-start bit value. */

/*
 * HC15xx SoC-level SDIO clock/reset controls, outside the controller MMIO
 * window.  Accesses are volatile 32-bit operations on fixed uncached physical
 * addresses.  The source implementation reproduces the vendor transitions:
 * clock_gate() conditionally changes gate0 bit 1 and always asserts the two
 * auxiliary gate2 bits; ip_reset() pulses reset-gate1 bit 18 with 5 us delays.
 * The user-visible meaning/polarity of the clock_gate() enable argument is
 * inherited from the vendor callback and should not be inferred from its name.
 */
#define SD_SOC_CLOCK_GATE0_REG 0xb8800060u
#define SD_SOC_CLOCK_GATE2_REG 0xb8800094u
#define SD_SOC_RESET_GATE1_REG 0xb8800084u
#define SD_SOC_SDIO_CLOCK_GATE0_BIT  (1u << 1)  /* Conditional primary gate bit. */
#define SD_SOC_SDIO_CLOCK_GATE2_BITS (3u << 18) /* Always-set auxiliary gate bits. */
#define SD_SOC_SDIO_RESET_GATE1_BIT  (1u << 18) /* Pulsed SDIO IP reset bit. */

/*
 * Fallback controller source-clock rate used only when libmmc's mmc_host
 * source-clock field is zero.  This is not the requested or achieved SD clock.
 */
#define HC_MMC_DEFAULT_SRC_CLOCK 200000000u

/*
 * Platform-data ABI consumed by the HC15xx probe and request callbacks.
 *
 * The 44-byte layout and offsets are vendor ABI and must not change.  A
 * platform_device may own and supply this object for the controller lifetime;
 * otherwise the source probe allocates it from selected device-tree
 * properties and frees it on remove/failure.  Several slots are retained for
 * vendor compatibility even though the current source probe does not populate
 * or consume all of them.  Their comments distinguish observed use from
 * inferred naming.
 */
struct hc_mmc_platform_data {
    uint32_t bus_width;               /* +0x00: vendor slot; source parser stores num-slots/default 1 */
    uint32_t flags;                   /* +0x04: host quirks; bit 3 forces card-present */
    uint32_t bus_hz;                  /* +0x08: clock-frequency fallback used by set_clock */
    uint32_t card_detect_delay;       /* +0x0c: card-detect-delay; retained vendor ABI slot */
    uint32_t max_frequency;           /* +0x10: vendor maximum-frequency slot; source currently unused */
    uint32_t caps;                    /* +0x14: replacement mmc_host capability mask when nonzero */
    uint32_t cd_gpios;                /* +0x18: vendor card-detect GPIO descriptor/index slot */
    uint32_t cd_inverted;             /* +0x1c: vendor card-detect polarity boolean slot */
    uint32_t wp_gpios;                /* +0x20: write-protect GPIO index; values below 128 enable GPIO query */
    uint32_t wp_inverted;             /* +0x24: vendor write-protect polarity boolean slot */
    uint32_t pin_group;               /* +0x28: vendor pin-group selector slot; source currently unused */
};

/* flags bit that bypasses GPIO card detection and always reports present. */
#define HC_MMC_PDATA_FLAG_BROKEN_CD (1u << 3)

/*
 * Hardware-operations ABI between the request engine and HC15xx register code.
 *
 * This is a 19-entry/76-byte MIPS32 function-pointer table.  Entry order and
 * signatures are vendor ABI and must remain stable.  The active host stores
 * one table pointer in hc_mmc_host.ops; runtime swapping replaces that pointer
 * only after storage quiescing.  The diagnostic table has the same ABI and
 * delegates each operation to either the source or saved vendor table while
 * recording arguments, results, and register snapshots.
 *
 * cmd and data are vendor-libmmc struct mmc_command/mmc_data pointers.
 * Direction passed to set_dma is the host callback convention (1 = write to
 * card, 2 = read from card), not the generic dma_map_sg direction constants.
 */
struct hc_mmc_host;

struct hc_mmc_host_hw_ops {
    /* Program the divider for requested card clock hz and publish actual hz. */
    void (*set_clock)(struct hc_mmc_host *host, unsigned int hz);
    /* Translate libmmc's encoded ios bus width into controller bits. */
    void (*set_bus_width)(struct hc_mmc_host *host, unsigned char width);
    /* Translate libmmc's ios timing mode into the controller timing selector. */
    void (*set_timing)(struct hc_mmc_host *host, unsigned char timing);
    /* Toggle the controller force-clock bit; polarity follows vendor ABI. */
    void (*enable_force_clock)(struct hc_mmc_host *host, int enable);
    /* Encode opcode, argument, response type, and optional data direction. */
    void (*set_cmd)(struct hc_mmc_host *host, void *cmd, void *data);
    /* Trigger the command already programmed by set_cmd. */
    void (*start_cmd)(struct hc_mmc_host *host);
    /* Return vendor-normalized command/data status, not HC_MMC_IRQ_* values. */
    uint32_t (*get_cmd_status)(struct hc_mmc_host *host);
    /* Decode controller response words into the vendor mmc_command response. */
    void (*get_response)(struct hc_mmc_host *host, void *cmd);
    /* Program block size and the controller's zero-based block count. */
    void (*set_block)(struct hc_mmc_host *host, unsigned int nblk, unsigned int blksz);
    /* Program one physical DMA span and start the requested transfer direction. */
    void (*set_dma)(struct hc_mmc_host *host, uint32_t addr, uint32_t len, int dir);
    /* Select PIO width from mmc_data flags and clear/reset the PIO FIFO. */
    void (*set_pio)(struct hc_mmc_host *host, uint32_t flags);
    /* Synchronously move one source-owned PIO write request through the FIFO. */
    void (*pio_write)(struct hc_mmc_host *host, void *data);
    /* Synchronously move one source-owned PIO read request through the FIFO. */
    void (*pio_read)(struct hc_mmc_host *host, void *data);
    /* Leave PIO mode and pulse the controller's PIO reset sequence. */
    void (*pio_cleanup)(struct hc_mmc_host *host);
    /* Enable/disable command completion IRQ generation; data is ABI context. */
    void (*enable_irq)(struct hc_mmc_host *host, void *data, int enable);
    /* Enable/disable SDIO-function interrupts in the controller. */
    void (*enable_sdio_irq)(struct hc_mmc_host *host, int enable);
    /* Read, acknowledge, and normalize controller interrupt status. */
    uint32_t (*get_and_clear_irq)(struct hc_mmc_host *host);
    /* Pulse the SoC-level SDIO IP reset; no host object is required. */
    void (*mmc_ip_reset)(void);
    /* Apply the vendor SoC clock-gate transition; host is currently ignored. */
    void (*mmc_clock_gate)(void *host, int enable);
};

/*
 * Per-controller private state shared by source and vendor host drivers.
 *
 * mmc_alloc_host(288, dev) owns the containing allocation for the entire
 * registered-host lifetime, and probe zero-initializes this private area.
 * This struct starts at private offset +0x240 from the vendor libmmc struct
 * mmc_host; conversely, the mmc_host pointer is obtained by subtracting 0x240.
 * Probe initializes persistent resources and callback state, the serialized
 * request engine owns transient request fields, IRQ/timer/work callbacks
 * consume them, and remove releases the allocation after unregistering the
 * host and IRQ.
 *
 * All field offsets and the total 288-byte size are enforced by static asserts
 * in hc_mmc_driver.c and verified against vendor disassembly.  Opaque byte
 * arrays embed vendor kernel timer/work objects in-place; their sizes and
 * internal offsets are ABI, not spare storage.
 */
struct hc_mmc_host {
    /* +0x00 */ void *resource;         /* Probe-owned MMIO resource descriptor; valid until device removal. */
    /* +0x04 */ volatile uint8_t *iobase; /* Probe-mapped byte-addressed controller MMIO base. */
    /* +0x08 */ uint32_t irq;           /* Platform IRQ number requested at probe and freed at remove. */
    /* +0x0C */ void *dev;              /* Non-owning back-pointer to the platform struct device. */
    /* +0x10 */ struct hc_mmc_platform_data *pdata; /* Platform-owned or source-probe-owned configuration. */
    /* +0x14 */ const struct hc_mmc_host_hw_ops *ops; /* Active vendor/source/diagnostic hardware table; runtime-swappable. */
    /* +0x18 */ void *mmc;              /* Stable non-owning back-pointer to containing vendor struct mmc_host. */
    /* +0x1C */ void *req;              /* Active vendor struct mmc_request; owned by libmmc until request_done. */
    /* +0x20 */ void *cmd;              /* Currently executing SBC/main/STOP command within req; cleared at request_done. */
    /* +0x24 */ uint8_t use_pio;        /* Nonzero selects synchronous PIO lifecycle instead of DMA; cleared by cleanup. */
    /* +0x25 */ uint8_t sdio_irq_enabled; /* Software shadow controlling SDIO polling timer and signal delivery. */
    /* +0x26 */ uint8_t data_transferring; /* Set while a data command is active; suppresses SDIO polling checks. */
    /* +0x27 */ uint8_t pad_27;         /* Vendor ABI padding; no known state or ownership. */
    /* +0x28 */ uint32_t cmd25_done_delay_ms; /* Vendor ABI CMD25 completion-delay setting; source currently leaves zero. */
    /* +0x2C */ void *virt_buf;         /* Probe-owned 1 MiB coherent bounce-buffer virtual address. */
    /* +0x30 */ uint32_t phys_buf;      /* DMA-visible physical address paired with virt_buf. */
    /* +0x34 */ uint32_t data_size;     /* Active data request size in bytes; valid from prepare through finish/abort. */
    /* +0x38 */ uint32_t dma_dir;       /* Active generic DMA mapping direction; zero means no prepared DMA state. */
    /* +0x3C */ int sg_len;             /* Active mapped SG count; -1 means bounce buffer, 0 means none/cleared. */
    /* +0x40 */ void *clk;              /* Vendor ABI clock-handle slot; source clock programming does not use it. */
    /* +0x44 */ uint32_t clock;         /* Last requested ios clock, retained so error reset can restore hardware. */
    /* +0x48 */ uint8_t bus_width;      /* Last encoded ios bus width and vendor set_cmd mode input at the same ABI byte. */
    /* +0x49 */ uint8_t timing;         /* Last encoded ios timing, retained so error reset can restore hardware. */
    /* +0x4A */ uint16_t pad_4a;        /* Vendor ABI alignment padding before embedded timer object. */
    /* +0x4C */ uint8_t rto_timer[56];  /* Embedded request-timeout timer; request engine initializes and owns it. */
    /* +0x84 */ uint8_t sdio_timer[56]; /* Embedded SDIO polling timer; armed only while SDIO IRQ is enabled. */
    /* +0xBC */ uint8_t cmdwork[48];    /* Embedded deferred command-completion work item queued by IRQ handler. */
    /* +0xEC */ uint8_t datawork[48];   /* Embedded deferred data-completion work item queued by IRQ handler. */
    /* +0x11C */ uint32_t pending_events; /* Atomic event bits shared by command start, IRQ, timer, and workers; bit 0 = completion pending. */
};

/*
 * Vendor-compatible exports consumed by platform code and historical archive
 * references.  In source-default builds these names resolve to source
 * implementations; in vendor-default builds the archive may own them.
 */
/* Hardware table selected by the live probe path at link/build time. */
extern const struct hc_mmc_host_hw_ops hc_mmc_host_hw_ops;
/* Vendor-name wrapper for the SoC SDIO clock-gate callback. */
extern void hc_mmc_clock_gate(void *host, int enable);
/* Vendor-name wrapper for the SoC SDIO reset pulse. */
extern void hc_mmc_ip_reset(void);

/*
 * Source and diagnostic control interfaces.
 *
 * These names remain distinct from vendor exports so a vendor-default build
 * can keep both implementations linked and move the live host ops pointer
 * explicitly.  They manipulate ops-table identity and the low-level diagnostic
 * wrapper only; they do not replace the libmmc core or transfer ownership of a
 * live request.  Callers must quiesce storage before swapping the host table.
 */
/* Remember the vendor table so runtime swap/diagnostics can return to it. */
extern void src_hc_mmc_host_save_vendor(const struct hc_mmc_host_hw_ops *ops);
/* Return the immutable source hardware-ops table. */
extern const struct hc_mmc_host_hw_ops *src_hc_mmc_host_hw_ops_ptr(void);
/* Recognize exported source, active, initialized diagnostic, or saved vendor tables. */
extern int src_hc_mmc_host_ops_known(const struct hc_mmc_host_hw_ops *ops);
/* Initialize/rebase the diagnostic delegate table and label captured base kind. */
extern void src_hc_mmc_host_diag_init(const struct hc_mmc_host_hw_ops *base,
                                      int base_is_source);
/* Return the diagnostic wrapper table initialized by diag_init(). */
extern const struct hc_mmc_host_hw_ops *src_hc_mmc_host_diag_ops_ptr(void);
/* Log up to 256 retained entries from the 1024-entry hardware-op trace ring. */
extern void src_hc_mmc_host_diag_dump(void);
/* Empty the diagnostic trace ring without changing its selected delegate. */
extern void src_hc_mmc_host_diag_reset(void);

#endif /* UNIFROG_HC_MMC_HOST_H */
