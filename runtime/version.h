/* The version of this build. It changes with every update, so
 * a bug report (whose folder name starts with it) shows which update it came
 * from. Two channels (docs/STATE.md): stable ("v26"; "v31.1"... then "v32" to
 * "v47" for this fork's weekly releases; since v2.0: "v2.1", "v2.2"... for
 * fixes, "v3", "v4"... for big additions) and nightly ("v27-nightly1": the
 * work in progress towards the next stable).
 *
 * Updaters of v47 and older read the first DQIX_VERSION line in this file,
 * even in a comment, and count whole numbers (v2.0 would look older than
 * v47 to them). The line below is theirs: keep "v48" first so they still see
 * a newer version, and the real one in brackets for their message.
 * #define DQIX_VERSION "v48 (v2.0)" */
#pragma once
#define DQIX_VERSION "v2.0"
#define DQIX_VERSION_DATE "2026-11-13"
