/*
 * HC15xx MMC host controller driver - source-owned swap target.
 *
 * This file implements the HC15xx hardware ops table from
 * libmmchosthc15.a. Vendor builds keep it as a separate swap target; source
 * builds also export the historical vendor symbol names required by the
 * source request/probe driver and other platform integration.
 *
 * The implementation below is based on the vendor archive disassembly. The
 * intent is fidelity first: keep the observed register contract coherent, then
 * iterate on behavior with hardware diagnostics.
 *
 * The controller exposes a small byte-addressed register window.  Several
 * locations are overloaded by access width or transfer phase:
 *
 *   0x00      command/response control; bit 0 starts a programmed command
 *   0x01      raw command/data completion and error status
 *   0x02      opcode plus command IRQ and PIO-direction control
 *   0x03/0x34 low/high bytes of the 16-bit clock divider
 *   0x04      32-bit command argument
 *   0x08      16-bit block size
 *   0x0a/0x36 low/high bytes of block-count-minus-one
 *   0x0b      bus-width encoding plus forced-clock bit
 *   0x0c      16-bit PIO data port
 *   0x0e      PIO status/control
 *   0x0f      SDIO interrupt enable
 *   0x10..1c  four 32-bit response words
 *   0x20..2c  directional DMA address/count pairs
 *   0x30      DMA start/control and write-to-acknowledge IRQ status
 *   0x50      timing-mode encoding
 *
 * Names and interpretations below describe behavior observed in the vendor
 * implementation and on hardware.  They are not based on a public HC15xx
 * datasheet, so comments distinguish exact bit operations from inferred
 * hardware meaning where appropriate.
 */

#include <unifrog/hc_mmc_host.h>

#include <stdint.h>
#include <string.h>

#include <unifrog/log.h>

extern void udelay(unsigned int us);

#define HC_MMC_REG_RESP_CTRL 0x0d

/* The vendor divider calculation uses ceiling division at both stages. */
static inline uint32_t hc_div_round_up(uint32_t num, uint32_t den)
{
    return den ? (num + den - 1u) / den : 0;
}

static inline uint8_t hc_host_cmd_mode(const struct hc_mmc_host *host)
{
    /*
     * The vendor set_cmd path reads host byte +0x48 when deciding whether to
     * set control bit 7. That state is distinct from host->use_pio at +0x24.
     */
    return host ? ((const uint8_t *)host)[0x48] : 0u;
}

static void src_set_bus_width(struct hc_mmc_host *host, unsigned char width)
{
    volatile uint8_t *io = host->iobase;
    /*
     * Preserve forced-clock bit 0 and the upper nibble while replacing width
     * bits 1..3.  The input values are the MMC core's enum encodings:
     * 0 = 1-bit, 2 = 4-bit, and 3 = 8-bit.
     */
    uint8_t val = io[HC_MMC_REG_BUS_WIDTH] & 0xf1u;

    switch (width) {
    case 2:
        val |= 0x08u;
        break;
    case 3:
        val |= 0x02u;
        break;
    case 0:
        val |= 0x04u;
        break;
    default:
        break;
    }

    io[HC_MMC_REG_BUS_WIDTH] = val;
}

static void src_set_timing(struct hc_mmc_host *host, unsigned char timing)
{
    /*
     * Register 0x50 is an inferred timing selector.  This mirrors the vendor
     * jump table exactly: timing values 0, 1, and 2 select 0; values 3 and 4
     * deliberately leave the current register value unchanged; values 5, 6,
     * and 7 select 3, 4, and 5 respectively.  The numeric inputs are preserved
     * here because the linked MMC core's complete timing enum is not in source.
     */
    switch (timing) {
    case 0:
    case 1:
    case 2:
        host->iobase[0x50] = 0;
        break;
    case 5:
        host->iobase[0x50] = 3;
        break;
    case 6:
        host->iobase[0x50] = 4;
        break;
    case 7:
        host->iobase[0x50] = 5;
        break;
    default:
        break;
    }
}

static void src_enable_force_clock(struct hc_mmc_host *host, int enable)
{
    volatile uint8_t *io = host->iobase;
    uint8_t val = io[HC_MMC_REG_BUS_WIDTH] & 0xffu;

    /* Bit 0 gates automatic clock stopping without disturbing bus width. */
    io[HC_MMC_REG_BUS_WIDTH] = enable ? (uint8_t)(val | 0x01u) :
        (uint8_t)(val & 0xfeu);
}

static void src_start_cmd(struct hc_mmc_host *host)
{
    /*
     * Byte 0 of the control register is also where src_set_cmd stores the
     * response-control encoding.  ORing bit 0 starts the prepared command
     * while preserving those response bits.
     */
    host->iobase[0x00] |= 0x01u;
}

static uint32_t src_get_cmd_status(struct hc_mmc_host *host)
{
    uint8_t raw = host->iobase[HC_MMC_REG_CMD_STATUS];
    uint32_t status = raw & HC_MMC_STATUS_CMD_DONE;

    /*
     * The driver layer consumes the vendor host-op status ABI, not the raw
     * register layout.  Repack raw bits 1, 3, 4, 6, 2, 5, and 7 into
     * normalized bits 1..7.  Raw bit 5 has no confirmed semantic name; the
     * vendor still exposes it as normalized bit 6.
     */
    if (raw & HC_MMC_STATUS_CMD_CRC_ERR)
        status |= 0x02u;
    if (raw & HC_MMC_STATUS_DATA_CRC_ERR)
        status |= 0x04u;
    if (raw & HC_MMC_STATUS_DATA_TIMEOUT)
        status |= 0x08u;
    if (raw & HC_MMC_STATUS_DATA_DONE)
        status |= 0x10u;
    if (raw & HC_MMC_STATUS_CMD_TIMEOUT)
        status |= 0x20u;
    if (raw & 0x20u)
        status |= 0x40u;
    if ((int8_t)raw < 0)
        status |= 0x80u;

    return status;
}

static void src_set_block(struct hc_mmc_host *host,
                          unsigned int nblk, unsigned int blksz)
{
    volatile uint8_t *io = host->iobase;
    /* Hardware represents one block as zero, so program nblk - 1. */
    uint16_t count = (uint16_t)(nblk - 1u);

    /*
     * Count is split across non-contiguous byte registers.  Block size is a
     * naturally little-endian 16-bit MMIO write.
     */
    io[HC_MMC_REG_BLOCK_COUNT] = (uint8_t)(count & 0xffu);
    io[HC_MMC_REG_BLOCK_COUNT_HI] = (uint8_t)(count >> 8);
    *(volatile uint16_t *)(io + HC_MMC_REG_BLOCK_SIZE) = (uint16_t)blksz;
}

static void src_set_dma(struct hc_mmc_host *host,
                        uint32_t addr, uint32_t len, int dir)
{
    volatile uint32_t *io = (volatile uint32_t *)host->iobase;

    /*
     * The request layer passes dir == 1 for a host-to-card write and dir != 1
     * for a card-to-host read.  Direction selects a dedicated address/count
     * pair.  The vendor starts writes with control value 3 and reads with 1;
     * the individual meaning of the extra write bit is not yet confirmed.
     */
    if (dir == 1) {
        io[HC_MMC_REG_DMA_WR_ADDR / 4] = addr;
        io[HC_MMC_REG_DMA_WR_BCNT / 4] = len;
        io[HC_MMC_REG_DMA_START / 4] = 3u;
        return;
    }

    io[HC_MMC_REG_DMA_RD_ADDR / 4] = addr;
    io[HC_MMC_REG_DMA_RD_BCNT / 4] = len;
    io[HC_MMC_REG_DMA_START / 4] = 1u;
}

static void src_set_pio(struct hc_mmc_host *host, uint32_t flags)
{
    volatile uint8_t *io = host->iobase;
    /* Preserve opcode/IRQ bits and replace only the PIO direction/width bit. */
    uint8_t reg = io[HC_MMC_REG_IRQ_CONFIG] & 0x7fu;

    /* MMC_DATA_WRITE (0x100) selects bit 7; reads leave it clear. */
    if (flags == 0x100u)
        reg |= 0x80u;

    io[HC_MMC_REG_IRQ_CONFIG] = reg;
    /* Vendor setup writes FIFO-clear/control value 4 before PIO transfer. */
    io[HC_MMC_REG_PIO_CTRL] = 0x04u;
}

static void src_enable_irq(struct hc_mmc_host *host, void *data, int enable)
{
    volatile uint8_t *io = host->iobase;
    uint8_t reg = io[HC_MMC_REG_IRQ_CONFIG] & 0xffu;

    (void)data;
    /* The opaque data argument is part of the vendor ABI but is unused here. */
    io[HC_MMC_REG_IRQ_CONFIG] = enable ? (uint8_t)(reg | 0x40u) :
        (uint8_t)(reg & 0xbfu);
}

static void src_enable_sdio_irq(struct hc_mmc_host *host, int enable)
{
    /* This register is programmed as a whole; no other bits are preserved. */
    host->iobase[HC_MMC_REG_SDIO_IRQ_EN] = enable ? 0x04u : 0x00u;
}

static uint32_t src_get_and_clear_irq(struct hc_mmc_host *host)
{
    volatile uint8_t *io = host->iobase;
    uint8_t raw = io[HC_MMC_REG_DMA_START];
    uint32_t status = 0;

    /*
     * At interrupt time the low byte of 0x30 is status.  Writing the value
     * back acknowledges every asserted bit (write-one-to-clear behavior).
     */
    io[HC_MMC_REG_DMA_START] = raw;

    /*
     * Raw bit 6 signals completion.  Control bit 3 at 0x00 distinguishes a
     * data completion (normalized 2) from command completion (normalized 1).
     * Raw bit 7 independently contributes the normalized error bit.
     */
    if (raw & 0x40u)
        status = (io[0x00] & 0x08u) ? 2u : 1u;
    if ((int8_t)raw < 0)
        status |= HC_MMC_IRQ_ERROR;

    return status;
}

void src_mmc_clock_gate(void *host, int enable)
{
    volatile uint32_t *gate0 = (volatile uint32_t *)(uintptr_t)
        SD_SOC_CLOCK_GATE0_REG;
    volatile uint32_t *gate2 = (volatile uint32_t *)(uintptr_t)
        SD_SOC_CLOCK_GATE2_REG;
    uint32_t reg0 = *gate0;

    (void)host;
    /*
     * The primary gate is reversible.  The two auxiliary SDIO clock bits are
     * always enabled, including on the disable path, matching the vendor.
     * The host argument exists only to match the exported vendor ABI.
     */
    *gate0 = enable ? (reg0 | SD_SOC_SDIO_CLOCK_GATE0_BIT) :
        (reg0 & ~SD_SOC_SDIO_CLOCK_GATE0_BIT);
    *gate2 |= SD_SOC_SDIO_CLOCK_GATE2_BITS;
}

void src_mmc_ip_reset(void)
{
    volatile uint32_t *reset = (volatile uint32_t *)(uintptr_t)
        SD_SOC_RESET_GATE1_REG;
    uint32_t saved = *reset;

    /*
     * Pulse the active-high SDIO reset bit for 5 us, then deassert it for at
     * least another 5 us.  Other reset-gate bits retain their saved values.
     */
    *reset = saved | SD_SOC_SDIO_RESET_GATE1_BIT;
    udelay(5);
    *reset = saved & ~SD_SOC_SDIO_RESET_GATE1_BIT;
    udelay(5);
}

static void src_pio_cleanup(struct hc_mmc_host *host)
{
    volatile uint8_t *io = host->iobase;

    host->use_pio = 0;
    /*
     * Clear the setup value, pulse the PIO reset bit for 1 us, then leave the
     * engine out of reset.  Read-modify-write preserves unclassified bits.
     */
    io[HC_MMC_REG_PIO_CTRL] &= (uint8_t)~HC_MMC_PIO_CTRL_FIFO_CLR;
    io[HC_MMC_REG_PIO_CTRL] |= HC_MMC_PIO_CTRL_RESET;
    udelay(1);
    io[HC_MMC_REG_PIO_CTRL] &= (uint8_t)~HC_MMC_PIO_CTRL_RESET;
}

static void src_set_clock(struct hc_mmc_host *host, unsigned int hz)
{
    volatile uint8_t *io = host->iobase;
    uint8_t *mmc = (uint8_t *)host->mmc;
    uint32_t src = mmc ? *(uint32_t *)(mmc + 0xd0) : 0u;
    uint32_t fallback = host->pdata ? host->pdata->bus_hz : 0u;
    uint32_t divider;
    uint32_t actual;

    /*
     * These raw mmc_host offsets are part of the vendor libmmc ABI:
     * +0xd0 supplies the preferred source-clock value and +0x214 receives the
     * rate that the vendor implementation reports as selected.
     */
    if (hz == 0u) {
        /* Divider zero disables the card clock and reports a zero rate. */
        io[HC_MMC_REG_CLKDIV_LO] = 0;
        io[HC_MMC_REG_CLKDIV_HI] = 0;
        if (mmc)
            *(uint32_t *)(mmc + 0x214) = 0u;
        return;
    }

    if (src == 0u)
        src = HC_MMC_DEFAULT_SRC_CLOCK;
    if (fallback == 0u)
        fallback = src;

    /*
     * For a requested rate below src, the programmed divisor is
     * ceil(src / (2 * hz)) + 1 and the reported result is
     * ceil((src - 1) / (2 * (divider - 1))).  The remaining branches and
     * their reported values intentionally preserve vendor behavior, including
     * the use of pdata->bus_hz only to choose/calculate a divider.
     */
    if (hz < src) {
        divider = hc_div_round_up(src, hz * 2u) + 1u;
        actual = hc_div_round_up(src - 1u, (divider - 1u) * 2u);
    } else if (hz < fallback) {
        divider = hc_div_round_up(fallback, hz * 2u) + 1u;
        actual = src;
    } else {
        divider = 1u;
        actual = src;
    }

    io[HC_MMC_REG_CLKDIV_LO] = (uint8_t)(divider & 0xffu);
    /* The 16-bit divider is split between byte registers 0x03 and 0x34. */
    io[HC_MMC_REG_CLKDIV_HI] = (uint8_t)(divider >> 8);
    if (mmc)
        *(uint32_t *)(mmc + 0x214) = actual;
}

static void src_get_response(struct hc_mmc_host *host, void *cmd)
{
    volatile uint32_t *resp = (volatile uint32_t *)(host->iobase +
        HC_MMC_REG_RESPONSE);
    uint32_t *cmd32 = (uint32_t *)cmd;
    uint32_t flags = cmd32[6];

    /*
     * cmd is the vendor libmmc command object, layout-compatible with the
     * Linux mmc_command prefix: opcode at word 0, argument at word 1,
     * response words at 2..5, and response flags at word 6.  A command
     * without MMC_RSP_PRESENT leaves its response storage untouched.
     */
    if (!(flags & 0x01u))
        return;

    if (flags & 0x02u) {
        /*
         * Long (136-bit/R2) responses arrive in four controller words with
         * controller framing/CRC bytes still affecting alignment.  The
         * assignments below reproduce the vendor's word reversal, 8-bit
         * shift, and CMD9-specific top-word correction.
         */
        uint32_t r0 = resp[0];
        uint32_t r1 = resp[1];
        uint32_t r2 = resp[2];
        uint32_t r3 = resp[3];
        uint32_t opcode = cmd32[0];
        uint32_t top = 0;

        cmd32[5] = r0;
        cmd32[4] = r1;
        cmd32[3] = r2;
        cmd32[2] = r3;

        if (opcode == 9u) {
            /*
             * CMD9 returns the CSD.  The vendor recognizes several CSD
             * structure/signature patterns and synthesizes the two low bits
             * of the corrected top fragment.  The pattern meanings remain
             * inferred; preserve them exactly until validated independently.
             */
            uint32_t csd_structure = (r3 >> 26) & 0x0fu;
            uint32_t sig = (r1 >> 22) & 0xfcu;

            if (csd_structure != 0u) {
                top = sig | 0x03u;
            } else if ((r3 >> 30) == 0u) {
                uint32_t code = ((r2 << 2) | (r1 >> 30)) & 0x0fffu;

                if (code == 0x0f4fu || code == 0x0f33u ||
                    (code & 0x0fefu) == 0x0f03u ||
                    code == 0x0f27u ||
                    (code & 0x0fdfu) == 0x0edfu)
                    top = sig | 0x02u;
                else
                    top = sig | 0x03u;
            }
        }

        cmd32[5] = (r1 << 24) | (r0 >> 8);
        cmd32[4] = (r1 & 0xff000000u) |
            ((r1 >> 8) & 0x0000ffffu) |
            (top << 16);
        return;
    }

    /* Short responses are shifted by one framing byte into response word 0. */
    cmd32[2] = (resp[1] << 24) | (resp[0] >> 8);
}

static void src_set_cmd(struct hc_mmc_host *host, void *cmd, void *data)
{
    volatile uint8_t *io = host->iobase;
    uint32_t *cmd32 = (uint32_t *)cmd;
    uint32_t flags = cmd32[6];
    uint8_t reg2 = (io[HC_MMC_REG_IRQ_CONFIG] & 0x40u) |
        (uint8_t)cmd32[0];
    uint8_t resp_ctrl = hc_host_cmd_mode(host) == 2u ? 0x80u : 0x00u;

    /*
     * Program the 32-bit argument first.  Register 0x02 combines the low
     * opcode byte with command-IRQ bit 6 and a data-direction/control bit 7.
     * Preserve only bit 6 from the previous value; all other bits are rebuilt
     * from this command.
     */
    *(volatile uint32_t *)(io + HC_MMC_REG_CMD_ARG) = cmd32[1];

    /*
     * Clear bit 7 only for MMC_DATA_READ (data flags bit 0x200).  Commands
     * without data and writes set it.  This exact polarity is vendor-observed;
     * its hardware label is not confirmed.
     */
    if (!data || !((((uint32_t *)data)[5]) & 0x200u))
        reg2 |= 0x80u;
    io[HC_MMC_REG_IRQ_CONFIG] = reg2;

    /*
     * Response-control byte 0x00 is assembled from three independent vendor
     * encodings:
     *
     * - host private byte +0x48 == 2 contributes bit 7.  Despite that field's
     *   historical bus_width name, the hardware meaning here is unconfirmed.
     * - flags bits 5..6 select an additional response mode.  For value 0x20,
     *   data flags distinguish read data (0x0a) from all other commands (0x08).
     * - flags bits 0..4 encode native response properties.  Present-only uses
     *   0x30, response type 7 uses 0x20, and types 0x15/0x1d use 0x10 except
     *   CMD3/CMD8, which use 0x40.
     *
     * These are exact vendor flag tests; the controller bit names are not
     * known well enough to replace the literal encodings.
     */
    switch (flags & 0x60u) {
    case 0x00u:
        resp_ctrl |= 0x04u;
        break;
    case 0x20u:
        if (data && ((((uint32_t *)data)[5]) & 0x100u))
            resp_ctrl |= 0x0au;
        else
            resp_ctrl |= 0x08u;
        break;
    case 0x40u:
        break;
    case 0x60u:
        resp_ctrl |= 0x02u;
        break;
    default:
        break;
    }

    switch (flags & 0x1fu) {
    case 0x00u:
        break;
    case 0x01u:
        resp_ctrl |= 0x30u;
        break;
    case 0x07u:
        resp_ctrl |= 0x20u;
        break;
    case 0x15u:
    case 0x1du:
        if (cmd32[0] == 3u || cmd32[0] == 8u)
            resp_ctrl |= 0x40u;
        else
            resp_ctrl |= 0x10u;
        break;
    default:
        break;
    }

    io[0x00] = resp_ctrl;
}

static void src_pio_read(struct hc_mmc_host *host, void *data)
{
    uint32_t *data32 = (uint32_t *)data;
    uint32_t *sg = (uint32_t *)data32[11];
    /*
     * The vendor PIO path uses only the first scatterlist entry.  It derives
     * the CPU address from the page-aligned base in sg word 0 plus the offset
     * in sg word 1, and transfers 16 bits per FIFO access.
     */
    uint16_t *buf = (uint16_t *)(((sg[0] & 0xfffff000u) + sg[1]));

    while (data32[6] < host->data_size) {
        unsigned int polls = 0;

        /*
         * Poll bit 1 of the 16-bit PIO control/status view, waiting at most
         * 8001 one-microsecond iterations.  A command CRC indication exits
         * early; a full timeout stores -ETIMEDOUT in host->cmd->error
         * (command byte offset 0x20).
         */
        while (!((*(volatile uint16_t *)(host->iobase + HC_MMC_REG_PIO_CTRL))
                & 0x0002u)) {
            if (polls++ == 8000u) {
                if (src_get_cmd_status(host) & 0x02u)
                    return;
                break;
            }
            udelay(1);
        }

        if (polls == 8001u) {
            *(uint32_t *)((uint8_t *)host->cmd + 0x20) =
                (uint32_t)-116;
            return;
        }

        /* Offset 0x0c is the 16-bit FIFO data port during PIO transfers. */
        *buf++ = *(volatile uint16_t *)(host->iobase + 0x0c);
        /* mmc_data word 6 is bytes_xfered in the vendor libmmc ABI. */
        data32[6] += 2u;
    }
}

static void src_pio_write(struct hc_mmc_host *host, void *data)
{
    uint32_t *data32 = (uint32_t *)data;
    uint32_t *sg = (uint32_t *)data32[11];
    /* See src_pio_read for the first-SG-only address derivation and ABI. */
    uint16_t *buf = (uint16_t *)(((sg[0] & 0xfffff000u) + sg[1]));

    while (data32[6] < host->data_size) {
        unsigned int polls = 0;

        /* PIO writes push a halfword before waiting for controller readiness. */
        *(volatile uint16_t *)(host->iobase + 0x0c) = *buf++;
        data32[6] += 2u;

        while (!((*(volatile uint16_t *)(host->iobase + HC_MMC_REG_PIO_CTRL))
                & 0x0002u)) {
            if (polls++ == 8000u) {
                if (src_get_cmd_status(host) & 0x02u)
                    return;
                break;
            }
            udelay(1);
        }

        if (polls == 8001u) {
            *(uint32_t *)((uint8_t *)host->cmd + 0x20) =
                (uint32_t)-116;
            return;
        }
    }
}

/*
 * Source-owned table used by runtime swaps and diagnostics in every build.
 * Keeping it under a distinct symbol lets a vendor-default image retain the
 * archive implementation while still selecting these operations explicitly.
 */
const struct hc_mmc_host_hw_ops src_hc_mmc_host_hw_ops = {
    .set_clock = src_set_clock,
    .set_bus_width = src_set_bus_width,
    .set_timing = src_set_timing,
    .enable_force_clock = src_enable_force_clock,
    .set_cmd = src_set_cmd,
    .start_cmd = src_start_cmd,
    .get_cmd_status = src_get_cmd_status,
    .get_response = src_get_response,
    .set_block = src_set_block,
    .set_dma = src_set_dma,
    .set_pio = src_set_pio,
    .pio_write = src_pio_write,
    .pio_read = src_pio_read,
    .pio_cleanup = src_pio_cleanup,
    .enable_irq = src_enable_irq,
    .enable_sdio_irq = src_enable_sdio_irq,
    .get_and_clear_irq = src_get_and_clear_irq,
    .mmc_ip_reset = src_mmc_ip_reset,
    .mmc_clock_gate = src_mmc_clock_gate,
};

#if UNIFROG_MMC_HOST_SOURCE_DEFAULT
/*
 * In a source-default build, export the exact symbol table expected by the
 * vendor probe/driver integration.  These names replace the archive's host-op
 * exports at link time; their entries deliberately point at the same source
 * functions as src_hc_mmc_host_hw_ops.
 */
const struct hc_mmc_host_hw_ops hc_mmc_host_hw_ops = {
    .set_clock = src_set_clock,
    .set_bus_width = src_set_bus_width,
    .set_timing = src_set_timing,
    .enable_force_clock = src_enable_force_clock,
    .set_cmd = src_set_cmd,
    .start_cmd = src_start_cmd,
    .get_cmd_status = src_get_cmd_status,
    .get_response = src_get_response,
    .set_block = src_set_block,
    .set_dma = src_set_dma,
    .set_pio = src_set_pio,
    .pio_write = src_pio_write,
    .pio_read = src_pio_read,
    .pio_cleanup = src_pio_cleanup,
    .enable_irq = src_enable_irq,
    .enable_sdio_irq = src_enable_sdio_irq,
    .get_and_clear_irq = src_get_and_clear_irq,
    .mmc_ip_reset = src_mmc_ip_reset,
    .mmc_clock_gate = src_mmc_clock_gate,
};

/* ABI-name forwarding wrappers required by the vendor-facing integration. */
void hc_mmc_clock_gate(void *host, int enable)
{
    src_mmc_clock_gate(host, enable);
}

void hc_mmc_ip_reset(void)
{
    src_mmc_ip_reset();
}
#endif

/*
 * Vendor-default builds hand us the archive's table at runtime.  Save the
 * pointer rather than copying it so diagnostic mode can wrap the original
 * implementation and runtime swaps can recognize it by identity.
 */
static const struct hc_mmc_host_hw_ops *saved_vendor_ops;

void src_hc_mmc_host_save_vendor(const struct hc_mmc_host_hw_ops *ops)
{
    saved_vendor_ops = ops;
}

const struct hc_mmc_host_hw_ops *src_hc_mmc_host_hw_ops_ptr(void)
{
    return &src_hc_mmc_host_hw_ops;
}

/*
 * Diagnostic wrappers record an operation's arguments, return value, and a
 * before/after snapshot of the controller window.  Completed records receive
 * an atomic sequence number and are then published into the wrapping ring.
 * Building records locally keeps concurrent IRQ/task writers from sharing a
 * partially populated slot.
 */
#define DIAG_RING_SIZE 1024u
#define DIAG_REG_COUNT 16u
#define DIAG_DUMP_LIMIT 256u
#define DIAG_SLOT_PUBLISHED 1u
#define DIAG_SLOT_WRITING 2u

/*
 * Snapshot aligned 32-bit words across command, response, DMA/control, clock,
 * and timing state.  Reading these offsets is diagnostic behavior and may
 * include multiple byte registers packed into one little-endian word.
 */
static const uint8_t diag_reg_offsets[DIAG_REG_COUNT] = {
    0x00, 0x04, 0x08, 0x0c,
    0x10, 0x14, 0x18, 0x1c,
    0x20, 0x24, 0x28, 0x2c,
    0x30, 0x34, 0x48, 0x50,
};

struct diag_entry {
    /* Function ID and whether the wrapped implementation was source/vendor. */
    uint8_t func_id;
    uint8_t base_kind;
    uint8_t _pad[2];
    uint32_t arg[3];
    uint32_t regs_before[DIAG_REG_COUNT];
    uint32_t regs_after[DIAG_REG_COUNT];
    uint32_t result;
};

struct diag_slot {
    /*
     * Writers acquire WRITING, replace sequence and entry, then publish.
     * Readers accept a copy only when PUBLISHED and sequence remain unchanged.
     */
    volatile uint32_t valid;
    volatile uint32_t sequence;
    struct diag_entry entry;
};

static struct diag_slot diag_ring[DIAG_RING_SIZE];
static struct diag_entry diag_snapshot[DIAG_DUMP_LIMIT];
static volatile uint32_t diag_next_sequence;
static volatile uint32_t diag_reset_sequence;
static volatile uint32_t diag_reset_busy;
static volatile uint32_t diag_dump_busy;
static const struct hc_mmc_host_hw_ops *diag_base_ops;
static int diag_base_is_source;

enum {
    /* Stable IDs used only to label entries in the in-memory diagnostic ring. */
    F_SET_CLOCK = 1,
    F_SET_BUS_WIDTH,
    F_SET_TIMING,
    F_ENABLE_FORCE_CLOCK,
    F_SET_CMD,
    F_START_CMD,
    F_GET_CMD_STATUS,
    F_GET_RESPONSE,
    F_SET_BLOCK,
    F_SET_DMA,
    F_SET_PIO,
    F_PIO_WRITE,
    F_PIO_READ,
    F_PIO_CLEANUP,
    F_ENABLE_IRQ,
    F_ENABLE_SDIO_IRQ,
    F_GET_AND_CLEAR_IRQ,
    F_IP_RESET,
    F_CLOCK_GATE,
};

static void diag_read_regs(volatile uint8_t *io, uint32_t *out)
{
    unsigned int i;

    if (!io) {
        /* SoC-level reset/gate operations have no controller iobase snapshot. */
        memset(out, 0, sizeof(uint32_t) * DIAG_REG_COUNT);
        return;
    }

    for (i = 0; i < DIAG_REG_COUNT; i++)
        out[i] = *(volatile uint32_t *)(io + diag_reg_offsets[i]);
}

static void diag_begin_entry(struct diag_entry *entry, uint8_t fid,
                             uint32_t a0, uint32_t a1, uint32_t a2,
                             volatile uint8_t *io)
{
    memset(entry, 0, sizeof(*entry));
    entry->func_id = fid;
    entry->base_kind = diag_base_is_source ? 1u : 0u;
    entry->arg[0] = a0;
    entry->arg[1] = a1;
    entry->arg[2] = a2;
    diag_read_regs(io, entry->regs_before);
}

static void diag_finish_entry(struct diag_entry *entry, volatile uint8_t *io,
                              uint32_t result)
{
    struct diag_slot *slot;
    uint32_t sequence;
    uint32_t state;

    diag_read_regs(io, entry->regs_after);
    entry->result = result;
    /*
     * Reserve only after the local record is complete.  The atomic sequence
     * gives concurrent writers unique slots; valid publishes the copied bytes
     * only after the sequence and payload are globally visible.
     */
    sequence = __sync_fetch_and_add(&diag_next_sequence, 1u);
    slot = &diag_ring[sequence % DIAG_RING_SIZE];
    /*
     * A writer delayed for a full ring wrap must not overwrite a newer writer
     * using the same slot. Drop diagnostics under that extreme contention
     * rather than publish a torn or out-of-order record.
     */
    do {
        state = __sync_fetch_and_add(&slot->valid, 0u);
        if (state == DIAG_SLOT_WRITING)
            return;
    } while (!__sync_bool_compare_and_swap(&slot->valid, state,
        DIAG_SLOT_WRITING));
    /*
     * A writer can also be delayed before acquiring the slot. Preserve a
     * publication from a newer sequence instead of overwriting it afterward.
     * Ring reuse is far below half the uint32_t sequence space, so signed
     * subtraction gives the intended wrap-safe ordering.
     */
    if (state == DIAG_SLOT_PUBLISHED &&
        (int32_t)(sequence - slot->sequence) < 0) {
        (void)__sync_lock_test_and_set(&slot->valid, DIAG_SLOT_PUBLISHED);
        return;
    }
    slot->sequence = sequence;
    memcpy(&slot->entry, entry, sizeof(slot->entry));
    __sync_synchronize();
    (void)__sync_lock_test_and_set(&slot->valid, DIAG_SLOT_PUBLISHED);
}

/*
 * Each d_* operation follows the same transparent-wrapper contract:
 * reserve/snapshot a ring entry, call the selected base table exactly once,
 * then record post-call registers and any scalar result.  Pointer arguments
 * are stored as 32-bit values because the target ABI is MIPS32.
 */
static void d_set_clock(struct hc_mmc_host *host, unsigned int hz)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_SET_CLOCK, hz, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->set_clock(host, hz);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_set_bus_width(struct hc_mmc_host *host, unsigned char width)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_SET_BUS_WIDTH, width, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->set_bus_width(host, width);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_set_timing(struct hc_mmc_host *host, unsigned char timing)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_SET_TIMING, timing, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->set_timing(host, timing);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_enable_force_clock(struct hc_mmc_host *host, int enable)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_ENABLE_FORCE_CLOCK, (uint32_t)enable, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->enable_force_clock(host, enable);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_set_cmd(struct hc_mmc_host *host, void *cmd, void *data)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_SET_CMD, (uint32_t)(uintptr_t)cmd,
        (uint32_t)(uintptr_t)data, 0, host ? host->iobase : NULL);
    diag_base_ops->set_cmd(host, cmd, data);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_start_cmd(struct hc_mmc_host *host)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_START_CMD, 0, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->start_cmd(host);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static uint32_t d_get_cmd_status(struct hc_mmc_host *host)
{
    struct diag_entry entry;
    uint32_t result;

    diag_begin_entry(&entry, F_GET_CMD_STATUS, 0, 0, 0,
        host ? host->iobase : NULL);
    result = diag_base_ops->get_cmd_status(host);
    diag_finish_entry(&entry, host ? host->iobase : NULL, result);
    return result;
}

static void d_get_response(struct hc_mmc_host *host, void *cmd)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_GET_RESPONSE, (uint32_t)(uintptr_t)cmd, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->get_response(host, cmd);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_set_block(struct hc_mmc_host *host, unsigned int nblk,
                        unsigned int blksz)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_SET_BLOCK, nblk, blksz, 0,
        host ? host->iobase : NULL);
    diag_base_ops->set_block(host, nblk, blksz);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_set_dma(struct hc_mmc_host *host, uint32_t addr, uint32_t len,
                      int dir)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_SET_DMA, addr, len, (uint32_t)dir,
        host ? host->iobase : NULL);
    diag_base_ops->set_dma(host, addr, len, dir);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_set_pio(struct hc_mmc_host *host, uint32_t flags)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_SET_PIO, flags, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->set_pio(host, flags);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_pio_write(struct hc_mmc_host *host, void *data)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_PIO_WRITE, (uint32_t)(uintptr_t)data, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->pio_write(host, data);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_pio_read(struct hc_mmc_host *host, void *data)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_PIO_READ, (uint32_t)(uintptr_t)data, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->pio_read(host, data);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_pio_cleanup(struct hc_mmc_host *host)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_PIO_CLEANUP, 0, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->pio_cleanup(host);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_enable_irq(struct hc_mmc_host *host, void *data, int enable)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_ENABLE_IRQ, (uint32_t)(uintptr_t)data,
        (uint32_t)enable, 0, host ? host->iobase : NULL);
    diag_base_ops->enable_irq(host, data, enable);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static void d_enable_sdio_irq(struct hc_mmc_host *host, int enable)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_ENABLE_SDIO_IRQ, (uint32_t)enable, 0, 0,
        host ? host->iobase : NULL);
    diag_base_ops->enable_sdio_irq(host, enable);
    diag_finish_entry(&entry, host ? host->iobase : NULL, 0);
}

static uint32_t d_get_and_clear_irq(struct hc_mmc_host *host)
{
    struct diag_entry entry;
    uint32_t result;

    diag_begin_entry(&entry, F_GET_AND_CLEAR_IRQ, 0, 0, 0,
        host ? host->iobase : NULL);
    result = diag_base_ops->get_and_clear_irq(host);
    diag_finish_entry(&entry, host ? host->iobase : NULL, result);
    return result;
}

static void d_ip_reset(void)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_IP_RESET, 0, 0, 0, NULL);
    diag_base_ops->mmc_ip_reset();
    diag_finish_entry(&entry, NULL, 0);
}

static void d_clock_gate(void *host, int enable)
{
    struct diag_entry entry;

    diag_begin_entry(&entry, F_CLOCK_GATE, (uint32_t)enable, 0, 0, NULL);
    diag_base_ops->mmc_clock_gate(host, enable);
    diag_finish_entry(&entry, NULL, 0);
}

static struct hc_mmc_host_hw_ops diag_ops_table;
static int diag_ops_initialized;

/* Convert the internal function ID to the short label used by diag dumps. */
static const char *diag_name(uint8_t id)
{
    switch (id) {
    case F_SET_CLOCK: return "set_clock";
    case F_SET_BUS_WIDTH: return "set_bus_width";
    case F_SET_TIMING: return "set_timing";
    case F_ENABLE_FORCE_CLOCK: return "enable_force_clock";
    case F_SET_CMD: return "set_cmd";
    case F_START_CMD: return "start_cmd";
    case F_GET_CMD_STATUS: return "get_cmd_status";
    case F_GET_RESPONSE: return "get_response";
    case F_SET_BLOCK: return "set_block";
    case F_SET_DMA: return "set_dma";
    case F_SET_PIO: return "set_pio";
    case F_PIO_WRITE: return "pio_write";
    case F_PIO_READ: return "pio_read";
    case F_PIO_CLEANUP: return "pio_cleanup";
    case F_ENABLE_IRQ: return "enable_irq";
    case F_ENABLE_SDIO_IRQ: return "enable_sdio_irq";
    case F_GET_AND_CLEAR_IRQ: return "get_and_clear_irq";
    case F_IP_RESET: return "ip_reset";
    case F_CLOCK_GATE: return "clock_gate";
    default: return "?";
    }
}

void src_hc_mmc_host_diag_init(const struct hc_mmc_host_hw_ops *base,
                               int base_is_source)
{
    /*
     * Prefer the caller's requested implementation, then a saved vendor
     * table, then source ops.  Copy every base entry first so future table
     * extensions remain pass-through unless explicitly wrapped below.
     */
    diag_base_ops = base ? base :
        (saved_vendor_ops ? saved_vendor_ops : &src_hc_mmc_host_hw_ops);
    diag_base_is_source = base_is_source ? 1 : 0;

    memcpy(&diag_ops_table, diag_base_ops, sizeof(diag_ops_table));
    diag_ops_table.set_clock = d_set_clock;
    diag_ops_table.set_bus_width = d_set_bus_width;
    diag_ops_table.set_timing = d_set_timing;
    diag_ops_table.enable_force_clock = d_enable_force_clock;
    diag_ops_table.set_cmd = d_set_cmd;
    diag_ops_table.start_cmd = d_start_cmd;
    diag_ops_table.get_cmd_status = d_get_cmd_status;
    diag_ops_table.get_response = d_get_response;
    diag_ops_table.set_block = d_set_block;
    diag_ops_table.set_dma = d_set_dma;
    diag_ops_table.set_pio = d_set_pio;
    diag_ops_table.pio_write = d_pio_write;
    diag_ops_table.pio_read = d_pio_read;
    diag_ops_table.pio_cleanup = d_pio_cleanup;
    diag_ops_table.enable_irq = d_enable_irq;
    diag_ops_table.enable_sdio_irq = d_enable_sdio_irq;
    diag_ops_table.get_and_clear_irq = d_get_and_clear_irq;
    diag_ops_table.mmc_ip_reset = d_ip_reset;
    diag_ops_table.mmc_clock_gate = d_clock_gate;
    diag_ops_initialized = 1;
}

const struct hc_mmc_host_hw_ops *src_hc_mmc_host_diag_ops_ptr(void)
{
    /* Callers must initialize the table before installing this pointer. */
    return &diag_ops_table;
}

int src_hc_mmc_host_ops_known(const struct hc_mmc_host_hw_ops *ops)
{
    /*
     * Storage runtime swapping uses pointer identity as a guard: only the
     * linked export, source table, initialized diagnostic table, or explicitly
     * saved vendor table is considered a recognized implementation.
     */
    if (!ops)
        return 0;
    if (ops == &hc_mmc_host_hw_ops)
        return 1;
    if (ops == &src_hc_mmc_host_hw_ops)
        return 1;
    if (ops == &diag_ops_table && diag_ops_initialized)
        return 1;
    if (saved_vendor_ops && ops == saved_vendor_ops)
        return 1;
    return 0;
}

void src_hc_mmc_host_diag_dump(void)
{
    uint32_t end_sequence;
    uint32_t reset_sequence;
    uint32_t available;
    uint32_t scanned;
    unsigned int count = 0;
    unsigned int i;

    /*
     * A single static snapshot avoids a large task stack allocation.  Reject
     * concurrent dumps so one logger cannot replace another logger's snapshot.
     */
    if (!__sync_bool_compare_and_swap(&diag_dump_busy, 0u, 1u))
        return;

    reset_sequence = __sync_fetch_and_add(&diag_reset_sequence, 0u);
    end_sequence = __sync_fetch_and_add(&diag_next_sequence, 0u);
    available = end_sequence - reset_sequence;
    if (available > DIAG_RING_SIZE)
        available = DIAG_RING_SIZE;

    /*
     * Walk backward to select the newest completed records.  Validate the
     * slot markers on both sides of each copy so a concurrent publication or
     * wrap cannot leave a torn record in the snapshot.  All copies complete
     * before unifrog_log can generate further MMC operations.
     */
    for (scanned = 0; scanned < available && count < DIAG_DUMP_LIMIT;
         scanned++) {
        uint32_t sequence = end_sequence - scanned - 1u;
        struct diag_slot *slot = &diag_ring[sequence % DIAG_RING_SIZE];
        uint32_t valid = __sync_fetch_and_add(&slot->valid, 0u);
        uint32_t published = __sync_fetch_and_add(&slot->sequence, 0u);

        if (valid != DIAG_SLOT_PUBLISHED || published != sequence)
            continue;
        memcpy(&diag_snapshot[count], &slot->entry,
               sizeof(diag_snapshot[count]));
        __sync_synchronize();
        if (__sync_fetch_and_add(&slot->valid, 0u) == DIAG_SLOT_PUBLISHED &&
            __sync_fetch_and_add(&slot->sequence, 0u) == sequence)
            count++;
    }

    unifrog_log("mmc_diag_dump: captured=%u\n", count);
    for (i = count; i > 0u; i--) {
        struct diag_entry *entry = &diag_snapshot[i - 1u];
        unsigned int index = count - i;

        /*
         * The snapshot was collected newest-first; emit it oldest-first.  Keep
         * before/result and after in separate bounded formatted records.
         */
        unifrog_log("D[%u] %s base=%s a=%08x/%08x/%08x "
                    "before=[00=%08x 04=%08x 08=%08x 0c=%08x "
                    "10=%08x 14=%08x 18=%08x 1c=%08x "
                    "20=%08x 24=%08x 28=%08x 2c=%08x "
                    "30=%08x 34=%08x 48=%08x 50=%08x] ret=%08x\n",
            index, diag_name(entry->func_id),
            entry->base_kind ? "source" : "vendor",
            entry->arg[0], entry->arg[1], entry->arg[2],
            entry->regs_before[0], entry->regs_before[1],
            entry->regs_before[2], entry->regs_before[3],
            entry->regs_before[4], entry->regs_before[5],
            entry->regs_before[6], entry->regs_before[7],
            entry->regs_before[8], entry->regs_before[9],
            entry->regs_before[10], entry->regs_before[11],
            entry->regs_before[12], entry->regs_before[13],
            entry->regs_before[14], entry->regs_before[15],
            entry->result);
        unifrog_log("     after=[00=%08x 04=%08x 08=%08x 0c=%08x "
                    "10=%08x 14=%08x 18=%08x 1c=%08x "
                    "20=%08x 24=%08x 28=%08x 2c=%08x "
                    "30=%08x 34=%08x 48=%08x 50=%08x]\n",
            entry->regs_after[0], entry->regs_after[1],
            entry->regs_after[2], entry->regs_after[3],
            entry->regs_after[4], entry->regs_after[5],
            entry->regs_after[6], entry->regs_after[7],
            entry->regs_after[8], entry->regs_after[9],
            entry->regs_after[10], entry->regs_after[11],
            entry->regs_after[12], entry->regs_after[13],
            entry->regs_after[14], entry->regs_after[15]);
    }
    __sync_lock_release(&diag_dump_busy);
}

void src_hc_mmc_host_diag_reset(void)
{
    uint32_t next;

    /*
     * Advance the logical floor instead of clearing slots that an IRQ writer
     * may be publishing. Coalesce concurrent resets so a delayed caller cannot
     * move the floor backward after a newer reset. A concurrent dump already
     * owns an immutable snapshot.
     */
    if (!__sync_bool_compare_and_swap(&diag_reset_busy, 0u, 1u))
        return;
    next = __sync_fetch_and_add(&diag_next_sequence, 0u);
    (void)__sync_lock_test_and_set(&diag_reset_sequence, next);
    __sync_lock_release(&diag_reset_busy);
}
