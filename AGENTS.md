# Surface Duo 2 (`zeta`) Linux Bring-up Protocol

All work on this device follows the rules in `~/.hermes/skills/linux-phone-porting/SKILL.md`:

1. **Phase 0 First:** Take full partition backups of device-unique calibration partitions (`modemst1`, `modemst2`, `persist`) before any flashing.
2. **One variable per flash:** Never change kernel config, DTB, and initramfs simultaneously.
3. **Falsifier before build:** Define what observable signal will prove or refute the change before building.
4. **Evidence over inference:** Read back flashed partitions directly; check dmesg and pstore rather than guessing boot states.
5. **Private data quarantine:** Device-unique partitions and userdata stay inside `artifacts/private/` and are never committed or published.
