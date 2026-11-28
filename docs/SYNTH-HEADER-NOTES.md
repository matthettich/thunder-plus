# Synth header lost (21 Nov, build 662)
Not reproduced (no headless Chromium). Reasoned from code.
- index.html:350-352 html,body height:100%, body overflow:hidden (propagates to the viewport): still scrollable from code. .top (3169) is a flex sibling of .main (406, its own scroller), so "header off the top" = document/viewport scrolled.
- Scroll-affecting calls on Synth: 14607 (rename focus), 14769 (pad scrollIntoView, sdesk only), 9454 askbox. None resets the document.
- Re-measure: resize/visualViewport -> scheduleRefit (34171-34173); return path only did arrLayout(true) after 150 ms (34175), no scroll reset. Owner: fold/unfold (a resize) fixes it.
- Fix: resetStrayScroll + remeasureAfterReturn near the ResizeObserver line (~34176): on scroll, resize, visualViewport resize/scroll, pageshow, focus, visibilitychange. Done; tests/header-lost-scroll.test.js.
