package dev.scuzz.app;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import java.io.File;
import java.io.FileOutputStream;

/** Loads libscuzz.so and blits the last sz_mobile_present frame. */
public final class MainActivity extends Activity implements SurfaceHolder.Callback {
  static {
    System.loadLibrary("scuzz");
  }

  private SurfaceHolder holder;
  private Bitmap bitmap;
  private int[] pixels;
  private int dumped;
  private final Handler tick = new Handler(Looper.getMainLooper());
  private final Runnable blit =
      new Runnable() {
        @Override
        public void run() {
          blitFrame();
          tick.postDelayed(this, 16);
        }
      };

  public native int nativeFrameWidth();

  public native int nativeFrameHeight();

  public native int nativeFrameCount();

  public native int nativeCopyFrame(int[] argb);

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    SurfaceView surface = new SurfaceView(this);
    holder = surface.getHolder();
    holder.addCallback(this);
    setContentView(surface);
  }

  @Override
  public void surfaceCreated(SurfaceHolder h) {
    holder = h;
    tick.post(blit);
  }

  @Override
  public void surfaceChanged(SurfaceHolder h, int format, int w, int ht) {
    holder = h;
  }

  @Override
  public void surfaceDestroyed(SurfaceHolder h) {
    tick.removeCallbacks(blit);
    holder = null;
  }

  private void blitFrame() {
    int w;
    int h;
    int n;
    int copied;
    Canvas c;
    if (holder == null) {
      return;
    }
    w = nativeFrameWidth();
    h = nativeFrameHeight();
    if (w <= 0 || h <= 0) {
      return;
    }
    n = w * h;
    if (pixels == null || pixels.length != n) {
      pixels = new int[n];
      if (bitmap != null) {
        bitmap.recycle();
      }
      bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
    }
    copied = nativeCopyFrame(pixels);
    if (copied <= 0) {
      return;
    }
    bitmap.setPixels(pixels, 0, w, 0, 0, w, h);
    c = holder.lockCanvas();
    if (c == null) {
      return;
    }
    try {
      c.drawBitmap(bitmap, null, new Rect(0, 0, c.getWidth(), c.getHeight()), null);
    } finally {
      holder.unlockCanvasAndPost(c);
    }
    if (dumped == 0) {
      dumped = 1;
      Log.i("scuzz", "blit " + w + "x" + h + " frames=" + nativeFrameCount());
      writeDump(w, h, nativeFrameCount());
    }
  }

  private void writeDump(int w, int h, int frames) {
    try {
      File dir = getFilesDir();
      dir.mkdirs();
      File f = new File(dir, "scuzz_android.debug.dump");
      FileOutputStream out = new FileOutputStream(f);
      String line = "present " + w + "x" + h + " frames=" + frames + "\n";
      out.write(line.getBytes("UTF-8"));
      out.close();
    } catch (Exception e) {
      Log.e("scuzz", "dump failed", e);
    }
  }
}
