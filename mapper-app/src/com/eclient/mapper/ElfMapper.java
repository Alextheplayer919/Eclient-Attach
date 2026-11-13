package com.eclient.mapper;

import java.io.*;
import java.nio.*;
import java.security.*;
import java.util.*;

/** Offline, read-only ELF64 AArch64 candidate mapper. Addresses are ELF virtual addresses, not runtime pointers. */
public final class ElfMapper {
    private static final String[][] TERMS = {
        {"inventory", "inventory", "container", "selectedslot", "selected_slot", "hotbar"},
        {"offhand and totem", "offhand", "off_hand", "totem"},
        {"placement and interaction", "useitemon", "use_item_on", "buildblock", "placeblock", "place_block", "itemuse", "item_use"},
        {"crystal and attack", "ender_crystal", "end_crystal", "endcrystal", "crystal", "gamemode::attack", "_attack", "destroyblock"},
        {"item and transaction", "itemstack", "item_stack", "inventorytransaction", "inventory_transaction", "mob_equipment"}
    };
    private static final int PER_CATEGORY=80, MAX_HITS=400, MAX_XREFS=800, MAX_SYMBOLS=400;
    private static final long MAX_FILE=1024L*1024*1024;
    private static final class Segment { long off, va, size; int flags; }
    private static final class Hit { long off, va; String category, text; Hit(long o,long v,String c,String t){off=o;va=v;category=c;text=t;} }
    private static long u32(ByteBuffer b,int p){return Integer.toUnsignedLong(b.getInt(p));}
    private static int u16(ByteBuffer b,int p){return Short.toUnsignedInt(b.getShort(p));}
    private static ByteBuffer bytes(RandomAccessFile f,long off,int n) throws IOException {
        if(off<0 || n<0 || off>f.length()-n) throw new IOException("ELF range outside file");
        byte[] out=new byte[n]; f.seek(off); f.readFully(out); return ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN);
    }
    private static boolean range(long o,long n,long size) { return o>=0 && n>=0 && o<=size && n<=size-o; }
    private static long virtual(List<Segment> segments,long off) {
        for(Segment s:segments) if(off>=s.off && off-s.off<s.size) return s.va+(off-s.off);
        return -1;
    }
    private static String hex(long n){return n<0 ? "unmapped" : "0x"+Long.toHexString(n);}
    private static int category(String text) {
        String lower=text.toLowerCase(Locale.ROOT);
        for(int i=0;i<TERMS.length;i++) for(int k=1;k<TERMS[i].length;k++) if(lower.contains(TERMS[i][k])) return i;
        return -1;
    }
    public static String sha256(File file) throws Exception {
        MessageDigest md=MessageDigest.getInstance("SHA-256"); byte[] buf=new byte[1024*1024];
        try(InputStream in=new BufferedInputStream(new FileInputStream(file))) { int n; while((n=in.read(buf))!=-1) md.update(buf,0,n); }
        StringBuilder out=new StringBuilder(); for(byte v:md.digest()) out.append(String.format(Locale.ROOT,"%02x",v&255)); return out.toString();
    }
    public static String analyze(File file) throws Exception {
        try(RandomAccessFile f=new RandomAccessFile(file,"r")) {
            final long length=f.length(); if(length<64 || length>MAX_FILE) throw new IOException("ELF size invalid or exceeds 1 GiB");
            ByteBuffer h=bytes(f,0,64);
            if(h.get(0)!=0x7f || h.get(1)!='E' || h.get(2)!='L' || h.get(3)!='F' || h.get(4)!=2 || h.get(5)!=1 || u16(h,18)!=183)
                throw new IOException("Expected a little-endian ELF64 ARM64 library");
            long phoff=h.getLong(32), shoff=h.getLong(40); int phsize=u16(h,54), phnum=u16(h,56), shsize=u16(h,58), shnum=u16(h,60);
            if(phnum==0 || phnum>1024 || phsize<56 || !range(phoff,(long)phsize*phnum,length)) throw new IOException("Invalid program headers");
            List<Segment> segments=new ArrayList<>();
            for(int i=0;i<phnum;i++) {
                ByteBuffer ph=bytes(f,phoff+(long)i*phsize,56);
                if(u32(ph,0)!=1) continue;
                Segment s=new Segment(); s.flags=ph.getInt(4); s.off=ph.getLong(8); s.va=ph.getLong(16); s.size=ph.getLong(32);
                if(!range(s.off,s.size,length) || s.va<0 || s.va>Long.MAX_VALUE-s.size) throw new IOException("Invalid load segment");
                segments.add(s);
            }
            if(segments.isEmpty()) throw new IOException("ELF has no loadable segments");
            StringBuilder out=new StringBuilder("E-Client Mapper - offline static candidate report\n");
            out.append("Library SHA-256: ").append(sha256(file)).append("\nSize: ").append(length).append(" bytes\nArchitecture: ELF64 little-endian ARM64\n");
            out.append("Addresses are ELF virtual addresses relative to the image, not runtime pointers or guaranteed callable function offsets.\n");
            out.append("String and xref matches are leads only; verify code paths, signatures, object layouts and lifetimes for this exact build.\n");
            out.append("No process, inventory, world or network data is read. Exact terms and count limits apply.\n\nLOAD SEGMENTS\n");
            for(Segment s:segments) out.append("file=").append(hex(s.off)).append(" vaddr=").append(hex(s.va)).append(" size=").append(hex(s.size)).append(" flags=").append(s.flags).append('\n');
            List<Hit> hits=new ArrayList<>(); int[] counts=new int[TERMS.length]; int skipped=0;
            try(InputStream in=new BufferedInputStream(new FileInputStream(file),1024*1024)) {
                long pos=0,start=0; int c; StringBuilder run=new StringBuilder(); boolean longRun=false;
                while((c=in.read())!=-1) {
                    if(c>=32 && c<=126) { if(run.length()==0 && !longRun) start=pos; if(run.length()<240) run.append((char)c); else longRun=true; }
                    else { if(!longRun && run.length()>=5) { String text=run.toString(); int cat=category(text); if(cat>=0) { if(counts[cat]<PER_CATEGORY && hits.size()<MAX_HITS) { hits.add(new Hit(start,virtual(segments,start),TERMS[cat][0],text)); counts[cat]++; } else skipped++; } } run.setLength(0); longRun=false; }
                    pos++;
                }
                if(!longRun && run.length()>=5){String text=run.toString();int cat=category(text);if(cat>=0){if(counts[cat]<PER_CATEGORY&&hits.size()<MAX_HITS)hits.add(new Hit(start,virtual(segments,start),TERMS[cat][0],text));else skipped++;}}
            }
            out.append("\nMATCHING ASCII STRINGS (capped; file offset and mapped vaddr)\n");
            for(Hit hit:hits) out.append('[').append(hit.category).append("] file=").append(hex(hit.off)).append(" vaddr=").append(hex(hit.va)).append(" ").append(hit.text).append('\n');
            out.append("Listed: ").append(hits.size()).append("; additional matching strings skipped by limits: ").append(skipped).append('\n');
            out.append("\nMATCHING ELF SYMBOLS (unmangled names; may be stripped)\n");
            int symbols=0;
            if(shnum>0 && shnum<=8192 && shsize>=64 && range(shoff,(long)shsize*shnum,length)) {
                for(int i=0;i<shnum && symbols<MAX_SYMBOLS;i++) {
                    ByteBuffer sh=bytes(f,shoff+(long)i*shsize,64); long type=u32(sh,4); if(type!=2 && type!=11) continue;
                    long off=sh.getLong(24), size=sh.getLong(32), entry=sh.getLong(56), link=u32(sh,40);
                    if(entry<24 || entry>1024 || !range(off,size,length) || link>=shnum) continue;
                    ByteBuffer str=bytes(f,shoff+link*shsize,64); long strOff=str.getLong(24), strSize=str.getLong(32);
                    if(!range(strOff,strSize,length)) continue;
                    long n=Math.min(size/entry,1000000);
                    for(long j=0;j<n && symbols<MAX_SYMBOLS;j++) {
                        ByteBuffer sym=bytes(f,off+j*entry,24); long name=u32(sym,0); if(name==0 || name>=strSize) continue;
                        int available=(int)Math.min(256,strSize-name); ByteBuffer nameBuf=bytes(f,strOff+name,available); StringBuilder text=new StringBuilder();
                        for(int k=0;k<available;k++){int v=nameBuf.get(k)&255;if(v==0)break;if(v<32 || v>126){text.setLength(0);break;}text.append((char)v);}
                        int cat=category(text.toString()); if(cat<0)continue;
                        out.append('[').append(TERMS[cat][0]).append("] value=").append(hex(sym.getLong(8))).append(" size=").append(sym.getLong(16)).append(" ").append(text).append('\n'); symbols++;
                    }
                }
            } else out.append("Section table unavailable or unsupported.\n");
            out.append("Listed: ").append(symbols).append(" (maximum ").append(MAX_SYMBOLS).append(")\n");
            // AArch64 ADRP + immediately following ADD immediate. Exact only;
            // no LDR, intervening instructions, pointer tables or indirect refs.
            Map<Long,List<Hit>> byAddress=new HashMap<>(); for(Hit hit:hits) if(hit.va>=0) byAddress.computeIfAbsent(hit.va,k->new ArrayList<>()).add(hit);
            out.append("\nADRP+ADD DIRECT STRING XREF CANDIDATES (adjacent instructions only)\n");
            int refs=0;
            for(Segment s:segments) {
                if((s.flags&1)==0 || refs>=MAX_XREFS) continue;
                byte[] buf=new byte[1024*1024+4]; long offset=0;
                while(offset+8<=s.size && refs<MAX_XREFS) {
                    int usable=(int)Math.min(1024*1024L,s.size-offset); usable-=usable%4;
                    if(usable==0) break;
                    int read=(int)Math.min((long)usable+4,s.size-offset); f.seek(s.off+offset); f.readFully(buf,0,read);
                    ByteBuffer b=ByteBuffer.wrap(buf).order(ByteOrder.LITTLE_ENDIAN);
                    for(int i=0;i<usable && i+8<=read && refs<MAX_XREFS;i+=4) {
                        int a=b.getInt(i), add=b.getInt(i+4);
                        if((a&0x9f000000)!=0x90000000 || (add&0xff800000)!=0x91000000 || ((add>>>5)&31)!=(a&31)) continue;
                        long imm=(((long)(a>>>5)&0x7ffffL)<<2)|((a>>>29)&3); if((imm&(1L<<20))!=0)imm-=1L<<21;
                        long pc=s.va+offset+i; long dest=(pc&~4095L)+(imm<<12)+(((add>>>10)&0xfffL)<<(((add>>>22)&1)*12));
                        List<Hit> matched=byAddress.get(dest); if(matched==null) continue;
                        for(Hit hit:matched) { if(refs++>=MAX_XREFS) break; out.append("code_vaddr=").append(hex(pc)).append(" code_file=").append(hex(s.off+offset+i)).append(" string_vaddr=").append(hex(dest)).append(" [").append(hit.category).append("] ").append(hit.text).append('\n'); }
                    }
                    offset+=usable;
                }
            }
            out.append("Listed: ").append(Math.min(refs,MAX_XREFS)).append(" (maximum ").append(MAX_XREFS).append("); absence is not proof of no reference.\n");
            return out.toString();
        }
    }
}
