// USB host (Renesas R8A66597 family, the SH7763's USB module; its register
// set as the SH7724/SH7763 manuals give it) and bulk-only mass storage.
//
// Control transfers run on the DCP (pipe 0): the setup packet from
// USBREQ/USBVAL/USBINDX/USBLENG on SUREQ, the data stage through CFIFO with
// PID = BUF, the status stage with CCPL.  Bulk runs on pipe 1 (OUT) and
// pipe 2 (IN), one ≤1 KB buffer at a time (a pipe's buffer: see docs/usb.md).
#include "usb.h"
#include "tmu.h"
#include "con.h"
#include <string.h>

namespace usb {
namespace {
constexpr uint32_t kBase = 0xFE400000u;
inline volatile uint16_t &r16(uint32_t o) { return *reinterpret_cast<volatile uint16_t *>(kBase + o); }
inline volatile uint32_t &r32(uint32_t o) { return *reinterpret_cast<volatile uint32_t *>(kBase + o); }
constexpr uint32_t SYSCFG = 0x00, SYSSTS0 = 0x04, DVSTCTR0 = 0x08, CFIFO = 0x14, CFIFOSEL = 0x20, CFIFOCTR = 0x22,
    INTENB1 = 0x32, INTSTS1 = 0x42, BRDYSTS = 0x46, NRDYSTS = 0x48, BEMPSTS = 0x4A, USBREQ = 0x54, USBVAL = 0x56,
    USBINDX = 0x58, USBLENG = 0x5A, DCPCFG = 0x5C, DCPMAXP = 0x5E, DCPCTR = 0x60, PIPESEL = 0x64, PIPECFG = 0x68,
    PIPEBUF = 0x6A, PIPEMAXP = 0x6C;
inline uint32_t PIPECTR(int n) { return 0x70 + 2 * (n - 1); }
inline uint32_t PIPETRE(int n) { return 0x90 + 4 * (n - 1); }
inline uint32_t PIPETRN(int n) { return 0x92 + 4 * (n - 1); }
constexpr uint16_t BCHG = 0x4000, SIGN = 0x0020, SACK = 0x0010, USBRST = 0x0040, UACT = 0x0010,
    SUREQ = 0x4000, CCPL = 0x0004, PID_BUF = 0x0001, SQCLR = 0x0100, BVAL = 0x8000, BCLR = 0x4000, DTLN = 0x0FFF;
constexpr int kOut = 1, kIn = 2;

uint8_t addr_, ep_out_, ep_in_;
uint16_t mps0_ = 8, mps_out_ = 64, mps_in_ = 64;
uint32_t blocks_, block_size_ = 512, tag_ = 1;

bool wait(uint32_t reg, uint16_t mask, uint32_t ms)
{
    const int64_t until = tmu_us64() + static_cast<int64_t>(ms) * 1000;
    while (!(r16(reg) & mask)) if (tmu_us64() > until) return false;
    return true;
}

void fifo_select(int pipe) { r16(CFIFOSEL) = static_cast<uint16_t>(pipe); }

int fifo_read(uint8_t *dst, int max)
{
    int n = r16(CFIFOCTR) & DTLN, got = 0;
    if (n > max) n = max;
    while (got + 4 <= n) { uint32_t w = r32(CFIFO); memcpy(dst + got, &w, 4); got += 4; }
    if (got < n) { uint32_t w = r32(CFIFO); memcpy(dst + got, &w, n - got); got = n; }
    return got;
}

void fifo_write(const uint8_t *src, int n)
{
    int i = 0;
    for (; i + 4 <= n; i += 4) { uint32_t w; memcpy(&w, src + i, 4); r32(CFIFO) = w; }
    for (; i < n; i++) *reinterpret_cast<volatile uint8_t *>(kBase + CFIFO) = src[i];
    r16(CFIFOCTR) = BVAL;
}

// One control transfer on the DCP.  `data` in or out per bmRequestType bit 7.
int control(uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint16_t len, uint8_t *data)
{
    const bool in = type & 0x80;
    r16(DCPMAXP) = static_cast<uint16_t>(addr_ << 12 | mps0_);
    r16(INTSTS1) = static_cast<uint16_t>(~(SACK | SIGN));
    r16(USBREQ) = static_cast<uint16_t>(req << 8 | type);
    r16(USBVAL) = value; r16(USBINDX) = index; r16(USBLENG) = len;
    r16(DCPCTR) = SUREQ;
    if (!wait(INTSTS1, SACK | SIGN, 100) || (r16(INTSTS1) & SIGN)) return -1;
    int got = 0;
    if (len && in) {
        r16(DCPCFG) = 0;                        // data stage in
        fifo_select(0);
        r16(BRDYSTS) = static_cast<uint16_t>(~1u);
        r16(DCPCTR) = PID_BUF;
        while (got < len) {
            if (!wait(BRDYSTS, 1, 200)) return -1;
            r16(BRDYSTS) = static_cast<uint16_t>(~1u);
            const int n = fifo_read(data + got, len - got);
            got += n;
            if (n < mps0_) break;                // a short packet ends the stage
        }
        r16(DCPCFG) = 0;                        // status stage: OUT, the other way
    } else if (len) {
        r16(DCPCFG) = 0x0010;                   // data stage out
        fifo_select(0x20);                      // ISEL: the CPU writes the DCP
        fifo_write(data, len);
        r16(BEMPSTS) = static_cast<uint16_t>(~1u);
        r16(DCPCTR) = PID_BUF;
        if (!wait(BEMPSTS, 1, 200)) return -1;
        got = len;
        r16(DCPCFG) = 0;                        // status stage: IN
    } else {
        r16(DCPCFG) = 0x0010;                   // no data stage: the status stage is an IN
    }
    r16(BEMPSTS) = static_cast<uint16_t>(~1u);
    r16(DCPCTR) = CCPL | PID_BUF;
    if (!wait(BEMPSTS, 1, 200)) return -1;
    r16(BEMPSTS) = static_cast<uint16_t>(~1u);
    return got;
}

void pipe_setup(int pipe, bool out, uint8_t ep, uint16_t mps)
{
    r16(PIPESEL) = static_cast<uint16_t>(pipe);
    // TYPE bulk (01), BFRE 0, DBLB 0, CNTMD, SHTNAK for IN, DIR, EPNUM.
    r16(PIPECFG) = static_cast<uint16_t>(0x4000 | 0x0100 | (out ? 0x0010 : 0x0080) | (ep & 0x0F));
    r16(PIPEBUF) = static_cast<uint16_t>(((1024 / 64 - 1) << 10) | (8 + 16 * (pipe - 1)));   // 1 KB from block 8 on
    r16(PIPEMAXP) = static_cast<uint16_t>(addr_ << 12 | mps);
    r16(PIPECTR(pipe)) = SQCLR;                 // DATA0, PID NAK
}

bool bulk_out(const uint8_t *src, int len)
{
    fifo_select(kOut);
    r16(CFIFOCTR) = BCLR;
    fifo_write(src, len);
    r16(BEMPSTS) = static_cast<uint16_t>(~(1u << kOut));
    r16(PIPECTR(kOut)) = PID_BUF;
    const bool ok = wait(BEMPSTS, 1u << kOut, 500);
    r16(PIPECTR(kOut)) = 0;
    return ok && !(r16(NRDYSTS) & (1u << kOut));
}

// Read exactly `len` (≤ 1024) bytes from the IN pipe.
int bulk_in(uint8_t *dst, int len)
{
    r16(PIPETRE(kIn)) = 0x0100;                 // TRCLR
    r16(PIPETRN(kIn)) = static_cast<uint16_t>((len + mps_in_ - 1) / mps_in_);
    r16(PIPETRE(kIn)) = 0x0200;                 // TRENB
    r16(NRDYSTS) = static_cast<uint16_t>(~(1u << kIn));
    r16(BRDYSTS) = static_cast<uint16_t>(~(1u << kIn));
    fifo_select(kIn);
    r16(PIPECTR(kIn)) = PID_BUF;
    int got = 0;
    while (got < len) {
        if (!wait(BRDYSTS, 1u << kIn, 1000)) break;
        r16(BRDYSTS) = static_cast<uint16_t>(~(1u << kIn));
        const int n = fifo_read(dst + got, len - got);
        if (!n) break;
        got += n;
        if (n % mps_in_) break;                 // short packet: the device ended it
    }
    r16(PIPECTR(kIn)) = 0;
    r16(PIPETRE(kIn)) = 0;
    return got;
}

// One SCSI command over bulk-only transport.  Data IN into `data` (≤ 1024 bytes).
bool scsi(const uint8_t *cdb, int cdb_len, uint8_t *data, uint32_t data_len)
{
    uint8_t cbw[31] = {0x55, 0x53, 0x42, 0x43};
    const uint32_t tag = tag_++;
    memcpy(cbw + 4, &tag, 4);
    memcpy(cbw + 8, &data_len, 4);
    cbw[12] = data_len ? 0x80 : 0x00;
    cbw[14] = static_cast<uint8_t>(cdb_len);
    memcpy(cbw + 15, cdb, cdb_len);
    if (!bulk_out(cbw, 31)) return false;
    if (data_len && bulk_in(data, static_cast<int>(data_len)) != static_cast<int>(data_len)) return false;
    uint8_t csw[13];
    if (bulk_in(csw, 13) != 13) return false;
    uint32_t sig, ctag;
    memcpy(&sig, csw, 4); memcpy(&ctag, csw + 4, 4);
    return sig == 0x53425355u && ctag == tag && csw[12] == 0;
}
} // namespace

bool open_stick(uint32_t ms)
{
    r16(SYSCFG) = 0x0461;                       // SCKE | DCFM (host) | DRPD (pull-downs) | USBE ⚠ bits per the R8A66597 layout
    r16(INTSTS1) = 0;
    r16(INTENB1) = BCHG;                        // tells us when a device comes or goes
    if (!wait(INTSTS1, BCHG, ms) && !(r16(SYSSTS0) & 3)) return false;
    r16(INTSTS1) = static_cast<uint16_t>(~BCHG);
    r16(DVSTCTR0) = USBRST;                     // bus reset, 50 ms
    tmu_delay_us(50000);
    r16(DVSTCTR0) = UACT;                       // SOFs on
    tmu_delay_us(20000);

    addr_ = 0;
    mps0_ = 8;
    uint8_t d[256];
    if (control(0x80, 6, 0x0100, 0, 8, d) < 8) { con_printf("usb: no device descriptor\n"); return false; }
    mps0_ = d[7] ? d[7] : 8;
    if (control(0x00, 5, 1, 0, 0, nullptr) < 0) { con_printf("usb: SET_ADDRESS refused\n"); return false; }
    addr_ = 1;
    tmu_delay_us(5000);
    if (control(0x80, 6, 0x0100, 0, 18, d) < 18) return false;
    con_printf("usb: device %04x:%04x\n", d[8] | d[9] << 8, d[10] | d[11] << 8);
    if (control(0x80, 6, 0x0200, 0, 9, d) < 9) return false;
    const int total = d[2] | d[3] << 8;
    if (control(0x80, 6, 0x0200, 0, static_cast<uint16_t>(total > 255 ? 255 : total), d) < 9) return false;
    // Find a mass-storage interface (class 8, SCSI transparent 6, bulk-only 0x50) and its bulk endpoints.
    bool msc = false;
    for (int i = 0; i + 2 <= total && i < 255; i += d[i] ? d[i] : 1) {
        if (d[i + 1] == 4) msc = d[i + 5] == 8 && d[i + 6] == 6 && d[i + 7] == 0x50;
        if (msc && d[i + 1] == 5 && (d[i + 3] & 3) == 2) {
            const uint16_t mps = static_cast<uint16_t>(d[i + 4] | d[i + 5] << 8);
            if (d[i + 2] & 0x80) { ep_in_ = d[i + 2] & 0x0F; mps_in_ = mps; }
            else { ep_out_ = d[i + 2] & 0x0F; mps_out_ = mps; }
        }
    }
    if (!ep_in_ || !ep_out_) { con_printf("usb: not a mass-storage stick\n"); return false; }
    if (control(0x00, 9, d[5], 0, 0, nullptr) < 0) return false;   // SET_CONFIGURATION
    pipe_setup(kOut, true, ep_out_, mps_out_);
    pipe_setup(kIn, false, ep_in_, mps_in_);

    const uint8_t tur[6] = {0};
    for (int i = 0; i < 10 && !scsi(tur, 6, nullptr, 0); i++) tmu_delay_us(100000);
    const uint8_t cap_cmd[10] = {0x25};
    uint8_t cap[8];
    if (!scsi(cap_cmd, 10, cap, 8)) { con_printf("usb: READ CAPACITY failed\n"); return false; }
    blocks_ = (static_cast<uint32_t>(cap[0]) << 24 | cap[1] << 16 | cap[2] << 8 | cap[3]) + 1;
    block_size_ = static_cast<uint32_t>(cap[4]) << 24 | cap[5] << 16 | cap[6] << 8 | cap[7];
    con_printf("usb: stick ready, %u blocks of %u bytes (%u MB)\n", blocks_, block_size_,
               static_cast<unsigned>((static_cast<uint64_t>(blocks_) * block_size_) >> 20));
    return block_size_ == 512;
}

uint32_t blocks() { return blocks_; }
uint32_t block_size() { return block_size_; }

bool read(uint32_t lba, uint32_t count, void *dst)
{
    uint8_t *p = static_cast<uint8_t *>(dst);
    while (count) {
        const uint32_t n = count > 2 ? 2 : count;      // a pipe buffer at a time
        const uint8_t cdb[10] = {0x28, 0, static_cast<uint8_t>(lba >> 24), static_cast<uint8_t>(lba >> 16),
                                 static_cast<uint8_t>(lba >> 8), static_cast<uint8_t>(lba), 0, 0,
                                 static_cast<uint8_t>(n), 0};
        if (!scsi(cdb, 10, p, n * 512)) return false;
        lba += n; count -= n; p += n * 512;
    }
    return true;
}
} // namespace usb
