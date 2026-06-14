/*
__/\\\\\\\\\\\\\\\______________/\\\\\\\\\__/\\\______________/\\\\\\\\\\\_        
 _\///////\\\/////____________/\\\////////__\/\\\_____________\/////\\\///__       
  _______\/\\\________/\\\___/\\\/___________\/\\\_________________\/\\\_____      
   _______\/\\\_______\///___/\\\_____________\/\\\_________________\/\\\_____     
    _______\/\\\________/\\\_\/\\\_____________\/\\\_________________\/\\\_____    
     _______\/\\\_______\/\\\_\//\\\____________\/\\\_________________\/\\\_____   
      _______\/\\\_______\/\\\__\///\\\__________\/\\\_________________\/\\\_____  
       _______\/\\\_______\/\\\____\////\\\\\\\\\_\/\\\\\\\\\\\\\\\__/\\\\\\\\\\\_ 
        _______\///________\///________\/////////__\///////////////__\///////////__

    Written and developed by:
    - sheet315 (GitHub) (indestinated on Discord)

    MIT License

    Copyright (c) 2026 Indestinate

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <functional>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <wchar.h>
#include <unistd.h>
#include <zlib.h>
#include <libusb-1.0/libusb.h>

static const uint16_t TI_VID = 0x0451;

struct CalcModel {
    uint16_t pid;
    const char *name;
    const char *family;
    int buf_size;
};

static const CalcModel KNOWN_MODELS[] = {
    { 0xE001, "TI-GraphLink USB",     "cable",   250  },
    { 0xE003, "TI-84 Plus",           "z80",     250  },
    { 0xE004, "TI-89 Titanium",       "68k",     1018 },
    { 0xE008, "TI-84 Plus CE",        "ez80",    1018 },
    { 0xE012, "TI-Nspire",            "nspire",  1018 },
    { 0xE019, "TI-Nspire CX",         "nspire",  1018 },
    { 0xE022, "TI-Nspire CX II",      "nspire",  1018 },
    { 0xE023, "TI-Nspire CX II CAS",  "nspire",  1018 },
    { 0xE025, "TI-84 Plus CE-T",      "ez80",    1018 },
    { 0,      nullptr,                nullptr,   0    },
};

const CalcModel *find_model(uint16_t pid) {
    for (const auto &m : KNOWN_MODELS)
        if (m.pid == pid) return &m;
    return nullptr;
}


enum RawType : uint8_t {
    RAW_BUF_SIZE_REQ   = 1,
    RAW_BUF_SIZE_ALLOC = 2,
    RAW_VPKT_DATA_CONT = 3,
    RAW_VPKT_DATA_LAST = 4,
    RAW_VPKT_DATA_ACK  = 5,
};

enum VPktType : uint16_t {
    VPKT_PING          = 0x0001,
    VPKT_OS_BEGIN      = 0x0002,
    VPKT_OS_ACK        = 0x0003,
    VPKT_PARAM_REQ     = 0x0007,
    VPKT_PARAM_DATA    = 0x0008,
    VPKT_DIR_REQ       = 0x0009,
    VPKT_VAR_HDR       = 0x000A,
    VPKT_RTS           = 0x000B,
    VPKT_VAR_REQ       = 0x000C,
    VPKT_VAR_CONTENTS  = 0x000D,
    VPKT_PARAM_SET     = 0x000E,
    VPKT_DELETE        = 0x0010,
    VPKT_REMOTE_CTL    = 0x0011,
    VPKT_MODE_ACK      = 0x0012,
    VPKT_DATA_ACK      = 0xAA00,
    VPKT_DELAY_ACK     = 0xBB00,
    VPKT_EOT           = 0xDD00,
    VPKT_ERROR         = 0xEE00,
};

enum ParamID : uint16_t {
    PID_PRODUCT_NUM    = 0x0001,
    PID_PRODUCT_NAME   = 0x0002,
    PID_CALC_ID        = 0x0003,
    PID_HW_VERSION     = 0x0004,
    PID_LANG_ID        = 0x0006,
    PID_DBUS_TYPE      = 0x0008,
    PID_OS_VERSION     = 0x000B,
    PID_RAM_FREE       = 0x000E,
    PID_FLASH_FREE     = 0x0011,
    PID_LCD_WIDTH      = 0x001E,
    PID_LCD_HEIGHT     = 0x001F,
    PID_LCD_CONTENTS   = 0x0022,
    PID_CLOCK_ON       = 0x0024,
    PID_CLOCK          = 0x0025,
    PID_BATTERY        = 0x002D,
    PID_AT_HOME        = 0x0037,
};

enum KeyCode : uint16_t {
    KEY_NONE    = 0x0000,
    KEY_DOWN    = 0x0001,
    KEY_LEFT    = 0x0002,
    KEY_RIGHT   = 0x0003,
    KEY_UP      = 0x0004,
    KEY_ENTER   = 0x0005,
    KEY_ALPHA   = 0x0030,
    KEY_2ND     = 0x0036,
    KEY_MODE    = 0x0037,
    KEY_DEL     = 0x0038,
    KEY_CLEAR   = 0x000F,
    KEY_QUIT    = 0x0040,
    KEY_HOME    = 0x0041,
};

static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static inline uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0]<<8)|p[1];
}
static inline void put_be32(uint8_t *p, uint32_t v) {
    p[0]=(v>>24)&0xFF; p[1]=(v>>16)&0xFF; p[2]=(v>>8)&0xFF; p[3]=v&0xFF;
}
static inline void put_be16(uint8_t *p, uint16_t v) {
    p[0]=(v>>8)&0xFF; p[1]=v&0xFF;
}


class TICalc {
public:
    libusb_device_handle *dev = nullptr;
    int ep_in  = 0x81;
    int ep_out = 0x01;
    int timeout_ms = 5000;
    int buf_size = 250;
    const CalcModel *model = nullptr;

    void bulk_write(const std::vector<uint8_t> &data) {
        int transferred = 0;
        int r = libusb_bulk_transfer(dev, ep_out,
            const_cast<uint8_t*>(data.data()),
            (int)data.size(), &transferred, timeout_ms);
        if (r < 0)
            throw std::runtime_error(std::string("USB write: ") + libusb_strerror((libusb_error)r));
        if (transferred != (int)data.size())
            throw std::runtime_error("USB write: short transfer");
    }

    std::vector<uint8_t> bulk_read(int maxlen) {
        std::vector<uint8_t> buf(maxlen);
        int transferred = 0;
        int r = libusb_bulk_transfer(dev, ep_in, buf.data(), maxlen, &transferred, timeout_ms);
        if (r < 0)
            throw std::runtime_error(std::string("USB read: ") + libusb_strerror((libusb_error)r));
        buf.resize(transferred);
        return buf;
    }

    void send_raw(RawType type, const std::vector<uint8_t> &payload) {
        std::vector<uint8_t> pkt(5 + payload.size());
        put_be32(pkt.data(), (uint32_t)payload.size());
        pkt[4] = (uint8_t)type;
        std::copy(payload.begin(), payload.end(), pkt.begin()+5);
        bulk_write(pkt);
    }

    std::pair<uint8_t, std::vector<uint8_t>> recv_raw() {
        auto pkt = bulk_read(buf_size + 5);
        if (pkt.size() < 5)
            throw std::runtime_error("Short raw header");

        uint32_t len  = be32(pkt.data());
        uint8_t  type = pkt[4];

        std::vector<uint8_t> payload;
        if (len > 0) {
            payload.insert(payload.end(), pkt.begin() + 5, pkt.end());
            while (payload.size() < len) {
                size_t need = len - payload.size();
                auto chunk = bulk_read((int)std::min(need, (size_t)(buf_size + 5)));
                if (chunk.empty())
                    throw std::runtime_error("Short raw payload (empty read)");
                payload.insert(payload.end(), chunk.begin(), chunk.end());
            }
            payload.resize(len);
        }
        return {type, payload};
    }

    void send_ack() {
        std::vector<uint8_t> p = {0xE0, 0x00};
        send_raw(RAW_VPKT_DATA_ACK, p);
    }

    void send_vpkt(VPktType type, const std::vector<uint8_t> &data) {
        std::vector<uint8_t> vpkt(6 + data.size());
        put_be32(vpkt.data(), (uint32_t)data.size());
        put_be16(vpkt.data()+4, (uint16_t)type);
        std::copy(data.begin(), data.end(), vpkt.begin()+6);

        size_t offset = 0;
        size_t total = vpkt.size();
        while (offset < total) {
            size_t chunk = std::min((size_t)buf_size, total - offset);
            bool last = (offset + chunk >= total);
            RawType rt = last ? RAW_VPKT_DATA_LAST : RAW_VPKT_DATA_CONT;
            std::vector<uint8_t> raw(vpkt.begin()+offset, vpkt.begin()+offset+chunk);
            send_raw(rt, raw);
            auto [rtype, rpay] = recv_raw();
            if (rtype != RAW_VPKT_DATA_ACK)
                throw std::runtime_error("Expected ACK after data packet");
            offset += chunk;
        }
    }

    std::pair<VPktType, std::vector<uint8_t>> recv_vpkt() {
        std::vector<uint8_t> assembled;
        bool got_header = false;
        VPktType vpkt_type = VPKT_PING;

        while (true) {
            auto [rtype, raw] = recv_raw();

            if (rtype == RAW_VPKT_DATA_ACK) {
                continue;
            }
            if (rtype != RAW_VPKT_DATA_CONT && rtype != RAW_VPKT_DATA_LAST)
                throw std::runtime_error("Unexpected raw packet type");

            send_ack();

            assembled.insert(assembled.end(), raw.begin(), raw.end());

            if (!got_header && assembled.size() >= 6) {
                vpkt_type = (VPktType)be16(assembled.data()+4);
                got_header = true;
            }

            if (rtype == RAW_VPKT_DATA_LAST)
                break;
        }

        if (!got_header)
            throw std::runtime_error("Empty virtual packet");

        std::vector<uint8_t> data(assembled.begin()+6, assembled.end());

        if (vpkt_type == VPKT_DELAY_ACK) {
            return recv_vpkt();
        }

        if (vpkt_type == VPKT_ERROR) {
            uint16_t code = (data.size() >= 2) ? be16(data.data()) : 0;
            std::ostringstream oss;
            oss << "Calculator error 0x" << std::hex << std::setw(4) << std::setfill('0') << code;
            throw std::runtime_error(oss.str());
        }

        return {vpkt_type, data};
    }

    void negotiate_buffer() {
        std::vector<uint8_t> req = {0x00,0x00,0x04,0x00};
        send_raw(RAW_BUF_SIZE_REQ, req);
        auto [rt, rp] = recv_raw();
        if (rt != RAW_BUF_SIZE_ALLOC || rp.size() < 4)
            throw std::runtime_error("Buffer size negotiation failed");
        buf_size = (int)be32(rp.data());
        if (buf_size < 32) buf_size = 32;
        if (buf_size > 1018) buf_size = 1018;
    }

    void ping(bool normal_mode = true) {
        negotiate_buffer();
        std::vector<uint8_t> d = {
            0x00, 0x03, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x07, 0xD0
        };
        if (!normal_mode) d[1] = 0x02;
        send_vpkt(VPKT_PING, d);
        auto [vt, vd] = recv_vpkt();
        if (vt != VPKT_MODE_ACK)
            throw std::runtime_error("Ping not acknowledged");
    }

    std::vector<uint8_t> request_params(const std::vector<uint16_t> &ids) {
        std::vector<uint8_t> d;
        put_be16_append(d, (uint16_t)ids.size());
        for (auto id : ids) put_be16_append(d, id);
        send_vpkt(VPKT_PARAM_REQ, d);

        auto [vt, vd] = recv_vpkt();
        if (vt != VPKT_PARAM_DATA)
            throw std::runtime_error("Expected PARAM_DATA");
        return vd;
    }

    struct ParamVal {
        bool valid;
        std::vector<uint8_t> data;
    };

    std::vector<std::pair<uint16_t,ParamVal>> parse_params(const std::vector<uint8_t> &blob) {
        std::vector<std::pair<uint16_t,ParamVal>> out;
        if (blob.size() < 2) return out;
        uint16_t n = be16(blob.data());
        size_t off = 2;
        for (int i = 0; i < n && off+2 <= blob.size(); i++) {
            uint16_t id = be16(blob.data()+off); off += 2;
            if (off >= blob.size()) break;
            uint8_t valid = blob[off++];
            ParamVal pv;
            pv.valid = (valid == 0);
            if (pv.valid && off+2 <= blob.size()) {
                uint16_t len = be16(blob.data()+off); off += 2;
                if (off+len <= blob.size()) {
                    pv.data.assign(blob.begin()+off, blob.begin()+off+len);
                    off += len;
                }
            }
            out.push_back({id, pv});
        }
        return out;
    }

    std::string param_string(const std::vector<uint8_t> &d) {
        return std::string(d.begin(), d.end());
    }

    std::string param_version(const std::vector<uint8_t> &d) {
        if (d.size() < 4) return "?";
        std::ostringstream s;
        s << be16(d.data()) << "." << (int)d[2] << "." << (int)d[3];
        return s.str();
    }

    std::string param_int64(const std::vector<uint8_t> &d) {
        if (d.size() < 8) return "?";
        uint64_t v = 0;
        for (int i=0;i<8;i++) v=(v<<8)|d[i];
        std::ostringstream s;
        s << v;
        return s.str();
    }

    struct VarEntry {
        std::string name;
        uint32_t type_id;
        bool archived;
        uint32_t size;
    };

    std::vector<VarEntry> list_vars() {
        std::vector<uint8_t> d;
        put_be32_append(d, 3);
        put_be16_append(d, 0x0001);
        put_be16_append(d, 0x0002);
        put_be16_append(d, 0x0003);
        d.push_back(0x00); d.push_back(0x01);
        d.push_back(0x00); d.push_back(0x01);
        d.push_back(0x00); d.push_back(0x01);
        d.push_back(0x01);
        send_vpkt(VPKT_DIR_REQ, d);

        std::vector<VarEntry> vars;
        while (true) {
            auto [vt, vd] = recv_vpkt();
            if (vt == VPKT_EOT) break;
            if (vt != VPKT_VAR_HDR) continue;
            VarEntry e;
            e.type_id = 0; e.archived = false; e.size = 0;
            if (vd.size() < 2) continue;
            uint16_t namelen = be16(vd.data());
            if (2+(size_t)namelen+1 > vd.size()) continue;
            e.name = std::string((char*)vd.data()+2, namelen);
            size_t off = 2+namelen+1;
            if (off+2 > vd.size()) { vars.push_back(e); continue; }
            uint16_t nattr = be16(vd.data()+off); off+=2;
            for (int a=0; a<nattr && off+2<=vd.size(); a++) {
                uint16_t attr_id = be16(vd.data()+off); off+=2;
                if (off >= vd.size()) break;
                uint8_t attr_valid = vd[off++];
                if (attr_valid != 0) continue;
                if (off+2 > vd.size()) break;
                uint16_t attr_len = be16(vd.data()+off); off+=2;
                if (off+attr_len > vd.size()) break;
                std::vector<uint8_t> adat(vd.begin()+off, vd.begin()+off+attr_len);
                off += attr_len;
                if (attr_id == 0x0001 && adat.size()>=4) e.size    = be32(adat.data());
                if (attr_id == 0x0002 && adat.size()>=4) e.type_id = be32(adat.data());
                if (attr_id == 0x0003 && adat.size()>=1) e.archived = (adat[0] != 0);
            }
            vars.push_back(e);
        }
        return vars;
    }

    void send_var(const std::string &name, uint32_t type_id, [[maybe_unused]] bool archive,
                  const std::vector<uint8_t> &var_data) {
        std::vector<uint8_t> rts;
        put_be16_append(rts, (uint16_t)name.size());
        rts.insert(rts.end(), name.begin(), name.end());
        rts.push_back(0x00);
        rts.push_back(0x00); rts.push_back(0x00); rts.push_back(0x00);
        rts.push_back(0x09); rts.push_back(0x01);
        put_be16_append(rts, 2);
        put_be16_append(rts, 0x0001);
        put_be16_append(rts, 4);
        put_be32_append(rts, (uint32_t)var_data.size());
        put_be16_append(rts, 0x0002);
        put_be16_append(rts, 4);
        put_be32_append(rts, type_id);
        send_vpkt(VPKT_RTS, rts);

        auto [vt, vd] = recv_vpkt();
        if (vt != VPKT_DATA_ACK)
            throw std::runtime_error("RTS not acknowledged");

        if (var_data.size() % 2 != 0) {
            negotiate_buffer();
        }

        send_vpkt(VPKT_VAR_CONTENTS, var_data);
        auto [vt2, vd2] = recv_vpkt();
        if (vt2 != VPKT_DATA_ACK)
            throw std::runtime_error("VAR_CONTENTS not acknowledged");

        send_vpkt(VPKT_EOT, {});
    }

    std::vector<uint8_t> recv_var(const std::string &name, uint32_t type_id) {
        std::vector<uint8_t> req;
        put_be16_append(req, (uint16_t)name.size());
        req.insert(req.end(), name.begin(), name.end());
        req.push_back(0x00);
        req.push_back(0x01);
        req.push_back(0xFF); req.push_back(0xFF); req.push_back(0xFF); req.push_back(0xFF);
        put_be16_append(req, 3);
        put_be16_append(req, 0x0001);
        put_be16_append(req, 0x0002);
        put_be16_append(req, 0x0003);
        put_be16_append(req, 1);
        put_be16_append(req, 0x0011);
        put_be16_append(req, 4);
        put_be32_append(req, type_id);
        req.push_back(0x00); req.push_back(0x00);
        send_vpkt(VPKT_VAR_REQ, req);

        auto [vt, vd] = recv_vpkt();
        if (vt != VPKT_VAR_HDR)
            throw std::runtime_error("Expected VAR_HDR");

        auto [vt2, contents] = recv_vpkt();
        if (vt2 != VPKT_VAR_CONTENTS)
            throw std::runtime_error("Expected VAR_CONTENTS");

        return contents;
    }

    void delete_var(const std::string &name, uint8_t type_byte) {
        std::vector<uint8_t> d;
        d.push_back(0x00);

        d.push_back((uint8_t)name.size());
        d.insert(d.end(), name.begin(), name.end());
        d.push_back(0x00);

        put_be16_append(d, 1);
        put_be16_append(d, 0x0011);
        put_be16_append(d, 4);
        d.push_back(0xF0); d.push_back(0x0B); d.push_back(0x00); d.push_back(type_byte);

        d.push_back(0x01);

        d.push_back(0x00);
        d.push_back(0x00);
        put_be16_append(d, 0);

        send_vpkt(VPKT_DELETE, d);
        auto [vt, vd] = recv_vpkt();
        if (vt != VPKT_DATA_ACK)
            throw std::runtime_error("DELETE not acknowledged (unexpected response)");
    }

    void press_key(uint16_t keycode) {
        std::vector<uint8_t> d;
        d.push_back(0x00);
        d.push_back(0x00);
        d.push_back(0x03);
        d.push_back(keycode & 0xFF);
        d.push_back((keycode >> 8) & 0xFF);
        send_vpkt(VPKT_REMOTE_CTL, d);
        auto [vt, vd] = recv_vpkt();
        if (vt == VPKT_DELAY_ACK) recv_vpkt();
    }

    void set_param(uint16_t param_id, const std::vector<uint8_t> &val) {
        std::vector<uint8_t> d;
        put_be16_append(d, param_id);
        put_be16_append(d, (uint16_t)val.size());
        d.insert(d.end(), val.begin(), val.end());
        send_vpkt(VPKT_PARAM_SET, d);
        auto [vt, vd] = recv_vpkt();
        if (vt != VPKT_DATA_ACK)
            throw std::runtime_error("PARAM_SET not acknowledged");
    }

    std::vector<uint8_t> get_screenshot() {
        {
            std::vector<uint8_t> d;
            put_be16_append(d, 1);
            put_be16_append(d, 0x0022);
            send_vpkt(VPKT_PARAM_REQ, d);
        }

        auto [vt, vd] = recv_vpkt();

        if (vt == VPKT_ERROR) {
            uint16_t code = (vd.size() >= 2) ? be16(vd.data()) : 0;
            std::ostringstream oss;
            oss << "Calculator error during screenshot: 0x" << std::hex << code;
            throw std::runtime_error(oss.str());
        }
        if (vt != VPKT_PARAM_DATA)
            throw std::runtime_error("Screenshot: unexpected vpkt type");

        if (vd.size() < 8)
            throw std::runtime_error("Screenshot: response too small");
        if (be16(vd.data()) != 1 || be16(vd.data()+2) != 0x0022 || vd[4] != 0x00)
            throw std::runtime_error("Screenshot: unexpected param header");

        return std::vector<uint8_t>(vd.begin() + 7, vd.end());
    }

    void detect_endpoints() {
        libusb_config_descriptor *cfg = nullptr;
        libusb_get_active_config_descriptor(libusb_get_device(dev), &cfg);
        if (!cfg) return;
        const auto &intf = cfg->interface[0].altsetting[0];
        for (int i = 0; i < intf.bNumEndpoints; i++) {
            const auto &ep = intf.endpoint[i];
            if ((ep.bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_BULK) {
                if (ep.bEndpointAddress & 0x80)
                    ep_in  = ep.bEndpointAddress;
                else
                    ep_out = ep.bEndpointAddress;
            }
        }
        libusb_free_config_descriptor(cfg);
    }

private:
    void put_be16_append(std::vector<uint8_t> &v, uint16_t x) {
        v.push_back((x>>8)&0xFF); v.push_back(x&0xFF);
    }
    void put_be32_append(std::vector<uint8_t> &v, uint32_t x) {
        v.push_back((x>>24)&0xFF); v.push_back((x>>16)&0xFF);
        v.push_back((x>>8)&0xFF);  v.push_back(x&0xFF);
    }
};


struct TIVar {
    std::string name;
    uint8_t  type_byte;
    uint32_t type_id;
    std::vector<uint8_t> data;
};

static uint32_t type_byte_to_id(uint8_t tb) {
    return 0xF0070000 | tb;
}

static uint8_t type_id_to_byte(uint32_t id) {
    return (uint8_t)(id & 0xFF);
}

static const char* type_name(uint8_t tb) {
    switch (tb) {
        case 0x05: return "Program";
        case 0x06: return "ProtectedProg";
        case 0x15: return "AppVar";
        case 0x00: return "Real";
        case 0x01: return "RealList";
        case 0x02: return "Matrix";
        case 0x03: return "Equation";
        case 0x04: return "String";
        default:   return "Unknown";
    }
}

std::vector<TIVar> parse_ti_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (buf.size() < 55)
        throw std::runtime_error("File too small to be a TI variable file");
    const char *sig = "**TI83F*";
    if (memcmp(buf.data(), sig, 8) != 0)
        throw std::runtime_error("Not a TI-8x file (bad signature)");
    uint16_t data_size = (uint16_t)buf[53] | ((uint16_t)buf[54]<<8);
    size_t off = 55;
    size_t total_end = 55 + (size_t)data_size;
    std::vector<TIVar> vars;
    while (off+4 <= total_end && off+4 <= buf.size()) {
        off += 2;
        uint16_t data_len = (uint16_t)buf[off] | ((uint16_t)buf[off+1]<<8); off+=2;
        if (off >= buf.size()) break;
        uint8_t  type_byte = buf[off++];
        uint8_t name_bytes[8];
        memset(name_bytes, 0, 8);
        for (int i=0; i<8 && off<buf.size(); i++) name_bytes[i] = buf[off++];
        std::string name;
        for (int i=0; i<8 && name_bytes[i]; i++) name += (char)name_bytes[i];
        off += 1;
        off += 1;
        if (off+2 > buf.size()) break;
        off += 2;
        if (off+data_len > buf.size()) break;
        TIVar v;
        v.name = name;
        v.type_byte = type_byte;
        v.type_id   = type_byte_to_id(type_byte);
        v.data.assign(buf.begin()+off, buf.begin()+off+data_len);
        off += data_len;
        vars.push_back(v);
    }
    return vars;
}

void write_ti_file(const std::string &path, const TIVar &var) {
    std::vector<uint8_t> out;
    const char *sig = "**TI83F*";
    out.insert(out.end(), sig, sig+8);
    out.push_back(0x1A); out.push_back(0x0A); out.push_back(0x00);
    const char *comment = "Created by ticli                          ";
    for (int i=0; i<42; i++) out.push_back((uint8_t)comment[i]);

    std::vector<uint8_t> entry;
    entry.push_back(var.data.size() & 0xFF);
    entry.push_back((var.data.size()>>8) & 0xFF);
    entry.push_back(var.type_byte);
    for (int i=0; i<8; i++)
        entry.push_back(i < (int)var.name.size() ? (uint8_t)var.name[i] : 0x00);
    entry.push_back(0x00);
    entry.push_back(0x00);
    entry.push_back(var.data.size() & 0xFF);
    entry.push_back((var.data.size()>>8) & 0xFF);
    entry.insert(entry.end(), var.data.begin(), var.data.end());

    out.push_back(entry.size() & 0xFF);
    out.push_back((entry.size()>>8) & 0xFF);
    out.insert(out.end(), entry.begin(), entry.end());

    uint16_t checksum = 0;
    for (auto b : entry) checksum += b;
    out.push_back(checksum & 0xFF);
    out.push_back((checksum>>8) & 0xFF);

    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write file: " + path);
    f.write((char*)out.data(), out.size());
}


struct DevInfo {
    int    index;
    uint16_t pid;
    uint8_t  bus, port;
    const CalcModel *model;
};

std::vector<DevInfo> enumerate_ti_devices(libusb_context *ctx) {
    libusb_device **list = nullptr;
    ssize_t cnt = libusb_get_device_list(ctx, &list);
    std::vector<DevInfo> result;
    int idx = 0;
    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device *d = list[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(d, &desc) < 0) continue;
        if (desc.idVendor != TI_VID) continue;
        const CalcModel *m = find_model(desc.idProduct);
        if (!m) continue;
        DevInfo di;
        di.index = idx++;
        di.pid   = desc.idProduct;
        di.bus   = libusb_get_bus_number(d);
        di.port  = libusb_get_port_number(d);
        di.model = m;
        result.push_back(di);
    }
    libusb_free_device_list(list, 1);
    return result;
}

libusb_device_handle *open_device(libusb_context *ctx, int index) {
    libusb_device **list = nullptr;
    ssize_t cnt = libusb_get_device_list(ctx, &list);
    int idx = 0;
    libusb_device_handle *handle = nullptr;
    for (ssize_t i = 0; i < cnt && !handle; i++) {
        libusb_device *d = list[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(d, &desc) < 0) continue;
        if (desc.idVendor != TI_VID) continue;
        if (!find_model(desc.idProduct)) continue;
        if (idx++ == index) {
            libusb_open(d, &handle);
        }
    }
    libusb_free_device_list(list, 1);
    return handle;
}


static void print_usage() {
    fprintf(stderr,
        "Usage: ticli [OPTIONS] FLAG [FLAG-ARGS]\n"
        "\n"
        "Options:\n"
        "  -d INDEX     Select calculator by index (default: 0)\n"
        "  -t MS        USB timeout in milliseconds (default: 5000)\n"
        "  --out FILE   Output file path (used with --recv and --screenshot)\n"
        "\n"
        "Flags:\n"
        "  --list                   List connected TI calculators\n"
        "  --info                   Show calculator info and free memory\n"
        "  --ls                     List variables on calculator\n"
        "  --send FILE [FILE...]    Send .8xp/.8xv/etc. file(s) to calculator\n"
        "  --recv VARNAME           Receive a variable (saved to VARNAME.8xp or --out path)\n"
        "  --rm VARNAME             Delete a variable from the calculator\n"
        "  --key KEYNAME|CODE       Send a keypress (e.g. --key enter, --key 0x0005)\n"
        "  --disable-test           Disable exam (test) mode via dummy file transfer\n"
        "  --screenshot             Capture LCD screenshot (saved to screenshot.bin or --out path)\n"
        "\n"
        "Examples:\n"
        "  ticli --list\n"
        "  ticli --info\n"
        "  ticli --ls\n"
        "  ticli --send myprog.8xp\n"
        "  ticli --recv MYPROG\n"
        "  ticli --recv MYPROG --out myprog.8xp\n"
        "  ticli --rm OLDPROG\n"
        "  ticli --key enter\n"
        "  ticli --disable-test\n"
        "  ticli --screenshot\n"
        "  ticli --screenshot --out screen.bin\n"
        "  ticli -d 1 --info\n"
        "\n"
        "Key names: up down left right enter clear alpha 2nd mode del home quit\n"
        "           or hex code: 0x0005\n"
    );
}

static uint16_t parse_key(const std::string &s) {
    if (s == "up")     return KEY_UP;
    if (s == "down")   return KEY_DOWN;
    if (s == "left")   return KEY_LEFT;
    if (s == "right")  return KEY_RIGHT;
    if (s == "enter")  return KEY_ENTER;
    if (s == "clear")  return KEY_CLEAR;
    if (s == "alpha")  return KEY_ALPHA;
    if (s == "2nd")    return KEY_2ND;
    if (s == "mode")   return KEY_MODE;
    if (s == "del")    return KEY_DEL;
    if (s == "home")   return KEY_HOME;
    if (s == "quit")   return KEY_QUIT;
    if (s.size() > 2 && s[0]=='0' && (s[1]=='x'||s[1]=='X'))
        return (uint16_t)std::stoul(s, nullptr, 16);
    return (uint16_t)std::stoul(s, nullptr, 10);
}


static void write_png(const std::string &path, const uint8_t *rgb, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve((1 + w * 3) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0x00);
        raw.insert(raw.end(), rgb + y*w*3, rgb + y*w*3 + w*3);
    }

    uLongf zlen = compressBound(raw.size());
    std::vector<uint8_t> zdata(zlen);
    if (compress2(zdata.data(), &zlen, raw.data(), raw.size(), 6) != Z_OK)
        throw std::runtime_error("PNG: zlib compress failed");
    zdata.resize(zlen);

    auto be32b = [](uint8_t *p, uint32_t v) {
        p[0]=(v>>24)&0xFF; p[1]=(v>>16)&0xFF; p[2]=(v>>8)&0xFF; p[3]=v&0xFF;
    };
    auto chunk = [&](std::ofstream &f, const char *type, const std::vector<uint8_t> &data) {
        uint8_t hdr[4]; be32b(hdr, (uint32_t)data.size());
        f.write((char*)hdr, 4);
        f.write(type, 4);
        f.write((char*)data.data(), data.size());
        uint32_t crc = crc32(crc32(0, (const uint8_t*)type, 4), data.data(), data.size());
        uint8_t crcb[4]; be32b(crcb, crc);
        f.write((char*)crcb, 4);
    };

    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write PNG: " + path);

    const uint8_t sig[] = {137,80,78,71,13,10,26,10};
    f.write((char*)sig, 8);

    std::vector<uint8_t> ihdr(13);
    be32b(ihdr.data()+0, (uint32_t)w);
    be32b(ihdr.data()+4, (uint32_t)h);
    ihdr[8]  = 8;
    ihdr[9]  = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    chunk(f, "IHDR", ihdr);

    chunk(f, "IDAT", zdata);

    chunk(f, "IEND", {});
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(); return 1; }

    int  device_index = 0;
    int  timeout_ms   = 5000;
    std::string out_path;

    std::string action;
    std::vector<std::string> action_args;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-d") {
            if (++i >= argc) { fprintf(stderr, "-d requires a number\n"); return 1; }
            device_index = std::stoi(argv[i]);
        } else if (arg == "-t") {
            if (++i >= argc) { fprintf(stderr, "-t requires milliseconds\n"); return 1; }
            timeout_ms = std::stoi(argv[i]);
        } else if (arg == "--out") {
            if (++i >= argc) { fprintf(stderr, "--out requires a path\n"); return 1; }
            out_path = argv[i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(); return 0;
        } else if (arg == "--list" || arg == "--info" || arg == "--ls" ||
                   arg == "--send" || arg == "--recv" || arg == "--rm"  ||
                   arg == "--key"  || arg == "--disable-test" || arg == "--screenshot") {
            if (!action.empty()) {
                fprintf(stderr, "Error: multiple action flags specified (%s and %s). "
                                "Use one flag at a time.\n",
                        action.c_str(), arg.c_str());
                return 1;
            }
            action = arg;
            while (i + 1 < argc && argv[i+1][0] != '-') {
                action_args.push_back(argv[++i]);
            }
        } else {
            fprintf(stderr, "Unknown flag: %s\n", arg.c_str());
            print_usage();
            return 1;
        }
    }

    if (action.empty()) { print_usage(); return 1; }

    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) < 0) {
        fprintf(stderr, "Failed to init libusb\n");
        return 1;
    }
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);

    int ret = 0;

    try {
        if (action == "--list") {
            auto devs = enumerate_ti_devices(ctx);
            if (devs.empty()) {
                printf("No TI calculators found.\n");
                printf("(Ensure calculator is connected and in normal mode,\n");
                printf(" and you have USB access — may need udev rules or root.)\n");
            } else {
                printf("%-4s  %-28s  %-8s  %s\n", "Idx", "Model", "Family", "Bus:Port");
                printf("%-4s  %-28s  %-8s  %s\n", "---", "-----", "------", "--------");
                for (auto &d : devs)
                    printf("%-4d  %-28s  %-8s  %d:%d\n",
                        d.index, d.model->name, d.model->family,
                        d.bus, d.port);
            }
        }

        {
            libusb_device_handle *handle = open_device(ctx, device_index);
            if (!handle) {
                fprintf(stderr, "No TI calculator found at index %d.\n", device_index);
                fprintf(stderr, "Run 'ticli --list' to see connected devices.\n");
                fprintf(stderr, "You may need udev rules or to run as root.\n");
                ret = 1;
            }

            libusb_set_auto_detach_kernel_driver(handle, 1);
            if (libusb_claim_interface(handle, 0) < 0) {
                fprintf(stderr, "Failed to claim USB interface (try running as root)\n");
                libusb_close(handle);
                ret = 1;
            }

            TICalc calc;
            calc.dev = handle;
            calc.timeout_ms = timeout_ms;
            calc.detect_endpoints();

            libusb_device *dev = libusb_get_device(handle);
            libusb_device_descriptor desc;
            libusb_get_device_descriptor(dev, &desc);
            calc.model = find_model(desc.idProduct);
            if (calc.model)
                calc.buf_size = calc.model->buf_size;


            if (action == "--info") {
                calc.ping();
                static const uint16_t INFO_PIDS[] = {
                    PID_PRODUCT_NAME, PID_OS_VERSION,
                    0x000D,
                    PID_RAM_FREE,
                    PID_FLASH_FREE, PID_BATTERY
                };
                auto blob = calc.request_params(
                    std::vector<uint16_t>(INFO_PIDS, INFO_PIDS + 6));
                auto params = calc.parse_params(blob);
                printf("Calculator: %s\n", calc.model ? calc.model->name : "Unknown");
                for (auto &[id, pv] : params) {
                    if (!pv.valid) continue;
                    switch (id) {
                        case PID_PRODUCT_NAME:
                            printf("Product:    %s\n", calc.param_string(pv.data).c_str());
                            break;
                        case PID_OS_VERSION:
                            printf("OS:         %s\n", calc.param_version(pv.data).c_str());
                            break;
                        case 0x000D: {
                            uint64_t v = 0;
                            for (auto b : pv.data) v=(v<<8)|b;
                            printf("RAM total:  %llu bytes (%.1f KB)\n",
                                (unsigned long long)v, v/1024.0);
                            break;
                        }
                        case PID_RAM_FREE: {
                            uint64_t v = 0;
                            for (auto b : pv.data) v=(v<<8)|b;
                            if (v == 0 && pv.data.size() <= 4) {
                                v = 0;
                                for (int _i = (int)pv.data.size()-1; _i >= 0; _i--)
                                    v = (v << 8) | pv.data[_i];
                            }
                            printf("RAM free:   %llu bytes (%.1f KB)\n",
                                (unsigned long long)v, v/1024.0);
                            break;
                        }
                        case PID_FLASH_FREE: {
                            uint64_t v = 0;
                            for (auto b : pv.data) v=(v<<8)|b;
                            printf("Flash free: %llu bytes (%.1f KB)\n",
                                (unsigned long long)v, v/1024.0);
                            break;
                        }
                        case PID_BATTERY:
                            printf("Battery:    %s\n",
                                (pv.data[0] == 1) ? "Good" : "Low");
                            break;
                    }
                }

            } else if (action == "--ls") {
                calc.ping();
                auto vars = calc.list_vars();
                if (vars.empty()) {
                    printf("No variables found.\n");
                } else {
                    auto utf8_cols = [](const std::string &s) -> int {
                        int cols = 0;
                        size_t i = 0;
                        const unsigned char *p = (const unsigned char *)s.c_str();
                        while (i < s.size()) {
                            uint32_t cp = 0;
                            unsigned char b = p[i];
                            int bytes;
                            if      (b < 0x80) { cp = b;            bytes = 1; }
                            else if (b < 0xC0) { ++i; continue; }
                            else if (b < 0xE0) { cp = b & 0x1F;    bytes = 2; }
                            else if (b < 0xF0) { cp = b & 0x0F;    bytes = 3; }
                            else               { cp = b & 0x07;     bytes = 4; }
                            for (int k = 1; k < bytes && i+k < s.size(); ++k)
                                cp = (cp << 6) | (p[i+k] & 0x3F);
                            i += bytes;
                            int w = wcwidth((wchar_t)cp);
                            if (w > 0) cols += w;
                        }
                        return cols;
                    };
                    auto lpad = [](const std::string &s, int width) -> std::string {
                        int spaces = width - (int)s.size();
                        return spaces > 0 ? std::string(spaces, ' ') + s : s;
                    };
                    auto rpad = [&](const std::string &s, int width) -> std::string {
                        int spaces = width - utf8_cols(s);
                        return spaces > 0 ? s + std::string(spaces, ' ') : s;
                    };

                    printf("%s  %s  %s  %s\n",
                        rpad("Name", 12).c_str(),
                        lpad("Type", 16).c_str(),
                        lpad("Size", 8).c_str(),
                        "Archived");
                    printf("%s  %s  %s  %s\n",
                        rpad("----", 12).c_str(),
                        lpad("----", 16).c_str(),
                        lpad("----", 8).c_str(),
                        "--------");
                    for (auto &v : vars) {
                        uint8_t tb = type_id_to_byte(v.type_id);
                        printf("%s  %s  %s  %s\n",
                            rpad(v.name, 12).c_str(),
                            lpad(type_name(tb), 16).c_str(),
                            lpad(std::to_string(v.size), 8).c_str(),
                            v.archived ? "Yes" : "No");
                    }
                }

            } else if (action == "--send") {
                if (action_args.empty()) {
                    fprintf(stderr, "--send requires at least one file argument\n");
                    ret = 1;
                } else {
                    calc.ping();
                    for (auto &path : action_args) {
                        printf("Parsing %s ...\n", path.c_str());
                        auto vars = parse_ti_file(path);
                        if (vars.empty()) {
                            fprintf(stderr, "  No variables found in file\n");
                            continue;
                        }
                        for (auto &v : vars) {
                            printf("  Sending %s (%s, %zu bytes) ...",
                                v.name.c_str(), type_name(v.type_byte),
                                v.data.size());
                            fflush(stdout);
                            calc.send_var(v.name, v.type_id, false, v.data);
                            printf(" OK\n");
                        }
                    }
                }

            } else if (action == "--recv") {
                if (action_args.empty()) {
                    fprintf(stderr, "--recv requires a variable name\n");
                    ret = 1;
                } else {
                    std::string varname = action_args[0];
                    for (auto &c : varname) c = toupper(c);
                    std::string save_path = out_path.empty() ? (varname + ".8xp") : out_path;

                    calc.ping();

                    uint32_t type_id = type_byte_to_id(0x05);
                    bool found = false;
                    for (auto &v : calc.list_vars()) {
                        std::string vname = v.name;
                        for (auto &c : vname) c = toupper(c);
                        if (vname == varname) {
                            type_id = v.type_id;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "Warning: %s not found in directory listing; "
                                        "assuming Program type.\n", varname.c_str());
                    }

                    printf("Receiving %s ...", varname.c_str()); fflush(stdout);
                    auto data = calc.recv_var(varname, type_id);
                    printf(" %zu bytes\n", data.size());

                    TIVar v;
                    v.name = varname;
                    v.type_byte = type_id_to_byte(type_id);
                    v.type_id   = type_id;
                    v.data = data;
                    write_ti_file(save_path, v);
                    printf("Saved to %s\n", save_path.c_str());
                }

            } else if (action == "--rm") {
                if (action_args.empty()) {
                    fprintf(stderr, "--rm requires a variable name\n");
                    ret = 1;
                } else {
                    std::string varname = action_args[0];
                    for (auto &c : varname) c = toupper(c);
                    calc.ping();

                    uint8_t type_byte = 0x05;
                    bool found = false;
                    for (auto &v : calc.list_vars()) {
                        std::string vname = v.name;
                        for (auto &c : vname) c = toupper(c);
                        if (vname == varname) {
                            type_byte = type_id_to_byte(v.type_id);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "Warning: %s not found in directory listing; "
                                        "assuming Program type.\n", varname.c_str());
                    }

                    printf("Deleting %s ...", varname.c_str()); fflush(stdout);
                    calc.delete_var(varname, type_byte);
                    printf(" OK\n");
                }

            } else if (action == "--key") {
                if (action_args.empty()) {
                    fprintf(stderr, "--key requires a key name or code\n");
                    ret = 1;
                } else {
                    uint16_t kc = parse_key(action_args[0]);
                    calc.ping();
                    printf("Sending key 0x%04X ...", kc); fflush(stdout);
                    calc.press_key(kc);
                    printf(" OK\n");
                }

            } else if (action == "--disable-test") {
                calc.ping();
                printf("Sending dummy transfer to exit exam mode...\n");

                std::vector<uint8_t> dummy_body = { 0x01, 0x00, 0x00 };
                calc.send_var("EXAMRST", type_byte_to_id(0x15), false, dummy_body);
                printf("Transfer complete. Cleaning up...\n");
                calc.delete_var("EXAMRST", 0x15);
                printf("Done. Calculator should have exited exam mode.\n");

            } else if (action == "--screenshot") {
                std::string save_path = out_path.empty() ? "screenshot.png" : out_path;
                calc.ping();
                printf("Capturing LCD contents...\n");
                auto raw = calc.get_screenshot();

                int w = 320, h = 240;
                if ((int)raw.size() != w * h * 2)
                    fprintf(stderr, "Warning: expected %d bytes, got %zu\n", w*h*2, raw.size());
                std::vector<uint8_t> rgb(w * h * 3);
                for (int i = 0; i < w * h && i*2+1 < (int)raw.size(); i++) {
                    uint16_t word = raw[i*2] | (raw[i*2+1] << 8);
                    rgb[i*3+0] = ((word >> 11) & 0x1F) << 3;
                    rgb[i*3+1] = ((word >>  5) & 0x3F) << 2;
                    rgb[i*3+2] = ((word >>  0) & 0x1F) << 3;
                }

                write_png(save_path, rgb.data(), w, h);
                printf("Saved screenshot to %s\n", save_path.c_str());
            }

            libusb_release_interface(handle, 0);
            libusb_close(handle);
        }
    } catch (const std::exception &ex) {
        fprintf(stderr, "Error: %s\n", ex.what());
        ret = 1;
    }
    libusb_exit(ctx);
    return ret;
}