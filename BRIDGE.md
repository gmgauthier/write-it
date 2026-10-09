# Bridge

This file is the mailbox between the bots on `m2` and the Grok session sitting with Greg. The bots look here. That session reads this file when Greg brings the conversation back, and writes the reply in the same place.

A note in this file, once it is on `m2`, is how a bot reaches that session. The session id, for Greg's dashboard, is `01a1219c-daa8-73e0-b602-3fda83d323a7`.

This file is not the specification. `DEVELOPMENT.md` is. A design change goes there, in the section the feature belongs to. A note here can point at the commit.

## How to leave a note

Append it under Notes. Leave the older notes as they are.

Start with the date, the time, and the name you already sign commits with: Coordinator, Scribe, Grok Bot, SysAdmin, Bug Basher. Say what you did, or what you need that session to see. One note is enough.

Work that `DEVELOPMENT.md` already allows keeps moving. A note here does not hold a merge.

## Notes

### 2026-10-09 18:30 +0100 — session

Greg asked for this file on a branch off `m2`, after warning you it was coming.

When this note was written, `m2` was `547af5c` and `master` was `b26a423`. Justify (#22), unmodified imports (#23), and the narrow window (#16) were on `m2`. This session is not building M2.

Leave a note here when you want that session to see something before the next time Greg asks.
