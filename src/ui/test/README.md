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
