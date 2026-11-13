package com.eclient.mapper;
import java.io.*;
import java.nio.*;
import java.nio.file.*;
public final class ElfMapperTest {
    private static void check(boolean b,String message){if(!b)throw new AssertionError(message);}
    private static void u16(ByteBuffer b,int off,int value){b.putShort(off,(short)value);}
    public static void main(String[] args) throws Exception {
        Path dir=Files.createTempDirectory("mapper-test");
        try {
            ByteBuffer b=ByteBuffer.allocate(1024).order(ByteOrder.LITTLE_ENDIAN);
            b.put(0,(byte)0x7f);b.put(1,(byte)'E');b.put(2,(byte)'L');b.put(3,(byte)'F');b.put(4,(byte)2);b.put(5,(byte)1);u16(b,18,183);
            b.putLong(32,64);u16(b,54,56);u16(b,56,1);u16(b,58,64);
            b.putInt(64,1);b.putInt(68,5);b.putLong(72,0);b.putLong(80,0x1000);b.putLong(96,1024);
            b.putInt(0x100,0x90000000);b.putInt(0x104,0x91080000); // adrp x0, page + add x0,x0,#0x200
            byte[] str="InventoryTransaction\0".getBytes("US-ASCII");b.position(0x200);b.put(str);
            byte[] s2="minecraft:ender_crystal\0".getBytes("US-ASCII");b.position(0x240);b.put(s2);
            File f=dir.resolve("fixture.so").toFile();Files.write(f.toPath(),b.array());
            String out=ElfMapper.analyze(f);
            check(out.contains("file=0x200 vaddr=0x1200 InventoryTransaction"),"string mapping");
            check(out.contains("code_vaddr=0x1100 code_file=0x100 string_vaddr=0x1200"),"ADRP+ADD xref");
            check(out.contains("minecraft:ender_crystal"),"crystal match");
            check(out.contains("Section table unavailable"),"stripped sections");
            check(ElfMapper.sha256(f).length()==64,"hash");
            b.putInt(0x104,0xd503201f); Files.write(f.toPath(),b.array());
            check(!ElfMapper.analyze(f).contains("code_vaddr=0x1100"),"no false adjacent xref");
            b.putShort(18,(short)40); Files.write(f.toPath(),b.array());
            try {ElfMapper.analyze(f);throw new AssertionError("accepted non ARM64");}catch(IOException expected){}
            System.out.println("ELF mapper tests passed");
        } finally { try(java.util.stream.Stream<Path> paths=Files.walk(dir)){paths.sorted(java.util.Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new RuntimeException(e);}});} }
    }
}
