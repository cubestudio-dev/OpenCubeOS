#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-10-wireless_bluetooth
#
# tools/bt_test_peer.py - H4 Bluetooth controller + virtual remote device
# for the kernel-side `bt test` flow (the same harness pattern as
# paramiko_sshd.py / https_test_server.py: a host-side protocol peer).
#
# Role: QEMU's second serial port (COM2, 0x2f8) carries H4 bytes; this
# process implements
#   - the CONTROLLER half: real HCI responses (Reset, Set_Event_Mask,
#     Read_Local_Version_Information, Read_BD_ADDR, Inquiry with
#     Command_Status + Inquiry_Result + Inquiry_Complete,
#     Create_Connection with Connection_Complete, Disconnect,
#     Remote_Name, Read_RSSI, Write_Scan_Enable)
#   - the REMOTE DEVICE half over the ACL link: L2CAP signaling
#     (Connection/Config responses, Echo), an SDP server (SPP record
#     with RFCOMM server channel 3), an RFCOMM peer (SABM/UA, PN/MSC,
#     UIH echo), and a HID device (GET_REPORT responses, boot keyboard
#     reports).
#
# Usage: bt_test_peer.py <port>

import socket
import struct
import sys
import threading
import time

H4_CMD, H4_ACL, H4_SCO, H4_EVT = 0x01, 0x02, 0x03, 0x04

CTRL_BD_ADDR = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])  # HCI order (LAP first)
PEER_ADDR = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
ACL_HANDLE = 0x0001
SPP_CHANNEL = 3
LOCAL_NAME = b"OC-TEST-PEER"
RF_MTU = 512


def crc8(data: bytes) -> int:
    """RFCOMM FCS: reflected CRC-8 poly 0x07 init 0xff (Linux rfcomm)."""
    crc = 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xE0 if crc & 1 else crc >> 1
    return crc & 0xFF


def evt(code: int, params: bytes) -> bytes:
    return bytes([H4_EVT, code, len(params)]) + params


def cmd_complete(opcode: int, status: int, params: bytes) -> bytes:
    return evt(0x0E, bytes([1]) + struct.pack("<H", opcode) +
               bytes([status]) + params)


def cmd_status(opcode: int, status: int) -> bytes:
    return evt(0x0F, bytes([status, 1]) + struct.pack("<H", opcode))


def l2cap_pdu(cid: int, payload: bytes) -> bytes:
    return struct.pack("<HH", len(payload), cid) + payload


def acl_pkt(handle: int, payload: bytes) -> bytes:
    return bytes([H4_ACL]) + struct.pack("<HH", handle, len(payload)) + payload


def sig(code: int, ident: int, data: bytes) -> bytes:
    return bytes([code, ident]) + struct.pack("<H", len(data)) + data


def rfcomm_frame(dlci: int, ctrl: int, body: bytes, cr: int = 1) -> bytes:
    addr = 0x03 | ((dlci & 0x3F) << 2) | (0 if cr else 2)
    out = bytearray([addr, ctrl])
    if len(body) < 128:
        out.append(0x80 | len(body))
        fcs = crc8(bytes([addr, ctrl]))
    else:
        out.append(len(body) & 0x7F)
        out.append(len(body) >> 7)
        fcs = crc8(bytes([addr, ctrl]) + bytes([out[2], out[3]]))
    out += body
    out.append(fcs)
    return bytes(out)


class Peer:
    def __init__(self, conn: socket.socket):
        self.conn = conn
        self.lock = threading.Lock()
        self.acl_inflight = 0

    def send_evt(self, b: bytes):
        with self.lock:
            self.conn.sendall(b)

    def send_acl(self, handle: int, payload: bytes):
        with self.lock:
            self.conn.sendall(acl_pkt(handle, payload))

    def send_l2cap(self, handle: int, cid: int, payload: bytes):
        self.send_acl(handle, l2cap_pdu(cid, payload))

    # ---------------- HCI command handling ----------------
    def handle_cmd(self, pkt: bytes):
        opcode = struct.unpack("<H", pkt[0:2])[0]
        plen = pkt[2]
        params = pkt[3:3 + plen]
        ogf, ocf = opcode >> 10, opcode & 0x3FF

        if ocf == 0x0003 and ogf == 0x03:            # Reset
            self.send_evt(cmd_complete(opcode, 0, b""))
        elif ocf == 0x0001 and ogf == 0x03:          # Set_Event_Mask
            self.send_evt(cmd_complete(opcode, 0, b""))
        elif ocf == 0x001A and ogf == 0x03:          # Write_Scan_Enable
            self.send_evt(cmd_complete(opcode, 0, b""))
        elif ocf == 0x0001 and ogf == 0x04:          # Read_Local_Version
            self.send_evt(cmd_complete(opcode, 0, bytes([
                0x09,                # hci ver 5.1
                0x01, 0x00,          # hci rev
                0x09,                # lmp ver
                0x37, 0x13,          # manufacturer 0x1337 (test id)
                0x01, 0x00,          # lmp subver
            ])))
        elif ocf == 0x0009 and ogf == 0x04:          # Read_BD_ADDR
            self.send_evt(cmd_complete(opcode, 0, CTRL_BD_ADDR))
        elif ocf == 0x0001 and ogf == 0x01:          # Inquiry
            self.send_evt(cmd_status(opcode, 0))
            threading.Thread(target=self.inquiry_flow, daemon=True).start()
        elif ocf == 0x0005 and ogf == 0x01:          # Create_Connection
            self.send_evt(cmd_status(opcode, 0))
            time.sleep(0.15)
            self.send_evt(evt(0x03, bytes([0]) +      # status
                              struct.pack("<H", ACL_HANDLE) +
                              PEER_ADDR + bytes([0x00, 0x01])))
        elif ocf == 0x0006 and ogf == 0x01:          # Disconnect
            self.send_evt(cmd_status(opcode, 0))
            time.sleep(0.1)
            h = struct.unpack("<H", params[0:2])[0]
            self.send_evt(evt(0x05, bytes([0x00]) +
                              struct.pack("<H", h) + bytes([0x13])))
        elif ocf == 0x0019 and ogf == 0x01:          # Remote_Name_Request
            self.send_evt(evt(0x08, bytes([0]) + PEER_ADDR + LOCAL_NAME +
                              b"\x00" * (248 - len(LOCAL_NAME))))
        elif ocf == 0x0005 and ogf == 0x05:          # Read_RSSI
            h = struct.unpack("<H", params[0:2])[0]
            self.send_evt(cmd_complete(opcode, 0,
                                       struct.pack("<HB", h, 0xC8)))
        else:
            self.send_evt(cmd_complete(opcode, 0x01, b""))  # unknown cmd

    def inquiry_flow(self):
        time.sleep(0.2)
        # Inquiry_Result: num(1) addr(6) psrm psp psm cod(3) clock(2)
        self.send_evt(evt(0x02, bytes([0x01]) + PEER_ADDR +
                          bytes([0x01, 0x00, 0x00, 0x04, 0x04, 0x28]) +
                          struct.pack("<H", 0x0000)))
        time.sleep(0.2)
        self.send_evt(evt(0x01, bytes([0x00])))      # Inquiry_Complete

    # ---------------- ACL/L2CAP handling ----------------
    def handle_acl(self, handle: int, pdu: bytes):
        if len(pdu) < 4:
            return
        plen, cid = struct.unpack("<HH", pdu[0:4])
        payload = pdu[4:4 + plen]
        if cid == 0x0001 and payload:                # signaling
            self.handle_sig(handle, payload)
            return
        if cid in self.dcid_of_psm and payload:
            self.handle_data(self.dcid_of_psm[cid], payload)

    dcid_of_psm = {}                                  # our cid -> psm
    scid_of_psm = {}                                  # psm -> peer's cid

    def handle_sig(self, handle: int, sigp: bytes):
        code, ident = sigp[0], sigp[1]
        dlen = struct.unpack("<H", sigp[2:4])[0]
        data = sigp[4:4 + dlen]
        if code == 0x02:                             # Connection Request
            psm, scid = struct.unpack("<HH", data[0:4])
            dcid = 0x0040 + psm
            self.dcid_of_psm[dcid] = psm
            # data for this psm must go to the PEER's scid (its local cid)
            self.scid_of_psm[psm] = scid
            resp = sig(0x03, ident,
                       struct.pack("<HHHH", dcid, scid, 0, 0))
            self.send_l2cap(handle, 0x0001, resp)
            # remote-side Config Request (real flow: both sides configure)
            cfg = sig(0x04, 0x7f,
                      struct.pack("<HHH", scid, 0, 0))
            self.send_l2cap(handle, 0x0001, cfg)
        elif code == 0x04:                           # Config Request
            scid = struct.unpack("<H", data[0:2])[0]
            resp = sig(0x05, ident,
                       struct.pack("<HHHH", scid, 0, 0, 0))
            self.send_l2cap(handle, 0x0001, resp)
        elif code == 0x06:                           # Disconnection Request
            dcid, scid = struct.unpack("<HH", data[0:4])
            resp = sig(0x07, ident, struct.pack("<HH", scid, dcid))
            self.send_l2cap(handle, 0x0001, resp)
        elif code == 0x08:                           # Echo Request
            self.send_l2cap(handle, 0x0001, sig(0x09, ident, data))

    # ---------------- profile data ----------------
    def send_to_psm(self, psm: int, payload: bytes):
        """Data to the remote end of the psm's channel = its scid."""
        cid = self.scid_of_psm.get(psm)
        if cid is None:
            return
        self.send_l2cap(ACL_HANDLE, cid, payload)

    def handle_data(self, psm: int, payload: bytes):
        if psm == 0x0001:                            # SDP
            self.handle_sdp(payload)
        elif psm == 0x0003:                          # RFCOMM
            self.handle_rfcomm(payload)
        elif psm in (0x0011, 0x0013):                # HID control/intr
            self.handle_hid(psm, payload)

    def handle_sdp(self, p: bytes):
        if not p:
            return
        pdu, tid = p[0], struct.unpack(">H", p[1:3])[0]
        if pdu == 0x02:                              # ServiceSearchRequest
            body = struct.pack(">HH", 1, 1) + struct.pack(">I", 0x00010001) + \
                b"\x00"
            self.send_to_psm(0x0001,
                             bytes([0x03]) + struct.pack(">H", tid) +
                             struct.pack(">H", len(body)) + body)
        elif pdu == 0x04:                            # ServiceAttributeRequest
            # attr list: DES { attr 0x0004: DES { DES{UUID16 L2CAP},
            #             DES{UUID16 RFCOMM, u8 channel} } }
            proto = bytes([0x35, 0x03, 0x19, 0x01, 0x00]) + \
                bytes([0x35, 0x05, 0x19, 0x00, 0x03, 0x08, SPP_CHANNEL])
            # outer DES content = attr-id element (3 B) + value element
            # (2 B header + proto) = 3 + 2 + len(proto)
            attrlist = bytes([0x35, 3 + 2 + len(proto), 0x09, 0x00, 0x04,
                              0x35, len(proto)]) + proto
            body = struct.pack(">H", len(attrlist)) + attrlist + b"\x00"
            self.send_to_psm(0x0001,
                             bytes([0x05]) + struct.pack(">H", tid) +
                             struct.pack(">H", len(body)) + body)

    def handle_rfcomm(self, f: bytes):
        if len(f) < 4:
            return
        addr, ctrl = f[0], f[1]
        dlci = (addr >> 2) & 0x3F
        if f[2] & 0x80:
            flen = f[2] & 0x7F
            body = f[3:3 + flen]
        else:
            flen = f[2] | (f[3] << 7)
            body = f[4:4 + flen]
        kind = ctrl & 0xEF
        if kind == 0x2F:                             # SABM
            self.send_to_psm(0x0003, rfcomm_frame(dlci, 0x63, b"", cr=0))
        elif kind == 0x43:                           # DISC
            self.send_to_psm(0x0003, rfcomm_frame(dlci, 0x0F, b"", cr=0))
        elif kind == 0xEF:                           # UIH
            if dlci == 0 and len(body) >= 1 and (body[0] & 0x02):
                # MCC on the control channel (PN/MSC): respond MSC resp
                mt = body[0]
                if mt == 0xE3 and len(body) >= 3:    # MSC cmd
                    resp = bytes([0xE1]) + body[1:3]
                    self.send_to_psm(0x0003,
                                     rfcomm_frame(0, 0xEF | 0x10, resp))
                elif mt == 0x83:                     # PN cmd -> PN resp
                    resp = bytes([0x81]) + body[1:]
                    self.send_to_psm(0x0003,
                                     rfcomm_frame(0, 0xEF | 0x10, resp))
            elif dlci == (SPP_CHANNEL << 1) | 1 and body:
                # SPP echo: send the same bytes back on the same DLCI
                self.send_to_psm(0x0003,
                                 rfcomm_frame(dlci, 0xEF | 0x10, body))

    def handle_hid(self, psm: int, p: bytes):
        if psm == 0x0011 and len(p) >= 2 and (p[0] & 0xF0) == 0x40:
            # GET_REPORT -> DATA|INPUT + boot keyboard report
            rep = bytes([0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00])
            self.send_to_psm(0x0011, bytes([0xA1]) + rep)
        elif psm == 0x0013:
            pass

    # ---------------- main loop ----------------
    def run(self):
        buf = b""
        try:
            while True:
                chunk = self.conn.recv(4096)
                if not chunk:
                    break
                buf += chunk
                while True:
                    if not buf:
                        break
                    t = buf[0]
                    if t == H4_CMD:
                        if len(buf) < 4:
                            break
                        plen = buf[3]
                        if len(buf) < 4 + plen:
                            break
                        self.handle_cmd(buf[1:4 + plen])
                        buf = buf[4 + plen:]
                    elif t == H4_ACL:
                        if len(buf) < 5:
                            break
                        dlen = struct.unpack("<H", buf[3:5])[0]
                        if len(buf) < 5 + dlen:
                            break
                        hf = struct.unpack("<H", buf[1:3])[0]
                        self.handle_acl(hf & 0xFFF, buf[5:5 + dlen])
                        buf = buf[5 + dlen:]
                    else:
                        buf = buf[1:]
        except OSError as e:
            print(f"[bt_peer] run loop ended: {type(e).__name__}: {e}",
                  flush=True)
        finally:
            try:
                self.conn.close()
            except OSError:
                pass


def main():
    port = int(sys.argv[1])
    # QEMU's chardev socket owns the listener (server=on); this peer is
    # the CLIENT connecting to it (retry until QEMU is up).
    for attempt in range(100):
        try:
            conn = socket.create_connection(("127.0.0.1", port), timeout=2)
            break
        except OSError:
            time.sleep(0.2)
    else:
        print("[bt_peer] could not connect to QEMU chardev", flush=True)
        sys.exit(1)
    # create_connection(timeout=2) leaves a 2 s socket timeout behind;
    # the peer runs blocking, so clear it before the run loop.
    conn.settimeout(None)
    print(f"[bt_peer] h4 uart connected (chardev 127.0.0.1:{port})",
          flush=True)
    # link probe: an unsolicited vendor-event to test QEMU->guest RX;
    # unknown events are ignored by the kernel's event dispatcher.
    conn.sendall(bytes([0x04, 0xff, 0x02, 0xde, 0xad]))
    Peer(conn).run()
    print("[bt_peer] peer disconnected", flush=True)


if __name__ == "__main__":
    main()
