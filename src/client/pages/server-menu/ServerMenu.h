#pragma once

namespace coop::ui {

// The F8 window: the server's settings (the [server] and [balance] sections of its server.ini), as the server sends them.
// The host of a game started from the F9 window and a server's admins change them here: each change is in effect at
// once for everyone and written to the server's file. Other players see the values. It takes the mouse and the keys
// while open, as F2 does.
void SetServerMenuVisible(bool visible);
bool IsServerMenuVisible();
void ToggleServerMenu();  // F8: opened online and in the game only
void RenderServerMenu();

}  // namespace coop::ui
