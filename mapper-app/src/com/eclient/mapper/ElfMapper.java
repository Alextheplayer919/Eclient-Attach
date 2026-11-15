package com.eclient.mapper;

import java.io.*;
import java.nio.*;
import java.security.*;
import java.util.*;

/** Offline, read-only ELF64 AArch64 candidate mapper. Addresses are ELF virtual addresses, not runtime pointers. */
public final class ElfMapper {
    // Specific names only: generic "container" matches thousands of unrelated
    // C++/Cohtml symbols and "totem" alone also matches LogToTemporaryFile.
    private static final String[][] TERMS = {
        {"block rendering and tessellation", "blocktessellator", "block_tessellator", "blockrenderer", "block_renderer", "rendertessellator", "renderchunk", "render_chunk", "chunkrenderer", "blockmodel", "block_model", "renderlayer", "render_layer"},
        {"texture atlas and materials", "textureatlas", "texture_atlas", "terrain_texture", "terraintexture", "blocktexture", "block_texture", "texturegroup", "texture_group", "materialinstance", "material_instance", "materialpointer", "meshbuilder", "mesh_builder"},
        {"item and transaction", "itemstack", "item_stack", "inventorytransaction", "inventory_transaction", "stackrequest", "stack_request", "mob_equipment"},
        {"inventory and slots", "inventory", "selectedslot", "selected_slot", "selectslot", "select_slot", "hotbar", "playerinventory", "inventoryslot"},
        {"offhand and totem", "offhand", "off_hand", "totem_of_undying", "minecraft:totem", "item.totem", "totem_particle", "totem_manual"},
        {"placement and interaction", "useitemon", "use_item_on", "buildblock", "placeblock", "place_block", "itemuse", "item_use", "interactblock"},
        {"crystal and attack", "ender_crystal", "end_crystal", "endcrystal", "endercrystal", "crystal_explod", "crystal_explode", "gamemode::attack", "_attack", "destroyblock"},
        {"actor and player", "localplayer", "actorfactory", "actor_factory", "createactor", "create_actor", "spawnactor", "spawn_actor", "addactor", "add_actor", "runtimeactor"}
    };
    private static final int PER_CATEGORY=200, PER_SYMBOL_CATEGORY=100, MAX_XREFS=1000, MAX_XREFS_PER_CATEGORY=125;
    private static final long MAX_FILE=1024L*1024*1024;
    private static final class Segment { long off, va, size; int flags; }
    private static final class Hit {
        long off, va; int cat, score; String category, text;
        Hit(long o,long v,int c,String t){off=o;va=v;cat=c;category=TERMS[c][0];text=t;score=score(t);}
    }
    private static int score(String text) {
        String t=text.toLowerCase(Locale.ROOT); int s=0;
        if(t.contains("minecraft:") || t.contains("gamemode") || t.contains("localplayer") || t.contains("playerinventory")) s+=4;
        if(t.contains("inventorytransaction") || t.contains("itemstacknet") || t.contains("useitemon") || t.contains("selectedslot") || t.contains("offhand")) s+=3;
        if(t.contains("blocktessellator") || t.contains("blockrenderer") || t.contains("textureatlas") || t.contains("terrain_texture") || t.contains("blockmodel")) s+=4;
        if(t.contains("::")) s+=2;
        if(t.startsWith("_z") || t.startsWith("nstd") || t.contains("cohtml") || t.contains("xbox::") || t.contains("v8::")) s-=3;
        if(t.contains("std::__ndk1::") || t.contains("std::vector")) s-=2;
        return s;
    }
    private static final class Collector {
        final List<PriorityQueue<Hit>> buckets=new ArrayList<>(); final int limit; long matched=0;
        Collector(int max) {
            limit=max;
            Comparator<Hit> cmp=Comparator.comparingInt((Hit h)->h.score).thenComparing((Hit h)->-h.off);
            for(int i=0;i<TERMS.length;i++) buckets.add(new PriorityQueue<>(cmp));
        }
        void add(long off,long va,String text) {
            int c=category(text); if(c<0)return; matched++;
            Hit hit=new Hit(off,va,c,text); PriorityQueue<Hit> q=buckets.get(c);
            if(q.size()<limit)q.add(hit);
            else if(q.comparator().compare(hit,q.peek())>0){q.poll();q.add(hit);}
        }
        List<Hit> selected() {
            List<Hit> result=new ArrayList<>();
            for(PriorityQueue<Hit> q:buckets){List<Hit> x=new ArrayList<>(q);x.sort(Comparator.comparingInt((Hit h)->-h.score).thenComparingLong(h->h.off));result.addAll(x);}
            return result;
        }
    }
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
            Collector strings=new Collector(PER_CATEGORY);
            try(InputStream in=new BufferedInputStream(new FileInputStream(file),1024*1024)) {
                long pos=0,start=0; int c; StringBuilder run=new StringBuilder(); boolean longRun=false;
                while((c=in.read())!=-1) {
                    if(c>=32 && c<=126) { if(run.length()==0 && !longRun) start=pos; if(run.length()<240) run.append((char)c); else longRun=true; }
                    else { if(!longRun && run.length()>=5) strings.add(start,virtual(segments,start),run.toString()); run.setLength(0); longRun=false; }
                    pos++;
                }
                if(!longRun && run.length()>=5)strings.add(start,virtual(segments,start),run.toString());
            }
            List<Hit> hits=strings.selected();
            out.append("\nMATCHING ASCII STRINGS (ranked and capped per category; file offset and mapped vaddr)\n");
            for(Hit hit:hits) out.append('[').append(hit.category).append("] file=").append(hex(hit.off)).append(" vaddr=").append(hex(hit.va)).append(" ").append(hit.text).append('\n');
            out.append("Listed: ").append(hits.size()).append("; additional matches omitted: ").append(strings.matched-hits.size()).append("; limit per category: ").append(PER_CATEGORY).append('\n');
            out.append("\nMATCHING ELF SYMBOLS (raw names, ranked and capped per category; may be stripped)\n");
            Collector symbols=new Collector(PER_SYMBOL_CATEGORY);
            if(shnum>0 && shnum<=8192 && shsize>=64 && range(shoff,(long)shsize*shnum,length)) {
                for(int i=0;i<shnum;i++) {
                    ByteBuffer sh=bytes(f,shoff+(long)i*shsize,64); long type=u32(sh,4); if(type!=2 && type!=11) continue;
                    long off=sh.getLong(24), size=sh.getLong(32), entry=sh.getLong(56), link=u32(sh,40);
                    if(entry<24 || entry>1024 || !range(off,size,length) || link>=shnum) continue;
                    ByteBuffer str=bytes(f,shoff+link*shsize,64); long strOff=str.getLong(24), strSize=str.getLong(32);
                    if(!range(strOff,strSize,length)) continue;
                    long n=Math.min(size/entry,1000000);
                    for(long j=0;j<n;j++) {
                        ByteBuffer sym=bytes(f,off+j*entry,24); long name=u32(sym,0); if(name==0 || name>=strSize) continue;
                        int available=(int)Math.min(256,strSize-name); ByteBuffer nameBuf=bytes(f,strOff+name,available); StringBuilder text=new StringBuilder();
                        for(int k=0;k<available;k++){int v=nameBuf.get(k)&255;if(v==0)break;if(v<32 || v>126){text.setLength(0);break;}text.append((char)v);}
                        symbols.add(off+j*entry,sym.getLong(8),text.toString());
                    }
                }
            } else out.append("Section table unavailable or unsupported.\n");
            List<Hit> symbolHits=symbols.selected();
            for(Hit hit:symbolHits) out.append('[').append(hit.category).append("] value=").append(hex(hit.va)).append(" ").append(hit.text).append('\n');
            out.append("Listed: ").append(symbolHits.size()).append("; additional matches omitted: ").append(symbols.matched-symbolHits.size()).append("; limit per category: ").append(PER_SYMBOL_CATEGORY).append('\n');
            // Heuristic ADRP + ADD within four following instructions. The
            // intervening code may change registers; stop on obvious control
            // flow and writes, but never claim this proves an actual xref.
            Map<Long,List<Hit>> byAddress=new HashMap<>(); for(Hit hit:hits) if(hit.va>=0) byAddress.computeIfAbsent(hit.va,k->new ArrayList<>()).add(hit);
            out.append("\nARM64 ADRP+ADD STRING XREF CANDIDATES (up to 3 intervening instructions; heuristic, not function entries)\n");
            int refs=0; int[] categoryRefs=new int[TERMS.length];
            for(Segment s:segments) {
                if((s.flags&1)==0 || refs>=MAX_XREFS) continue;
                byte[] buf=new byte[1024*1024+16]; long offset=0;
                while(offset+8<=s.size && refs<MAX_XREFS) {
                    int usable=(int)Math.min(1024*1024L,s.size-offset); usable-=usable%4;
                    if(usable==0) break;
                    int read=(int)Math.min((long)usable+16,s.size-offset); f.seek(s.off+offset); f.readFully(buf,0,read);
                    ByteBuffer b=ByteBuffer.wrap(buf).order(ByteOrder.LITTLE_ENDIAN);
                    for(int i=0;i<usable && i+8<=read && refs<MAX_XREFS;i+=4) {
                        int a=b.getInt(i); if((a&0x9f000000)!=0x90000000)continue;
                        int reg=a&31; if(reg==31)continue;
                        long imm=(((long)(a>>>5)&0x7ffffL)<<2)|((a>>>29)&3); if((imm&(1L<<20))!=0)imm-=1L<<21;
                        long pc=s.va+offset+i, page=(pc&~4095L)+(imm<<12);
                        for(int gap=1;gap<=4 && i+(gap+1)*4<=read;gap++) {
                            int next=b.getInt(i+gap*4);
                            if((next&0xff800000)==0x91000000 && ((next>>>5)&31)==reg) {
                                long dest=page+(((next>>>10)&0xfffL)<<(((next>>>22)&1)*12));
                                List<Hit> matched=byAddress.get(dest);
                                if(matched!=null) for(Hit hit:matched) {
                                    if(refs>=MAX_XREFS)break; if(categoryRefs[hit.cat]>=MAX_XREFS_PER_CATEGORY)continue; refs++; categoryRefs[hit.cat]++;
                                    out.append("code_vaddr=").append(hex(pc)).append(" code_file=").append(hex(s.off+offset+i)).append(" string_vaddr=").append(hex(dest)).append(" gap=").append(gap-1).append(" [").append(hit.category).append("] ").append(hit.text).append('\n');
                                }
                                break;
                            }
                            // Conservative barriers; this is not full data-flow analysis.
                            if((next&0x7c000000)==0x14000000 || (next&0xff000010)==0x54000000 ||
                               (next&0x7e000000)==0x34000000 || (next&0x7e000000)==0x36000000 ||
                               (next&0xfffffc1f)==0xd61f0000 || (next&0xfffffc1f)==0xd65f0000 ||
                               (next&31)==reg) break;
                        }
                    }
                    offset+=usable;
                }
            }
            out.append("Listed: ").append(refs).append(" (maximum ").append(MAX_XREFS).append(", maximum ").append(MAX_XREFS_PER_CATEGORY).append(" per category); only selected strings are searched, and absence does not prove no reference.\n");
            return out.toString();
        }
    }
}
