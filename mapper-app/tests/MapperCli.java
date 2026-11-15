package com.eclient.mapper;
import java.io.File;
public final class MapperCli {
    public static void main(String[] args) throws Exception {
        if(args.length!=1) throw new IllegalArgumentException("Expected ARM64 .so path");
        System.out.print(ElfMapper.analyze(new File(args[0])));
    }
}
