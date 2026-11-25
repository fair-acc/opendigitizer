# UX Design Rationale

This document is meant to explain some UX design choices of opendigitizer. These may be motivated by common design best practices such as discoverability, progressive disclosure, accessibility and translation, or mobile-first design. They may also be motivated by factors specific to this application, such as ease of onboarding for GNURadio 3.0 users, or budget and time constraints.

## Icon Buttons

OpenDigitizer generally makes an effort to avoid text in the UI. One reason this might be to generally avoid clutter. Another is to minimize elements on the dashboard page specifically, which in particular needs to be distraction-free for display purposes. This leads to our use of icon buttons. These have the following advantages:

- **visual recognition**: a well-chosen, familiar icon can be recognised at a glance rather than requiring a text label to be read and interpreted every time.
- **screen real estate**: especially on mobile, this allows for a more compact layout whilst retaining sufficiently large touch targets.
- **language independence and inclusivity**: established symbols are less language-dependent than text. What may be immediately intuitive to a native English speaker is not necessarily so for everyone else. For a real-life example, look to European traffic signs, which predominantly rely on graphical symbols, shapes and colours. This is done on purpose, in order to allow the designs to be uniform across borders. Meanwhile, their US counterparts rely more heavily on written instructions.
- **consistency/familiarity**: commonly used icons (play, pause, search, zoom, settings, etc.) allow users to transfer their existing knowledge from other applications rather than having to learn another UI vocabulary.
- **visual structure**: icons provide distinct visual anchors, making it easier to scan a toolbar and locate frequently used functionality without adding unnecessary visual clutter.
- **responsive design**: icons have predictable dimensions, irrespective of language. Their dimensions can be chosen based on physical screen size and therefore be identical on mobile and desktop.

This is not a universal rule. The important prerequisite is that the icons are well chosen, sufficiently recognisable, consistent and complemented by meaningful tooltips. Where the functionality of the button is ambiguous or unfamiliar, text or a combination of icon and text may be the better choice.
