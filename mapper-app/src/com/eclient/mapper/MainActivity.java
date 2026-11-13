package com.eclient.mapper;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.*;
import java.io.*;
import java.util.zip.*;

public final class MainActivity extends Activity {
    private static final int OPEN=1, SAVE=2;
    private static final long MAX_INPUT=2L*1024*1024*1024, MAX_LIB=1024L*1024*1024;
    private TextView status, reportView;
    private Button choose, save;
    private volatile String report;
    private boolean busy;
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
            try(OutputStream out=getContentResolver().openOutputStream(uri,"wt")) {
                if(out==null) throw new IOException("Could not open destination"); out.write(report.getBytes("UTF-8")); status.setText("Report saved.");
            } catch(Exception e) { status.setText("Save failed: "+e.getMessage()); }
            return;
        }
        if(request!=OPEN)return;
        report=null; reportView.setText("Analyzing…"); setBusy(true); status.setText("Copying and analyzing locally. Please keep the app open.");
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
                runOnUiThread(()->{report=resultText;reportView.setText(resultText);status.setText("Analysis complete. Save the report wherever you choose.");setBusy(false);});
            } catch(Exception e) {
                final String message=e.getMessage()==null?e.getClass().getSimpleName():e.getMessage();
                runOnUiThread(()->{reportView.setText("No report generated.");status.setText("Analysis failed: "+message);setBusy(false);});
            } finally { input.delete(); lib.delete(); }
        },"elf-mapper").start();
    }
}
