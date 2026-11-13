package com.eclient.mapper;

import java.io.*;
import java.nio.charset.StandardCharsets;

/** Persists a complete report before export; independent of Android lifecycle. */
public final class ReportStore {
    private static final int MAX_REPORT = 2 * 1024 * 1024;
    private ReportStore() {}
    public static void persist(File file, String text) throws IOException {
        if (text == null || text.trim().isEmpty()) throw new IOException("Cannot save an empty report");
        byte[] bytes=text.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > MAX_REPORT) throw new IOException("Report exceeds size limit");
        File tmp=new File(file.getParentFile(), file.getName()+".tmp");
        try {
            try (FileOutputStream out=new FileOutputStream(tmp)) { out.write(bytes); out.flush(); out.getFD().sync(); }
            if (!tmp.renameTo(file)) throw new IOException("Could not replace local report");
        } finally { if(tmp.exists()) tmp.delete(); }
    }
    public static String read(File file) throws IOException {
        if (!file.isFile() || file.length()<=0 || file.length()>MAX_REPORT) throw new IOException("No completed local report available");
        ByteArrayOutputStream data=new ByteArrayOutputStream();
        try(InputStream in=new FileInputStream(file)) { copy(in,data,MAX_REPORT); }
        String text=new String(data.toByteArray(),StandardCharsets.UTF_8);
        if(text.trim().isEmpty()) throw new IOException("Local report is empty");
        return text;
    }
    public static long copy(InputStream in, OutputStream out, long limit) throws IOException {
        byte[] buffer=new byte[8192]; long count=0; int n;
        while ((n=in.read(buffer))!=-1) {
            if (n>limit-count) throw new IOException("Report exceeds size limit");
            out.write(buffer,0,n); count+=n;
        }
        return count;
    }
}
