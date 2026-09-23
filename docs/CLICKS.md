# Native clicks and CSS selection

Evidence filenames and workspace-relative paths below refer to local validation records. See `../validation/README.md` for the published summary; raw local logs are not included.

`element_click` and legacy `click` share a native C++ executor in
`src/actions/click_action.cpp`. Coordinates take priority when both are supplied;
otherwise nonempty text precedes a nonempty selector. An empty text argument does
not hide a selector. A missing `if_exists` target returns before `hover_first`.
An empty hover selector is ignored. Element targeting remains scoped to the
selected document; result URL/title describe the root page.

Before pressing, the executor requires a visible, enabled, uncovered target,
scrolls it into view, and compares geometry across two animation frames. After
paced pointer movement it checks identity, geometry and hit testing again. An
observed detached node can be reacquired before pressing. A frame owner that
moves requires new projection into root coordinates. Shadow targets also check
outer host/document overlays. Input is pinned to the original tab.

Once a press is attempted, the action does not reacquire or replay it. Double and
triple clicks revalidate the same node before later presses. Partial actions can
therefore fail after their first effect. Held buttons and temporary input
observations receive bounded cleanup after failure. A last geometry observation
and a later CDP input command are not atomic; arbitrary page mutation between
them cannot be ruled out. Preparatory remote-object failures may terminate the
action instead of reacquiring. Returned element text is sampled before input.

The CSS parser separates top-level comma branches while respecting escapes,
quotes, comments, brackets and parentheses. A trailing unescaped `:visible` on
each branch is a visibility filter; `#literal\\:visible` remains a literal CSS
identifier. Union results use document/host traversal order and remove duplicates.
Visible input indices use the same ordered input/textarea union, including open
shadow roots. Native CSS is queried in each root. Extended Playwright selector
engines, cross-shadow ancestry selectors and arbitrary nested-shadow ordering
equivalence are not claimed.

## Measured evidence

The original unchanged server ran in an isolated owned Chrome with Playwright
1.50.0. `click-values.json` contains 28 result/native-event vectors, covering all
five click types, three modes, coordinates, target priority, conditional hover,
menus and scrolling. `css-list-values.json` contains 16 observed selection
vectors, including escaped punctuation, nested CSS functions, visibility,
document/shadow order and mixed input indices.

`relay-click-tests` passed 118 checks in Debug, ASan/UBSan and ThreadSanitizer.
It adds moving/replaced/animated targets, native AX role reacquisition, new
overlays/disabled controls, no replay after press, long-press deadline cleanup,
shadow overlays, moving cross-process frame owners and independent tab closure.
Records live under `../../evidence/UltimateBrowserJS/`:

- `click-baseline/` and `css-list-baseline-final/`: original-server observations.
- `click-before-native/`: old native code times out for empty text, hovers before
  a conditional skip, fails a skip with missing hover, and clicks the old body
  position after a target moves. These are retained failures, not acceptance.
- `css-list-before/`: escaped literal `:visible` selection failed before the fix.
- `click-{debug,sanitize,thread-sanitize}-browser-final/`: final browser receipts.

The first focused test compared incidental hover counts during the baseline's
random human pointer path. That assertion was corrected to compare hover effects
only for explicit `hover_first` vectors. Trusted click event sequences remain
compared for every vector. The long-press failure test allows enough time for
the first press under instrumented builds, then checks its bounded release.
