#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP05-ocbl
# File: ocbl/elf_to_efi.py
# Purpose: convert a freestanding x86-64 ELF into a UEFI PE/COFF
#          application, generating the COMPLETE base-relocation
#          directory from the ELF's emitted relocations.
#
#          Why this exists (B5-3 live hunt, found the hard way): the
#          plain `ld -m i386pep` link produced a PARTIAL .reloc
#          directory - 228 entries covering only the front of .text,
#          while later movabs instructions (the GUID argument to
#          HandleProtocol among them) carried no relocation at all.
#          OVMF loads the image far above ImageBase, so every
#          unrelocated absolute address kept its link-time value:
#          the BlockIo GUID read garbage memory, HandleProtocol
#          happily matched a *different* protocol (the FAT volume's
#          SimpleFileSystem - whose Revision 0x00010000 made the
#          wrong interface look plausible), and calling its
#          "ReadBlocks" landed on FAT code -> #UD.  The whole failure
#          chain was serial-visible only as misleading struct garbage.
#
#          The converter does the classic gnu-efi job, self-made:
#            link with `ld -m elf_x86_64 --emit-relocs` (keeps every
#            R_X86_64_64 in the ELF), then map the loadable sections
#            to two PE sections (.text RX, .data RW), apply nothing
#          (the ELF image already carries link-time VMA values), and
#            emit IMAGE_REL_BASED_DIR64 entries for every absolute
#            64-bit reference - nothing left to chance.
#
# Usage: elf_to_efi.py <in.elf> <out.efi> <entry_symbol>
import struct
import sys

IMAGE_BASE = 0x10000
SEC_ALIGN = 0x1000          # SectionAlignment
FILE_ALIGN = 0x200          # FileAlignment


def main():
    elf_path, out_path, entry_sym = sys.argv[1], sys.argv[2], sys.argv[3]
    d = open(elf_path, 'rb').read()
    assert d[:4] == b'\x7fELF' and d[4] == 2 and d[5] == 1, 'want ELF64 LE'

    e_shoff, = struct.unpack_from('<Q', d, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', d, 0x3a)
    shs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, typ, flags, addr, offset, size, link, info, align, entsz = \
            struct.unpack_from('<IIQQQQIIQQ', d, off)
        shs.append(dict(name=name, typ=typ, flags=flags, addr=addr,
                        offset=offset, size=size, link=link, info=info,
                        align=align, entsz=entsz))
    shstr = shs[e_shstrndx]

    def sname(s):
        b = d[shstr['offset'] + s['name']:]
        return b[:b.index(b'\0')].decode()

    for s in shs:
        s['nm'] = sname(s)

    # ---- entry point from the symbol table ----
    entry_rva = None
    for s in shs:
        if s['typ'] == 2:  # SYMTAB
            n = s['size'] // 24
            for i in range(n):
                off = s['offset'] + i * 24
                nmidx, info, other, shndx, value, _sz = struct.unpack_from(
                    '<IBBHQQ', d, off)
                strtab = shs[s['link']]
                b = d[strtab['offset'] + nmidx:]
                nm = b[:b.index(b'\0')].decode()
                if nm == entry_sym:
                    entry_rva = value        # ELF VMA, mapped later
        if entry_rva is not None:
            break
    assert entry_rva is not None, f'entry symbol {entry_sym} not found'
    entry_elf_vma = entry_rva            # keep the VMA before mapping

    # ---- section placement: the PE MIRRORS the ELF layout ----
    # RVA = ELF VMA - IMAGE_BASE for every section: no re-placement,
    # no alignment surprises.  (B5-3 final architecture: an earlier
    # draft re-packed the sections and the compiler's constant-folded
    # address DIFFERENCES - e.g. movabs $0xfffffffffffb4360,%rcx as
    # (fixed_low_buffer - rodata_symbol) - silently broke, because one
    # side of the difference moved with the image and the other did
    # not.  Keeping the layout identical keeps every difference true.)
    RX_NAMES = ('.text', '.rodata')
    RW_NAMES = ('.data',)

    def is_rx(nm):
        return nm == '.text' or nm.startswith('.text.') or \
            nm == '.rodata' or nm.startswith('.rodata.')

    def is_rw(nm):
        return nm == '.data' or nm.startswith('.data.')

    rx_secs, rw_secs = [], []
    for s in shs:
        if s['typ'] != 1 or s['size'] == 0:
            continue
        if s['nm'] in ('.comment', '.note.gnu.property', '.note',
                       '.eh_frame', '.eh_frame_hdr'):
            continue
        if is_rx(s['nm']):
            rx_secs.append(s)
        elif is_rw(s['nm']):
            rw_secs.append(s)
    rx_secs.sort(key=lambda s: s['addr'])
    rw_secs.sort(key=lambda s: s['addr'])

    def secs_rva(secs):
        lo = min((s['addr'] for s in secs), default=None)
        hi = max((s['addr'] + s['size'] for s in secs), default=None)
        return (lo, hi)

    text_lo, text_hi = secs_rva(rx_secs)
    data_lo, data_hi = secs_rva(rw_secs)
    assert text_lo is not None, 'no code sections'
    text_rva = text_lo - IMAGE_BASE
    text_vsize = text_hi - text_lo
    data_rva = (data_lo - IMAGE_BASE) if data_lo is not None else 0
    data_vsize = (data_hi - data_lo) if data_lo is not None else 0
    bss_size = 0
    bss_addr = None
    for s in shs:
        if s['typ'] == 8 and s['nm'] == '.bss':
            bss_size = max(bss_size, s['size'])
            bss_addr = s['addr'] if bss_addr is None else min(bss_addr, s['addr'])
    if bss_addr is not None and bss_size:
        # the .bss range rides inside the PE .data section's
        # raw-vs-virtual slack at its ORIGINAL VMA position
        bss_end_rva = bss_addr + bss_size - IMAGE_BASE
        data_rva = (data_rva or (bss_addr - IMAGE_BASE))
        data_vsize = max(data_vsize, bss_end_rva - data_rva)
    total_img = max((data_rva + data_vsize) if data_rva else 0,
                    text_rva + text_vsize)

    # ---- apply relocations: nothing to apply in the IMAGE (the ELF
    # file already stores link-time VMA values in the instruction
    # streams); we only need the LIST of absolute 64-bit spots ----
    reloc_vas = set()
    # symbol values (for the operand-rewrite pass below)
    symvals = [0]
    for s in shs:
        if s['typ'] == 2:  # SYMTAB
            n = s['size'] // 24
            symvals = [0] * n
            for i in range(n):
                off = s['offset'] + i * 24
                _, _, _, _, value, _sz2 = struct.unpack_from(
                    '<IBBHQQ', d, off)
                symvals[i] = value
            break
    elf_lo = IMAGE_BASE + text_rva
    elf_hi = IMAGE_BASE + total_img

    # identity mapping: RVA = VMA - IMAGE_BASE (the PE mirrors the
    # ELF layout since this revision)
    def vma2rva(va):
        rva = va - IMAGE_BASE
        if rva < text_rva or rva >= total_img:
            return None
        return rva

    # the entry symbol's VMA must travel through the same mapping
    entry_rva = vma2rva(entry_elf_vma)
    assert entry_rva is not None, 'entry outside mapped sections'

    # ---- RELA pass: rewrite every absolute 64-bit operand ----
    # The ELF's tight VMA layout differs from the PE's page-granular
    # RVA layout, so the linker-time operand values (ELF VMAs) are
    # WRONG for the PE image.  Rewrite each operand to
    # IMAGE_BASE + vma2rva(value); OVMF then adds its load delta on
    # top.  Non-address constants (out-of-image values, e.g. negative
    # magic numbers) are skipped: no rewrite, no directory entry.
    R_X86_64_64 = 1
    rewrites = []          # (operand RVA, new 8-byte value)
    reloc_rvas = set()
    for s in shs:
        if s['typ'] != 4:
            continue
        target = shs[s['info']]
        if target['nm'] not in ('.text', '.rodata', '.data') and \
                not target['nm'].startswith(('.text.', '.rodata.',
                                             '.data.')):
            continue
        n = s['size'] // 24
        for i in range(n):
            off = s['offset'] + i * 24
            r_off, r_info, r_add = struct.unpack_from('<QQq', d, off)
            if (r_info & 0xFFFFFFFF) != R_X86_64_64:
                continue
            symi = r_info >> 32
            val = (symvals[symi] if symi < len(symvals) else 0) + r_add
            if not (elf_lo <= val < elf_hi):
                continue               # a constant, not an address
            spot_rva = vma2rva(r_off)
            new_rva = vma2rva(val)
            if spot_rva is None or new_rva is None:
                continue
            rewrites.append((spot_rva, IMAGE_BASE + new_rva))
            reloc_rvas.add(spot_rva)
    pe_relocs = sorted(reloc_rvas)

    # ---- build the .reloc directory content ----
    blocks = []
    by_page = {}
    for r in pe_relocs:
        by_page.setdefault(r & ~0xFFF, []).append((r & 0xFFF) | (10 << 12))
    reloc_blob = b''
    for page in sorted(by_page):
        ents = by_page[page]
        while ents:
            chunk, ents = ents[:1022], ents[1022:]
            # pad to 4-byte block size
            blob = struct.pack('<II', page, 8 + 2 * len(chunk)) + \
                b''.join(struct.pack('<H', e) for e in chunk)
            if len(blob) % 4:
                blob += b'\0' * (4 - len(blob) % 4)
                # fix size including padding
                blob = struct.pack('<II', page, len(blob)) + blob[8:]
            reloc_blob += blob

    # place .reloc in its own tail section after .data
    reloc_rva = (total_img + SEC_ALIGN - 1) & ~(SEC_ALIGN - 1)
    reloc_vsize = len(reloc_blob)
    total_img = reloc_rva + reloc_vsize

    # ---- assemble the PE image ----
    hdr_size = 0x18 + 0xF0 + 3 * 40   # sig + COFF + PE32+ opt + 3 sections
    hdr_size = (hdr_size + FILE_ALIGN - 1) & ~(FILE_ALIGN - 1)

    text_off = hdr_size
    text_fsize = (text_vsize + FILE_ALIGN - 1) & ~(FILE_ALIGN - 1)
    data_off = text_off + text_fsize
    data_fsize = ((data_vsize + FILE_ALIGN - 1) & ~(FILE_ALIGN - 1)) \
        if data_rva else 0
    reloc_off = data_off + data_fsize
    file_size = reloc_off + len(reloc_blob)

    out = bytearray(file_size)

    def put(off, blob):
        out[off:off + len(blob)] = blob

    # copy sections at their original VMA positions (raw layout may
    # contain gaps between .text/.rodata when page-aligned - the file
    # offsets just follow the RVAs)
    def rva2file(rva):
        if text_rva <= rva < text_rva + text_fsize:
            return text_off + (rva - text_rva)
        if data_rva and data_rva <= rva < data_rva + data_fsize:
            return data_off + (rva - data_rva)
        return None

    for s in rx_secs:
        put(rva2file(s['addr'] - IMAGE_BASE),
            d[s['offset']:s['offset'] + s['size']])
    for s in rw_secs:
        put(rva2file(s['addr'] - IMAGE_BASE),
            d[s['offset']:s['offset'] + s['size']])

    for spot_rva, new_val in rewrites:
        fo = rva2file(spot_rva)
        assert fo is not None, f'operand 0x{spot_rva:x} outside raw image'
        put(fo, struct.pack('<Q', new_val))

    put(reloc_off, reloc_blob)

    # DOS header
    put(0, b'MZ' + b'\0' * 0x3a + struct.pack('<I', 0x80))
    pe_off = 0x80
    put(pe_off, b'PE\x00\x00')                 # the signature
    # COFF header (sits right after the 4-byte signature)
    machine, nsec, ts, symptr, nsyms, optsz, chars = \
        0x8664, 3, 0, 0, 0, 0xF0, 0x22
    put(pe_off + 4, struct.pack('<HHIIIHH', machine, nsec, ts, symptr,
                                nsyms, optsz, chars))
    # PE32+ optional header (the COFF header is 0x14 bytes, NOT 0x18
    # - this off-by-4 misaligned every optional field and OVMF refused
    # the image with a misleading "Not Found")
    o = pe_off + 4 + 0x14
    put(o, struct.pack('<H', 0x20b))                # magic PE32+
    put(o + 0x02, bytes([14, 0]))                   # link major/minor
    put(o + 0x04, struct.pack('<I', file_size))     # SizeOfCode-ish (any)
    put(o + 0x10, struct.pack('<I', entry_rva))     # entry
    put(o + 0x14, struct.pack('<I', text_rva))      # BaseOfCode
    put(o + 0x18, struct.pack('<Q', IMAGE_BASE))
    put(o + 0x20, struct.pack('<I', SEC_ALIGN))
    put(o + 0x24, struct.pack('<I', FILE_ALIGN))
    put(o + 0x28, struct.pack('<HHHHHH', 6, 0, 0, 0, 6, 0))  # OS/subsys ver
    put(o + 0x38, struct.pack('<I', total_img))     # SizeOfImage
    put(o + 0x3c, struct.pack('<I', hdr_size))      # SizeOfHeaders
    put(o + 0x40, struct.pack('<I', 0))             # CheckSum
    put(o + 0x44, struct.pack('<H', 10))            # Subsystem: EFI app
    put(o + 0x46, struct.pack('<H', 0))             # DllCharacteristics
    put(o + 0x48, struct.pack('<QQ', 0x100000, 0x10000))  # stack reserve/commit
    put(o + 0x58, struct.pack('<QQ', 0x100000, 0x10000))  # heap reserve/commit
    put(o + 0x6c, struct.pack('<I', 16))            # NumberOfRvaAndSizes
    # data directory[5] = base relocation table
    put(o + 0x70 + 5 * 8,
        struct.pack('<II', reloc_rva, len(reloc_blob)))

    # section table
    st = o + 0xF0

    def sect(idx, name, vsize, vaddr, rawsize, rawptr, chars):
        p = st + idx * 40
        put(p, name.encode().ljust(8, b'\0')[:8])
        put(p + 8, struct.pack('<IIII', vsize, vaddr, rawsize, rawptr))
        put(p + 24, struct.pack('<I', 0))           # relocations (file)
        put(p + 28, b'\0\0\0\0')
        # Characteristics live at +36 (an early draft wrote +32 and the
        # PE loader refused the image with a bare "Unsupported")
        put(p + 36, struct.pack('<I', chars))

    text_chars = 0x60000020   # code + execute + read
    data_chars = 0xC0000040   # initialized data + read + write
    reloc_chars = 0x42000040  # discarded + initialized + read
    sect(0, '.text', text_vsize, text_rva, text_fsize, text_off,
         text_chars)
    sect(1, '.data', data_vsize, data_rva, data_fsize, data_off,
         data_chars)
    sect(2, '.reloc', reloc_vsize, reloc_rva, len(reloc_blob), reloc_off,
         reloc_chars)

    open(out_path, 'wb').write(out)
    print(f"[elf_to_efi] {out_path}: {len(out)} B, entry RVA 0x{entry_rva:x}, "
          f"{len(pe_relocs)} DIR64 relocs ({len(rewrites)} operands rewritten), "
          f"text 0x{text_rva:x}+0x{text_vsize:x}, "
          f"data 0x{data_rva:x}+0x{data_vsize:x}")


if __name__ == '__main__':
    main()
