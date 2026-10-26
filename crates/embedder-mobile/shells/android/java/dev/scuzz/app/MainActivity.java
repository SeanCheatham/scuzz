package dev.scuzz.app;

import android.app.Activity;
import android.content.Context;
import android.database.ContentObserver;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Insets;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.Editable;
import android.text.TextWatcher;
import android.text.Selection;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.MotionEvent;
import android.view.KeyEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.FrameLayout;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

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

  private ScuzzSurface surface;
  private SurfaceHolder holder;
  private EditText hidden;
  private Bitmap bitmap;
  private int[] pixels;
  private int dumped;
  private int keyboardRequest;
  private int watchLock;
  private float density = 1f;
  private int pointW;
  private int pointH;
  private final Handler tick = new Handler(Looper.getMainLooper());
  private boolean motionWatching;
  private final ContentObserver motionObserver = new ContentObserver(tick) {
    @Override
    public void onChange(boolean selfChange) {
      if (motionWatching) syncMotionPreference();
    }
  };
  private final Runnable blit =
      new Runnable() {
        @Override
        public void run() {
          blitFrame();
          syncKeyboard();
          surface.syncAccessibility();
          tick.postDelayed(this, 16);
        }
      };

  public native void nativeStart(String dumpPath, String recordPath, int width, int height, float scale);

  public native int nativeFrameWidth();

  public native int nativeFrameHeight();

  public native int nativeCopyFrame(int[] argb);

  public native void nativePointer(float x, float y, int phase);

  public native void nativeTextEdit(byte[] text);

  public native byte[] nativeAccessibilitySnapshot(long previous);

  public native boolean nativeAccessibilityAction(
      long id, int action, byte[] text, int start, int end, long value);

  public native void nativeResize(int width, int height);

  public native void nativeLifecycle(int phase);

  public native void nativeMotionPreference(boolean reduced);

  public native void nativeSetAlive(int alive);

  public native int nativeKeyboardRequest();

  private void syncMotionPreference() {
    float scale = Settings.Global.getFloat(
        getContentResolver(), Settings.Global.ANIMATOR_DURATION_SCALE, 1f);
    nativeMotionPreference(scale == 0f);
  }

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    FrameLayout root = new FrameLayout(this);
    root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
      @Override
      @SuppressWarnings("deprecation")
      public WindowInsets onApplyWindowInsets(View view, WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30) {
          Insets safe = insets.getInsets(WindowInsets.Type.systemBars()
              | WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
          view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
        } else {
          view.setPadding(insets.getSystemWindowInsetLeft(),
              insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(),
              insets.getSystemWindowInsetBottom());
        }
        return insets;
      }
    });
    surface = new ScuzzSurface(this);
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
    hidden.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
    hidden.setBackground(null);
    hidden.setAlpha(0f);
    hidden.setText(ZWSP);
    hidden.setSelection(hidden.length());
    hidden.setFocusable(false);
    hidden.setFocusableInTouchMode(false);
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
    root.requestApplyInsets();
    DisplayMetrics dm = getResources().getDisplayMetrics();
    density = dm.density > 0f ? dm.density : 1f;
    pointW = Math.max(1, (int) (dm.widthPixels / density));
    pointH = Math.max(1, (int) (dm.heightPixels / density));
    File dump = new File(getFilesDir(), "scuzz_android.debug.dump");
    File record = new File(getFilesDir(), "scuzz_android.record.json");
    getContentResolver().registerContentObserver(
        Settings.Global.getUriFor(Settings.Global.ANIMATOR_DURATION_SCALE),
        false, motionObserver);
    motionWatching = true;
    syncMotionPreference();
    nativeStart(dump.getAbsolutePath(), record.getAbsolutePath(), pointW, pointH, density);
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
    if (motionWatching) syncMotionPreference();
    nativeLifecycle(LIFECYCLE_RESUME);
  }

  @Override
  protected void onDestroy() {
    motionWatching = false;
    getContentResolver().unregisterContentObserver(motionObserver);
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
    Selection.setSelection(s, s.length());
    watchLock = 0;
  }

  private void dispatchEdit(String text) {
    nativeTextEdit((text == null ? "" : text).getBytes(StandardCharsets.UTF_8));
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
    int request = nativeKeyboardRequest();
    InputMethodManager imm;
    if (request == keyboardRequest) {
      return;
    }
    keyboardRequest = request;
    int want = request & 1;
    imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
    if (imm == null) {
      return;
    }
    if (want != 0) {
      surface.requestFocus();
      imm.showSoftInput(surface, 0);
    } else {
      imm.hideSoftInputFromWindow(surface.getWindowToken(), 0);
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

  private static final int FOCUS = 1, ACTIVATE = 2, SET_TEXT = 4, SET_SELECTION = 8;
  private static final int INCREMENT = 16, DECREMENT = 32, SET_VALUE = 64, CLEAR_FOCUS = 128;

  private static final class SemanticNode {
    final long id, parent, number, minimum, maximum;
    final int role, actions, checked, selected, expanded, start, end;
    final boolean focused, editable, multiline;
    final String label, value, description;
    final float x, y, width, height;
    final int virtualId;

    SemanticNode(JSONObject data, int virtualId) throws JSONException {
      id = data.getLong("id");
      parent = data.getLong("parent");
      role = data.getInt("role");
      actions = data.getInt("actions");
      checked = data.getInt("checked");
      selected = data.getInt("selected");
      expanded = data.getInt("expanded");
      focused = data.getInt("focused") != 0;
      editable = data.getInt("editable") != 0;
      multiline = data.getInt("multiline") != 0;
      start = data.getInt("start");
      end = data.getInt("end");
      number = data.getLong("number");
      minimum = data.getLong("minimum");
      maximum = data.getLong("maximum");
      label = data.getString("label");
      value = data.getString("value");
      description = data.getString("description");
      JSONArray bounds = data.getJSONArray("bounds");
      x = (float)bounds.getDouble(0);
      y = (float)bounds.getDouble(1);
      width = (float)bounds.getDouble(2);
      height = (float)bounds.getDouble(3);
      this.virtualId = virtualId;
    }

    boolean has(int action) { return (actions & action) != 0; }
    boolean focusable() { return actions != 0 || (!label.isEmpty() && role != 44); }
    boolean range() { return role == 8 || role == 42 || role == 10 || role == 20; }
  }

  private static int utf16Offset(String text, int bytes) {
    int index = 0, count = 0;
    while (index < text.length()) {
      int point = text.codePointAt(index);
      int size = point < 128 ? 1 : point < 2048 ? 2 : point < 65536 ? 3 : 4;
      if (count + size > bytes) break;
      count += size;
      index += Character.charCount(point);
    }
    return index;
  }

  private static int utf8Offset(String text, int offset) {
    offset = Math.max(0, Math.min(offset, text.length()));
    if (offset > 0 && offset < text.length() && Character.isHighSurrogate(text.charAt(offset - 1))
        && Character.isLowSurrogate(text.charAt(offset))) offset--;
    return text.substring(0, offset).getBytes(StandardCharsets.UTF_8).length;
  }

  private final class ScuzzSurface extends SurfaceView {
    private LinkedHashMap<Integer, SemanticNode> nodes = new LinkedHashMap<>();
    private HashMap<Long, Integer> identities = new HashMap<>();
    private long version = -1;
    private int nextId = 1, accessibilityFocus = View.NO_ID, hovered = View.NO_ID;
    private int logicalWidth, logicalHeight;
    private final AccessibilityManager manager;
    private final AccessibilityNodeProvider provider = new AccessibilityNodeProvider() {
      @Override
      public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
        return nodeInfo(id);
      }

      @Override
      public AccessibilityNodeInfo findFocus(int focus) {
        if (focus == AccessibilityNodeInfo.FOCUS_ACCESSIBILITY)
          return accessibilityFocus == View.NO_ID ? null : nodeInfo(accessibilityFocus);
        for (SemanticNode node : nodes.values()) if (node.focused) return nodeInfo(node.virtualId);
        return null;
      }

      @Override
      public List<AccessibilityNodeInfo> findAccessibilityNodeInfosByText(String text, int root) {
        List<AccessibilityNodeInfo> result = new ArrayList<>();
        String query = text.toLowerCase(Locale.ROOT);
        for (SemanticNode node : nodes.values()) {
          if (isDescendant(node, root) && (node.label.toLowerCase(Locale.ROOT).contains(query)
              || node.value.toLowerCase(Locale.ROOT).contains(query))) result.add(nodeInfo(node.virtualId));
        }
        return result;
      }

      @Override
      public boolean performAction(int id, int action, Bundle args) {
        if (id == HOST_VIEW_ID) return ScuzzSurface.this.performAccessibilityAction(action, args);
        SemanticNode node = nodes.get(id);
        if (node == null || !isShown()) return false;
        if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS) {
          if (accessibilityFocus == id || !node.focusable()) return false;
          if (accessibilityFocus != View.NO_ID)
            sendNodeEvent(accessibilityFocus, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
          accessibilityFocus = id;
          sendNodeEvent(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED);
          return true;
        }
        if (action == AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) {
          if (accessibilityFocus != id) return false;
          accessibilityFocus = View.NO_ID;
          sendNodeEvent(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
          return true;
        }
        int shared;
        String text = "";
        int start = 0, end = 0;
        long value = 0;
        switch (action) {
          case AccessibilityNodeInfo.ACTION_FOCUS: shared = FOCUS; break;
          case AccessibilityNodeInfo.ACTION_CLEAR_FOCUS: shared = CLEAR_FOCUS; break;
          case AccessibilityNodeInfo.ACTION_CLICK: shared = ACTIVATE; break;
          case AccessibilityNodeInfo.ACTION_SCROLL_FORWARD: shared = INCREMENT; break;
          case AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD: shared = DECREMENT; break;
          case AccessibilityNodeInfo.ACTION_SET_TEXT:
            shared = SET_TEXT;
            CharSequence sequence = args == null ? null
                : args.getCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE);
            text = sequence == null ? "" : sequence.toString();
            break;
          case AccessibilityNodeInfo.ACTION_SET_SELECTION:
            shared = SET_SELECTION;
            int caret = utf16Offset(node.value, node.end);
            start = args == null ? caret : args.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT, caret);
            end = args == null ? caret : args.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT, caret);
            start = utf8Offset(node.value, start);
            end = utf8Offset(node.value, end);
            break;
          default:
            if (action != AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.getId() || args == null)
              return false;
            float progress = args.getFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE, Float.NaN);
            if (Float.isNaN(progress) || Float.isInfinite(progress)) return false;
            shared = SET_VALUE;
            value = Math.max(node.minimum, Math.min(node.maximum, (long)progress));
        }
        if (!node.has(shared)) return false;
        boolean queued = nativeAccessibilityAction(node.id, shared,
            text.getBytes(StandardCharsets.UTF_8), start, end, value);
        if (queued && shared == ACTIVATE) sendNodeEvent(id, AccessibilityEvent.TYPE_VIEW_CLICKED);
        return queued;
      }
    };

    ScuzzSurface(Context context) {
      super(context);
      manager = (AccessibilityManager)context.getSystemService(Context.ACCESSIBILITY_SERVICE);
      setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
      setFocusable(true);
      setFocusableInTouchMode(true);
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent event) {
      if ((nativeKeyboardRequest() & 1) != 0 && hidden != null && hidden.onKeyDown(code, event)) return true;
      return super.onKeyDown(code, event);
    }

    @Override
    public boolean onKeyUp(int code, KeyEvent event) {
      if ((nativeKeyboardRequest() & 1) != 0 && hidden != null && hidden.onKeyUp(code, event)) return true;
      return super.onKeyUp(code, event);
    }

    @Override
    public boolean onCheckIsTextEditor() { return true; }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo attributes) {
      return hidden == null ? null : hidden.onCreateInputConnection(attributes);
    }

    @Override
    public AccessibilityNodeProvider getAccessibilityNodeProvider() { return provider; }

    private boolean isDescendant(SemanticNode node, int root) {
      if (root == AccessibilityNodeProvider.HOST_VIEW_ID) return true;
      while (node != null) {
        if (node.virtualId == root) return true;
        node = nodes.get(identities.get(node.parent));
      }
      return false;
    }

    void syncAccessibility() {
      byte[] encoded = nativeAccessibilitySnapshot(version);
      if (encoded == null) return;
      try {
        JSONObject document = new JSONObject(new String(encoded, StandardCharsets.UTF_8));
        JSONObject tree = document.getJSONObject("tree");
        JSONArray array = tree.getJSONArray("nodes");
        LinkedHashMap<Integer, SemanticNode> fresh = new LinkedHashMap<>();
        HashMap<Long, Integer> freshIds = new HashMap<>();
        for (int i = 0; i < array.length(); i++) {
          JSONObject data = array.getJSONObject(i);
          long identity = data.getLong("id");
          Integer id = identities.get(identity);
          if (id == null) {
            if (nextId == Integer.MAX_VALUE) throw new JSONException("Virtual control identities are exhausted");
            id = nextId++;
          }
          SemanticNode node = new SemanticNode(data, id);
          fresh.put(id, node);
          freshIds.put(identity, id);
        }
        LinkedHashMap<Integer, SemanticNode> old = nodes;
        nodes = fresh;
        identities = freshIds;
        logicalWidth = tree.getInt("width");
        logicalHeight = tree.getInt("height");
        version = document.getLong("version");
        if (!nodes.containsKey(accessibilityFocus) && accessibilityFocus != View.NO_ID) {
          sendNodeEvent(accessibilityFocus, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
          accessibilityFocus = View.NO_ID;
        }
        if (!nodes.containsKey(hovered) && hovered != View.NO_ID) {
          sendNodeEvent(hovered, AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
          hovered = View.NO_ID;
        }
        for (SemanticNode node : nodes.values()) {
          SemanticNode prior = old.get(node.virtualId);
          if (prior == null) {
            if (node.focused) sendNodeEvent(node.virtualId, AccessibilityEvent.TYPE_VIEW_FOCUSED);
            continue;
          }
          if (node.editable && !node.value.equals(prior.value)) {
            AccessibilityEvent event = event(node.virtualId, AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED);
            event.setBeforeText(prior.value);
            event.setFromIndex(0);
            event.setRemovedCount(prior.value.length());
            event.setAddedCount(node.value.length());
            event.getText().add(node.value);
            sendEvent(event);
          }
          if (node.editable && (node.start != prior.start || node.end != prior.end)) {
            AccessibilityEvent event = event(node.virtualId, AccessibilityEvent.TYPE_VIEW_TEXT_SELECTION_CHANGED);
            event.setFromIndex(utf16Offset(node.value, node.start));
            event.setToIndex(utf16Offset(node.value, node.end));
            event.setItemCount(node.value.length());
            event.getText().add(node.value);
            sendEvent(event);
          }
          if (node.focused && !prior.focused) sendNodeEvent(node.virtualId, AccessibilityEvent.TYPE_VIEW_FOCUSED);
        }
        AccessibilityEvent changed = event(AccessibilityNodeProvider.HOST_VIEW_ID,
            AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
        changed.setContentChangeTypes(AccessibilityEvent.CONTENT_CHANGE_TYPE_SUBTREE);
        sendEvent(changed);
      } catch (JSONException error) {
        Log.e("scuzz", "Cannot read accessibility controls", error);
      }
    }

    private Rect bounds(SemanticNode node) {
      float sx = logicalWidth > 0 ? (float)getWidth() / logicalWidth : 1f;
      float sy = logicalHeight > 0 ? (float)getHeight() / logicalHeight : 1f;
      return new Rect((int)Math.floor(node.x * sx), (int)Math.floor(node.y * sy),
          (int)Math.ceil((node.x + node.width) * sx), (int)Math.ceil((node.y + node.height) * sy));
    }

    private String className(SemanticNode node) {
      if (node.editable) return "android.widget.EditText";
      switch (node.role) {
        case 7: case 22: return "android.widget.CheckBox";
        case 9: case 24: return "android.widget.RadioButton";
        case 11: case 23: return "android.widget.Switch";
        case 8: case 42: return "android.widget.SeekBar";
        case 10: case 20: return "android.widget.ProgressBar";
        case 6: return "android.widget.ScrollView";
        case 4: case 21: case 49: return "android.widget.ImageView";
        case 55: return "android.widget.Spinner";
        default: return node.has(ACTIVATE) ? "android.widget.Button" : "android.widget.TextView";
      }
    }

    @SuppressWarnings("deprecation")
    private AccessibilityNodeInfo nodeInfo(int id) {
      if (id == AccessibilityNodeProvider.HOST_VIEW_ID) {
        AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(this);
        super.onInitializeAccessibilityNodeInfo(info);
        for (SemanticNode node : nodes.values()) if (node.parent == 0) info.addChild(this, node.virtualId);
        return info;
      }
      SemanticNode node = nodes.get(id);
      if (node == null) return null;
      AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain();
      info.setSource(this, id);
      info.setPackageName(getPackageName());
      info.setClassName(className(node));
      Integer parentId = identities.get(node.parent);
      SemanticNode parent = nodes.get(parentId);
      if (parent == null) info.setParent(this);
      else info.setParent(this, parentId);
      Rect local = bounds(node);
      Rect relative = new Rect(local);
      if (parent != null) { Rect enclosing = bounds(parent); relative.offset(-enclosing.left, -enclosing.top); }
      info.setBoundsInParent(relative);
      int[] location = new int[2];
      getLocationOnScreen(location);
      local.offset(location[0], location[1]);
      Rect visible = new Rect();
      boolean shown = isShown() && getGlobalVisibleRect(visible) && local.intersect(visible);
      info.setBoundsInScreen(local);
      info.setVisibleToUser(shown);
      info.setEnabled(true);
      info.setFocusable(node.has(FOCUS));
      info.setFocused(node.focused);
      info.setAccessibilityFocused(accessibilityFocus == id);
      info.setEditable(node.editable);
      info.setMultiLine(node.multiline);
      if (node.editable) {
        info.setText(node.value);
        info.setContentDescription(node.label);
        info.setTextSelection(utf16Offset(node.value, node.start), utf16Offset(node.value, node.end));
        if (Build.VERSION.SDK_INT >= 26) info.setHintText(node.label);
      } else if (node.role != 44) info.setText(node.label);
      if (node.checked >= 0) { info.setCheckable(true); info.setChecked(node.checked != 0); }
      if (node.selected >= 0) info.setSelected(node.selected != 0);
      if (Build.VERSION.SDK_INT >= 28) {
        info.setTooltipText(node.description);
        info.setHeading(node.role == 44 || (parent != null && parent.role == 44));
        info.setScreenReaderFocusable(node.focusable());
      }
      if (Build.VERSION.SDK_INT >= 30) {
        if (node.expanded >= 0) info.setStateDescription(node.expanded != 0 ? "Expanded" : "Collapsed");
        else if (node.range()) info.setStateDescription(node.value);
      }
      if (node.range()) info.setRangeInfo(AccessibilityNodeInfo.RangeInfo.obtain(
          AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_FLOAT, (float)node.minimum,
          (float)node.maximum, (float)node.number));
      info.setScrollable(node.role == 6);
      if (node.has(FOCUS)) info.addAction(AccessibilityNodeInfo.ACTION_FOCUS);
      if (node.has(CLEAR_FOCUS)) info.addAction(AccessibilityNodeInfo.ACTION_CLEAR_FOCUS);
      if (node.has(ACTIVATE)) { info.setClickable(true); info.addAction(AccessibilityNodeInfo.ACTION_CLICK); }
      if (node.has(SET_TEXT)) info.addAction(AccessibilityNodeInfo.ACTION_SET_TEXT);
      if (node.has(SET_SELECTION)) info.addAction(AccessibilityNodeInfo.ACTION_SET_SELECTION);
      if (node.has(INCREMENT)) info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);
      if (node.has(DECREMENT)) info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);
      if (node.has(SET_VALUE)) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS);
      if (node.focusable()) info.addAction(accessibilityFocus == id
          ? AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS : AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS);
      for (SemanticNode child : nodes.values()) if (child.parent == node.id) info.addChild(this, child.virtualId);
      return info;
    }

    @SuppressWarnings("deprecation")
    private AccessibilityEvent event(int id, int type) {
      AccessibilityEvent event = AccessibilityEvent.obtain(type);
      event.setSource(this, id);
      event.setPackageName(getPackageName());
      SemanticNode node = nodes.get(id);
      event.setClassName(node == null ? "android.view.SurfaceView" : className(node));
      event.setEnabled(true);
      return event;
    }

    private void sendEvent(AccessibilityEvent event) {
      if (manager != null && manager.isEnabled() && getParent() != null)
        getParent().requestSendAccessibilityEvent(this, event);
    }

    private void sendNodeEvent(int id, int type) { sendEvent(event(id, type)); }

    @Override
    public boolean dispatchHoverEvent(MotionEvent motion) {
      if (manager == null || !manager.isEnabled() || !manager.isTouchExplorationEnabled())
        return super.dispatchHoverEvent(motion);
      int hit = View.NO_ID;
      if (motion.getActionMasked() != MotionEvent.ACTION_HOVER_EXIT) {
        for (SemanticNode node : nodes.values())
          if (node.focusable() && bounds(node).contains((int)motion.getX(), (int)motion.getY())) hit = node.virtualId;
      }
      if (hit != hovered) {
        int old = hovered;
        hovered = hit;
        if (hit != View.NO_ID) sendNodeEvent(hit, AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);
        if (old != View.NO_ID) sendNodeEvent(old, AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
      }
      return hit != View.NO_ID;
    }
  }

}
