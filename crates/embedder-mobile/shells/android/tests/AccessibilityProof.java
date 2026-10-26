package dev.scuzz.proof;

import android.accessibilityservice.AccessibilityServiceInfo;
import android.app.Activity;
import android.app.Instrumentation;
import android.app.UiAutomation;
import android.content.ComponentName;
import android.content.Context;
import android.graphics.Rect;
import android.graphics.Bitmap;
import android.content.Intent;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;
import java.io.PrintWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.StringWriter;
import java.util.ArrayList;
import java.util.List;

/** Check the public Android accessibility tree and its actions. */
public final class AccessibilityProof extends Instrumentation {
  private String target;
  private UiAutomation automation;

  @Override
  public void onCreate(Bundle args) {
    super.onCreate(args);
    target = args.getString("target");
    start();
  }

  @Override
  public void onStart() {
    Bundle result = new Bundle();
    try {
      automation = getUiAutomation();
      android.accessibilityservice.AccessibilityServiceInfo service = automation.getServiceInfo();
      service.flags |= android.accessibilityservice.AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS;
      automation.setServiceInfo(service);
      Intent intent = new Intent().setComponent(new ComponentName(target, "dev.scuzz.app.MainActivity"));
      intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
      startActivitySync(intent);
      prove();
      result.putString("result", "pass");
      finish(Activity.RESULT_OK, result);
    } catch (Throwable error) {
      StringWriter trace = new StringWriter();
      error.printStackTrace(new PrintWriter(trace));
      result.putString("result", trace.toString());
      finish(Activity.RESULT_CANCELED, result);
    }
  }

  private void require(boolean condition, String message) {
    if (!condition) throw new AssertionError(message);
  }

  private void collect(AccessibilityNodeInfo node, List<AccessibilityNodeInfo> result) {
    if (node == null) return;
    result.add(node);
    for (int i = 0; i < node.getChildCount(); i++) collect(node.getChild(i), result);
  }

  private List<AccessibilityNodeInfo> tree() {
    List<AccessibilityNodeInfo> result = new ArrayList<>();
    for (AccessibilityWindowInfo window : automation.getWindows()) {
      AccessibilityNodeInfo root = window.getRoot();
      if (root != null && matches(target, root.getPackageName())) collect(root, result);
    }
    return result;
  }

  private boolean matches(String label, CharSequence value) {
    return value != null && label.contentEquals(value);
  }

  private List<AccessibilityNodeInfo> named(String label) {
    List<AccessibilityNodeInfo> result = new ArrayList<>();
    for (AccessibilityNodeInfo node : tree()) {
      if (matches(label, node.getText()) || matches(label, node.getContentDescription()))
        result.add(node);
    }
    return result;
  }

  private interface Check { boolean run(); }

  private void await(Check check, String message) throws Exception {
    long deadline = android.os.SystemClock.uptimeMillis() + 20000;
    do {
      if (check.run()) return;
      Thread.sleep(100);
    } while (android.os.SystemClock.uptimeMillis() < deadline);
    StringBuilder text = new StringBuilder();
    for (AccessibilityNodeInfo node : tree()) text.append(node.getClassName()).append(":")
        .append(node.getText()).append("/").append(node.getContentDescription()).append("\n");
    throw new AssertionError(message + "\n" + text);
  }

  private AccessibilityNodeInfo one(String label) {
    List<AccessibilityNodeInfo> result = named(label);
    require(result.size() == 1, "Expected one control: " + label + "; found " + result.size());
    return result.get(0);
  }

  private void click(String label) {
    require(one(label).performAction(AccessibilityNodeInfo.ACTION_CLICK), "Cannot activate " + label);
  }

  private void hover(int action, float x, float y) {
    long now = android.os.SystemClock.uptimeMillis();
    MotionEvent event = MotionEvent.obtain(now, now, action, x, y, 0);
    event.setSource(InputDevice.SOURCE_TOUCHSCREEN);
    try { require(automation.injectInputEvent(event, true), "Cannot inject exploration hover input"); }
    finally { event.recycle(); }
  }

  private void proveHoverExploration() throws Exception {
    AccessibilityServiceInfo service = automation.getServiceInfo();
    int flags = service.flags;
    service.flags |= AccessibilityServiceInfo.FLAG_REQUEST_TOUCH_EXPLORATION_MODE;
    automation.setServiceInfo(service);
    AccessibilityManager manager = (AccessibilityManager)getTargetContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
    boolean[] hovering = {false};
    Rect bounds = new Rect();
    one("Draft").getBoundsInScreen(bounds);
    float x = bounds.exactCenterX(), y = bounds.exactCenterY();
    try {
      await(() -> manager.isTouchExplorationEnabled(), "Touch exploration does not start");
      // Input injection starts after the Android exploration filter.
      automation.executeAndWaitForEvent(() -> {
        hover(MotionEvent.ACTION_HOVER_ENTER, x, y);
        hovering[0] = true;
        hover(MotionEvent.ACTION_HOVER_MOVE, x + 1, y);
      }, event -> {
        AccessibilityNodeInfo source = event.getSource();
        return event.getEventType() == AccessibilityEvent.TYPE_VIEW_HOVER_ENTER
            && source != null && matches("Draft", source.getContentDescription());
      }, 10000);
      require(!one("Draft").isFocused(), "Hover exploration changes text input focus");
      automation.executeAndWaitForEvent(() -> {
        hover(MotionEvent.ACTION_HOVER_EXIT, x + 1, y);
        hovering[0] = false;
      }, event -> {
        AccessibilityNodeInfo source = event.getSource();
        return event.getEventType() == AccessibilityEvent.TYPE_VIEW_HOVER_EXIT
            && source != null && matches("Draft", source.getContentDescription());
      }, 10000);
    } finally {
      try { if (hovering[0]) hover(MotionEvent.ACTION_HOVER_EXIT, x, y); }
      finally { service.flags = flags; automation.setServiceInfo(service); }
    }
  }

  private Rect screenBounds(String label) {
    Rect result = new Rect();
    one(label).getBoundsInScreen(result);
    require(!result.isEmpty(), "Empty bounds: " + label);
    return result;
  }

  private void near(int actual, float expected, String label) {
    require(Math.abs(actual - expected) <= 2f, label + ": " + actual + " != " + expected);
  }

  private void expectedBounds(String scene, String child, float x, float y, float width, float height) {
    Rect parent = screenBounds(scene), box = screenBounds(child);
    float sx = parent.width() / 200f, sy = parent.height() / 100f;
    near(box.left, parent.left + x * sx, child + " left");
    near(box.top, parent.top + y * sy, child + " top");
    near(box.right, parent.left + (x + width) * sx, child + " right");
    near(box.bottom, parent.top + (y + height) * sy, child + " bottom");
  }

  private Rect ink(Bitmap image, int color) {
    int width = image.getWidth(), height = image.getHeight();
    int[] pixels = new int[width * height];
    image.getPixels(pixels, 0, width, 0, 0, width, height);
    // Surface screenshots can use RGB565 channel values. Match each exact format.
    int r = Math.round(((color >> 16) & 255) * 31f / 255f);
    int g = Math.round(((color >> 8) & 255) * 63f / 255f);
    int b = Math.round((color & 255) * 31f / 255f);
    int rgb565 = (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | (b << 3) | (b >> 2);
    int left = width, top = height, right = 0, bottom = 0;
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
      int pixel = pixels[y * width + x] & 0xffffff;
      if (pixel == color || pixel == rgb565) {
        left = Math.min(left, x);
        top = Math.min(top, y);
        right = Math.max(right, x + 1);
        bottom = Math.max(bottom, y + 1);
      }
    }
    return right > left && bottom > top ? new Rect(left, top, right, bottom) : new Rect();
  }

  private boolean near(Rect first, Rect second) {
    return !first.isEmpty() && !second.isEmpty()
        && Math.abs(first.left - second.left) <= 2 && Math.abs(first.right - second.right) <= 2
        && Math.abs(first.top - second.top) <= 2 && Math.abs(first.bottom - second.bottom) <= 2;
  }

  private void proveBounds() throws Exception {
    expectedBounds("Transform scene", "Scaled paint", 75, 50, 50, 20);
    expectedBounds("Transform scene", "Clipped paint", 195, 90, 5, 10);
    expectedBounds("Scaled clip scene", "Scaled clip paint", 135, 65, 15, 10);
    String[] measured = {"No screenshot"};
    try {
      await(() -> {
        Bitmap image = automation.takeScreenshot();
        if (image == null) return false;
        try {
          Rect scaled = ink(image, 0xf111dd), clipped = ink(image, 0x11e5f1), nested = ink(image, 0x71f111);
          Rect first = screenBounds("Scaled paint"), second = screenBounds("Clipped paint"), third = screenBounds("Scaled clip paint");
          measured[0] = "Scaled " + first + "/" + scaled + "; clipped " + second + "/" + clipped
              + "; nested " + third + "/" + nested;
          return near(first, scaled) && near(second, clipped) && near(third, nested);
        } finally { image.recycle(); }
      }, "Accessibility bounds differ from the visible paint");
    } catch (AssertionError error) {
      Bitmap image = automation.takeScreenshot();
      if (image != null) {
        try (FileOutputStream output = new FileOutputStream(new File(getTargetContext().getFilesDir(), "scuzz_android.bounds.png"))) {
          image.compress(Bitmap.CompressFormat.PNG, 100, output);
        } finally { image.recycle(); }
      }
      throw new AssertionError(error.getMessage() + "\n" + measured[0]);
    }
  }

  @SuppressWarnings("deprecation")
  private void prove() throws Exception {
    await(() -> named("Same").size() == 100, "The tree needs 100 repeated-label controls");
    proveHoverExploration();
    AccessibilityNodeInfo stale = named("Same").get(99);
    require(stale.performAction(AccessibilityNodeInfo.ACTION_CLICK), "Cannot activate the last repeated control");
    await(() -> named("Chosen 99").size() == 1, "The last repeated control selects the wrong item");

    require(!one("Chosen 99").isFocusable(), "Static text reports input focus support");
    AccessibilityNodeInfo field = one("Draft");
    require(field.isEditable(), "The field is not editable");
    automation.executeAndWaitForEvent(() -> require(field.performAction(
        AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS), "Cannot set accessibility focus"),
        event -> event.getEventType() == AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED, 10000);
    require(field.refresh() && field.isAccessibilityFocused() && !field.isFocused(),
        "Accessibility focus changes input focus");
    require(field.performAction(AccessibilityNodeInfo.ACTION_FOCUS), "Cannot focus the field");
    await(() -> {
      AccessibilityNodeInfo focused = automation.findFocus(AccessibilityNodeInfo.FOCUS_INPUT);
      return focused != null && matches("Draft", focused.getContentDescription()) && focused.isEditable();
    }, "Android input focus does not identify the virtual field");
    Bundle edit = new Bundle();
    edit.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, "é😀z");
    require(field.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, edit), "Cannot set Unicode text");
    await(() -> matches("é😀z", one("Draft").getText()), "The field loses Unicode text");
    Bundle selection = new Bundle();
    selection.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT, 1);
    selection.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT, 3);
    automation.executeAndWaitForEvent(() -> require(one("Draft").performAction(
        AccessibilityNodeInfo.ACTION_SET_SELECTION, selection), "Cannot select the emoji"),
        event -> event.getEventType() == AccessibilityEvent.TYPE_VIEW_TEXT_SELECTION_CHANGED
            && event.getFromIndex() == 1 && event.getToIndex() == 3, 10000);
    await(() -> one("Draft").getTextSelectionStart() == 1 && one("Draft").getTextSelectionEnd() == 3,
        "Selection does not use UTF-16 offsets");
    sendKeyDownUpSync(KeyEvent.KEYCODE_DEL);
    await(() -> matches("éz", one("Draft").getText()), "A native Backspace does not remove the selected emoji");
    await(() -> one("Draft").getTextSelectionStart() == 1 && one("Draft").getTextSelectionEnd() == 1,
        "Backspace does not collapse the selection");
    require(one("Draft").performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, edit), "Cannot restore Unicode text");
    await(() -> matches("é😀z", one("Draft").getText()), "Unicode text does not return");
    require(one("Draft").performAction(AccessibilityNodeInfo.ACTION_SET_SELECTION, selection), "Cannot restore the selection");
    await(() -> one("Draft").getTextSelectionStart() == 1 && one("Draft").getTextSelectionEnd() == 3,
        "The Unicode selection does not return");
    require(one("Draft").performAction(AccessibilityNodeInfo.ACTION_CLEAR_FOCUS), "Cannot clear input focus");
    await(() -> named("Same").size() == 100, "The keyboard hides the control tree after focus clears");
    require(!one("Draft").isFocused(), "The virtual field keeps input focus after clear");
    AccessibilityNodeInfo focused = automation.findFocus(AccessibilityNodeInfo.FOCUS_INPUT);
    require(focused == null || !matches("Draft", focused.getContentDescription()),
        "The service keeps the cleared input-focus target");
    require(!one("Enabled").isChecked(), "Unexpected initial checkbox state");
    click("Enabled");
    await(() -> one("Enabled").isChecked(), "The checkbox state does not update");
    Bundle progress = new Bundle();
    progress.putFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE, 42f);
    require(one("Amount").performAction(
        AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.getId(), progress), "Cannot set range value");
    await(() -> one("Amount").getRangeInfo().getCurrent() == 42f, "The range value does not update");
    require(one("Amount").performAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD), "Cannot decrement range");
    await(() -> one("Amount").getRangeInfo().getCurrent() == 41f, "The range decrement is incorrect");

    automation.executeAndWaitForEvent(() -> click("Open dialog"), event -> {
      AccessibilityNodeInfo source = event.getSource();
      return event.getEventType() == AccessibilityEvent.TYPE_VIEW_FOCUSED
          && source != null && matches("Close dialog", source.getText());
    }, 10000);
    await(() -> named("Close dialog").size() == 1 && named("Same").isEmpty(),
        "The dialog exposes background controls");
    require(!stale.performAction(AccessibilityNodeInfo.ACTION_CLICK), "A background control accepts input");
    proveBounds();
    AccessibilityNodeInfo close = one("Close dialog");
    require(close.performAction(AccessibilityNodeInfo.ACTION_CLICK), "Cannot close the dialog");
    await(() -> named("Same").size() == 100, "The background tree does not return");
    require(!close.performAction(AccessibilityNodeInfo.ACTION_CLICK), "A closed dialog accepts input");
    require(named("Chosen 99").size() == 1, "Background input changes state");

    click("Remove controls");
    await(() -> named("Same").isEmpty(), "Removed controls remain in the tree");
    click("Restore controls");
    await(() -> named("Same").size() == 100, "Replacement controls do not appear");
    require(!stale.performAction(AccessibilityNodeInfo.ACTION_CLICK), "A removed identity targets a replacement");
    require(named("Same").get(99).performAction(AccessibilityNodeInfo.ACTION_CLICK), "Cannot activate the replacement");
    await(() -> named("Chosen 199").size() == 1, "The replacement action reaches the wrong control");
  }
}
