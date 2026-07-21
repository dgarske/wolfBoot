#!/usr/bin/env python3
# unit-sign-custom-tlv-large.py
#
# Coverage for large custom TLVs in the C sign tool (tools/keytools/sign.c).
#
# Custom TLV values passed with --custom-tlv-buffer and --custom-tlv-string
# were historically capped at 255 bytes.  The cap is now the TLV wire-format
# limit of 65535 bytes (16-bit length field), --custom-tlv-file loads a value
# from a raw binary file, and make_header_ex() grows the manifest header to
# the next power of two when the custom TLVs do not fit.  The delta signing
# path (base_diff()) pre-computes the same growth before capturing
# patch_inv_off, so HDR_IMG_DELTA_INVERSE matches the header actually written.
#
# This test drives the C sign binary and asserts:
#   - a >255-byte --custom-tlv-buffer, --custom-tlv-string and
#     --custom-tlv-file value each land byte-exact in the manifest header,
#     parsed the same way wolfBoot_find_header() walks it
#   - the header grows to a power of two and the firmware payload starts
#     exactly at the grown header size (header size derived from
#     filesize - payload size, not from the tool's stdout)
#   - the 65535-byte format maximum is accepted via --custom-tlv-file
#   - a 65536-byte file, a 65536-char string, an empty file and a missing
#     file are all rejected without producing a signed image
#   - a delta image signed with a large custom TLV keeps
#     HDR_IMG_DELTA_INVERSE consistent with the grown header: the inverse
#     patch must be the trailing HDR_IMG_DELTA_INVERSE_SIZE bytes of the
#     file, which fails if base_diff() sizes the header differently from
#     make_header_ex()
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

import os
import struct
import subprocess
import sys
import tempfile

HDR_PADDING = 0xFF
HDR_IMG_DELTA_SIZE         = 0x06
HDR_IMG_DELTA_INVERSE      = 0x15
HDR_IMG_DELTA_INVERSE_SIZE = 0x16

TAG_BUFFER = 0x0030
TAG_STRING = 0x0031
TAG_FILE   = 0x0032

SECTOR_SIZE = 0x1000

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(THIS_DIR, "..", ".."))
SIGN = os.path.join(ROOT, "tools", "keytools", "sign")

failures = []


def skip(msg):
    print("SKIP unit-sign-custom-tlv-large: " + msg)
    sys.exit(0)


def fail(msg):
    failures.append(msg)


def parse_tlvs(data, scan_end):
    """Walk the header like wolfBoot_find_header(): {tag: value bytes}."""
    tlvs = {}
    p = 8  # skip 4-byte magic + 4-byte image size
    while p + 4 <= scan_end:
        htype = data[p] | (data[p + 1] << 8)
        if htype == 0:
            break
        if data[p] == HDR_PADDING or (p & 1) != 0:
            p += 1
            continue
        length = data[p + 2] | (data[p + 3] << 8)
        if p + 4 + length > scan_end:
            break
        if htype not in tlvs:
            tlvs[htype] = bytes(data[p + 4:p + 4 + length])
        p += 4 + length
    return tlvs


def tlv_u32(tlvs, tag):
    val = tlvs.get(tag)
    if val is None or len(val) != 4:
        return None
    return struct.unpack("<I", val)[0]


def ensure_sign():
    if os.path.exists(SIGN):
        return True
    try:
        subprocess.run(["make", "sign"],
                       cwd=os.path.join(ROOT, "tools", "keytools"),
                       check=True, capture_output=True, text=True)
    except (subprocess.CalledProcessError, OSError):
        return False
    return os.path.exists(SIGN)


def make_ed25519_key(path):
    """Write a 64-byte raw ed25519 key (seed + public) as expected by sign."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import \
            Ed25519PrivateKey
        from cryptography.hazmat.primitives import serialization
    except Exception:
        return False
    seed = b"\x42" * 32
    sk = Ed25519PrivateKey.from_private_bytes(seed)
    pub = sk.public_key().public_bytes(serialization.Encoding.Raw,
                                        serialization.PublicFormat.Raw)
    with open(path, "wb") as f:
        f.write(seed + pub)
    return True


def run_sign(args, image, key, version, env=None):
    cmd = [SIGN, "--ed25519", "--sha256"] + args + [image, key, version]
    run_env = dict(os.environ)
    if env:
        run_env.update(env)
    return subprocess.run(cmd, cwd=ROOT, env=run_env,
                          capture_output=True, text=True)


def signed_name(image, version):
    return image.replace(".bin", "_v%s_signed.bin" % version)


def make_image(path, payload):
    with open(path, "wb") as f:
        f.write(payload)


def check_signed_layout(name, signed, payload):
    """Return (data, header_size) or None; header must be a power of two
    holding the payload right after it."""
    with open(signed, "rb") as f:
        data = f.read()
    hdr = len(data) - len(payload)
    if hdr <= 0 or (hdr & (hdr - 1)) != 0:
        fail("%s: file size %d - payload %d = %d is not a power-of-two "
             "header" % (name, len(data), len(payload), hdr))
        return None
    if data[hdr:] != payload:
        fail("%s: payload is not stored at header size %d" % (name, hdr))
        return None
    if struct.unpack("<I", data[4:8])[0] != len(payload):
        fail("%s: image size field does not match payload size" % name)
        return None
    return data, hdr


def check_value(name, tlvs, tag, expected):
    got = tlvs.get(tag)
    if got is None:
        fail("%s: tag 0x%04x not found in header" % (name, tag))
    elif got != expected:
        fail("%s: tag 0x%04x value mismatch (%d bytes, want %d)" %
             (name, tag, len(got), len(expected)))


def expect_reject(name, args, image, key, version, needle):
    signed = signed_name(image, version)
    if os.path.exists(signed):
        os.unlink(signed)
    r = run_sign(args, image, key, version)
    if r.returncode == 0:
        fail("%s: sign succeeded, expected rejection" % name)
        return
    if needle not in r.stderr:
        fail("%s: expected '%s' in stderr, got: %s" %
             (name, needle, r.stderr.strip()[:200]))
    if os.path.exists(signed):
        fail("%s: rejected sign still produced an output image" % name)


def main():
    if not ensure_sign():
        skip("could not build tools/keytools/sign")

    with tempfile.TemporaryDirectory() as work:
        key = os.path.join(work, "priv.der")
        if not make_ed25519_key(key):
            skip("python cryptography module not available")

        payload = bytes((i * 7) & 0xFF for i in range(2048))

        # Control: a plain sign must work, or the environment is broken and
        # every other failure below would be misleading.
        control = os.path.join(work, "control.bin")
        make_image(control, payload)
        r = run_sign([], control, key, "1")
        if r.returncode != 0 or not os.path.exists(signed_name(control, "1")):
            skip("control sign failed: " + r.stderr.strip())

        # Large buffer, string and file TLVs in one image.
        buf_val = bytes((i * 13 + 5) & 0xFF for i in range(1000))
        str_val = "".join(chr(65 + (i % 26)) for i in range(300))
        file_val = bytes((i * 31 + 7) & 0xFF for i in range(40000))
        tlv_file = os.path.join(work, "tlv.bin")
        with open(tlv_file, "wb") as f:
            f.write(file_val)

        image = os.path.join(work, "large.bin")
        make_image(image, payload)
        r = run_sign(["--custom-tlv-buffer", hex(TAG_BUFFER), buf_val.hex(),
                      "--custom-tlv-string", hex(TAG_STRING), str_val,
                      "--custom-tlv-file", hex(TAG_FILE), tlv_file],
                     image, key, "1")
        if r.returncode != 0:
            fail("large: sign failed: " + r.stderr.strip()[:200])
        else:
            res = check_signed_layout("large", signed_name(image, "1"),
                                      payload)
            if res:
                data, hdr = res
                if hdr <= 256:
                    fail("large: header did not grow (size %d)" % hdr)
                tlvs = parse_tlvs(data, hdr)
                check_value("large", tlvs, TAG_BUFFER, buf_val)
                check_value("large", tlvs, TAG_STRING,
                            str_val.encode("ascii"))
                check_value("large", tlvs, TAG_FILE, file_val)

        # Format maximum: 65535 bytes via file.
        max_val = bytes((i * 3 + 1) & 0xFF for i in range(65535))
        max_file = os.path.join(work, "max.bin")
        with open(max_file, "wb") as f:
            f.write(max_val)
        image = os.path.join(work, "max_img.bin")
        make_image(image, payload)
        r = run_sign(["--custom-tlv-file", hex(TAG_FILE), max_file],
                     image, key, "1")
        if r.returncode != 0:
            fail("max: sign failed for 65535-byte TLV: " +
                 r.stderr.strip()[:200])
        else:
            res = check_signed_layout("max", signed_name(image, "1"), payload)
            if res:
                data, hdr = res
                check_value("max", parse_tlvs(data, hdr), TAG_FILE, max_val)

        # Rejections.
        image = os.path.join(work, "rej.bin")
        make_image(image, payload)

        over_file = os.path.join(work, "over.bin")
        with open(over_file, "wb") as f:
            f.write(b"\x00" * 65536)
        expect_reject("reject-file-65536",
                      ["--custom-tlv-file", hex(TAG_FILE), over_file],
                      image, key, "1", "too big")

        expect_reject("reject-string-65536",
                      ["--custom-tlv-string", hex(TAG_STRING), "X" * 65536],
                      image, key, "1", "too big")

        empty_file = os.path.join(work, "empty.bin")
        open(empty_file, "wb").close()
        expect_reject("reject-empty-file",
                      ["--custom-tlv-file", hex(TAG_FILE), empty_file],
                      image, key, "1", "empty")

        expect_reject("reject-missing-file",
                      ["--custom-tlv-file", hex(TAG_FILE),
                       os.path.join(work, "does-not-exist.bin")],
                      image, key, "1", "Cannot open")

        # Delta: a large custom TLV must not desync HDR_IMG_DELTA_INVERSE
        # from the grown header.
        env = {"WOLFBOOT_SECTOR_SIZE": str(SECTOR_SIZE)}
        base = os.path.join(work, "delta.bin")
        make_image(base, payload)
        r = run_sign([], base, key, "1", env=env)
        if r.returncode != 0:
            fail("delta: base sign failed: " + r.stderr.strip()[:200])
        else:
            upd = os.path.join(work, "delta2.bin")
            make_image(upd, payload[:512] + b"PATCHED!" + payload[520:])
            r = run_sign(["--delta", signed_name(base, "1"),
                          "--custom-tlv-file", hex(TAG_FILE), tlv_file],
                         upd, key, "2", env=env)
            diff = upd.replace(".bin", "_v2_signed_diff.bin")
            if r.returncode != 0 or not os.path.exists(diff):
                fail("delta: delta sign failed: " + r.stderr.strip()[:200])
            else:
                # The full v2 image from the same run reveals the header
                # size the tool settled on.
                res = check_signed_layout("delta-full",
                                          signed_name(upd, "2"), payload[:512]
                                          + b"PATCHED!" + payload[520:])
                if res:
                    hdr = res[1]
                    with open(diff, "rb") as f:
                        ddata = f.read()
                    tlvs = parse_tlvs(ddata, hdr)
                    check_value("delta-diff", tlvs, TAG_FILE, file_val)
                    inv_off = tlv_u32(tlvs, HDR_IMG_DELTA_INVERSE)
                    inv_sz = tlv_u32(tlvs, HDR_IMG_DELTA_INVERSE_SIZE)
                    fwd_sz = tlv_u32(tlvs, HDR_IMG_DELTA_SIZE)
                    if inv_off is None or inv_sz is None or fwd_sz is None:
                        fail("delta-diff: delta TLVs missing from header")
                    else:
                        if inv_off + inv_sz != len(ddata):
                            fail("delta-diff: HDR_IMG_DELTA_INVERSE=%d + "
                                 "size=%d != filesize %d (stale header "
                                 "size in patch_inv_off)" %
                                 (inv_off, inv_sz, len(ddata)))
                        if inv_off < hdr + fwd_sz:
                            fail("delta-diff: inverse patch at %d overlaps "
                                 "forward patch (header %d + %d)" %
                                 (inv_off, hdr, fwd_sz))

        # Boundary delta: size the TLV so the non-delta header fits in a
        # power of two but the extra delta TLVs push past it.  The full v2
        # image is signed first, so base_diff() starts from the non-delta
        # header size and must pre-grow it before capturing patch_inv_off;
        # without the pre-grow, HDR_IMG_DELTA_INVERSE is stale by a full
        # header step and no longer points at the trailing inverse patch.
        bnd_val = bytes((i * 11 + 3) & 0xFF for i in range(800))
        bnd_file = os.path.join(work, "bnd.bin")
        with open(bnd_file, "wb") as f:
            f.write(bnd_val)
        base = os.path.join(work, "bdelta.bin")
        make_image(base, payload)
        r = run_sign([], base, key, "1", env=env)
        if r.returncode != 0:
            fail("bdelta: base sign failed: " + r.stderr.strip()[:200])
        else:
            upd = os.path.join(work, "bdelta2.bin")
            make_image(upd, payload[:512] + b"PATCHED!" + payload[520:])
            r = run_sign(["--delta", signed_name(base, "1"),
                          "--custom-tlv-file", hex(TAG_FILE), bnd_file],
                         upd, key, "2", env=env)
            diff = upd.replace(".bin", "_v2_signed_diff.bin")
            if r.returncode != 0 or not os.path.exists(diff):
                fail("bdelta: delta sign failed: " + r.stderr.strip()[:200])
            else:
                res = check_signed_layout("bdelta-full",
                                          signed_name(upd, "2"), payload[:512]
                                          + b"PATCHED!" + payload[520:])
                if res:
                    h_full = res[1]
                    with open(diff, "rb") as f:
                        ddata = f.read()
                    tlvs = parse_tlvs(ddata,
                                      min(4 * h_full, len(ddata)))
                    check_value("bdelta-diff", tlvs, TAG_FILE, bnd_val)
                    inv_off = tlv_u32(tlvs, HDR_IMG_DELTA_INVERSE)
                    inv_sz = tlv_u32(tlvs, HDR_IMG_DELTA_INVERSE_SIZE)
                    if inv_off is None or inv_sz is None:
                        fail("bdelta-diff: delta TLVs missing from header")
                    else:
                        if inv_off < 2 * h_full:
                            fail("bdelta-diff: test setup no longer "
                                 "straddles a header size boundary "
                                 "(inv_off=%d, full header=%d); re-tune "
                                 "the 800-byte TLV size" % (inv_off, h_full))
                        if inv_off + inv_sz != len(ddata):
                            fail("bdelta-diff: HDR_IMG_DELTA_INVERSE=%d + "
                                 "size=%d != filesize %d (stale header "
                                 "size in patch_inv_off)" %
                                 (inv_off, inv_sz, len(ddata)))

    if failures:
        for msg in failures:
            print("FAIL unit-sign-custom-tlv-large: " + msg)
        sys.exit(1)

    print("unit-sign-custom-tlv-large: OK")
    sys.exit(0)


if __name__ == "__main__":
    main()
