package com.eclient.mapper;
import java.io.*;
import java.nio.file.*;
public final class ReportStoreTest {
    public static void main(String[] args) throws Exception {
        Path dir=Files.createTempDirectory("report-store-test"); File report=dir.resolve("report.txt").toFile();
        try {
            ReportStore.persist(report,"First report\n");
            if(!ReportStore.read(report).equals("First report\n"))throw new AssertionError("restore");
            try {ReportStore.persist(report," ");throw new AssertionError("empty accepted");}catch(IOException expected){}
            if(!ReportStore.read(report).equals("First report\n"))throw new AssertionError("previous report lost");
            ReportStore.persist(report,"Second report\n");
            if(!ReportStore.read(report).equals("Second report\n"))throw new AssertionError("replacement");
            ByteArrayOutputStream output=new ByteArrayOutputStream();
            long size=ReportStore.copy(new ByteArrayInputStream(new byte[]{1,2,3}),output,10);
            if(size!=3 || output.size()!=3)throw new AssertionError("copy");
            try {ReportStore.copy(new ByteArrayInputStream(new byte[]{1,2,3}),new ByteArrayOutputStream(),2);throw new AssertionError("limit");}catch(IOException expected){}
            System.out.println("Report store tests passed");
        } finally {Files.deleteIfExists(report.toPath());Files.deleteIfExists(dir);}
    }
}
