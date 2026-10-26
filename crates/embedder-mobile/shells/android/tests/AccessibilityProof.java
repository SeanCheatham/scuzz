package dev.scuzz.proof;

import android.app.Activity;
import android.app.Instrumentation;
import android.app.UiAutomation;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;
import java.io.PrintWriter;
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

  @SuppressWarnings("deprecation")
  private void prove() throws Exception {
    await(() -> named("Same").size() == 100, "The tree needs 100 repeated-label controls");
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

    click("Open dialog");
    await(() -> named("Close dialog").size() == 1 && named("Same").isEmpty(),
        "The dialog exposes background controls");
    require(!stale.performAction(AccessibilityNodeInfo.ACTION_CLICK), "A background control accepts input");
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
