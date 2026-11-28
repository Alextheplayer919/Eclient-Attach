from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'apollon-adapter/automation/auto_use.h').read_text()
assert 'if(blockRelease.load() && game==heldGame.load())' in s
assert s.index('blockRelease.store(false);heldGame.store(nullptr);\n    unsigned blocked') < s.index('attempting release after cancellation/timeout')
assert 'blockRelease.store(false);heldGame.store(nullptr);traceGame.store(nullptr)' in s
assert s.index('heldGame.store(game);blockRelease.store(true);') < s.index('fn<bool(*)(void*,void*)>(0xccc4438)')
print('Auto Use hold integration checks passed')
