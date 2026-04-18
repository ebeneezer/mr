# Ah, I see what happened. In commit e5b6620a78e263be28e2e9660c38d8f3110d75e8, I had the python scripts `patch_windowcommands_cpp.py` created but they were NOT executed before committing, or they were reverted during some `git reset` or checkout!
# Wait, when the user said "Die Dummy Dialoge erscheinen immer noch", it means my `patch_router_final` wasn't right or wasn't committed.
# But wait, looking at `git log -p -n 1`, I see I *deleted* the python scripts in the commit! I did a `git add .` and committed the deletion of the scripts.
# Let's write the commands back to MRWindowCommands.cpp!
