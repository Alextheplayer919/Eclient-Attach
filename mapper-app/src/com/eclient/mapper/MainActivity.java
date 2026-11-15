package com.eclient.mapper;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.zip.*;

public final class MainActivity extends Activity {
    private static final int OPEN=1, SAVE=2;
    private static final long MAX_INPUT=2L*1024*1024*1024, MAX_LIB=1024L*1024*1024;
    private TextView status, reportView;
    private Button choose, save;
    private volatile String report;
    private boolean busy;
    private File reportFile() { return new File(getFilesDir(), "mapper-report.txt"); }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root=new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL); root.setPadding(24,24,24,24);
        TextView title=new TextView(this); title.setText("E-Client Mapper"); title.setTextSize(24); root.addView(title);
        TextView intro=new TextView(this); intro.setText("Offline static mapping of an ARM64 libminecraftpe.so or an APK containing it. Findings are candidates, not confirmed offsets. No Minecraft modification or network access."); root.addView(intro);
        choose=new Button(this); choose.setText("Choose Minecraft APK or ARM64 .so"); root.addView(choose);
        save=new Button(this); save.setText("Save report…"); save.setEnabled(false); root.addView(save);
        status=new TextView(this); status.setText("Choose a file to start. Large APKs need free storage and may take several minutes."); root.addView(status);
        ScrollView scroll=new ScrollView(this); scroll.setFillViewport(true);
        reportView=new TextView(this); reportView.setTextIsSelectable(true); reportView.setTextSize(12); reportView.setGravity(Gravity.TOP); reportView.setText("No report yet.");
        scroll.addView(reportView); root.addView(scroll,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,0,1)); setContentView(root);
        try {
            report=ReportStore.read(reportFile()); reportView.setText(report); save.setEnabled(true);
            status.setText("Previous completed report restored. You can save it again.");
        } catch (IOException ignored) { /* No completed report yet. */ }
        choose.setOnClickListener(v->{ Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT); i.addCategory(Intent.CATEGORY_OPENABLE); i.setType("*/*"); startActivityForResult(i,OPEN); });
        save.setOnClickListener(v->{ if(report==null)return; Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT); i.addCategory(Intent.CATEGORY_OPENABLE); i.setType("text/plain"); i.putExtra(Intent.EXTRA_TITLE,"eclient-mapper-report.txt"); startActivityForResult(i,SAVE); });
    }
    private static void copy(InputStream in, File dest,long limit) throws IOException {
        byte[] buf=new byte[1024*1024]; long total=0;
        try(OutputStream out=new BufferedOutputStream(new FileOutputStream(dest))) {
            int n; while((n=in.read(buf))!=-1){ if(n>limit-total) throw new IOException("Input exceeds size limit"); out.write(buf,0,n); total+=n; }
        }
    }
    private void setBusy(boolean v){busy=v;choose.setEnabled(!v);save.setEnabled(!v && report!=null);}
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(result!=RESULT_OK || data==null || data.getData()==null || busy)return;
        final Uri uri=data.getData();
        if(request==SAVE) {
            // Read the completed private report before opening/truncating the chosen file.
            final byte[] bytes;
            try { bytes=ReportStore.read(reportFile()).getBytes(StandardCharsets.UTF_8); }
            catch(Exception e) { status.setText("No completed report to export: "+e.getMessage()); return; }
            setBusy(true); status.setText("Saving report…");
            new Thread(()->{
                try {
                    try(OutputStream out=getContentResolver().openOutputStream(uri,"wt")) {
                        if(out==null) throw new IOException("Could not open destination");
                        out.write(bytes); out.flush();
                    }
                    // Some providers cannot read back immediately. Distinguish that
                    // case from a verified export; retain the private copy either way.
                    boolean verified=false;
                    try(InputStream in=getContentResolver().openInputStream(uri)) {
                        if(in!=null) {
                            MessageDigest digest=MessageDigest.getInstance("SHA-256");
                            byte[] buffer=new byte[8192]; long total=0; int n;
                            while((n=in.read(buffer))!=-1) { total+=n; if(total>bytes.length) throw new IOException("Saved file is larger than the report"); digest.update(buffer,0,n); }
                            if(total!=bytes.length || !Arrays.equals(digest.digest(),MessageDigest.getInstance("SHA-256").digest(bytes)))
                                throw new IOException("Saved file does not match the report");
                            verified=true;
                        }
                    } catch(SecurityException e) { /* Provider denied read-back. */ }
                    final boolean checked=verified;
                    runOnUiThread(()->{status.setText(checked ? "Report saved and verified ("+bytes.length+" bytes)." : "Report written ("+bytes.length+" bytes), but provider did not allow verification. Private copy retained.");setBusy(false);});
                } catch(Exception e) {
                    final String message=e.getMessage()==null?e.getClass().getSimpleName():e.getMessage();
                    runOnUiThread(()->{status.setText("Save failed: "+message+". Private report retained; try again.");setBusy(false);});
                }
            },"report-export").start();
            return;
        }
        if(request!=OPEN)return;
        reportView.setText("Analyzing…"); setBusy(true); status.setText("Copying and analyzing locally. Please keep the app open. The previous report is retained until this succeeds.");
        new Thread(()->{
            File input=new File(getCacheDir(),"mapper-input.tmp"), lib=new File(getCacheDir(),"mapper-library.tmp");
            try {
                try(InputStream in=getContentResolver().openInputStream(uri)) { if(in==null)throw new IOException("Could not open selected file"); copy(in,input,MAX_INPUT); }
                boolean zip;
                try(InputStream in=new FileInputStream(input)) { zip=in.read()==0x50 && in.read()==0x4b; }
                String inputSha=ElfMapper.sha256(input); File target=input; String origin="Direct library";
                if(zip) {
                    try(ZipFile archive=new ZipFile(input)) {
                        ZipEntry entry=archive.getEntry("lib/arm64-v8a/libminecraftpe.so");
                        if(entry==null || entry.isDirectory()) throw new IOException("APK does not contain lib/arm64-v8a/libminecraftpe.so. For split APKs choose the ARM64 split or extract the library first.");
                        if(entry.getSize()>MAX_LIB) throw new IOException("Native library exceeds 1 GiB");
                        try(InputStream in=archive.getInputStream(entry)){copy(in,lib,MAX_LIB);}
                    }
                    target=lib; origin="APK entry lib/arm64-v8a/libminecraftpe.so";
                }
                final String resultText="Source: "+origin+"\nSelected file SHA-256: "+inputSha+"\n\n"+ElfMapper.analyze(target);
                ReportStore.persist(reportFile(), resultText);
                runOnUiThread(()->{report=resultText;reportView.setText(resultText);status.setText("Analysis complete. Save the report wherever you choose.");setBusy(false);});
            } catch(Exception e) {
                final String message=e.getMessage()==null?e.getClass().getSimpleName():e.getMessage();
                runOnUiThread(()->{reportView.setText(report != null ? report : "No report generated.");status.setText("Analysis failed: "+message+(report != null ? ". Previous report retained." : ""));setBusy(false);});
            } finally { input.delete(); lib.delete(); }
        },"elf-mapper").start();
    }
}
