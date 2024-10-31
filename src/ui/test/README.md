# General

- Pass `--keep-gui` to your test. It will maintain the window open for you, which helps with visual inspection
- Pass `--interactive`. It will open a dialog where you can run tests manually.

# Popups

Popups can be an headache to test, here's some tips that might save you time:

- Get to the popup directly via `ImGuiContext::OpenPopupStack` instead of trying to assemble its path/ID manually
- When passing popup IDs for screencapture, be sure to use the popup's window `Window->ID` and never `Window->PopupId`
- Play with the flags passed to captureScreenshot(), ImGuiCaptureFlags_StitchAll seems buggy with popups
- Be sure your popup's "dismiss logic" isn't getting in the way. For example `components/PopupMenu.hpp` goes away if
  mouse cursor is too far.

# Event Loop and Frames

- The `TestFunc` is called exactly once
- The `GuiFunc` is called once per frame. One test can imply several frames.
- In pseudo-code, our event loop looks like this:

```
while !testQueue.isEmpty
    GuiFunc();
    testQueue.take().runTest();
```

- `GuiFunc` and `TestFunc` run in different threads, but this is just a `ImGuiTestEngine` implementation detail.
  They don't actually run concurrently. When testing is running, event loop thread is blocked, and vice-versa.

- You should not assume your `TestFunc` last only 1 frame. A screen capture takes 10 frames. Pressing a `Button` takes around 7.

- You can explicitly make `TestFunc` wait any number of frames by calling `ImGuiTestEngine_Yield(ctx->Engine)`.
  This will pass control to the `GuiFunc` thread, for 1 frame.

- Our main loop is the `while (!aborted)` in `ImGuiTestApp::runTests()`, which is standard from the ImGui examples, but we can tune it to our needs.

- The call to `ImGuiTestEngine_QueueTests()` can also accept `RunFlags`, which accepts `ImGuiTestRunFlags_RunFromGui`, which presumably skips `TestFunc`. Not sure if it simplifies certain use cases, but sounds relevant for this subject.

# IDs

Sometimes it's difficult to find the correct ID of an item.

- Try passing `--interactive` and use the debug window to see the full path of each item
- Popups are tricky, see the Popups section above
- Child windows (`ImGui::BeginChild`) seem special, use `ctx->WindowInfo` to get the sub-window handle, for example:

```
  auto subWindowInfo = ctx->WindowInfo("//KeypadX/drawKeypad Input");
  ctx->SetRef(subWindowInfo.Window->ID);
```

Do not use their path directly as ID, as the real path is mangled.
