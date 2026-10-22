// =============================================================================
// AIDoser TEST BUILD SWITCH (client side)
//
//   true  -> the red "Reset to New User" bar appears at the top of the page
//   false -> test-reset.js does nothing at all
//
// For a production deploy: set this to false (or do not deploy this file or
// test-reset.js). The server-side switch is separate (AIDOSER_TEST_BUILD in
// functions/.env) so a leftover client file can never reset a real customer.
// =============================================================================
window.AIDOSER_TEST_BUILD = true;
