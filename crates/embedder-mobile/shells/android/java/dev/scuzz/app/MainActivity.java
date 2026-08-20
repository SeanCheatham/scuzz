package dev.scuzz.app;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import java.io.File;

/** Loads libscuzz.so, blits frames, and forwards tap / typed text. */
public final class MainActivity extends Activity implements SurfaceHolder.Callback {
  static {
    System.loadLibrary("scuzz");
  }

  private static final int POINTER_DOWN = 1;
  private static final int POINTER_MOVE = 2;
  private static final int POINTER_UP = 3;
  private static final int LIFECYCLE_RESUME = 1;
  private static final int LIFECYCLE_PAUSE = 2;
  private static final int LIFECYCLE_STOP = 3;
  private static final String ZWSP = "\u200b";

  private SurfaceView surface;
  private SurfaceHolder holder;
  private EditText hidden;
  private Bitmap bitmap;
  private int[] pixels;
  private int dumped;
  private int keyboard;
  private int watchLock;
  private float density = 1f;
  private int pointW;
  private int pointH;
  private final Handler tick = new Handler(Looper.getMainLooper());
  private final Runnable blit =
      new Runnable() {
        @Override
        public void run() {
          blitFrame();
          syncKeyboard();
          tick.postDelayed(this, 16);
        }
      };

  public native void nativeStart(String dumpPath, int width, int height, float scale);

  public native int nativeFrameWidth();

  public native int nativeFrameHeight();

  public native int nativeCopyFrame(int[] argb);

  public native void nativePointer(float x, float y, int phase);

  public native void nativeTextEdit(String text);

  public native void nativeResize(int width, int height);

  public native void nativeLifecycle(int phase);

  public native void nativeSetAlive(int alive);

  public native int nativeKeyboardVisible();

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    FrameLayout root = new FrameLayout(this);
    surface = new SurfaceView(this);
    holder = surface.getHolder();
    holder.addCallback(this);
    surface.setOnTouchListener(
        new View.OnTouchListener() {
          @Override
          public boolean onTouch(View v, MotionEvent ev) {
            return onSurfaceTouch(ev);
          }
        });
    hidden = new EditText(this);
    hidden.setBackground(null);
    hidden.setAlpha(0f);
    hidden.setText(ZWSP);
    hidden.addTextChangedListener(
        new TextWatcher() {
          @Override
          public void beforeTextChanged(CharSequence s, int st, int c, int a) {}

          @Override
          public void onTextChanged(CharSequence s, int st, int b, int c) {}

          @Override
          public void afterTextChanged(Editable s) {
            onHiddenChanged(s);
          }
        });
    root.addView(
        surface, new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    root.addView(hidden, new FrameLayout.LayoutParams(1, 1));
    setContentView(root);
    DisplayMetrics dm = getResources().getDisplayMetrics();
    density = dm.density > 0f ? dm.density : 1f;
    pointW = Math.max(1, (int) (dm.widthPixels / density));
    pointH = Math.max(1, (int) (dm.heightPixels / density));
    File dump = new File(getFilesDir(), "scuzz_android.debug.dump");
    nativeStart(dump.getAbsolutePath(), pointW, pointH, density);
    scheduleInject();
  }

  @Override
  protected void onPause() {
    nativeLifecycle(LIFECYCLE_PAUSE);
    super.onPause();
  }

  @Override
  protected void onResume() {
    super.onResume();
    nativeLifecycle(LIFECYCLE_RESUME);
  }

  @Override
  protected void onDestroy() {
    nativeLifecycle(LIFECYCLE_STOP);
    nativeSetAlive(0);
    super.onDestroy();
  }

  @Override
  public void surfaceCreated(SurfaceHolder h) {
    holder = h;
    tick.post(blit);
  }

  @Override
  public void surfaceChanged(SurfaceHolder h, int format, int w, int ht) {
    holder = h;
    if (w > 0 && ht > 0) {
      pointW = Math.max(1, (int) (w / density));
      pointH = Math.max(1, (int) (ht / density));
      nativeResize(pointW, pointH);
    }
  }

  @Override
  public void surfaceDestroyed(SurfaceHolder h) {
    tick.removeCallbacks(blit);
    holder = null;
  }

  private boolean onSurfaceTouch(MotionEvent ev) {
    int vw = surface.getWidth();
    int vh = surface.getHeight();
    int phase;
    float x;
    float y;
    if (pointW <= 0 || pointH <= 0 || vw <= 0 || vh <= 0) {
      return true;
    }
    x = ev.getX() * pointW / vw;
    y = ev.getY() * pointH / vh;
    switch (ev.getActionMasked()) {
      case MotionEvent.ACTION_DOWN:
        phase = POINTER_DOWN;
        break;
      case MotionEvent.ACTION_MOVE:
        phase = POINTER_MOVE;
        break;
      case MotionEvent.ACTION_UP:
      case MotionEvent.ACTION_CANCEL:
        phase = POINTER_UP;
        break;
      default:
        return true;
    }
    nativePointer(x, y, phase);
    return true;
  }

  private void onHiddenChanged(Editable s) {
    String t;
    if (watchLock != 0) {
      return;
    }
    t = s.toString();
    if (t.equals(ZWSP)) {
      return;
    }
    if (t.length() == 0) {
      dispatchEdit("");
    } else {
      dispatchEdit(t.replace(ZWSP, ""));
    }
    watchLock = 1;
    s.replace(0, s.length(), ZWSP);
    watchLock = 0;
  }

  private void dispatchEdit(String text) {
    nativeTextEdit(text == null ? "" : text);
  }

  private void scheduleInject() {
    final String typed = extraOrEnv("SCUZZ_ANDROID_TYPE");
    final String tap = extraOrEnv("SCUZZ_ANDROID_TAP");
    tick.postDelayed(
        new Runnable() {
          @Override
          public void run() {
            if (typed != null && typed.length() > 0) {
              dispatchEdit(typed);
              dispatchEdit("");
            }
            if (tap != null && tap.length() > 0) {
              injectTap(tap);
            }
          }
        },
        800);
  }

  private void injectTap(String spec) {
    int comma = spec.indexOf(',');
    float x;
    float y;
    if (comma <= 0) {
      return;
    }
    try {
      x = Float.parseFloat(spec.substring(0, comma));
      y = Float.parseFloat(spec.substring(comma + 1));
    } catch (NumberFormatException e) {
      return;
    }
    nativePointer(x, y, POINTER_DOWN);
    nativePointer(x, y, POINTER_UP);
  }

  private String extraOrEnv(String key) {
    String v = getIntent().getStringExtra(key);
    if (v != null && v.length() > 0) {
      return v;
    }
    return System.getenv(key);
  }

  private void syncKeyboard() {
    int want = nativeKeyboardVisible();
    InputMethodManager imm;
    if (want == keyboard) {
      return;
    }
    keyboard = want;
    imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
    if (imm == null) {
      return;
    }
    if (want != 0) {
      hidden.requestFocus();
      imm.showSoftInput(hidden, 0);
    } else {
      imm.hideSoftInputFromWindow(hidden.getWindowToken(), 0);
    }
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
      Log.i("scuzz", "blit " + w + "x" + h + " frames=" + copied);
    }
  }
}
