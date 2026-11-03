#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) 2026 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""
Crafts ambiguous or inconsistent hap/hsp-shaped packages for the linker tests.

Variants:
  ambiguous        - two local entries under the entry name, the central directory
                     indexes only the second one (an earlier same-name local header
                     must fail the consistency traversal).
  duplicate        - two local entries under the entry name with distinguishable
                     payloads, the central directory keeps both records; the
                     historical reader selected the last record.
  descriptor       - an unrelated first local entry with the data-descriptor flag
                     stops the traversal before the target entry.
  corrupt          - the first local header carries a wrong signature.
  malformed        - truncated before the EOCD (no EOCD at all).
  corrupt_central  - intact EOCD, the first central-directory entry signature is
                     corrupted.
  oversized_extra  - intact EOCD and signatures, the central header's extraSize
                     declares a length exceeding the directory boundary.
  native_only      - a valid archive without any abc entry.
"""

import struct
import sys
import zlib

LOCAL_HEADER_SIGNATURE = 0x04034B50
CENTRAL_HEADER_SIGNATURE = 0x04034B50
CENTRAL_DIR_SIGNATURE = 0x02014B50
EOCD_SIGNATURE = 0x06054B50
FLAG_DATA_DESC = 0x0008


def local_part(name, data, flags, corrupt):
    signature = LOCAL_HEADER_SIGNATURE + 1 if corrupt else LOCAL_HEADER_SIGNATURE
    return (struct.pack('<IHHHHHIIIHH', signature, 20, flags, 0, 0, 0,
                        zlib.crc32(data) & 0xFFFFFFFF, len(data), len(data),
                        len(name), 0) + name + data)


def central_part(name, data, local_offset, corrupt_signature=False, oversized_extra=False):
    signature = CENTRAL_DIR_SIGNATURE + 1 if corrupt_signature else CENTRAL_DIR_SIGNATURE
    extra_size = 0xFFFF if oversized_extra else 0
    return (struct.pack('<IHHHHHHIIIHHHHHII', signature, 20, 20,
                        0, 0, 0, 0, zlib.crc32(data) & 0xFFFFFFFF, len(data),
                        len(data), len(name), extra_size, 0, 0, 0, 0, local_offset) + name)


def make_zip(entries):
    local_blob = b''
    central_blob = b''
    for name, data, flags, corrupt, indexed, bad_sig, big_extra in entries:
        if indexed:
            central_blob += central_part(name, data, len(local_blob), bad_sig, big_extra)
        local_blob += local_part(name, data, flags, corrupt)
    eocd = struct.pack('<IHHHHIIH', EOCD_SIGNATURE, 0, 0, len(entries), len(entries),
                       len(central_blob), len(local_blob), 0)
    return local_blob + central_blob + eocd


def main():
    variant, entry_name, first_path, last_path, out_path = sys.argv[1:6]
    with open(first_path, 'rb') as f:
        first = f.read()
    with open(last_path, 'rb') as f:
        last = f.read()

    if variant == 'ambiguous':
        entries = [(entry_name.encode(), first, 0, False, False, False, False),
                   (entry_name.encode(), last, 0, False, True, False, False)]
    elif variant == 'duplicate':
        entries = [(entry_name.encode(), first, 0, False, True, False, False),
                   (entry_name.encode(), last, 0, False, True, False, False)]
    elif variant == 'descriptor':
        entries = [(b'dd.bin', last, FLAG_DATA_DESC, False, True, False, False),
                   (entry_name.encode(), first, 0, False, True, False, False)]
    elif variant == 'corrupt':
        entries = [(b'bad.bin', last, 0, True, True, False, False),
                   (entry_name.encode(), first, 0, False, True, False, False)]
    elif variant == 'malformed':
        # Write a valid zip then truncate before the EOCD
        full = make_zip([(b'x.bin', b'x', 0, False, True, False, False)])
        with open(out_path, 'wb') as f:
            f.write(full[:-22])
        return
    elif variant == 'corrupt_central':
        # Intact EOCD, first central entry signature corrupted
        entries = [(b'x.bin', b'x', 0, False, True, True, False),
                   (entry_name.encode(), first, 0, False, True, False, False)]
    elif variant == 'oversized_extra':
        # Intact EOCD and signatures, extraSize exceeds the directory boundary
        entries = [(b'x.bin', b'x', 0, False, True, False, True),
                   (entry_name.encode(), first, 0, False, True, False, False)]
    elif variant == 'native_only':
        # Valid archive, no abc entry at all
        entries = [(b'lib.so', b'\x7fELF', 0, False, True, False, False)]
    else:
        raise ValueError("unknown variant: " + variant)

    with open(out_path, 'wb') as f:
        f.write(make_zip(entries))


if __name__ == "__main__":
    main()
